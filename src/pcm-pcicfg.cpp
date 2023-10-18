// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, 2018-2022 Intel Corporation

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
    std::cout << "Usage " << progname << " [-w value] [-d] [-i ID] [group bus device function] offset\n\n";
    std::cout << "  Reads/writes 32-bit PCICFG register \n";
    std::cout << "   -w value    : write the value before reading \n";
    std::cout << "   -b low:high : read or write only low..high bits of the register\n";
    std::cout << "   -d          : output all numbers in dec (default is hex)\n";
    std::cout << "   -i ID       : specify Intel device ID instead of group bus device function\n";
    std::cout << "   --version   : print application version\n";
    std::cout << "\n";
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        return 0;

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n PCICFG read/write utility\n\n";

    #ifdef __linux__
    #ifndef PCM_USE_PCI_MM_LINUX
    std::cout << "\n To access *extended* configuration space recompile with -DPCM_USE_PCI_MM_LINUX option.\n";
    #endif
    #endif

    uint32 value = 0;
    bool write = false;
    bool dec = false;
    uint32 deviceID = 0;
    std::pair<int64,int64> bits{-1, -1};

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "i:w:db:")) != -1)
    {
        switch (my_opt)
        {
        case 'i':
            deviceID = (uint32)read_number(optarg);
            break;
        case 'w':
            write = true;
            value = (pcm::uint32)read_number(optarg);
            break;
        case 'b':
            bits = parseBitsParameter(optarg);
            break;
        case 'd':
            dec = true;
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (optind + ((deviceID)?0:4) >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    int group = -1;
    int bus = -1;
    int device = -1;
    int function = -1;
    int offset = -1;

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

    auto one = [&dec,&write,&bits](const uint32 & group, const uint32 & bus, const uint32 & device, const uint32 & function, const uint32 & offset, uint32 value)
    {

        try {
            PciHandleType h(group, bus, device, function);
            if (!dec) std::cout << std::hex << std::showbase;
            readOldValueHelper(bits, value, write, [&h, &offset](uint32 & old_value){ h.read32(offset, &old_value); return true; });
            if (write)
            {
                std::cout << " Writing " << value << " to " << group << ":" << bus << ":" << device << ":" << function << "@" << offset << "\n";
                h.write32(offset, value);
            }
            value = 0;
            h.read32(offset, &value);
            extractBitsPrintHelper(bits, value, dec);
            std::cout << " from " << group << ":" << bus << ":" << device << ":" << function << "@" << offset << "\n\n";
        }
        catch (std::exception& e)
        {
            std::cerr << "Error accessing registers: " << e.what() << "\n";
            std::cerr << "Please check if the program can access MSR/PCICFG drivers.\n";
        }
    };

    if (deviceID)
    {
        offset = (int)read_number(argv[optind]);
        forAllIntelDevices([&deviceID,&one,&offset, &value](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
            {
                if (deviceID == device_id)
                {
                    one(group, bus, device, function, offset, value);
                }
            });
    }
    else
    {
        group = (int)read_number(argv[optind]);
        bus = (int)read_number(argv[optind + 1]);
        device = (int)read_number(argv[optind + 2]);
        function = (int)read_number(argv[optind + 3]);
        offset = (int)read_number(argv[optind + 4]);
        one(group, bus, device, function, offset, value);
    }

    return 0;
}
