// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev,
//            Patrick Konsor
//

#include <iostream>
#include <string.h>
#ifndef _MSC_VER
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include "pci.h"
#include "mmio.h"

#ifndef _MSC_VER
#include <sys/mman.h>
#include <errno.h>
#endif

#ifdef _MSC_VER
#include <windows.h>
#endif

#include "utils.h"
#include <exception>
#include <assert.h>

namespace pcm {

#ifdef _MSC_VER

class PCMPmem : public WinPmem {
protected:
    virtual int load_driver_()
    {
        SYSTEM_INFO sys_info;
        SecureZeroMemory(&sys_info, sizeof(sys_info));

        GetCurrentDirectory(MAX_PATH - 10, driver_filename);

        GetNativeSystemInfo(&sys_info);
        switch (sys_info.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64:
            _tcscat_s(driver_filename, MAX_PATH, TEXT("\\winpmem_x64.sys"));
            if (GetFileAttributes(driver_filename) == INVALID_FILE_ATTRIBUTES)
            {
                std::cerr << "ERROR: winpmem_x64.sys not found in current directory. Download it from https://github.com/Velocidex/WinPmem/blob/master/kernel/binaries/winpmem_x64.sys .\n";
                std::cerr << "ERROR: Memory bandwidth statistics will not be available.\n";
            }
            break;
        case PROCESSOR_ARCHITECTURE_INTEL:
            _tcscat_s(driver_filename, MAX_PATH, TEXT("\\winpmem_x86.sys"));
            if (GetFileAttributes(driver_filename) == INVALID_FILE_ATTRIBUTES)
            {
                std::cerr << "ERROR: winpmem_x86.sys not found in current directory. Download it from https://github.com/Velocidex/WinPmem/blob/master/kernel/binaries/winpmem_x86.sys .\n";
                std::cerr << "ERROR: Memory bandwidth statistics will not be available.\n";
            }
            break;
        default:
            return -1;
        }
        return 1;
    }
    virtual int write_crashdump_header_(struct PmemMemoryInfo * info)
    {
        return -1;
    }
};

std::shared_ptr<WinPmem> WinPmemMMIORange::pmem;
Mutex WinPmemMMIORange::mutex;
bool WinPmemMMIORange::writeSupported = false;

WinPmemMMIORange::WinPmemMMIORange(uint64 baseAddr_, uint64 /* size_ */, bool readonly_) : startAddr(baseAddr_), readonly(readonly_)
{
    mutex.lock();
    if (pmem.get() == NULL)
    {
        pmem = std::make_shared<PCMPmem>();
        pmem->install_driver(false);
        pmem->set_acquisition_mode(PMEM_MODE_IOSPACE);
        writeSupported = pmem->toggle_write_mode() >= 0; // since it is a global object enable write mode just in case someone needs it
    }
    mutex.unlock();
}

MMIORange::MMIORange(uint64 baseAddr_, uint64 size_, bool readonly_)
{
    auto hDriver = openMSRDriver();
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        DWORD reslength = 0;
        uint64 result = 0;
        const BOOL status = DeviceIoControl(hDriver, IO_CTL_MMAP_SUPPORT, NULL, 0, &result, sizeof(uint64), &reslength, NULL);
        CloseHandle(hDriver);
        if (status == TRUE && reslength == sizeof(uint64) && result == 1)
        {
            impl = std::make_shared<OwnMMIORange>(baseAddr_, size_, readonly_);
            return;
        }
        else
        {
            std::cerr << "MSR.sys does not support mmap operations\n";
        }
    }

    impl = std::make_shared<WinPmemMMIORange>(baseAddr_, size_, readonly_);
}

OwnMMIORange::OwnMMIORange(uint64 baseAddr_, uint64 size_, bool /* readonly_ */)
{
    hDriver = openMSRDriver();
    MMAP_Request req{};
    uint64 result = 0;
    DWORD reslength = 0;
    req.address.QuadPart = baseAddr_;
    req.size = size_;
    const BOOL status = DeviceIoControl(hDriver, IO_CTL_MMAP, &req, sizeof(MMAP_Request), &result, sizeof(uint64), &reslength, NULL);
    if (status == FALSE || result == 0)
    {
        std::cerr << "Error mapping address 0x" << std::hex << req.address.QuadPart << " with size " << std::dec << req.size << "\n";
        throw std::runtime_error("OwnMMIORange error");
    }
    mmapAddr = (char*)result;
}

uint32 OwnMMIORange::read32(uint64 offset)
{
    return *((uint32*)(mmapAddr + offset));
}

uint64 OwnMMIORange::read64(uint64 offset)
{
    return *((uint64*)(mmapAddr + offset));
}

void OwnMMIORange::write32(uint64 offset, uint32 val)
{
    *((uint32*)(mmapAddr + offset)) = val;
}
void OwnMMIORange::write64(uint64 offset, uint64 val)
{
    *((uint64*)(mmapAddr + offset)) = val;
}

