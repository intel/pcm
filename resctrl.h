/*
Copyright (c) 2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev

#pragma once

/*!     \file resctrl.h
        \brief interface to MBM and CMT using Linux resctrl
  */

#ifdef __linux__

#include <stdlib.h>
#include <stdio.h>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>

namespace pcm
{
    class PCM;

    class Resctrl
    {
        PCM & pcm;
        typedef std::unordered_map<int, std::vector<std::string> > FileMapType;
        FileMapType L3OCC, MBL, MBT;
        Resctrl() = delete;
        size_t getMetric(FileMapType & fileMap, int core);
        static constexpr auto PCMPath = "/sys/fs/resctrl/mon_groups/pcm";
    public:
        Resctrl(PCM & m) : pcm(m) {}
        bool isMounted();
        void init();
        size_t getL3OCC(int core);
        size_t getMBL(int core);
        size_t getMBT(int core);
        void cleanup();
    };
};

 #endif // __linux__