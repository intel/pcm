// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev,
//            Pat Fay
//	      Austen Ott
//            Jim Harris (FreeBSD)

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "pci.h"
#include "cpucounters.h"

#ifndef _MSC_VER
#include <sys/mman.h>
#include <errno.h>
#include <strings.h>
#endif

#ifdef _MSC_VER

#include <windows.h>
#include "Winmsrdriver\msrstruct.h"
#include "winring0/OlsDef.h"
#include "winring0/OlsApiInitExt.h"
#endif

#include "utils.h"

#if defined (__FreeBSD__) || defined(__DragonFly__)
#include <sys/pciio.h>
#include <sys/sysctl.h>
#endif

namespace pcm {

#ifdef _MSC_VER

extern HMODULE hOpenLibSys;

static char * nonZeroGroupErrMsg = "Non-zero PCI group segments are not supported in Winring0 driver, make sure MSR.sys driver can be used.";

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    hDriver(openMSRDriver()),
    bus((groupnr_ << 8) | bus_),
    device(device_),
    function(function_),
    pciAddress(PciBusDevFunc(bus_, device_, function_))
{
    DBG(3, "Creating PCI Config space handle at g:b:d:f ", groupnr_, ":", bus_, ":", device_, ":", function_);
    if (groupnr_ != 0 && hDriver == INVALID_HANDLE_VALUE)
    {
        std::cerr << nonZeroGroupErrMsg << '\n';
        throw std::runtime_error(nonZeroGroupErrMsg);
    }

    if (hDriver == INVALID_HANDLE_VALUE && hOpenLibSys == NULL)
    {
        throw std::runtime_error("MSR and Winring0 drivers can't be opened");
    }
}

int32 PciHandle::getNUMANode() const
{
    // Windows implementation: read SRAT ACPI table to map PCI devices to NUMA nodes
    static std::unordered_map<uint64_t, uint32_t> pciToNuma;
    static std::mutex initMutex;
    static bool initialized = false;
    
    // Thread-safe initialization using double-checked locking
    if (!initialized)
    {
        std::lock_guard<std::mutex> lock(initMutex);
        if (!initialized)
        {
            readSRATTable(pciToNuma);
            initialized = true;
        }
    }
    
    // The bus field contains (groupnr << 8) | bus_, so we need to extract them
    const uint32 groupnr = (bus >> 8);
    const uint32 actual_bus = bus & 0xFF;
    
    // Construct key matching SRAT format: segment(16) | bus(8) | device(5) | function(3)
    uint64_t key = ((uint64_t)groupnr << 16) | ((uint64_t)actual_bus << 8) | 
                  ((uint64_t)device << 3) | function;
    
    auto it = pciToNuma.find(key);
    if (it != pciToNuma.end())
    {
        DBG(3, "Found NUMA node ", it->second, " for PCI device ", std::hex, 
            groupnr, ":", actual_bus, ":", device, ".", function, std::dec);
        return (int32)it->second;
    }
    
    DBG(2, "No NUMA affinity found in SRAT for PCI device ", std::hex, 
        groupnr, ":", actual_bus, ":", device, ".", function, std::dec);
    return -1;
}

bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    DBG(3, "Checking PCI Config space at g:b:d:f ", groupnr_, ":", bus_, ":", device_, ":", function_);
    HANDLE tempHandle = openMSRDriver();
    if (tempHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(tempHandle);

        PciHandle tempHandle(groupnr_, bus_, device_, function_);
        uint32 value = 0;
        return tempHandle.read32(0, &value) == sizeof(uint32);
    }

    if (groupnr_ != 0)
    {
        std::cerr << nonZeroGroupErrMsg << '\n';
        return false;
    }

    if (hOpenLibSys != NULL)
    {
        DWORD addr(PciBusDevFunc(bus_, device_, function_));
        DWORD result = 0;
        return ReadPciConfigDwordEx(addr, 0, &result)?true:false;
    }

    return false;
}

