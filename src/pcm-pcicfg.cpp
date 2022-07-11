// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, 2018 Intel Corporation

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
    std::cout << "Usage " << progname << " [-w value] [-d] group bus device function offset\n\n";
    std::cout << "  Reads/writes 32-bit PCICFG register \n";
    std::cout << "   -w value : write the value before reading \n";
    std::cout << "   -d       : output all numbers in dec (default is hex)\n";
    std::cout << "\n";
}

int main(int argc, char * argv[])
{
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n PCICFG read/write utility\n\n";

    #ifdef __linux__
    #ifndef PCM_USE_PCI_MM_LINUX
    std::cout << "\n To access *extended* configuration space recompile with -DPCM_USE_PCI_MM_LINUX option.\n";
    #endif
    #endif

    uint32 value = 0;
    bool write = false;
    bool dec = false;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:d")) != -1)
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
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (optind + 4 >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    int group = (int)read_number(argv[optind]);
    int bus = (int)read_number(argv[optind + 1]);
    int device = (int)read_number(argv[optind+2]);
    int function = (int)read_number(argv[optind+3]);
    int offset = (int)read_number(argv[optind+4]);

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
    try {
        PciHandleType h(group, bus, device, function);
        if (!dec) std::cout << std::hex << std::showbase;
        if (write)
        {
            std::cout << " Writing " << value << " to " << group << ":" << bus << ":" << device << ":" << function << "@" << offset << "\n";
            h.write32(offset, value);
        }
        value = 0;
        h.read32(offset, &value);
        std::cout << " Read value " << value << " from " << group << ":" << bus << ":" << device << ":" << function << "@" << offset << "\n\n";
    }
    catch (std::exception & e)
    {
        std::cerr << "Error accessing registers: " << e.what() << "\n";
        std::cerr << "Please check if the program can access MSR/PCICFG drivers.\n";
    }
    return 0;
}
