/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
   written by Roman Dementiev
*/

#ifndef MSR_STRUCT_HEADER
#define MSR_STRUCT_HEADER


#ifndef CTL_CODE
#include <WinIoCtl.h>
#endif

#define MSR_DEV_TYPE 50000

#define IO_CTL_MSR_READ     CTL_CODE(MSR_DEV_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_MSR_WRITE    CTL_CODE(MSR_DEV_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PCICFG_READ  CTL_CODE(MSR_DEV_TYPE, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IO_CTL_PCICFG_WRITE CTL_CODE(MSR_DEV_TYPE, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

struct MSR_Request
{
    int core_id;
    ULONG64 msr_address;
    ULONG64 write_value;     /* value to write if write requet
                                 ignored if read request */
};

struct PCICFG_Request
{
    ULONG bus, dev, func, reg, bytes;
    // "bytes" can be only 4 or 8
    /* value to write if write request ignored if read request */
    ULONG64 write_value;
};


#endif
