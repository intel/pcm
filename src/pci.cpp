// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev,
//            Pat Fay
//	      Austen Ott
//            Jim Harris (FreeBSD)

#include <iostream>
#include <stdexcept>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
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

#elif __APPLE__

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    bus(bus_),
    device(device_),
    function(function_)
{ }

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
