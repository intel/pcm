// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012-2020, Intel Corporation

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
    std::cout << "Usage " << progname << " [-w value] [-c core] [-a] [-d] msr\n\n";
    std::cout << "  Reads/writes specified msr (model specific register) \n";
    std::cout << "   -w value : write the value before reading \n";
    std::cout << "   -c core  : perform msr read/write on specified core (default is 0)\n";
    std::cout << "   -d       : output all numbers in dec (default is hex)\n";
    std::cout << "   -a       : perform msr read/write operations on all cores\n";
    std::cout << "\n";
}

int main(int argc, char * argv[])
{
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n MSR read/write utility\n\n";

    uint64 value = 0;
    bool write = false;
    int core = 0;
    int msr = -1;
    bool dec = false;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:c:da")) != -1)
    {
        switch (my_opt)
        {
        case 'w':
            write = true;
            value = read_number(optarg);
            break;
        case 'c':
            core = (int)read_number(optarg);
            break;
        case 'd':
            dec = true;
            break;
        case 'a':
            core = -1;
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (optind >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    msr = (int)read_number(argv[optind]);

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
    auto doOne = [&dec, &write, &msr](int core, uint64 value)
    {
        try {
            MsrHandle h(core);
            if (!dec) std::cout << std::hex << std::showbase;
            if (write)
            {
                std::cout << " Writing " << value << " to MSR " << msr << " on core " << core << "\n";
                if (h.write(msr, value) != 8)
                {
                    std::cout << " Write error!\n";
                }
            }
            value = 0;
            if (h.read(msr, &value) == 8)
            {
                std::cout << " Read value " << value << " from MSR " << msr << " on core " << core << "\n\n";
            }
            else
            {
                std::cout << " Read error!\n";
            }
        }
        catch (std::exception & e)
        {
            std::cerr << "Error accessing MSRs: " << e.what() << "\n";
            std::cerr << "Please check if the program can access MSR drivers.\n";
        }
    };
    if (core >= 0)
    {
        doOne(core, value);
    }
    else
    {
        set_signal_handlers();
        auto m = PCM::getInstance();
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (m->isCoreOnline(i))
            {
                doOne(i, value);
            }
        }
    }
    return 0;
}