OwnMMIORange::~OwnMMIORange()
{
    MMAP_Request req{};
    uint64 result = 0;
    DWORD reslength = 0;
    req.address.QuadPart = (LONGLONG)mmapAddr;
    req.size = 0;
    DeviceIoControl(hDriver, IO_CTL_MUNMAP, &req, sizeof(MMAP_Request), &result, sizeof(uint64), &reslength, NULL);
    CloseHandle(hDriver);
}

#elif __APPLE__

#include "PCIDriverInterface.h"

MMIORange::MMIORange(uint64 physical_address, uint64 size_, bool) :
    mmapAddr(NULL),
    size(size_)
{
    if (size > 4096)
    {
        std::cerr << "PCM Error: the driver does not support mapping of regions > 4KB\n";
        return;
    }
    if (physical_address) {
        PCIDriver_mapMemory((uint32_t)physical_address, (uint8_t **)&mmapAddr);
    }
}

uint32 MMIORange::read32(uint64 offset)
{
    uint32 val = 0;
    PCIDriver_readMemory32((uint8_t *)mmapAddr + offset, &val);
    return val;
}

uint64 MMIORange::read64(uint64 offset)
{
    uint64 val = 0;
    PCIDriver_readMemory64((uint8_t *)mmapAddr + offset, &val);
    return val;
}

void MMIORange::write32(uint64 offset, uint32 val)
{
    std::cerr << "PCM Error: the driver does not support writing to MMIORange\n";
}
void MMIORange::write64(uint64 offset, uint64 val)
{
    std::cerr << "PCM Error: the driver does not support writing to MMIORange\n";
}

MMIORange::~MMIORange()
{
    if(mmapAddr) PCIDriver_unmapMemory((uint8_t *)mmapAddr);
}

#elif defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)

MMIORange::MMIORange(uint64 baseAddr_, uint64 size_, bool readonly_) :
    fd(-1),
    mmapAddr(NULL),
    size(size_),
    readonly(readonly_)
{
    const int oflag = readonly ? O_RDONLY : O_RDWR;
    int handle = ::open("/dev/mem", oflag);
    if (handle < 0)
    {
       std::cerr << "opening /dev/mem failed: errno is " << errno << " (" << strerror(errno) << ")\n";
       throw std::exception();
    }
    fd = handle;

    const int prot = readonly ? PROT_READ : (PROT_READ | PROT_WRITE);
    mmapAddr = (char *)mmap(NULL, size, prot, MAP_SHARED, fd, baseAddr_);

    if (mmapAddr == MAP_FAILED)
    {
        std::cerr << "mmap failed: errno is " << errno << " (" << strerror(errno) << ")\n";
        if (1 == errno)
        {
            std::cerr << "Try to add 'iomem=relaxed' parameter to the kernel boot command line and reboot.\n";
        }
        throw std::exception();
    }
}

uint32 MMIORange::read32(uint64 offset)
{
    return *((uint32 *)(mmapAddr + offset));
}

uint64 MMIORange::read64(uint64 offset)
{
    return *((uint64 *)(mmapAddr + offset));
}

void MMIORange::write32(uint64 offset, uint32 val)
{
    if (readonly)
    {
        std::cerr << "PCM Error: attempting to write to a read-only MMIORange\n";
        return;
    }
    *((uint32 *)(mmapAddr + offset)) = val;
}
void MMIORange::write64(uint64 offset, uint64 val)
{
    if (readonly)
    {
        std::cerr << "PCM Error: attempting to write to a read-only MMIORange\n";
        return;
    }
    *((uint64 *)(mmapAddr + offset)) = val;
}

MMIORange::~MMIORange()
{
    if (mmapAddr) munmap(mmapAddr, size);
    if (fd >= 0) ::close(fd);
}

#endif

void mmio_memcpy(void * dest_, const uint64 src, const size_t n, const bool checkFailures)
{
    assert((src % sizeof(uint32)) == 0);
    assert((n % sizeof(uint32)) == 0);

    const uint64 end = src + n;
    const uint64 mapBegin = roundDownTo4K(src);
    const uint64 mapSize = roundUpTo4K(end) - mapBegin;
    uint32 * dest = (uint32 *)dest_;
    MMIORange range(mapBegin, mapSize);

    for (uint64 i = src; i < end; i += sizeof(uint32), ++dest)
    {
        const auto value = range.read32(i - mapBegin);
        if (checkFailures && value == ~uint32(0))
        {
            // a bad read
            std::ostringstream strstr;
            strstr << "Failed to read memory at 0x" << std::hex << i << std::dec << "\n";
            std::cerr << strstr.str();
            throw std::runtime_error(strstr.str());
        }
        *dest = value;
    }
}

} // namespace pcm
