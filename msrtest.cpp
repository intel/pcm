/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//            Austen Ott

#include <iostream>
#include <assert.h>
#include <unistd.h>
#include "msr.h"

#define NUM_CORES 16 


int main()
{
    uint32 i = 0;
    uint32 res;
    MsrHandle * cpu_msr[NUM_CORES];

    for (i = 0; i < NUM_CORES; ++i)
    {
        cpu_msr[i] = new MsrHandle(i);
        assert(cpu_msr >= 0);

        FixedEventControlRegister ctrl_reg;
        res = cpu_msr[i]->read(IA32_CR_FIXED_CTR_CTRL, &ctrl_reg.value);
        assert(res >= 0);

        ctrl_reg.fields.os0 = 1;
        ctrl_reg.fields.usr0 = 1;
        ctrl_reg.fields.any_thread0 = 0;
        ctrl_reg.fields.enable_pmi0 = 0;

        ctrl_reg.fields.os1 = 1;
        ctrl_reg.fields.usr1 = 1;
        ctrl_reg.fields.any_thread1 = 0;
        ctrl_reg.fields.enable_pmi1 = 0;

        ctrl_reg.fields.os2 = 1;
        ctrl_reg.fields.usr2 = 1;
        ctrl_reg.fields.any_thread2 = 0;
        ctrl_reg.fields.enable_pmi2 = 0;

        res = cpu_msr[i]->write(IA32_CR_FIXED_CTR_CTRL, ctrl_reg.value);
        assert(res >= 0);

        // start counting
        uint64 value = (1ULL << 0) + (1ULL << 1) + (1ULL << 2) + (1ULL << 3) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34);
        res = cpu_msr[i]->write(IA32_CR_PERF_GLOBAL_CTRL, value);
        assert(res >= 0);
    }
    uint64 counters_before[NUM_CORES][3];
    uint64 counters_after[NUM_CORES][3];
   
    for (i = 0; i < NUM_CORES; ++i)
    {
        res = cpu_msr[i]->read(INST_RETIRED_ANY_ADDR, &counters_before[i][0]);
        assert(res >= 0);
        res = cpu_msr[i]->read(CPU_CLK_UNHALTED_THREAD_ADDR, &counters_before[i][1]);
        assert(res >= 0);
        res = cpu_msr[i]->read(CPU_CLK_UNHALTED_REF_ADDR, &counters_before[i][2]);
        assert(res >= 0);
    }
    //sleep for some time
    ::sleep(1);
    for (i = 0; i < NUM_CORES; ++i)
    {
        res = cpu_msr[i]->read(INST_RETIRED_ANY_ADDR, &counters_after[i][0]);
        assert(res >= 0);
        res = cpu_msr[i]->read(CPU_CLK_UNHALTED_THREAD_ADDR, &counters_after[i][1]);
        assert(res >= 0);
        res = cpu_msr[i]->read(CPU_CLK_UNHALTED_REF_ADDR, &counters_after[i][2]);
        assert(res >= 0);
    }
    for (i = 0; i < NUM_CORES; ++i)
        delete cpu_msr[i];
    for (i = 0; i < NUM_CORES; ++i)
        std::cout << "Core " << i <<
        "\t Instructions: " << (counters_after[i][0] - counters_before[i][0]) <<
        "\t Cycles: " << (counters_after[i][1] - counters_before[i][1]) <<
        "\t IPC: " << double(counters_after[i][0] - counters_before[i][0]) / double(counters_after[i][1] - counters_before[i][1]) << std::endl;

}
