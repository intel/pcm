// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// originally written by Patrick Lu
// redesigned by Roman Sudarikov


/*!     \file pcm-pcie.cpp
  \brief Example of using uncore CBo counters: implements a performance counter monitoring utility for monitoring PCIe bandwidth
  */
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
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

bool events_printed = false;

void print_events()
{
    if(events_printed)
    {
        return;
    }

    cout << " PCIe event definitions (each event counts as a transfer): \n";
    cout << "   PCIe read events (PCI devices reading from memory - application writes to disk/network/PCIe device):\n";
    cout << "     PCIePRd   - PCIe UC read transfer (partial cache line)\n";
    cout << "     PCIeRdCur* - PCIe read current transfer (full cache line)\n";
    cout << "         On Haswell Server PCIeRdCur counts both full/partial cache lines\n";
    cout << "     RFO*      - Demand Data RFO\n";
    cout << "     CRd*      - Demand Code Read\n";
    cout << "     DRd       - Demand Data Read\n";
    cout << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cout << "   PCIe write events (PCI devices writing to memory - application reads from disk/network/PCIe device):\n";
    cout << "     PCIeWiLF  - PCIe Write transfer (non-allocating) (full cache line)\n";
    cout << "     PCIeItoM  - PCIe Write transfer (allocating) (full cache line)\n";
    cout << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cout << "     PCIeNSWrF - PCIe Non-snoop write transfer (full cache line)\n";
    cout << "     ItoM      - PCIe write full cache line\n";
    cout << "     RFO       - PCIe partial Write\n";
    cout << "   CPU MMIO events (CPU reading/writing to PCIe devices):\n";
    cout << "     UCRdF     - read from uncacheable memory, including MMIO\n";
    cout << "     WCiL      - streaming store (partial cache line), includes MOVDIRI\n\n";
    cout << "     WCiLF     - streaming store (full cache line), includes MOVDIR64\n\n";
    cout << "     PRd       - MMIO Read [Haswell Server only] (Partial Cache Line)\n";
    cout << "     WiL       - MMIO Write (Full/Partial)\n\n";
    cout << " * - NOTE: Depending on the configuration of your BIOS, this tool may report '0' if the message\n";
    cout << "           has not been selected.\n\n";

    events_printed = true;
}

void print_usage(const string & progname)
{
    cout << "\n Usage: \n " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cout << "   <delay>                           => time interval to sample performance counters.\n";
    cout << "                                        If not specified, or 0, with external program given\n";
    cout << "                                        will read counters only after external program finishes\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help  | /h               => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  --version                          => print application version\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cout << "  -B                                 => Estimate PCIe B/W (in Bytes/sec) by multiplying\n";
    cout << "                                        the number of transfers by the cache line size (=64 bytes).\n";
    cout << "  -e                                 => print additional PCIe LLC miss/hit statistics.\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << " It overestimates the bandwidth under traffic with many partial cache line transfers.\n";
    cout << "\n";
    print_events();
    cout << "\n";
    cout << " Examples:\n";
    cout << "  " << progname << " 1                  => print counters every second without core and socket output\n";
    cout << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << progname << " /csv 5 2>/dev/null => one sample every 5 seconds, and discard all diagnostic output\n";
    cout << "\n";
}

IPlatform *IPlatform::getPlatform(PCM *m, bool csv, bool print_bandwidth, bool print_additional_info, uint32 delay)
{
    switch (m->getCPUFamilyModel()) {
        case PCM::GNR:
        case PCM::SRF:
            return new BirchStreamPlatform(m, csv, print_bandwidth, print_additional_info, delay);
        case PCM::GRR:
            return new LoganvillePlatform(m, csv, print_bandwidth, print_additional_info, delay);
        case PCM::SPR:
        case PCM::EMR:
            return new EagleStreamPlatform(m, csv, print_bandwidth, print_additional_info, delay);
        case PCM::ICX:
        case PCM::SNOWRIDGE:
            return new WhitleyPlatform(m, csv, print_bandwidth, print_additional_info, delay);
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

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream2;
#ifdef PCM_FORCE_SILENT
    null_stream nullStream1;
    cout.rdbuf(&nullStream1);
    cerr.rdbuf(&nullStream2);
#else
    check_and_set_silent(argc, argv, nullStream2);
#endif

    set_signal_handlers();

    cerr << "\n";
    cerr << " Intel(r) Performance Counter Monitor: PCIe Bandwidth Monitoring Utility \n";
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
        string arg_value;

        if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
        {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            // handled in check_and_set_silent
            continue;
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"}))
        {
            csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
        {
            csv = true;
            if (!arg_value.empty()) {
                m->setOutput(arg_value);
            }
            continue;
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"-B", "/b"}))
        {
            print_bandwidth = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-e"}))
        {
            print_additional_info = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"--"}))
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            delay = parse_delay(*argv, program, (print_usage_func)print_usage);
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
        if ( ((delay < 1.0) && (delay > 0.0)) || (delay <= 0.0) ) {
            cerr << "For non-CSV mode delay < 1.0s does not make a lot of practical sense. Default delay 1s is used. Consider to use CSV mode for lower delay values\n";
            delay = PCM_DELAY_DEFAULT;
        }
    }

    cerr << "Update every " << delay << " seconds\n";

    // Delay in milliseconds
    unique_ptr<IPlatform> platform(IPlatform::getPlatform(m, csv, print_bandwidth,
                                    print_additional_info, (uint)(delay * 1000)));

    if (!platform)
    {
        print_cpu_details();
        cerr << "Jaketown, Ivytown, Haswell, Broadwell-DE, Skylake, Icelake, Snowridge and Sapphirerapids Server CPU is required for this tool! Program aborted\n";
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
