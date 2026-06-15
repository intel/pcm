// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev


/*!     \file pcm-numa.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for NUMA (remote and local memory accesses counting). Example for programming offcore response events
*/
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/time.h> // for gettimeofday()
#endif
#include <math.h>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <assert.h>
#include <list>
#include "cpucounters.h"
#include "utils.h"
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include <vector>
#define PCM_DELAY_DEFAULT 1.0       // in seconds
#define PCM_DELAY_MIN 0.015         // 15 milliseconds is practical on most modern CPUs

using namespace std;
using namespace pcm;

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
    cout << "  -c=corelist                        => check specified cores (default all cores)\n";
    cout << "                                        (examples: -c=10  -c=10-11 -c=4,6,12-20,6)\n";
    cout << "  --version                          => print application version\n";
    cout << "  -pid PID | /pid PID                => collect core metrics only for specified process ID\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << " Examples:\n";
    cout << "  " << progname << " 1                  => print counters every second without core and socket output\n";
    cout << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << progname << " /csv 5 2>/dev/null => one sample every 5 seconds, and discard all diagnostic output\n";
    cout << "\n";
}

template <class StateType>
void print_stats(const StateType & BeforeState, const StateType & AfterState, bool csv)
{
    uint64 cycles = getCycles(BeforeState, AfterState);
    uint64 instr = getInstructionsRetired(BeforeState, AfterState);

    if (csv)
    {
        cout << double(instr) / double(cycles) << ",";
        cout << instr << ",";
        cout << cycles << ",";
    }
    else
    {
        cout << double(instr) / double(cycles) << "       ";
        cout << unit_format(instr) << "     ";
        cout << unit_format(cycles) << "      ";
    }

    for (int i = 0; i < 2; ++i)
        if (!csv)
            cout << unit_format(getNumberOfCustomEvents(i, BeforeState, AfterState)) << "              ";
        else
            cout << getNumberOfCustomEvents(i, BeforeState, AfterState) << ",";

    cout << "\n";
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
    cerr << " Intel(r) Performance Counter Monitor: NUMA monitoring utility \n";
    cerr << "\n";

    double delay = -1.0;
    int pid{ -1 };
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    bool csv = false;
    MainLoop mainLoop;
    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    parsePID(argc, argv, pid);

    std::list<int> corelist;
    
    if (argc > 1) do
        {
            argv++;
            argc--;
            string arg_value;

            if (*argv == nullptr)
            {
                continue;
            }
            else if (extract_argument_value(*argv, {"-c"}, arg_value))
            {
                const char *pstr = arg_value.c_str();
                corelist = extract_integer_list(pstr);
            }
            else if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
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
            }
            else if (isPIDOption(argv))
            {
                argv++;
                argc--;
                continue;
            }
            else if (mainLoop.parseArg(*argv))
            {
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
        } while (argc > 1); // end of command line partsing loop

    EventSelectRegister def_event_select_reg;
    def_event_select_reg.value = 0;
    def_event_select_reg.fields.usr = 1;
    def_event_select_reg.fields.os = 1;
    def_event_select_reg.fields.enable = 1;

    PCM::ExtendedCustomCoreEventDescription conf;
    conf.fixedCfg = NULL; // default
    conf.nGPCounters = 2;
    
    try {
        m->setupCustomCoreEventsForNuma(conf);
    }
    catch (UnsupportedProcessorException& ) {
        cerr << "pcm-numa tool does not support your processor currently.\n";
        exit(EXIT_FAILURE);
    }

    EventSelectRegister regs[4];
    conf.gpCounterCfg = regs;
    for (int i = 0; i < 4; ++i)
        regs[i] = def_event_select_reg;

    regs[0].fields.event_select = m->getOCREventNr(0, 0).first; // OFFCORE_RESPONSE 0 event
    regs[0].fields.umask =        m->getOCREventNr(0, 0).second;
    regs[1].fields.event_select = m->getOCREventNr(1, 0).first; // OFFCORE_RESPONSE 1 event
    regs[1].fields.umask =        m->getOCREventNr(1, 0).second;

    print_pid_collection_message(pid);

    PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf, false, pid);
    m->checkError(status);

    print_cpu_details();

    uint64 BeforeTime = 0, AfterTime = 0;
    SystemCounterState SysBeforeState, SysAfterState;
    const uint32 ncores = m->getNumCores();
    if (corelist.size()==0){
      for (int ii = 0; ii < (int)ncores; ++ii) corelist.push_back(ii);
    }
    vector<CoreCounterState> BeforeState, AfterState;
    vector<SocketCounterState> DummySocketStates;

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (csv) {
        if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;
    } else {
        // for non-CSV mode delay < 1.0 does not make a lot of practical sense:
        // hard to read from the screen, or
        // in case delay is not provided in command line => set default
        if (((delay < 1.0) && (delay > 0.0)) || (delay <= 0.0)) delay = PCM_DELAY_DEFAULT;
    }

    cerr << "Update every " << delay << " seconds\n";

    cout.precision(2);
    cout << fixed;

    BeforeTime = m->getTickCount();
    m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
        if (!csv) cout << flush;

        calibratedSleep(delay, sysCmd, mainLoop, m);

        AfterTime = m->getTickCount();
        m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);

        cout << "Time elapsed: " << dec << fixed << AfterTime - BeforeTime << " ms\n";
        //cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";

        if (csv)
            cout << "Core,IPC,Instructions,Cycles,Local DRAM accesses,Remote DRAM accesses \n";
        else
            cout << "Core | IPC  | Instructions | Cycles  |  Local DRAM accesses | Remote DRAM Accesses \n";

    for (int ix : corelist)
        {
            uint32 i = ix;
            if (csv)
                cout << i << ",";
            else
                cout << " " << setw(3) << i << "   " << setw(2);

            print_stats(BeforeState[i], AfterState[i], csv);
        }


        if (csv)
            cout << "*,";
        else
        {
            cout << "-------------------------------------------------------------------------------------------------------------------\n";
            cout << "   *   ";
        }

        print_stats(SysBeforeState, SysAfterState, csv);

        cout << "\n";

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);
        swap(SysBeforeState, SysAfterState);

        if (m->isBlocked()) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    exit(EXIT_SUCCESS);
}