int32 PciHandle::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandle::read32", false, offset);
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        PCICFG_Request req;
        ULONG64 result = 0;
        DWORD reslength = 0;
        req.bus = (ULONG)bus;
        req.dev = (ULONG)device;
        req.func = (ULONG)function;
        req.bytes = sizeof(uint32);
        req.reg = (ULONG)offset;

        BOOL status = DeviceIoControl(hDriver, IO_CTL_PCICFG_READ, &req, (DWORD)sizeof(PCICFG_Request), &result, (DWORD)sizeof(uint64), &reslength, NULL);
        *value = (uint32)result;
        if (!status)
        {
            DBG(3, "Error reading PCI Config space at bus ", bus, " dev ", device , " function ", function ," offset ",  offset , " size ", req.bytes  , ". Windows error: ", GetLastError());
        }
        return (int32)reslength;
    }
    DWORD result = 0;
    if (ReadPciConfigDwordEx(pciAddress, (DWORD)offset, &result))
    {
        *value = result;
        return (int32)sizeof(uint32);
    }
    return 0;
}

int32 PciHandle::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandle::write32", false, offset);
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        PCICFG_Request req;
        ULONG64 result;
        DWORD reslength = 0;
        req.bus = bus;
        req.dev = device;
        req.func = function;
        req.bytes = sizeof(uint32);
        req.reg = (uint32)offset;
        req.write_value = value;

        BOOL status = DeviceIoControl(hDriver, IO_CTL_PCICFG_WRITE, &req, (DWORD)sizeof(PCICFG_Request), &result, (DWORD)sizeof(uint64), &reslength, NULL);
        if (!status)
        {
            DBG(3, "Error writing PCI Config space at bus " , bus, " dev ", device, " function ", function ," offset ", offset , " size ",  req.bytes  , ". Windows error: ", GetLastError());
            return 0;
        }
        return (int32)sizeof(uint32);
    }

    return (WritePciConfigDwordEx(pciAddress, (DWORD)offset, value)) ? sizeof(uint32) : 0;
}

int32 PciHandle::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandle::read64", false, offset);
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        if (offset & 7)
        {
            // this driver supports only 8-byte aligned reads
            // use read32 for unaligned reads
            uint32* value32Ptr = (uint32*)value;
            return read32(offset, value32Ptr) + read32(offset + sizeof(uint32), value32Ptr + 1);
        }
        PCICFG_Request req;
        DWORD reslength = 0;
        req.bus = bus;
        req.dev = device;
        req.func = function;
        req.bytes = sizeof(uint64);
        req.reg = (uint32)offset;

        BOOL status = DeviceIoControl(hDriver, IO_CTL_PCICFG_READ, &req, (DWORD)sizeof(PCICFG_Request), value, (DWORD)sizeof(uint64), &reslength, NULL);
        if (!status)
        {
            DBG(3, "Error reading PCI Config space at bus ", bus, " dev ", device, " function ", function ," offset ", offset , " size ", req.bytes  , ". Windows error: ", GetLastError());
        }
        return (int32)reslength;
    }

    cvt_ds cvt;
    cvt.ui64 = 0;

    BOOL status = ReadPciConfigDwordEx(pciAddress, (DWORD)offset, &(cvt.ui32.low));
    status &= ReadPciConfigDwordEx(pciAddress, ((DWORD)offset) + sizeof(uint32), &(cvt.ui32.high));

    if (status)
    {
        *value = cvt.ui64;
        return (int32)sizeof(uint64);
    }
    return 0;
}

PciHandle::~PciHandle()
{
    if (hDriver != INVALID_HANDLE_VALUE) CloseHandle(hDriver);
}

// Windows implementation to read MCFG table from ACPI firmware
int PciHandle::openMcfgTable() {
    // On Windows, ACPI tables are accessed via GetSystemFirmwareTable API
    // rather than through file system. This function returns -1 to indicate
    // the file-based approach is not available on Windows.
    // See PciHandle::readMCFGRecords() for the Windows implementation.
    return -1;
}

