// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023 Intel Corporation

// written by Roman Dementiev
#include "cpucounters.h"
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#endif
#include <iostream>
#include <stdlib.h>
#include <iomanip>
#include <string.h>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

using namespace pcm;

void print_usage(const char * progname)
{
    std::cout << "Usage " << progname << " [-w value] [-d] [-b low:high] ID offset\n\n";
    std::cout << "  Reads/writes TPMI (Topology Aware Register and PM Capsule Interface) register \n";
    std::cout << "   ID          : TPMI ID\n";
    std::cout << "   offset      : register offset\n";
    std::cout << "   -w value    : write the value before reading \n";
    std::cout << "   -b low:high : read or write only low..high bits of the register\n";
    std::cout << "   -d          : output all numbers in dec (default is hex)\n";
    std::cout << "   -v          : verbose ouput\n";
    std::cout << "   --version   : print application version\n";
    std::cout << "\n";
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        return 0;

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n TPMI (Topology Aware Register and PM Capsule Interface) read/write utility\n\n";
    // register documentation: https://github.com/intel/tpmi_power_management

    uint64 value = 0;
    bool write = false;
    bool dec = false;
    bool verbose = false;
    std::pair<int64,int64> bits{-1, -1};

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:dvb:")) != -1)
    {
        switch (my_opt)
        {
        case 'w':
            write = true;
            value = (pcm::uint32)read_number(optarg);
            break;
        case 'd':
            dec = true;
            break;
        case 'v':
            verbose = true;
            break;
        case 'b':
            bits = parseBitsParameter(optarg);
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (optind + 1 >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    uint64 requestedID = (uint64)read_number(argv[optind]);
    uint64 requestedRelativeOffset = (uint64)read_number(argv[optind + 1]);

    #ifdef _MSC_VER
    // Increase the priority a bit to improve context switching delays on Windows
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // WARNING: This driver code (msr.sys) is only for testing purposes, not for production use
    Driver drv = Driver(Driver::msrLocalPath());
    // drv.stop();     // restart driver (usually not needed)
    if (!drv.start())
    {
        tcerr << "Can not load MSR driver.\n";
        tcerr << "You must have a signed  driver at " << drv.driverPath() << " and have administrator rights to run this program\n";
        return -1;
    }
    #endif
    
    processDVSEC([](const VSEC & vsec)
        {
            return vsec.fields.cap_id == 0xb // Vendor Specific DVSEC
                && vsec.fields.vsec_id == 0x42; // TPMI PM_Features
        }, [&](const uint64 bar, const VSEC & vsec)
        {
            struct PFS
            {
                uint64 TPMI_ID:8;
                uint64 NumEntries:8;
                uint64 EntrySize:16;
                uint64 CapOffset:16;
                uint64 Attribute:2;
                uint64 Reserved:14;
            };
            static_assert(sizeof(PFS) == sizeof(uint64), "sizeof(PFS) != sizeof(uint64)");
            assert(vsec.fields.EntrySize == 2);
            std::vector<PFS> pfsArray(vsec.fields.NumEntries);
            pcm::mmio_memcpy(&(pfsArray[0]), bar + vsec.fields.Address, vsec.fields.NumEntries * sizeof(PFS), true);
            for (const auto & pfs : pfsArray)
            {
                if (verbose)
                {
                    std::cout << "PFS" <<
                    "\t TPMI_ID: " << pfs.TPMI_ID <<
                    "\t NumEntries: " << pfs.NumEntries <<
                    "\t EntrySize: " << pfs.EntrySize <<
                    "\t CapOffset: " << pfs.CapOffset <<
                    "\t Attribute: " << pfs.Attribute <<
                    "\n";
                }
                for (uint64 p = 0; p < pfs.NumEntries; ++p)
                {
                    uint32 reg0 = 0;
                    const auto addr = bar + vsec.fields.Address + pfs.CapOffset * 1024ULL + p * pfs.EntrySize * sizeof(uint32); 
                    mmio_memcpy(&reg0, addr, sizeof(uint32), false);
                    if (reg0 == ~0U)
                    {
                        if (verbose)
                        {
                            std::cout << "invalid entry " << p << "\n";
                        }
                    }
                    else if (pfs.TPMI_ID == requestedID)
                    {
                        if (verbose)
                        {
                            std::cout << "Entry "<< p << std::hex;
                            for (uint64 i_offset = 0; i_offset < pfs.EntrySize * sizeof(uint32);  i_offset += sizeof(uint64))
                            {
                                uint64 reg = 0;
                                mmio_memcpy(&reg, addr + i_offset, sizeof(uint64), false);
                                std::cout << " register "<< i_offset << " = " << reg;
                            }
                            std::cout << std::dec << "\n";
                        }
                        try {
                            const auto requestedAddr = addr + requestedRelativeOffset;
                            const auto baseAddr = roundDownTo4K(requestedAddr);
                            const auto baseOffset = requestedAddr - baseAddr;
                            MMIORange range(baseAddr, 4096ULL, !write);
                            if (!dec) std::cout << std::hex << std::showbase;
                            readOldValueHelper(bits, value, write, [&range, &baseOffset](uint64 & old_value){ old_value = range.read64(baseOffset); return true; });
                            if (write)
                            {
                                std::cout << " Writing " << value << " to TPMI ID " << requestedID << "@" << requestedRelativeOffset << " for entry " << p << "\n";
                                range.write64(baseOffset, value);
                            }
                            value = range.read64(baseOffset);
                            extractBitsPrintHelper(bits, value, dec);
                            std::cout << " from TPMI ID " << requestedID << "@" << requestedRelativeOffset << " for entry " << p << "\n\n";
                        }
                        catch (std::exception& e)
                        {
                            std::cerr << "Error accessing registers: " << e.what() << "\n";
                            std::cerr << "Please check if the program can access MSR/PCICFG drivers.\n";
                        }
                    }
                }
            }
        });

    return 0;
}
