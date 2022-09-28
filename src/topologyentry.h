// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-, Intel Corporation

#pragma once

#include "types.h"

namespace pcm
{

struct PCM_API TopologyEntry // describes a core
{
    int32 os_id;
    int32 thread_id;
    int32 core_id;
    int32 tile_id; // tile is a constalation of 1 or more cores sharing salem L2 cache. Unique for entire system
    int32 socket;
    int32 native_cpu_model = -1;
    enum CoreType
    {
        Atom = 0x20,
        Core = 0x40,
        Invalid = -1
    };
    CoreType core_type = Invalid;

    TopologyEntry() : os_id(-1), thread_id (-1), core_id(-1), tile_id(-1), socket(-1) { }
    const char* getCoreTypeStr()
    {
        switch (core_type)
        {
            case Atom:
                return "Atom";
            case Core:
                return "Core";
            case Invalid:
                return "invalid";
        }
        return "unknown";
    }
};

}

