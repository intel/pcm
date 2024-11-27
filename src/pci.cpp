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
#include "pci.h"

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

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
    bus(bus_),
    device(device_),
    function(function_),
    pciAddress(PciBusDevFunc(bus_, device_, function_))
{
    if (groupnr_ != 0)
    {
        std::cerr << "Non-zero PCI group segments are not supported in PCM/Windows\n";
        throw std::exception();
    }

    hDriver = openMSRDriver();

    if (hDriver == INVALID_HANDLE_VALUE && hOpenLibSys == NULL)
        throw std::exception();
}

bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    if (groupnr_ != 0)
    {
        std::cerr << "Non-zero PCI group segments are not supported in PCM/Windows\n";
        return false;
    }
    if (hOpenLibSys != NULL)
    {
        DWORD addr(PciBusDevFunc(bus_, device_, function_));
        DWORD result = 0;
        return ReadPciConfigDwordEx(addr, 0, &result)?true:false;
    }

    HANDLE tempHandle = openMSRDriver();
    if (tempHandle == INVALID_HANDLE_VALUE)
        return false;

    // TODO: check device availability

    CloseHandle(tempHandle);

    return true;
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
            //std::cerr << "Error reading PCI Config space at bus "<<bus<<" dev "<< device<<" function "<< function <<" offset "<< offset << " size "<< req.bytes  << ". Windows error: "<<GetLastError()<<"\n";
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
            //std::cerr << "Error writing PCI Config space at bus "<<bus<<" dev "<< device<<" function "<< function <<" offset "<< offset << " size "<< req.bytes  << ". Windows error: "<<GetLastError()<<"\n";
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
        PCICFG_Request req;
        // ULONG64 result;
        DWORD reslength = 0;
        req.bus = bus;
        req.dev = device;
        req.func = function;
        req.bytes = sizeof(uint64);
        req.reg = (uint32)offset;

        BOOL status = DeviceIoControl(hDriver, IO_CTL_PCICFG_READ, &req, (DWORD)sizeof(PCICFG_Request), value, (DWORD)sizeof(uint64), &reslength, NULL);
        if (!status)
        {
            //std::cerr << "Error reading PCI Config space at bus "<<bus<<" dev "<< device<<" function "<< function <<" offset "<< offset << " size "<< req.bytes  << ". Windows error: "<<GetLastError()<<"\n";
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

#elif __APPLE__

PciHandle::PciHandle(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_) :
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
    bus(bus_),
    device(device_),
    function(function_)
{
    if (groupnr_ != 0)
    {
        std::cout << "ERROR: non-zero PCI segment groupnr is not supported in this PciHandle implementation\n";
        throw std::exception();
    }

    int handle = ::open("/dev/pci", O_RDWR);
    if (handle < 0) throw std::exception();
    fd = handle;
}

bool PciHandle::exists(uint32 groupnr_, uint32 bus_, uint32 device_, uint32 function_)
{
    if (groupnr_ != 0)
    {
        std::cerr << "Non-zero PCI group segments are not supported in PCM/FreeBSD/DragonFlyBSD\n";
        return false;
    }
    struct pci_conf_io pc;
    struct pci_match_conf pattern;
    struct pci_conf conf[4];
    int fd;
    int ret;

    fd = ::open("/dev/pci", O_RDWR, 0);
    if (fd < 0) return false;

    bzero(&pc, sizeof(pc));

    pattern.pc_sel.pc_bus = bus_;
    pattern.pc_sel.pc_dev = device_;
    pattern.pc_sel.pc_func = function_;
    pattern.flags = (pci_getconf_flags)(PCI_GETCONF_MATCH_BUS | PCI_GETCONF_MATCH_DEV | PCI_GETCONF_MATCH_FUNC);

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

    pi.pi_sel.pc_domain = 0;
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

    pi.pi_sel.pc_domain = 0;
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

    pi.pi_sel.pc_domain = 0;
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

    int handle = ::open(path.str().c_str(), O_RDWR);
    if (handle < 0)
    {
       if (errno == 24) std::cerr << "ERROR: " << PCM_ULIMIT_RECOMMENDATION;
       handle = ::open((std::string("/pcm") + path.str()).c_str(), O_RDWR);
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

#ifndef PCM_USE_PCI_MM_LINUX

PciHandleM::PciHandleM(uint32 bus_, uint32 device_, uint32 function_) :
    fd(-1),
    bus(bus_),
    device(device_),
    function(function_),
    base_addr(0)
{
    int handle = ::open("/dev/mem", O_RDWR);
    if (handle < 0) throw std::exception();
    fd = handle;

    int mcfg_handle = PciHandle::openMcfgTable();
    if (mcfg_handle < 0) throw std::runtime_error("Cannot open any of /[pcm]/sys/firmware/acpi/tables/MCFG* files!");

    int32 result = ::pread(mcfg_handle, (void *)&base_addr, sizeof(uint64), 44);

    if (result != sizeof(uint64))
    {
        ::close(mcfg_handle);
        throw std::exception();
    }

    unsigned char max_bus = 0;

    result = ::pread(mcfg_handle, (void *)&max_bus, sizeof(unsigned char), 55);

    ::close(mcfg_handle);
    if (result != sizeof(unsigned char))
    {
        throw std::exception();
    }

    if (bus > (unsigned)max_bus)
    {
        std::cout << "WARNING: Requested bus number " << bus << " is larger than the max bus number " << (unsigned)max_bus << "\n";
        throw std::exception();
    }

    // std::cout << "PCI config base addr: "<< std::hex << base_addr<< "\n" << std::dec;

    base_addr += (bus * 1024ULL * 1024ULL + device * 32ULL * 1024ULL + function * 4ULL * 1024ULL);
}


bool PciHandleM::exists(uint32 /*groupnr_*/, uint32 /* bus_*/, uint32 /* device_ */, uint32 /* function_ */)
{
    int handle = ::open("/dev/mem", O_RDWR);

    if (handle < 0) {
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

int32 PciHandleM::read32(uint64 offset, uint32 * value)
{
    warnAlignment<4>("PciHandleM::read32", false, offset);
    return ::pread(fd, (void *)value, sizeof(uint32), offset + base_addr);
}

int32 PciHandleM::write32(uint64 offset, uint32 value)
{
    warnAlignment<4>("PciHandleM::write32", false, offset);
    return ::pwrite(fd, (const void *)&value, sizeof(uint32), offset + base_addr);
}

int32 PciHandleM::read64(uint64 offset, uint64 * value)
{
    warnAlignment<4>("PciHandleM::read64", false, offset);
    return ::pread(fd, (void *)value, sizeof(uint64), offset + base_addr);
}

PciHandleM::~PciHandleM()
{
    if (fd >= 0) ::close(fd);
}

#endif // PCM_USE_PCI_MM_LINUX

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
    int handle = ::open("/dev/mem", O_RDWR);
    if (handle < 0) throw std::exception();
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
    int handle = ::open("/dev/mem", O_RDWR);

    if (handle < 0) {
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
