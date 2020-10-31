/*
   Copyright (c) 2009-2018, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Roman Dementiev


/*!     \file pcm-numa.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for NUMA (remote and local memory accesses counting). Example for programming offcore response events
*/
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
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
    cerr << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cerr << " Examples:\n";
    cerr << "  " << progname << " 1                  => print counters every second without core and socket output\n";
    cerr << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << progname << " /csv 5 2>/dev/null => one sample every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
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


int main(int argc, char * argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    cout.rdbuf(&nullStream1);
    cerr.rdbuf(&nullStream2);
#endif

    cerr << "\n";
    cerr << " Processor Counter Monitor: NUMA monitoring utility \n";
    cerr << "\n";

    double delay = -1.0;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    bool csv = false;
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
            else if (strncmp(*argv, "-csv", 4) == 0 ||
                     strncmp(*argv, "/csv", 4) == 0)
            {
                csv = true;
                string cmd = string(*argv);
                size_t found = cmd.find('=', 4);
                if (found != string::npos) {
                    string filename = cmd.substr(found + 1);
                    if (!filename.empty()) {
                        m->setOutput(filename);
                    }
                }
                continue;
            }
            else if (mainLoop.parseArg(*argv))
            {
                continue;
            }
            else if (strncmp(*argv, "--", 2) == 0)
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

    regs[0].fields.event_select = OFFCORE_RESPONSE_0_EVTNR; // OFFCORE_RESPONSE 0 event
    regs[0].fields.umask =        OFFCORE_RESPONSE_0_UMASK;
    regs[1].fields.event_select = OFFCORE_RESPONSE_1_EVTNR; // OFFCORE_RESPONSE 1 event
    regs[1].fields.umask =        OFFCORE_RESPONSE_1_UMASK;

    PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
    switch (status)
    {
    case PCM::Success:
        break;
    case PCM::MSRAccessDenied:
        cerr << "Access to Processor Counter Monitor has denied (no MSR or PCI CFG space access).\n";
        exit(EXIT_FAILURE);
    case PCM::PMUBusy:
        cerr << "Access to Processor Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
        cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)\n";
        char yn;
        cin >> yn;
        if ('y' == yn)
        {
            m->resetPMU();
            cerr << "PMU configuration has been reset. Try to rerun the program again.\n";
        }
        exit(EXIT_FAILURE);
    default:
        cerr << "Access to Processor Counter Monitor has denied (Unknown error).\n";
        exit(EXIT_FAILURE);
    }

    print_cpu_details();

    uint64 BeforeTime = 0, AfterTime = 0;
    SystemCounterState SysBeforeState, SysAfterState;
    const uint32 ncores = m->getNumCores();
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

        for (uint32 i = 0; i < ncores; ++i)
        {
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
