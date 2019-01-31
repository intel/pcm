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


/*!     \file pcm-tsx.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for Intel Transactional Synchronization Extensions
  */
#define HACK_TO_REMOVE_DUPLICATE_ERROR
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
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration

using namespace std;

struct TSXEvent
{
    const char * name;
    unsigned char event;
    unsigned char umask;
    const char * description;
};

TSXEvent eventDefinition[] = {
    { "RTM_RETIRED.START", 0xC9, 0x01, "Number of times an RTM execution started." },
    { "RTM_RETIRED.COMMIT", 0xC9, 0x02, "Number of times an RTM execution successfully committed" },
    { "RTM_RETIRED.ABORTED", 0xC9, 0x04, "Number of times an RTM execution aborted due to any reasons (multiple categories may count as one)" },
    { "RTM_RETIRED.ABORTED_MISC1", 0xC9, 0x08, "Number of times an RTM execution aborted due to various memory events" },
    { "RTM_RETIRED.ABORTED_MISC2", 0xC9, 0x10, "Number of times an RTM execution aborted due to uncommon conditions" },
    { "RTM_RETIRED.ABORTED_MISC3", 0xC9, 0x20, "Number of times an RTM execution aborted due to Intel TSX-unfriendly instructions" },
    { "RTM_RETIRED.ABORTED_MISC4", 0xC9, 0x40, "Number of times an RTM execution aborted due to incompatible memory type" },
    { "RTM_RETIRED.ABORTED_MISC5", 0xC9, 0x80, "Number of times an RTM execution aborted due to none of the previous 4 categories (e.g. interrupt)" },

    { "HLE_RETIRED.START", 0xC8, 0x01, "Number of times an HLE execution started." },
    { "HLE_RETIRED.COMMIT", 0xC8, 0x02, "Number of times an HLE execution successfully committed" },
    { "HLE_RETIRED.ABORTED", 0xC8, 0x04, "Number of times an HLE execution aborted due to any reasons (multiple categories may count as one)" },
    { "HLE_RETIRED.ABORTED_MISC1", 0xC8, 0x08, "Number of times an HLE execution aborted due to various memory events" },
    { "HLE_RETIRED.ABORTED_MISC2", 0xC8, 0x10, "Number of times an HLE execution aborted due to uncommon conditions" },
    { "HLE_RETIRED.ABORTED_MISC3", 0xC8, 0x20, "Number of times an HLE execution aborted due to Intel TSX-unfriendly instructions" },
    { "HLE_RETIRED.ABORTED_MISC4", 0xC8, 0x40, "Number of times an HLE execution aborted due to incompatible memory type" },
    { "HLE_RETIRED.ABORTED_MISC5", 0xC8, 0x80, "Number of times an HLE execution aborted due to none of the previous 4 categories (e.g. interrupt)" },

    { "TX_MEM.ABORT_CONFLICT", 0x54, 0x01, "Number of times a transactional abort was signaled due to a data conflict on a transactionally accessed address" },
    { "TX_MEM.ABORT_CAPACITY_WRITE", 0x54, 0x02, "Number of times a transactional abort was signaled due to limited resources for transactional stores" },
    { "TX_MEM.ABORT_HLE_STORE_TO_ELIDED_LOCK", 0x54, 0x04, "Number of times a HLE transactional region aborted due to a non XRELEASE prefixed instruction writing to an elided lock in the elision buffer" },
    { "TX_MEM.ABORT_HLE_ELISION_BUFFER_NOT_EMPTY", 0x54, 0x08, "Number of times an HLE transactional execution aborted due to NoAllocatedElisionBuffer being nonzero." },
    { "TX_MEM.ABORT_HLE_ELISION_BUFFER_MISMATCH", 0x54, 0x10, "Number of times an HLE transactional execution aborted due to XRELEASE lock not satisfying the address and value requirements in the elision buffer." },
    { "TX_MEM.ABORT_HLE_ELISION_BUFFER_UNSUPPORTED_ALIGNMENT", 0x54, 0x20, "Number of times an HLE transactional execution aborted due to an unsupported read alignment from the elision buffer." },
    { "TX_MEM.HLE_ELISION_BUFFER_FULL", 0x54, 0x40, "Number of times HLE lock could not be elided due to ElisionBufferAvailable being zero." },

    { "TX_EXEC.MISC1", 0x5D, 0x01, "Counts the number of times a class of instructions that may cause a transactional abort was executed. Since this is the count of execution, it may not always cause a transactional abort." },
    { "TX_EXEC.MISC2", 0x5D, 0x02, "Counts the number of times a class of instructions that may cause a transactional abort was executed inside a transactional region" },
    { "TX_EXEC.MISC3", 0x5D, 0x04, "Counts the number of times an instruction execution caused the nest count supported to be exceeded" },
    { "TX_EXEC.MISC4", 0x5D, 0x08, "Counts the number of times an HLE XACQUIRE instruction was executed inside an RTM transactional region" }
};