// Windows implementation to read MCFG ACPI table using Firmware Table API or physical memory
void PciHandle::readMCFGRecords(std::vector<MCFGRecord>& mcfg)
{
    mcfg.clear();
    
    // Signature for ACPI firmware tables
    const DWORD acpiSignature = 'ACPI';
    // MCFG table signature (note: stored in reverse byte order in ACPI tables)
    const DWORD mcfgSignature = 'GFCM'; // 'MCFG' in reverse
    
    // Try to get the MCFG table size first
    UINT tableSize = GetSystemFirmwareTable(acpiSignature, mcfgSignature, nullptr, 0);
    
    if (tableSize == 0)
    {
        DWORD error = GetLastError();
        DBG(1, "GetSystemFirmwareTable failed to get MCFG table size. Error: ", error);
        
        // Fallback: use default segments for known platforms
        MCFGRecord segment;
        segment.startBusNumber = 0;
        segment.endBusNumber = 0xff;
        segment.baseAddress = 0; // Actual base address is platform-specific and not available without MCFG table
        
        auto maxSegments = 1;
        switch (PCM::getCPUFamilyModelFromCPUID())
        {
        case PCM::SPR:
        case PCM::GNR:
            maxSegments = 4;
            break;
        }
        
        for (segment.PCISegmentGroupNumber = 0; segment.PCISegmentGroupNumber < maxSegments; ++(segment.PCISegmentGroupNumber))
        {
            mcfg.push_back(segment);
        }
        
        std::cerr << "PCM Warning: Could not read MCFG table from firmware, using default segments\n";
        return;
    }
    
    // Allocate buffer for the MCFG table
    std::vector<BYTE> tableBuffer(tableSize);
    
    // Read the actual table
    UINT bytesRead = GetSystemFirmwareTable(acpiSignature, mcfgSignature, tableBuffer.data(), tableSize);
    
    if (bytesRead == 0 || bytesRead != tableSize)
    {
        std::cerr << "PCM Error: Failed to read MCFG table from firmware\n";
        return;
    }
    
    // Parse the MCFG table
    // The table format is: ACPI header (variable) + MCFG records
    if (tableSize < sizeof(MCFGHeader))
    {
        std::cerr << "PCM Error: MCFG table too small\n";
        return;
    }
    
    // Use memcpy to avoid potential alignment issues
    MCFGHeader header;
    std::memcpy(&header, tableBuffer.data(), sizeof(MCFGHeader));
    
    DBG(1, "MCFG table signature: \"",
        header.signature[0], header.signature[1],
        header.signature[2], header.signature[3],
        "\" MCFG table length: ", header.length,
        " Number of MCFG records: ", header.nrecords());
    
    // Verify signature
    if (std::strncmp(header.signature, "MCFG", 4) != 0)
    {
        std::cerr << "PCM Error: Invalid MCFG table signature\n";
        return;
    }
    
    // Validate header length to prevent integer underflow in nrecords()
    if (header.length < sizeof(MCFGHeader))
    {
        std::cerr << "PCM Error: Invalid MCFG table length (too small)\n";
        return;
    }
    
    // Validate that the reported length matches the actual table size
    if (header.length > tableSize)
    {
        std::cerr << "PCM Error: MCFG table length mismatch\n";
        return;
    }
    
    // Read MCFG records
    const unsigned segments = header.nrecords();
    const BYTE* recordPtr = tableBuffer.data() + sizeof(MCFGHeader);
    
    for (unsigned int i = 0; i < segments; ++i)
    {
        if (recordPtr + sizeof(MCFGRecord) > tableBuffer.data() + tableSize)
        {
            std::cerr << "PCM Error: MCFG record out of bounds\n";
            break;
        }
        
        MCFGRecord record;
        std::memcpy(&record, recordPtr, sizeof(MCFGRecord));
        
        DBG(1, "MCFG segment " , i , ": ",
               "BaseAddress=0x" , std::hex , record.baseAddress,
               " PCISegmentGroupNumber=0x" , record.PCISegmentGroupNumber,
               " startBusNumber=0x" , (unsigned)record.startBusNumber,
               " endBusNumber=0x" , (unsigned)record.endBusNumber,
               std::dec);
        
        mcfg.push_back(record);
        recordPtr += sizeof(MCFGRecord);
    }
}

