/*

 * Copyright (c) 2009-2020, Intel Corporation
  * All rights reserved.

*Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// originally written by Patrick Lu
// redesigned by Roman Sudarikov


/*!     \file pcm-pcie.cpp
  \brief Example of using uncore CBo counters: implements a performance counter monitoring utility for monitoring PCIe bandwidth
  */
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
#endif
#include <math.h>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <assert.h>
#include "pcm-pcie.h"

#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs

using namespace std;

void print_events()
{
    cerr << " PCIe event definitions (each event counts as a transfer): \n";
    cerr << "   PCIe read events (PCI devices reading from memory - application writes to disk/network/PCIe device):\n";
    cerr << "     PCIePRd   - PCIe UC read transfer (partial cache line)\n";
    cerr << "     PCIeRdCur* - PCIe read current transfer (full cache line)\n";
    cerr << "         On Haswell Server PCIeRdCur counts both full/partial cache lines\n";
    cerr << "     RFO*      - Demand Data RFO\n";
    cerr << "     CRd*      - Demand Code Read\n";
    cerr << "     DRd       - Demand Data Read\n";
    cerr << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cerr << "   PCIe write events (PCI devices writing to memory - application reads from disk/network/PCIe device):\n";
    cerr << "     PCIeWiLF  - PCIe Write transfer (non-allocating) (full cache line)\n";
    cerr << "     PCIeItoM  - PCIe Write transfer (allocating) (full cache line)\n";
    cerr << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cerr << "     PCIeNSWrF - PCIe Non-snoop write transfer (full cache line)\n";
    cerr << "     ItoM      - PCIe write full cache line\n";
    cerr << "     RFO       - PCIe partial Write\n";
    cerr << "   CPU MMIO events (CPU reading/writing to PCIe devices):\n";
    cerr << "     PRd       - MMIO Read [Haswell Server only] (Partial Cache Line)\n";
    cerr << "     WiL       - MMIO Write (Full/Partial)\n\n";
    cerr << " * - NOTE: Depending on the configuration of your BIOS, this tool may report '0' if the message\n";
    cerr << "           has not been selected.\n\n";
}

void print_usage(const string progname)
{
    cerr << "\n Usage: \n " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cerr << "   <delay>                           => time interval to sample performance counters.\n";
    cerr << "                                        If not specified, or 0, with external program given\n";
    cerr << "                                        will read counters only after external program finishes\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help  | /h               => print this help and exit\n";
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cerr << "  -B                                 => Estimate PCIe B/W (in Bytes/sec) by multiplying\n";
    cerr << "                                        the number of transfers by the cache line size (=64 bytes).\n";
    cerr << "  -e                                 => print additional PCIe LLC miss/hit statistics.\n";
    cerr << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cerr << " It overestimates the bandwidth under traffic with many partial cache line transfers.\n";
    cerr << "\n";
    print_events();
    cerr << "\n";
    cerr << " Examples:\n";
    cerr << "  " << progname << " 1                  => print counters every second without core and socket output\n";
    cerr << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << progname << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
}

IPlatform *IPlatform::getPlatform(PCM *m, bool csv, bool print_bandwidth, bool print_additional_info, uint32 delay)
{
    switch (m->getCPUModel()) {
        case PCM::SKX:
            return new PurleyPlatform(m, csv, print_bandwidth, print_additional_info, delay);
        case PCM::BDX_DE:
        case PCM::BDX:
        case PCM::KNL:
        case PCM::HASWELLX:
            return new GrantleyPlatform(m, csv, print_bandwidth, print_additional_info, delay);
        case PCM::IVYTOWN:
        case PCM::JAKETOWN:
            return new BromolowPlatform(m, csv, print_bandwidth, print_additional_info, delay);
        default:
          return NULL;
    }
}

int main(int argc, char * argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    cout.rdbuf(&nullStream1);
    cerr.rdbuf(&nullStream2);
#endif

    cerr << "\n";
    cerr << " Processor Counter Monitor: PCIe Bandwidth Monitoring Utility \n";
    cerr << " This utility measures PCIe bandwidth in real-time\n";
    cerr << "\n";
    print_events();

    double delay = -1.0;
    bool csv = false;
    bool print_bandwidth = false;
	bool print_additional_info = false;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    MainLoop mainLoop;

    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    if (argc > 1) do
    {
        argv++;
        argc--;
        if (strncmp(*argv, "--help", 6) == 0 ||
            strncmp(*argv, "-h", 2) == 0 ||
            strncmp(*argv, "/h", 2) == 0)
        {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else
        if (strncmp(*argv, "-csv",4) == 0 ||
            strncmp(*argv, "/csv",4) == 0)
        {
            csv = true;
            string cmd = string(*argv);
            size_t found = cmd.find('=',4);
            if (found != string::npos) {
                string filename = cmd.substr(found+1);
                if (!filename.empty()) {
                    m->setOutput(filename);
                }
            }
            continue;
        }
	else
        if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else
        if (strncmp(*argv, "-B", 2) == 0 ||
            strncmp(*argv, "/b", 2) == 0)
        {
            print_bandwidth = true;
            continue;
        }
        else
        if (strncmp(*argv, "-e", 2) == 0 )
        {
            print_additional_info = true;
            continue;
        }
        else
        if (strncmp(*argv, "--", 2) == 0)
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            // any other options positional that is a floating point number is treated as <delay>,
            // while the other options are ignored with a warning issues to stderr
            double delay_input;
            istringstream is_str_stream(*argv);
            is_str_stream >> noskipws >> delay_input;
            if (is_str_stream.eof() && !is_str_stream.fail()) {
                delay = delay_input;
            } else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                print_usage(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while(argc > 1); // end of command line partsing loop

    if ( (sysCmd != NULL) && (delay<=0.0) ) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (csv) {
        if ( delay<=0.0 ) delay = PCM_DELAY_DEFAULT;
    } else {
        // for non-CSV mode delay < 1.0 does not make a lot of practical sense:
        // hard to read from the screen, or
        // in case delay is not provided in command line => set default
        if ( ((delay<1.0) && (delay>0.0)) || (delay<=0.0) ) delay = PCM_DELAY_DEFAULT;
    }

    cerr << "Update every " << delay << " seconds\n";

    unique_ptr<IPlatform> platform(IPlatform::getPlatform(m, csv, print_bandwidth,
                                    print_additional_info, (uint)delay)); // FIXME: do we support only integer delay?

    if (!platform)
    {
        print_cpu_details();
        cerr << "Jaketown, Ivytown, Haswell, Broadwell-DE Server CPU is required for this tool! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    if ( sysCmd != NULL ) {
        MySystem(sysCmd, sysArgv);
    }

    // ================================== Begin Printing Output ==================================
    mainLoop([&]()
    {
        if (!csv) cout << flush;

        for(uint i=0; i < NUM_SAMPLES; i++)
            platform->getEvents();

        platform->printHeader();

        platform->printEvents();

        platform->printAggregatedEvents();

        platform->cleanup();

        if (m->isBlocked())
            return false;

        return true;
    });
    // ================================== End Printing Output ==================================

    exit(EXIT_SUCCESS);
}
