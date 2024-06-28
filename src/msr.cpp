// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev
//            Austen Ott
//            Jim Harris (FreeBSD)

#include <sys/types.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include "types.h"
#include "msr.h"
#include "utils.h"
#include <assert.h>

#ifdef _MSC_VER

#include <windows.h>
#include "utils.h"
#include "Winmsrdriver\msrstruct.h"
#include "winring0/OlsApiInitExt.h"

#endif

#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/ioccom.h>
#include <sys/cpuctl.h>
#endif

#include <mutex>

namespace pcm {

#ifdef _MSC_VER

extern HMODULE hOpenLibSys;

// here comes an implementation for Windows
MsrHandle::MsrHandle(uint32 cpu) : cpu_id(cpu)
{
    hDriver = openMSRDriver();

    if (hDriver == INVALID_HANDLE_VALUE && hOpenLibSys == NULL)
        throw std::exception();
}

MsrHandle::~MsrHandle()
{
    if (hDriver != INVALID_HANDLE_VALUE) CloseHandle(hDriver);
}

int32 MsrHandle::write(uint64 msr_number, uint64 value)
{
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        MSR_Request req;
        ULONG64 result;
        DWORD reslength = 0;
        req.core_id = cpu_id;
        req.msr_address = msr_number;
        req.write_value = value;
        BOOL status = DeviceIoControl(hDriver, IO_CTL_MSR_WRITE, &req, sizeof(MSR_Request), &result, sizeof(uint64), &reslength, NULL);
        assert(status && "Error in DeviceIoControl");
        return status ? sizeof(uint64) : 0;
    }

    cvt_ds cvt;
    cvt.ui64 = value;

    ThreadGroupTempAffinity affinity(cpu_id);
    DWORD status = Wrmsr((DWORD)msr_number, cvt.ui32.low, cvt.ui32.high);

    return status ? sizeof(uint64) : 0;
}

int32 MsrHandle::read(uint64 msr_number, uint64 * value)
{
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        MSR_Request req;
        // ULONG64 result;
        DWORD reslength = 0;
        req.core_id = cpu_id;
        req.msr_address = msr_number;
        BOOL status = DeviceIoControl(hDriver, IO_CTL_MSR_READ, &req, sizeof(MSR_Request), value, sizeof(uint64), &reslength, NULL);
        assert(status && "Error in DeviceIoControl");
        return (int32)reslength;
    }

    cvt_ds cvt;
    cvt.ui64 = 0;

    ThreadGroupTempAffinity affinity(cpu_id);
    DWORD status = Rdmsr((DWORD)msr_number, &(cvt.ui32.low), &(cvt.ui32.high));

    if (status) *value = cvt.ui64;

    return status ? sizeof(uint64) : 0;
}

#elif __APPLE__
// OSX Version

MSRAccessor * MsrHandle::driver = NULL;
int MsrHandle::num_handles = 0;

MsrHandle::MsrHandle(uint32 cpu)
{
    cpu_id = cpu;
    if (!driver)
    {
        driver = new MSRAccessor();
        MsrHandle::num_handles = 1;
    }
    else
    {
        MsrHandle::num_handles++;
    }
}

MsrHandle::~MsrHandle()
{
    MsrHandle::num_handles--;
    if (MsrHandle::num_handles == 0)
    {
        deleteAndNullify(driver);
    }
}

int32 MsrHandle::write(uint64 msr_number, uint64 value)
{
    return driver->write(cpu_id, msr_number, value);
}

int32 MsrHandle::read(uint64 msr_number, uint64 * value)
{
    return driver->read(cpu_id, msr_number, value);
}

int32 MsrHandle::buildTopology(uint32 num_cores, void * ptr)
{
    return driver->buildTopology(num_cores, ptr);
}

uint32 MsrHandle::getNumInstances()
{
    return driver->getNumInstances();
}

uint32 MsrHandle::incrementNumInstances()
{
    return driver->incrementNumInstances();
}

uint32 MsrHandle::decrementNumInstances()
{
    return driver->decrementNumInstances();
}

#elif defined(__FreeBSD__) || defined(__DragonFly__)

MsrHandle::MsrHandle(uint32 cpu) : fd(-1), cpu_id(cpu)
{
    char path[200];
    snprintf(path, 200, "/dev/cpuctl%d", cpu);
    int handle = ::open(path, O_RDWR);
    if (handle < 0) throw std::exception();
    fd = handle;
}

MsrHandle::~MsrHandle()
{
    if (fd >= 0) ::close(fd);
}

int32 MsrHandle::write(uint64 msr_number, uint64 value)
{
    cpuctl_msr_args_t args;
    int ret;

    args.msr = msr_number;
    args.data = value;
    ret = ::ioctl(fd, CPUCTL_WRMSR, &args);
    if (ret) return ret;
    return sizeof(value);
}

int32 MsrHandle::read(uint64 msr_number, uint64 * value)
{
    cpuctl_msr_args_t args;
    int32 ret;

    args.msr = msr_number;
    ret = ::ioctl(fd, CPUCTL_RDMSR, &args);
    if (ret) return ret;
    *value = args.data;
    return sizeof(*value);
}

#else
// here comes a Linux version

bool noMSRMode()
{
    static int noMSR = -1;
    if (noMSR < 0)
    {
        noMSR = (safe_getenv("PCM_NO_MSR") == std::string("1")) ? 1 : 0;
    }
    return 1 == noMSR;
}

MsrHandle::MsrHandle(uint32 cpu) : fd(-1), cpu_id(cpu)
{
    if (noMSRMode()) return;
    constexpr auto allowWritesPath = "/sys/module/msr/parameters/allow_writes";
    static bool writesEnabled = false;
    if (writesEnabled == false)
    {
        if (readSysFS(allowWritesPath, true).length() > 0)
        {
            writeSysFS(allowWritesPath, "on", false);
        }
        writesEnabled = true;
    }
    char * path = new char[200];
    if (!path) throw std::runtime_error("Allocation of 200 bytes failed.");
    snprintf(path, 200, "/dev/cpu/%d/msr", cpu);
    int handle = ::open(path, O_RDWR);
    if (handle < 0)
    {   // try Android msr device path
        snprintf(path, 200, "/dev/msr%d", cpu);
        handle = ::open(path, O_RDWR);
    }
    deleteAndNullifyArray(path);
    if (handle < 0)
    {
         std::cerr << "PCM Error: can't open MSR handle for core " << cpu << " (" << strerror(errno) << ")\n";
         std::cerr << "Try no-MSR mode by setting env variable PCM_NO_MSR=1\n";
         throw std::exception();
    }
    fd = handle;
}

MsrHandle::~MsrHandle()
{
    if (fd >= 0) ::close(fd);
}

int32 MsrHandle::write(uint64 msr_number, uint64 value)
{
#if 0
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    std::cout << "DEBUG: writing MSR 0x" << std::hex << msr_number << " value 0x" << value << " on cpu " << std::dec << cpu_id << std::endl;
#endif
    if (fd < 0) return 0;
    return ::pwrite(fd, (const void *)&value, sizeof(uint64), msr_number);
}

int32 MsrHandle::read(uint64 msr_number, uint64 * value)
{
    if (fd < 0) return 0;
    return ::pread(fd, (void *)value, sizeof(uint64), msr_number);
}

#endif


#ifndef __linux__
bool noMSRMode() { return false; }
#endif

} // namespace pcm