void print_usage(const string progname)
{
    cerr << endl << " Usage: " << endl << " " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help  | /h               => print this help and exit" << endl;
    cerr << "  -F    | -force                     => force running this program despite lack of HW RTM support (optional)" << endl;
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
         << "                                        to a file, in case filename is provided" << endl;
    cerr << "  [-e event1] [-e event2] [-e event3]=> optional list of custom TSX events to monitor (up to 4)."
         << "  The list of supported events:" << endl;
    for (uint32 i = 0; i < sizeof(eventDefinition) / sizeof(TSXEvent); ++i)
    {
        cerr << eventDefinition[i].name << "\t" << eventDefinition[i].description << endl;
    }
    cerr << endl;
    cerr << " Examples:" << endl;
    cerr << "  " << progname << " 1                  => print counters every second without core and socket output" << endl;
    cerr << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format" << endl;
    cerr << "  " << progname << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output" << endl;
    cerr << endl;
}


#define TX_CYCLES_POS           (1)
#define TX_CYCLES_COMMITED_POS  (2)
#define N_HLE_POS               (3)
#define N_RTM_POS               (0)

template <class StateType>
void print_basic_stats(const StateType & BeforeState, const StateType & AfterState, bool csv)
{
    uint64 cycles = getCycles(BeforeState, AfterState);
    uint64 instr = getInstructionsRetired(BeforeState, AfterState);
    const uint64 TXcycles = getNumberOfCustomEvents(TX_CYCLES_POS, BeforeState, AfterState);
    const uint64 TXcycles_commited = getNumberOfCustomEvents(TX_CYCLES_COMMITED_POS, BeforeState, AfterState);
    const uint64 Abr_cycles = (TXcycles > TXcycles_commited) ? (TXcycles - TXcycles_commited) : 0ULL;
    uint64 nRTM = getNumberOfCustomEvents(N_RTM_POS, BeforeState, AfterState);
    uint64 nHLE = getNumberOfCustomEvents(N_HLE_POS, BeforeState, AfterState);

    if (csv)
    {
        cout << double(instr) / double(cycles) << ",";
        cout << instr << ",";
        cout << cycles << ",";
        cout << TXcycles << "," << std::setw(5) << 100. * double(TXcycles) / double(cycles) << "%,";
        cout << Abr_cycles << "," << std::setw(5) << 100. * double(Abr_cycles) / double(cycles) << "%,";
        cout << nRTM << ",";
        cout << nHLE << ",";
    }
    else
    {
        cout << double(instr) / double(cycles) << "       ";
        cout << unit_format(instr) << "     ";
        cout << unit_format(cycles) << "      ";
        cout << unit_format(TXcycles) << " (" << std::setw(5) << 100. * double(TXcycles) / double(cycles) << "%)       ";
        cout << unit_format(Abr_cycles) << " (" << std::setw(5) << 100. * double(Abr_cycles) / double(cycles) << "%) ";
        cout << unit_format(nRTM) << "   ";
        cout << unit_format(nHLE) << "    ";
    }

    if (nRTM + nHLE)
    {
        uint64 cyclesPerTransaction = TXcycles / (nRTM + nHLE);
        if (csv)
            cout << cyclesPerTransaction << "\n";
        else
            cout << unit_format(cyclesPerTransaction) << "\n";
    }
    else
        cout << " N/A" << "\n";
}

