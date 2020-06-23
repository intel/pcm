/*
Copyright (c) 2012-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// written by Roman Dementiev
#include "cpucounters.h"
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
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
        std::wcerr << "Can not load MSR driver.\n";
        std::wcerr << "You must have a signed  driver at " << drv.driverPath() << " and have administrator rights to run this program\n";
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
}
