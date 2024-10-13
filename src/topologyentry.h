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
    int32 module_id;
    int32 tile_id; // tile is a constalation of 1 or more cores sharing same L2 cache. Unique for entire system
    int32 die_id;
    int32 die_grp_id;
    int32 socket_id;
    int32 native_cpu_model = -1;
    enum DomainTypeID
    {
        InvalidDomainTypeID       = 0,
        LogicalProcessorDomain    = 1,
        CoreDomain                = 2,
        ModuleDomain              = 3,
        TileDomain                = 4,
        DieDomain                 = 5,
        DieGrpDomain              = 6,
        SocketPackageDomain       = 0xffff
    };
    enum CoreType
    {
        Atom = 0x20,
        Core = 0x40,
        Invalid = -1
    };
    CoreType core_type = Invalid;

    TopologyEntry() : os_id(-1), thread_id (-1), core_id(-1), module_id(-1), tile_id(-1), die_id(-1), die_grp_id(-1), socket_id(-1) { }
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
    static const char* getDomainTypeStr(const DomainTypeID & id)
    {
        switch (id)
        {
            case InvalidDomainTypeID: return "invalid";
            case LogicalProcessorDomain: return "LogicalProcessor";
            case CoreDomain: return "Core";
            case ModuleDomain: return "Module";
            case TileDomain: return "Tile";
            case DieDomain: return "Die";
            case DieGrpDomain: return "DieGroup";
            case SocketPackageDomain: return "Socket/Package";
        }
        return "unknown";
    }
};

inline void fillEntry(TopologyEntry & entry, const uint32 & smtMaskWidth, const uint32 & coreMaskWidth, const uint32 & l2CacheMaskShift, const int apic_id)
{
    entry.thread_id = smtMaskWidth ? extract_bits_ui(apic_id, 0, smtMaskWidth - 1) : 0;
    entry.core_id = (smtMaskWidth + coreMaskWidth) ? extract_bits_ui(apic_id, smtMaskWidth, smtMaskWidth + coreMaskWidth - 1) : 0;
    entry.socket_id = extract_bits_ui(apic_id, smtMaskWidth + coreMaskWidth, 31);
    entry.tile_id = extract_bits_ui(apic_id, l2CacheMaskShift, 31);
}

inline bool initCoreMasks(uint32 & smtMaskWidth, uint32 & coreMaskWidth, uint32 & l2CacheMaskShift)
{
    // init constants for CPU topology leaf 0xB
    // adapted from Topology Enumeration Reference code for Intel 64 Architecture
    // https://software.intel.com/en-us/articles/intel-64-architecture-processor-topology-enumeration
    int wasCoreReported = 0, wasThreadReported = 0;
    PCM_CPUID_INFO cpuid_args;
    if (true)
    {
        uint32 corePlusSMTMaskWidth = 0;
        int subleaf = 0, levelType, levelShift;
        do
        {
            pcm_cpuid(0xb, subleaf, cpuid_args);
            if (cpuid_args.array[1] == 0)
            { // if EBX ==0 then this subleaf is not valid, we can exit the loop
                break;
            }
            levelType = extract_bits_ui(cpuid_args.array[2], 8, 15);
            levelShift = extract_bits_ui(cpuid_args.array[0], 0, 4);
            switch (levelType)
            {
            case 1: //level type is SMT, so levelShift is the SMT_Mask_Width
                smtMaskWidth = levelShift;
                wasThreadReported = 1;
                break;
            case 2: //level type is Core, so levelShift is the CorePlusSMT_Mask_Width
                corePlusSMTMaskWidth = levelShift;
                wasCoreReported = 1;
                break;
            default:
                break;
            }
            subleaf++;
        } while (1);

        if (wasThreadReported && wasCoreReported)
        {
            coreMaskWidth = corePlusSMTMaskWidth - smtMaskWidth;
        }
        else if (!wasCoreReported && wasThreadReported)
        {
            coreMaskWidth = smtMaskWidth;
        }
        else
        {
            return false;
        }

        (void) coreMaskWidth; // to suppress warnings on MacOS (unused vars)

    #ifdef PCM_DEBUG_TOPOLOGY
        uint32 threadsSharingL2;
    #endif
        uint32 l2CacheMaskWidth;

        pcm_cpuid(0x4, 2, cpuid_args); // get ID for L2 cache
        l2CacheMaskWidth = 1 + extract_bits_ui(cpuid_args.array[0],14,25); // number of APIC IDs sharing L2 cache
    #ifdef PCM_DEBUG_TOPOLOGY
        threadsSharingL2 = l2CacheMaskWidth;
    #endif
        for( ; l2CacheMaskWidth > 1; l2CacheMaskWidth >>= 1)
        {
            l2CacheMaskShift++;
        }
    #ifdef PCM_DEBUG_TOPOLOGY
        std::cerr << "DEBUG: Number of threads sharing L2 cache = " << threadsSharingL2
                << " [the most significant bit = " << l2CacheMaskShift << "]\n";
    #endif
    }
    return true;
}

}

