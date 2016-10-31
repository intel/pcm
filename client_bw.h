/*
Copyright (c) 2012-2013, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//

#ifndef CPUCounters_CLIENTBW_H
#define CPUCounters_CLIENTBW_H

/*!     \file client_bw.h
        \brief Interface to access client bandwidth counters 

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

#define PCM_CLIENT_IMC_BAR_OFFSET       (0x0048)
#define PCM_CLIENT_IMC_DRAM_IO_REQESTS  (0x5048)
#define PCM_CLIENT_IMC_DRAM_DATA_READS  (0x5050)
#define PCM_CLIENT_IMC_DRAM_DATA_WRITES (0x5054)
#define PCM_CLIENT_IMC_MMAP_SIZE        (0x6000)


class ClientBW
{
#if defined(__linux__) || defined(__FreeBSD__)
    int32 fd;
    char * mmapAddr;
#endif
#ifdef __APPLE__
    char * mmapAddr;
#endif
#ifdef _MSC_VER
	std::shared_ptr<WinPmem> pmem;
    uint64 startAddr;
    PCM_Util::Mutex mutex;
#endif

public:
   ClientBW();

   uint64 getImcReads();
   uint64 getImcWrites();
   uint64 getIoRequests();

   ~ClientBW();
};


#endif
