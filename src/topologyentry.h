// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-, Intel Corporation

#pragma once

#include "types.h"
#ifndef USER_KERNEL_SHARED
#include "debug.h"
#endif

namespace pcm
{

struct PCM_API TopologyEntry // describes a core
{
    /*
     * core_id is the hw specific physical core id that was introduced in newer generations, on
     * older generations this is the same as socket_unique_core_id.
     * socket_unique_core_id is like the name says a socket unique core id that is built out of the core_id, module_id, thread_id, die_id and die_grp_id.
     * With it, we have unique core ids inside a socket for keeping backward compatibility in the prometheus and json output.
     */
    int32 os_id;
    int32 thread_id;
    int32 core_id;
    int32 module_id;
    int32 tile_id; // tile is a constellation of 1 or more cores sharing same L2 cache. Unique for entire system
    int32 die_id;
    int32 die_grp_id;
    int32 socket_id;
    int32 socket_unique_core_id;
    int32 l3_cache_id = -1;
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

    TopologyEntry() : os_id(-1), thread_id (-1), core_id(-1), module_id(-1), tile_id(-1), die_id(-1), die_grp_id(-1), socket_id(-1), socket_unique_core_id(-1) { }
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
    bool isSameSocket( TopologyEntry& te ) {
        return this->socket_id == te.socket_id;
    }
    bool isSameDieGroup( TopologyEntry& te ) {
        return this->die_grp_id == te.die_grp_id && isSameSocket(te);
    }
    bool isSameDie( TopologyEntry& te ) {
        return this->die_id == te.die_id && isSameDieGroup(te);
    }
    bool isSameTile( TopologyEntry& te ) {
        return this->tile_id == te.tile_id && isSameDie(te);
    }
    bool isSameModule( TopologyEntry& te ) {
        return this->module_id == te.module_id && isSameTile (te);
    }
    bool isSameCore( TopologyEntry& te ) {
        return this->core_id == te.core_id && isSameModule(te);
    }
};

inline void fillEntry(TopologyEntry & entry, const uint32 & smtMaskWidth, const uint32 & coreMaskWidth, const uint32 & l2CacheMaskShift, const int apic_id)
{
    entry.thread_id = smtMaskWidth ? extract_bits_32(apic_id, 0, smtMaskWidth - 1) : 0;
    entry.core_id = (smtMaskWidth + coreMaskWidth) ? extract_bits_32(apic_id, smtMaskWidth, smtMaskWidth + coreMaskWidth - 1) : 0;
    entry.socket_id = extract_bits_32(apic_id, smtMaskWidth + coreMaskWidth, 31);
    entry.tile_id = extract_bits_32(apic_id, l2CacheMaskShift, 31);
    entry.socket_unique_core_id = entry.core_id;
}

inline bool initCoreMasks(uint32 & smtMaskWidth, uint32 & coreMaskWidth, uint32 & l2CacheMaskShift, uint32 & l3CacheMaskShift)
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
            levelType = extract_bits_32(cpuid_args.array[2], 8, 15);
            levelShift = extract_bits_32(cpuid_args.array[0], 0, 4);
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

        uint32 threadsSharingL2 = 0;
        uint32 l2CacheMaskWidth = 0;

        pcm_cpuid(0x4, 2, cpuid_args); // get ID for L2 cache
        l2CacheMaskWidth = 1 + extract_bits_32(cpuid_args.array[0],14,25); // number of APIC IDs sharing L2 cache
        threadsSharingL2 = l2CacheMaskWidth;
        for( ; l2CacheMaskWidth > 1; l2CacheMaskWidth >>= 1)
        {
            l2CacheMaskShift++;
        }

#ifndef USER_KERNEL_SHARED
        DBG(1, "Number of threads sharing L2 cache = " , threadsSharingL2, " [the most significant bit = " , l2CacheMaskShift , "]");
#endif

        uint32 threadsSharingL3 = 0;
        uint32 l3CacheMaskWidth = 0;

        pcm_cpuid(0x4, 3, cpuid_args); // get ID for L3 cache
        l3CacheMaskWidth = 1 + extract_bits_32(cpuid_args.array[0], 14, 25); // number of APIC IDs sharing L3 cache
        threadsSharingL3 = l3CacheMaskWidth;
        for( ; l3CacheMaskWidth > 1; l3CacheMaskWidth >>= 1)
        {
            l3CacheMaskShift++;
        }

#ifndef USER_KERNEL_SHARED
        DBG(1, "Number of threads sharing L3 cache = " , threadsSharingL3, " [the most significant bit = " , l3CacheMaskShift , "]");
#endif

        (void) threadsSharingL2; // to suppress warnings on MacOS (unused vars)
        (void) threadsSharingL3; // to suppress warnings on MacOS (unused vars)

        // Validate l3CacheMaskShift and ensure the bit range is correct
        if (l3CacheMaskShift > 31)
        {
#ifndef USER_KERNEL_SHARED
            DBG(0, "Invalid bit range for L3 cache ID extraction = ", l3CacheMaskShift);
#endif
            return false;
        }

#ifndef USER_KERNEL_SHARED
        uint32 it = 0;

        for (int i = 0; i < 100; ++i)
        {
            uint32 threadsSharingCache = 0;
            uint32 CacheMaskWidth = 0;
            uint32 CacheMaskShift = 0;
            pcm_cpuid(0x4, it, cpuid_args);
            const auto cacheType = extract_bits_32(cpuid_args.array[0], 0, 4);
            if (cacheType == 0)
            {
                break; // no more caches
            }
            const char * cacheTypeStr = nullptr;
            switch (cacheType)
            {
                case 1: cacheTypeStr = "data"; break;
                case 2: cacheTypeStr = "instruction"; break;
                case 3: cacheTypeStr = "unified"; break;
                default: cacheTypeStr = "unknown"; break;
            }
            const auto level = extract_bits_32(cpuid_args.array[0], 5, 7);
            CacheMaskWidth = 1 + extract_bits_32(cpuid_args.array[0], 14, 25); // number of APIC IDs sharing cache
            threadsSharingCache = CacheMaskWidth;
            for( ; CacheMaskWidth > 1; CacheMaskWidth >>= 1)
            {
                CacheMaskShift++;
            }
            DBG(1, "Max number of threads sharing L" , level , " " , cacheTypeStr , " cache = " , threadsSharingCache, " [the most significant bit = " , CacheMaskShift , "]",
                " shift = " , CacheMaskShift);
            ++it;
        }
#endif
    }
    return true;
}

}