template <class StateType>
void print_custom_stats(const StateType & BeforeState, const StateType & AfterState, bool csv)
{
    for (int i = 0; i < 4; ++i)
        if (!csv)
            cout << unit_format(getNumberOfCustomEvents(i, BeforeState, AfterState)) << "    ";
        else
            cout << getNumberOfCustomEvents(i, BeforeState, AfterState) << ",";

    cout << "\n";
}


int findEvent(const char * name)
{
    const int all = sizeof(eventDefinition) / sizeof(TSXEvent);
    for (int i = 0; i < all; ++i)
    {
        if (strcmp(name, eventDefinition[i].name) == 0)
            return i;
    }
    return -1;
}

int main(int argc, char * argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#endif

    cerr << endl;
    cerr << " Processor Counter Monitor: Intel(r) Transactional Synchronization Extensions Monitoring Utility " << endl;
    cerr << endl;

    double delay = -1.0;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    std::vector<int> events;
    int cur_event;
    bool csv = false;
    long diff_usec = 0;                            // deviation of clock is useconds between measurements
    bool force = false;
    int calibrated = PCM_CALIBRATION_INTERVAL - 2; // keeps track is the clock calibration needed
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
            else if (strncmp(*argv, "-e", 2) == 0)
            {
                argv++;
                argc--;
                if (events.size() >= 4) {
                    cerr << "At most 4 events are allowed" << endl;
                    exit(EXIT_FAILURE);
                }
                cur_event = findEvent(*argv);
                if (cur_event < 0) {
                    cerr << "Event " << *argv << " is not supported. See the list of supported events" << endl;
                    print_usage(program);
                    exit(EXIT_FAILURE);
                }
                events.push_back(cur_event);
                continue;
            }
            else if ((strncmp(*argv, "-F", 2) == 0) ||
                     (strncmp(*argv, "-f", 2) == 0) ||
                     (strncmp(*argv, "-force", 6) == 0))
            {
                force = true;
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
                std::istringstream is_str_stream(*argv);
                is_str_stream >> noskipws >> delay_input;
                if (is_str_stream.eof() && !is_str_stream.fail()) {
                    delay = delay_input;
                } else {
                    cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it." << endl;
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
    EventSelectRegister regs[4];
    conf.gpCounterCfg = regs;
    for (int i = 0; i < 4; ++i)
        regs[i] = def_event_select_reg;

    if (events.empty())
    {
        conf.nGPCounters = 4;
        regs[N_RTM_POS].fields.event_select = 0xc9;
        regs[N_RTM_POS].fields.umask = 0x01;
        regs[N_HLE_POS].fields.event_select = 0xc8;
        regs[N_HLE_POS].fields.umask = 0x01;
        regs[TX_CYCLES_COMMITED_POS].fields.event_select = 0x3c;
        regs[TX_CYCLES_COMMITED_POS].fields.in_tx = 1;
        regs[TX_CYCLES_COMMITED_POS].fields.in_txcp = 1;
        regs[TX_CYCLES_POS].fields.event_select = 0x3c;
        regs[TX_CYCLES_POS].fields.in_tx = 1;
    }
    else
    {
        conf.nGPCounters = (uint32) events.size();
        for (unsigned int i = 0; i < events.size(); ++i)
        {
            regs[i].fields.event_select = eventDefinition[events[i]].event;
            regs[i].fields.umask = eventDefinition[events[i]].umask;
        }
    }

    PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
    switch (status)
    {
    case PCM::Success:
        break;
    case PCM::MSRAccessDenied:
        cerr << "Access to Processor Counter Monitor has denied (no MSR or PCI CFG space access)." << endl;
        exit(EXIT_FAILURE);
    case PCM::PMUBusy:
        cerr << "Access to Processor Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU." << endl;
        cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)" << endl;
        char yn;
        std::cin >> yn;
        if ('y' == yn)
        {
            m->resetPMU();
            cerr << "PMU configuration has been reset. Try to rerun the program again." << endl;
        }
        exit(EXIT_FAILURE);
    default:
        cerr << "Access to Processor Counter Monitor has denied (Unknown error)." << endl;
        exit(EXIT_FAILURE);
    }

    print_cpu_details();

    bool rtm_support = m->supportsRTM();

    if (!rtm_support) {
        if (!force) {
            cerr << "No RTM support detected, use -F if you still want to run this program." << endl;
            exit(EXIT_FAILURE);
        }
        cerr << "No RTM support detected, but -F found as argument, running anyway." << endl;
    }

    uint64 BeforeTime = 0, AfterTime = 0;
    SystemCounterState SysBeforeState, SysAfterState;
    const uint32 ncores = m->getNumCores();
    std::vector<CoreCounterState> BeforeState, AfterState;
    std::vector<SocketCounterState> DummySocketStates;

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

    cerr << "Update every " << delay << " seconds" << endl;

    std::cout.precision(2);
    std::cout << std::fixed;

    BeforeTime = m->getTickCount();
    m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    while (1)
    {
        if (!csv) cout << std::flush;
        int delay_ms = int(delay * 1000);
        int calibrated_delay_ms = delay_ms;
#ifdef _MSC_VER
        // compensate slow Windows console output
        if (AfterTime) delay_ms -= (int)(m->getTickCount() - BeforeTime);
        if (delay_ms < 0) delay_ms = 0;
#else
        // compensation of delay on Linux/UNIX
        // to make the samling interval as monotone as possible
        struct timeval start_ts, end_ts;
        if (calibrated == 0) {
            gettimeofday(&end_ts, NULL);
            diff_usec = (end_ts.tv_sec - start_ts.tv_sec) * 1000000.0 + (end_ts.tv_usec - start_ts.tv_usec);
            calibrated_delay_ms = delay_ms - diff_usec / 1000.0;
        }
#endif

        MySleepMs(calibrated_delay_ms);

#ifndef _MSC_VER
        calibrated = (calibrated + 1) % PCM_CALIBRATION_INTERVAL;
        if (calibrated == 0) {
            gettimeofday(&start_ts, NULL);
        }
#endif
        AfterTime = m->getTickCount();
        m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);

        cout << "Time elapsed: " << dec << fixed << AfterTime - BeforeTime << " ms\n";
        //cout << "Called sleep function for "<<dec<<fixed<<delay_ms<<" ms\n";

        if (events.empty())
        {
            if (csv)
                cout << "Core,IPC,Instructions,Cycles,Transactional Cycles,Transactional Cycles %,Aborted Cycles,Aborted Cycles %,#RTM,#HLE,Cycles/Transaction \n";
            else
                cout << "Core | IPC  | Instructions | Cycles  | Transactional Cycles | Aborted Cycles  | #RTM  | #HLE  | Cycles/Transaction \n";
        }
        else
        {
            for (uint32 i = 0; i < events.size(); ++i)
            {
                cout << "Event" << i << ": " << eventDefinition[events[i]].name << " " << eventDefinition[events[i]].description << " (raw 0x" <<
                std::hex << (uint32)eventDefinition[events[i]].umask << (uint32)eventDefinition[events[i]].event << std::dec << ")" << endl;
            }
            cout << "\n";
            if (csv)
                cout << "Core,Event0,Event1,Event2,Event3\n";
            else
                cout << "Core | Event0  | Event1  | Event2  | Event3 \n";
        }
        for (uint32 i = 0; i < ncores; ++i)
        {
            if (csv)
                cout << i << ",";
            else
                cout << " " << setw(3) << i << "   " << setw(2);
            if (events.empty())
                print_basic_stats(BeforeState[i], AfterState[i], csv);
            else
                print_custom_stats(BeforeState[i], AfterState[i], csv);
        }
        if (csv)
            cout << "*,";
        else
        {
            cout << "-------------------------------------------------------------------------------------------------------------------\n";
            cout << "   *   ";
        }
        if (events.empty())
            print_basic_stats(SysBeforeState, SysAfterState, csv);
        else
            print_custom_stats(SysBeforeState, SysAfterState, csv);

        std::cout << std::endl;

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);
        swap(SysBeforeState, SysAfterState);

        if (m->isBlocked()) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
    }
    exit(EXIT_SUCCESS);
}
