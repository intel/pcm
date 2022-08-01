// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012-2019, Intel Corporation
// written by Roman Dementiev
//            Patrick Konsor
//

#pragma once

/*!     \file mmio.h
        \brief Interface to access memory mapped IO registers

*/

#include "types.h"

#ifdef _MSC_VER
#include "windows.h"
#include "winpmem\winpmem.h"
#include "Winmsrdriver\msrstruct.h"
#else
#include <unistd.h>
#endif

#include "mutex.h"
#include <memory>

namespace pcm {

#ifdef _MSC_VER

class MMIORangeInterface
{
public:
    virtual uint32 read32(uint64 offset) = 0;
    virtual uint64 read64(uint64 offset) = 0;
    virtual void write32(uint64 offset, uint32 val) = 0;
    virtual void write64(uint64 offset, uint64 val) = 0;
    virtual ~MMIORangeInterface() {}
};

class WinPmemMMIORange : public MMIORangeInterface
{
    static std::shared_ptr<WinPmem> pmem;
    static Mutex mutex;
    static bool writeSupported;
    uint64 startAddr;

    template <class T>
    void writeInternal(uint64 offset, T val)
    {
        if (!writeSupported)
        {
            std::cerr << "PCM Error: MMIORange writes are not supported by the driver\n";
            return;
        }
        if (readonly)
        {
            std::cerr << "PCM Error: attempting to write to a read-only MMIORange\n";
            return;
        }
        mutex.lock();
        pmem->write(startAddr + offset, val);
        mutex.unlock();
    }
    template <class T>
    void readInternal(uint64 offset, T & res)
    {
        mutex.lock();
        pmem->read(startAddr + offset, res);
        mutex.unlock();
    }
    const bool readonly;
public:
    WinPmemMMIORange(uint64 baseAddr_, uint64 size_, bool readonly_ = true);
    uint32 read32(uint64 offset)
    {
        uint32 result = 0;
        readInternal(offset, result);
        return result;
    }
    uint64 read64(uint64 offset)
    {
        uint64 result = 0;
        readInternal(offset, result);
        return result;
    }
    void write32(uint64 offset, uint32 val)
    {
        writeInternal(offset, val);
    }
    void write64(uint64 offset, uint64 val)
    {
        writeInternal(offset, val);
    }
};

class OwnMMIORange : public MMIORangeInterface
{
    HANDLE hDriver;
    char * mmapAddr;
public:
    OwnMMIORange(uint64 baseAddr_, uint64 size_, bool readonly_ = true);
    uint32 read32(uint64 offset);
    uint64 read64(uint64 offset);
    void write32(uint64 offset, uint32 val);
    void write64(uint64 offset, uint64 val);
    ~OwnMMIORange();
};

class MMIORange
{
    std::shared_ptr<MMIORangeInterface> impl;
public:
    MMIORange(uint64 baseAddr_, uint64 size_, bool readonly_ = true);
    uint32 read32(uint64 offset)
    {
        return impl->read32(offset);
    }
    uint64 read64(uint64 offset)
    {
        return impl->read64(offset);
    }
    void write32(uint64 offset, uint32 val)
    {
        impl->write32(offset, val);
    }
    void write64(uint64 offset, uint64 val)
    {
        impl->write64(offset, val);
    }
};

#elif defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)

class MMIORange
{
#ifndef __APPLE__
    int32 fd;
#endif
    char * mmapAddr;
    const uint64 size;
#ifndef __APPLE__
    const bool readonly;
#endif
public:
    MMIORange(uint64 baseAddr_, uint64 size_, bool readonly_ = true);
    uint32 read32(uint64 offset);
    uint64 read64(uint64 offset);
    void write32(uint64 offset, uint32 val);
    void write64(uint64 offset, uint64 val);
    ~MMIORange();
};
#endif

} // namespace pcm