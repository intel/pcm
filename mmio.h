/*
Copyright (c) 2012-2019, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
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
#else
#include <unistd.h>
#endif

#include "mutex.h"
#include <memory>

namespace pcm {

#ifdef _MSC_VER
class MMIORange
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
    MMIORange(uint64 baseAddr_, uint64 size_, bool readonly_ = true);
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

#elif defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__) || defined(__DragonFly__)

class MMIORange
{
    int32 fd;
    char * mmapAddr;
    const uint64 size;
    const bool readonly;
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