// Windows implementation to read SRAT ACPI table and build PCI device to NUMA node mapping
static void readSRATTable(std::unordered_map<uint64_t, uint32_t>& pciToNuma)
{
    pciToNuma.clear();
    
    const DWORD acpiSignature = 'ACPI';
    // SRAT table signature (note: stored in reverse byte order in ACPI tables)
    const DWORD sratSignature = 'TARS'; // 'SRAT' in reverse
    
    // Try to get the SRAT table size first
    UINT tableSize = GetSystemFirmwareTable(acpiSignature, sratSignature, nullptr, 0);
    
    if (tableSize == 0)
    {
        DBG(1, "SRAT table not available, NUMA node information will not be available");
        return;
    }
    
    // Allocate buffer for the SRAT table
    std::vector<BYTE> tableBuffer(tableSize);
    
    // Read the actual table
    UINT bytesRead = GetSystemFirmwareTable(acpiSignature, sratSignature, tableBuffer.data(), tableSize);
    
    if (bytesRead == 0 || bytesRead != tableSize)
    {
        DBG(1, "Failed to read SRAT table from firmware");
        return;
    }
    
    // SRAT table structure:
    // - ACPI header (36 bytes): Signature(4) + Length(4) + Revision(1) + Checksum(1) + OEMID(6) + 
    //                           OEM Table ID(8) + OEM Revision(4) + Creator ID(4) + Creator Revision(4)
    // - Reserved(4) + Reserved(8)
    // - Followed by variable-length subtable structures
    
    if (tableSize < 36)
    {
        DBG(1, "SRAT table too small");
        return;
    }
    
    // Verify signature
    if (std::memcmp(tableBuffer.data(), "SRAT", 4) != 0)
    {
        DBG(1, "Invalid SRAT table signature");
        return;
    }
    
    // Get table length from header using memcpy to avoid alignment issues
    uint32_t tableLength;
    std::memcpy(&tableLength, tableBuffer.data() + 4, sizeof(uint32_t));
    
    DBG(2, "SRAT table found, length: ", tableLength);
    
    // Skip ACPI header (36 bytes) + Reserved fields (12 bytes) = 48 bytes
    const BYTE* ptr = tableBuffer.data() + 48;
    const BYTE* endPtr = tableBuffer.data() + std::min((uint32_t)tableSize, tableLength);
    
    while (ptr + 2 <= endPtr)
    {
        uint8_t type = ptr[0];
        uint8_t length = ptr[1];
        
        if (ptr + length > endPtr)
        {
            DBG(2, "SRAT subtable extends beyond table boundary, stopping parse");
            break;
        }
        
        if (type == 2)  // PCI Device Affinity Structure
        {
            // Structure format (variable, at least 16 bytes):
            // Type(1) + Length(1) + Reserved(2) + 
            // Proximity Domain(4) + PCI Segment(2) + PCI Bus(1) + 
            // Device/Function(1) + Flags(4) + Reserved(4)
            
            if (length < 16)
            {
                DBG(2, "SRAT PCI Device Affinity structure too small: ", (int)length);
                ptr += length;
                continue;
            }
            
            // Use memcpy to avoid alignment issues
            uint32_t proximityDomain;
            uint16_t pciSegment;
            std::memcpy(&proximityDomain, ptr + 4, sizeof(uint32_t));
            std::memcpy(&pciSegment, ptr + 8, sizeof(uint16_t));
            uint8_t pciBus = ptr[10];
            uint8_t deviceFunction = ptr[11];
            uint8_t pciDevice = (deviceFunction >> 3) & 0x1F;
            uint8_t pciFunction = deviceFunction & 0x07;
            
            // Construct unique key: segment(16) | bus(8) | device(5) | function(3)
            uint64_t key = ((uint64_t)pciSegment << 16) | ((uint64_t)pciBus << 8) | 
                          ((uint64_t)pciDevice << 3) | pciFunction;
            
            pciToNuma[key] = proximityDomain;
            
            DBG(2, "SRAT: PCI ", std::hex, pciSegment, ":", (unsigned)pciBus, ":", 
                (unsigned)pciDevice, ".", (unsigned)pciFunction, 
                " -> NUMA node ", std::dec, proximityDomain);
        }
        
        ptr += length;
    }
    
    DBG(2, "SRAT parsing complete, found ", pciToNuma.size(), " PCI device entries");
}

#elif __APPLE__

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    bus(bus_),
    device(device_),
    function(function_)
{ }

int32 PciHandle::getNUMANode() const
{
    // macOS typically doesn't expose NUMA node information for PCI devices
    // Return -1 to indicate not available
    return -1;
}

bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    if (groupnr_ != 0)
    {
        std::cerr << "Non-zero PCI group segments are not supported in PCM/APPLE OSX\n";
        return false;
    }
    uint32_t pci_address = FORM_PCI_ADDR(bus_, device_, function_, 0);
    uint32_t value = 0;
    PCIDriver_read32(pci_address, &value);
    uint32_t vendor_id = value & 0xffff;
    uint32_t device_id = (value >> 16) & 0xffff;

    //if (vendor_id == PCM_INTEL_PCI_VENDOR_ID) {
    if (vendor_id != 0xffff && device_id != 0xffff) {
        return true;
    } else {
        return false;
    }
}

