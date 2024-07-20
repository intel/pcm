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
    std::cout << "Usage " << progname << " [-w value] [-d] [-b low:high] [-e entries] ID offset\n\n";
    std::cout << "  Reads/writes TPMI (Topology Aware Register and PM Capsule Interface) register \n";
    std::cout << "   ID          : TPMI ID\n";
    std::cout << "   offset      : register offset\n";
    std::cout << "   -w value    : write the value before reading \n";
    std::cout << "   -b low:high : read or write only low..high bits of the register\n";
    std::cout << "   -e entries  : perform read/write on specified entries (default is all entries)\n";
    std::cout << "                 (examples: -e 10 -e 10-11 -e 4,6,12-20,6)\n";
    std::cout << "   -i instances: perform read/write on specified instances (default is all instances)\n";
    std::cout << "                 (examples: -i 1 -i 0,1 -i 0,2-3)\n";
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
    std::pair<int64,int64> bits{-1, -1};
    std::list<int> entries, instances;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:dvb:e:i:")) != -1)
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
            TPMIHandle::setVerbose(true);
            break;
        case 'b':
            bits = parseBitsParameter(optarg);
            break;
        case 'e':
            entries = extract_integer_list(optarg);
            break;
        case 'i':
            instances = extract_integer_list(optarg);
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

    try
    {
        if (instances.empty())
        {
            for (size_t i = 0; i < TPMIHandle::getNumInstances(); ++i)
            {
                instances.push_back(i);
            }
        }
        for (const size_t i : instances)
        {
            if (i >= TPMIHandle::getNumInstances())
            {
                std::cerr << "Instance " << i << " does not exist\n";
                continue;
            }
            TPMIHandle h(i, requestedID, requestedRelativeOffset, !write);
            auto one = [&](const size_t p)
            {
                if (!dec)
                    std::cout << std::hex << std::showbase;
                readOldValueHelper(bits, value, write, [&h, &p](uint64& old_value)
                { old_value = h.read64(p); return true; });
                if (write)
                {
                    std::cout << " Writing " << value << " to TPMI ID " << requestedID << "@" << requestedRelativeOffset << " for entry " << p << " in instance " << i << "\n";
                    h.write64(p, value);
                }
                value = h.read64(p);
                extractBitsPrintHelper(bits, value, dec);
                std::cout << " from TPMI ID " << requestedID << "@" << requestedRelativeOffset << " for entry " << p << " in instance " << i << "\n\n";
            };
            if (entries.empty())
            {
                for (size_t p = 0; p < h.getNumEntries(); ++p)
                {
                    entries.push_back(p);
                }
            }
            for (const size_t p : entries)
            {
                if (p < h.getNumEntries())
                {
                    one(p);
                }
            }
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "Error accessing registers: " << e.what() << "\n";
        std::cerr << "Please check if the program can access MSR/PCICFG drivers.\n";
    }

    return 0;
}
