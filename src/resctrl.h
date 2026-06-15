// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation
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
        size_t getMetric(const FileMapType & fileMap, int core);
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