int32 PciHandle::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandle::read32", false, offset);
    uint32_t pci_address = FORM_PCI_ADDR(bus, device, function, (uint32_t)offset);
    return PCIDriver_read32(pci_address, value);
}

int32 PciHandle::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandle::write32", false, offset);
    uint32_t pci_address = FORM_PCI_ADDR(bus, device, function, (uint32_t)offset);
    return PCIDriver_write32(pci_address, value);
}

int32 PciHandle::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandle::read64", false, offset);
    uint32_t pci_address = FORM_PCI_ADDR(bus, device, function, (uint32_t)offset);
    return PCIDriver_read64(pci_address, value);
}

PciHandle::~PciHandle()
{ }

#elif defined (__FreeBSD__) || defined(__DragonFly__)

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    groupnr(groupnr_),
    bus(bus_),
    device(device_),
    function(function_)
{
    int handle = ::open("/dev/pci", O_RDWR | O_NOFOLLOW);
    if (handle < 0) {
        if (errno == ELOOP) {
            std::cerr << "SDL330 ERROR: Symlink detected at /dev/pci\n";
        }
        throw std::exception();
    }
    fd = handle;
}

int32 PciHandle::getNUMANode() const
{
    // FreeBSD implementation: try to query NUMA domain information via sysctl
    // Return -1 if not available or on error
    
#if defined(__FreeBSD__) || defined(__DragonFly__)
    // First check if NUMA is enabled on this system
    int ndomains = 0;
    size_t len = sizeof(ndomains);
    
    if (sysctlbyname("vm.ndomains", &ndomains, &len, nullptr, 0) == 0)
    {
        if (ndomains <= 1)
        {
            // NUMA not enabled or single domain system
            DBG(3, "NUMA not enabled on FreeBSD (vm.ndomains = ", ndomains, ")");
            return -1;
        }
    }
    else
    {
        DBG(2, "Cannot query vm.ndomains, assuming NUMA not available");
        return -1;
    }
    
    // Try platform-specific sysctl path for PCI device NUMA domain
    // Note: This is not standardized across FreeBSD versions
    // Buffer size: "hw.pci." (7) + max domain (10) + "." (1) + max bus (10) + "." (1) + 
    //              max device (10) + "." (1) + max function (10) + ".numa_domain" (12) + null (1) = 63
    // Use 128 to be safe
    constexpr size_t SYSCTL_PATH_MAX = 128;
    char sysctl_path[SYSCTL_PATH_MAX];
    int ret;
    
    ret = snprintf(sysctl_path, sizeof(sysctl_path), 
                   "hw.pci.%u.%u.%u.%u.numa_domain",
                   groupnr, bus, device, function);
    
    if (ret < 0 || ret >= (int)sizeof(sysctl_path))
    {
        DBG(2, "sysctl path truncated or error for PCI device ", 
            std::hex, groupnr, ":", bus, ":", device, ".", function, std::dec);
        return -1;
    }
    
    int numa_node = -1;
    len = sizeof(numa_node);
    
    if (sysctlbyname(sysctl_path, &numa_node, &len, nullptr, 0) == 0)
    {
        DBG(3, "Found NUMA node ", numa_node, " for PCI device ",
            std::hex, groupnr, ":", bus, ":", device, ".", function, std::dec);
        return numa_node;
    }
    
    // Try alternative sysctl format with colon separators
    ret = snprintf(sysctl_path, sizeof(sysctl_path), 
                   "hw.pci.%u:%u:%u.%u.numa_domain",
                   groupnr, bus, device, function);
    
    if (ret < 0 || ret >= (int)sizeof(sysctl_path))
    {
        DBG(2, "sysctl path truncated or error for PCI device ", 
            std::hex, groupnr, ":", bus, ":", device, ".", function, std::dec);
        return -1;
    }
    
    if (sysctlbyname(sysctl_path, &numa_node, &len, nullptr, 0) == 0)
    {
        DBG(3, "Found NUMA node ", numa_node, " for PCI device ",
            std::hex, groupnr, ":", bus, ":", device, ".", function, std::dec);
        return numa_node;
    }
    
    DBG(2, "NUMA node information not available for PCI device ",
        std::hex, groupnr, ":", bus, ":", device, ".", function, std::dec);
#endif
    
    return -1;
}

bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    struct pci_conf_io pc;
    struct pci_match_conf pattern;
    struct pci_conf conf[4];
    int fd;
    int ret;

    fd = ::open("/dev/pci", O_RDWR | O_NOFOLLOW, 0);
    if (fd < 0) {
        if (errno == ELOOP) {
            std::cerr << "SDL330 ERROR: Symlink detected at /dev/pci\n";
        }
        return false;
    }

    bzero(&pc, sizeof(pc));

    pattern.pc_sel.pc_domain = groupnr_;
    pattern.pc_sel.pc_bus = bus_;
    pattern.pc_sel.pc_dev = device_;
    pattern.pc_sel.pc_func = function_;
    pattern.flags = (pci_getconf_flags)(PCI_GETCONF_MATCH_DOMAIN | PCI_GETCONF_MATCH_BUS | PCI_GETCONF_MATCH_DEV | PCI_GETCONF_MATCH_FUNC);

    pc.pat_buf_len = sizeof(pattern);
    pc.patterns = &pattern;
    pc.num_patterns = 1;
    pc.match_buf_len = sizeof(conf);
    pc.matches = conf;

    ret = ioctl(fd, PCIOCGETCONF, &pc);
    ::close(fd);

    if (ret) return false;

    if (pc.status != PCI_GETCONF_LAST_DEVICE) return false;

    if (pc.num_matches > 0) return true;

    return false;
}

int32 PciHandle::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandle::read32", false, offset);
    struct pci_io pi;
    int ret;

    pi.pi_sel.pc_domain = groupnr;
    pi.pi_sel.pc_bus = bus;
    pi.pi_sel.pc_dev = device;
    pi.pi_sel.pc_func = function;
    pi.pi_reg = offset;
    pi.pi_width = 4;

    ret = ioctl(fd, PCIOCREAD, &pi);
    if (ret) return ret;

    *value = pi.pi_data;
    return sizeof(*value);
}

int32 PciHandle::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandle::write32", false, offset);
    struct pci_io pi;
    int ret;

    pi.pi_sel.pc_domain = groupnr;
    pi.pi_sel.pc_bus = bus;
    pi.pi_sel.pc_dev = device;
    pi.pi_sel.pc_func = function;
    pi.pi_reg = offset;
    pi.pi_width = 4;
    pi.pi_data = value;

    ret = ioctl(fd, PCIOCWRITE, &pi);
    if (ret) return ret;

    return sizeof(value);
}

int32 PciHandle::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandle::read64", false, offset);
    struct pci_io pi;
    int32 ret;

    pi.pi_sel.pc_domain = groupnr;
    pi.pi_sel.pc_bus = bus;
    pi.pi_sel.pc_dev = device;
    pi.pi_sel.pc_func = function;
    pi.pi_reg = offset;
    pi.pi_width = 4;

    ret = ioctl(fd, PCIOCREAD, &pi);
    if (ret) return ret;

    *value = pi.pi_data;

    pi.pi_reg += 4;

    ret = ioctl(fd, PCIOCREAD, &pi);
    if (ret) return ret;

    *value += ((uint64)pi.pi_data << 32);
    return sizeof(value);
}

PciHandle::~PciHandle()
{
    if (fd >= 0) ::close(fd);
}

#else


// Linux implementation

// Helper function to retrieve NUMA node for a PCI device
static int32 getNUMANodeLinux(uint32 groupnr, uint32 bus, uint32 device, uint32 function)
{
    std::ostringstream path;
    path << std::hex << "/sys/bus/pci/devices/"
         << std::setw(4) << std::setfill('0') << groupnr << ":"
         << std::setw(2) << std::setfill('0') << bus << ":"
         << std::setw(2) << std::setfill('0') << device << "."
         << function << "/numa_node";
    
    std::string numa_path = path.str();
    std::ifstream numa_file(numa_path);
    if (!numa_file.is_open())
    {
        // Try alternative path with /pcm prefix (follows existing codebase pattern
        // for containerized or chroot environments where sysfs may be mounted under /pcm)
        numa_file.open("/pcm" + numa_path);
        if (!numa_file.is_open())
        {
            DBG(2, "Cannot open NUMA node file: ", numa_path);
            return -1;
        }
    }
    
    int32 numa_node = -1;
    numa_file >> numa_node;
    
    DBG(3, "NUMA node for ", std::hex, std::setw(4), std::setfill('0'), groupnr, ":",
        std::setw(2), bus, ":", std::setw(2), device, ".", function, std::dec, " is ", numa_node);
    
    return numa_node;
}

int openHandle(uint32 groupnr_, uint32 bus, uint32 device, uint32 function)
{
    std::ostringstream path(std::ostringstream::out);

    path << std::hex << "/proc/bus/pci/";
    if (groupnr_)
    {
        path << std::setw(4) << std::setfill('0') << groupnr_ << ":";
    }
    path << std::setw(2) << std::setfill('0') << bus << "/" << std::setw(2) << std::setfill('0') << device << "." << function;

//    std::cout << "PciHandle: Opening "<<path.str()<<"\n";

    int handle = ::open(path.str().c_str(), O_RDWR | O_NOFOLLOW);
    if (handle < 0)
    {
       if (errno == 24) std::cerr << "ERROR: " << PCM_ULIMIT_RECOMMENDATION;
       if (errno == ELOOP) std::cerr << "SDL330 ERROR: Symlink detected at " << path.str() << "\n";
       handle = ::open((std::string("/pcm") + path.str()).c_str(), O_RDWR | O_NOFOLLOW);
       if (handle < 0 && errno == ELOOP) {
           std::cerr << "SDL330 ERROR: Symlink detected at /pcm" << path.str() << "\n";
       }
    }
    return handle;
}

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    groupnr(groupnr_),
    bus(bus_),
    device(device_),
    function(function_)
{
    int handle = openHandle(groupnr_, bus_, device_, function_);
    if (handle < 0)
    {
        throw std::runtime_error(std::string("PCM error: can't open PciHandle ")
            + std::to_string(groupnr_) + ":" + std::to_string(bus_) + ":" + std::to_string(device_) + ":" + std::to_string(function_));
    }
    fd = handle;

    // std::cout << "DEBUG: Opened "<< path.str().c_str() << " on handle "<< fd << "\n";
}

int32 PciHandle::getNUMANode() const
{
    return getNUMANodeLinux(groupnr, bus, device, function);
}


bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    int handle = openHandle(groupnr_, bus_, device_, function_);

    if (handle < 0) return false;

    ::close(handle);

    return true;
}

int32 PciHandle::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandle::read32", false, offset);
    return ::pread(fd, (void *)value, sizeof(uint32), offset);
}

int32 PciHandle::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandle::write32", false, offset);
    return ::pwrite(fd, (const void *)&value, sizeof(uint32), offset);
}

int32 PciHandle::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandle::read64", false, offset);
    size_t res = ::pread(fd, (void *)value, sizeof(uint64), offset);
    if(res != sizeof(uint64))
    {
        std::cerr << " ERROR: pread from " << fd << " with offset 0x" << std::hex << offset << std::dec << " returned " << res << " bytes \n";
    }
    return res;
}

PciHandle::~PciHandle()
{
    if (fd >= 0) ::close(fd);
}

int PciHandle::openMcfgTable() {
    const std::vector<std::string> base_paths = {"/sys/firmware/acpi/tables/MCFG", "/sys/firmware/acpi/tables/MCFG1"};
    std::vector<std::string> paths = base_paths;
    for (const auto & p: base_paths)
    {
        paths.push_back(std::string("/pcm") + p);
    }
    int handle = -1;
    for (const auto & p: paths)
    {
        if (handle < 0)
        {
            handle = ::open(p.c_str(), O_RDONLY);
        }
    }
    if (handle < 0)
    {
        for (const auto & p: paths)
        {
            std::cerr << "Can't open MCFG table. Check permission of " << p << "\n";
        }
    }
    return handle;
}

// mmapped I/O version

MCFGHeader PciHandleMM::mcfgHeader;
std::vector<MCFGRecord> PciHandleMM::mcfgRecords;

const std::vector<MCFGRecord> & PciHandleMM::getMCFGRecords()
{
    readMCFG();
    return mcfgRecords;
}

void PciHandleMM::readMCFG()
{
    if (mcfgRecords.size() > 0)
        return; // already initialized

    int mcfg_handle = PciHandle::openMcfgTable();
    if (mcfg_handle < 0) throw std::runtime_error("cannot open any of /[pcm]/sys/firmware/acpi/tables/MCFG* files!");

    ssize_t read_bytes = ::read(mcfg_handle, (void *)&mcfgHeader, sizeof(MCFGHeader));

    if (read_bytes == 0)
    {
        ::close(mcfg_handle);
        const auto msg = "PCM Error: Cannot read MCFG-table";
        std::cerr << msg;
        std::cerr << "\n";
        throw std::runtime_error(msg);
    }

    const unsigned segments = mcfgHeader.nrecords();
#ifdef PCM_DEBUG
    mcfgHeader.print();
    std::cout << "PCM Debug: total segments: " << segments << "\n";
#endif

    for (unsigned int i = 0; i < segments; ++i)
    {
        MCFGRecord record;
        read_bytes = ::read(mcfg_handle, (void *)&record, sizeof(MCFGRecord));
        if (read_bytes == 0)
        {
            ::close(mcfg_handle);
            const auto msg = "PCM Error: Cannot read MCFG-table (2)";
            std::cerr << msg;
            std::cerr << "\n";
            throw std::runtime_error(msg);
        }
#ifdef PCM_DEBUG
        std::cout << "PCM Debug: segment " << std::dec << i << " ";
        record.print();
#endif
        mcfgRecords.push_back(record);
    }

    ::close(mcfg_handle);
}

PciHandleMM::PciHandleMM(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    mmapAddr(NULL),
    groupnr(groupnr_),
    bus(bus_),
    device(device_),
    function(function_),
    base_addr(0)
{
    int handle = ::open("/dev/mem", O_RDWR | O_NOFOLLOW);
    if (handle < 0) {
        if (errno == ELOOP) {
            std::cerr << "SDL330 CRITICAL: Symlink detected at /dev/mem - potential privilege escalation attack!\n";
        }
        throw std::exception();
    }
    fd = handle;

    readMCFG();

    unsigned segment = 0;
    for ( ; segment < mcfgRecords.size(); ++segment)
    {
        if (mcfgRecords[segment].PCISegmentGroupNumber == groupnr_
            && mcfgRecords[segment].startBusNumber <= bus_
            && bus <= mcfgRecords[segment].endBusNumber)
            break;
    }
    if (segment == mcfgRecords.size())
    {
        std::cerr << "PCM Error: (group " << groupnr_ << ", bus " << bus_ << ") not found in the MCFG table.\n";
        throw std::exception();
    }
    else
    {
#ifdef PCM_DEBUG
        std::cout << "PCM Debug: (group " << groupnr_ << ", bus " << bus_ << ") found in the MCFG table in segment " << segment << "\n";
#endif
    }

    base_addr = mcfgRecords[segment].baseAddress;

    base_addr += (bus * 1024ULL * 1024ULL + device * 32ULL * 1024ULL + function * 4ULL * 1024ULL);

    mmapAddr = (char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base_addr);

    if (mmapAddr == MAP_FAILED)
    {
        std::cout << "mmap failed: errno is " << errno << "\n";
        throw std::exception();
    }
}

int32 PciHandleMM::getNUMANode() const
{
    return getNUMANodeLinux(groupnr, bus, device, function);
}

bool PciHandleMM::exists(uint32 /*groupnr_*/, uint32 /*bus_*/, uint32 /*device_*/, uint32 /*function_*/)
{
    int handle = ::open("/dev/mem", O_RDWR | O_NOFOLLOW);

    if (handle < 0) {
        if (errno == ELOOP) {
            std::cerr << "SDL330 CRITICAL: Symlink detected at /dev/mem - potential privilege escalation attack!\n";
        }
        perror("error opening /dev/mem");
        return false;
    }

    ::close(handle);

    handle = PciHandle::openMcfgTable();

    if (handle < 0) {
        return false;
    }

    ::close(handle);

    return true;
}


int32 PciHandleMM::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandleMM::read32", false, offset);
    *value = *((uint32 *)(mmapAddr + offset));

    return sizeof(uint32);
}

int32 PciHandleMM::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandleMM::write32", false, offset);
    *((uint32 *)(mmapAddr + offset)) = value;

    return sizeof(uint32);
}

int32 PciHandleMM::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandleMM::read64", false, offset);
    read32(offset, (uint32 *)value);
    read32(offset + sizeof(uint32), ((uint32 *)value) + 1);

    return sizeof(uint64);
}

PciHandleMM::~PciHandleMM()
{
    if (mmapAddr) munmap(mmapAddr, 4096);
    if (fd >= 0) ::close(fd);
}

#endif

} // namespace pcm
