/*
   Copyright (c) 2009-2020, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /*!     \file pcm-raw.cpp
         \brief Example of using CPU counters: implements a performance counter monitoring utility with raw events interface
   */
#include <iostream>
#ifdef _MSC_VER
#define strtok_r strtok_s
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
#include <bitset>
#include "cpucounters.h"
#include "utils.h"
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include <vector>
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define MAX_CORES 4096

using namespace std;
using namespace pcm;

void print_usage(const string progname)
{
    cerr << "\n Usage: \n " << progname
        << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cerr << "   <delay>                               => time interval to sample performance counters.\n";
    cerr << "                                            If not specified, or 0, with external program given\n";
    cerr << "                                            will read counters only after external program finishes\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help      | /h               => print this help and exit\n";
    cerr << "  -csv[=file.csv]     | /csv[=file.csv]  => output compact CSV format to screen or\n"
        << "                                            to a file, in case filename is provided\n";
    cerr << "  [-e event1] [-e event2] [-e event3] .. => list of custom events to monitor\n";
    cerr << "  event description example: -e core/config=0x30203,name=LD_BLOCKS.STORE_FORWARD/ -e core/fixed,config=0x333/ \n";
    cerr << "                             -e cha/config=0,name=UNC_CHA_CLOCKTICKS/ -e imc/fixed,name=DRAM_CLOCKS/\n";
    cerr << "  -yc   | --yescores  | /yc              => enable specific cores to output\n";
    cerr << "  -f    | /f                             => enforce flushing each line for interactive output\n";
    cerr << "  -i[=number] | /i[=number]              => allow to determine number of iterations\n";
    print_help_force_rtm_abort_mode(41);
    cerr << " Examples:\n";
    cerr << "  " << progname << " 1                   => print counters every second without core and socket output\n";
    cerr << "  " << progname << " 0.5 -csv=test.log   => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << progname << " /csv 5 2>/dev/null  => one sampe every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
}


// emulates scanf %i for hex 0x prefix otherwise assumes dec (no oct support)
bool match(const string& subtoken, const string& sname, uint64* result)
{
    if (pcm_sscanf(subtoken) >> s_expect(sname + "0x") >> std::hex >> *result)
        return true;

    if (pcm_sscanf(subtoken) >> s_expect(sname) >> std::dec >> *result)
        return true;

    return false;
}

PCM::RawPMUConfigs allPMUConfigs;

bool addEvent(string eventStr)
{
    PCM::RawEventConfig config = { {0,0,0}, "" };
    const auto typeConfig = split(eventStr, '/');
    if (typeConfig.size() < 2)
    {
        cerr << "ERROR: wrong syntax in event description \"" << eventStr << "\"\n";
        return false;
    }
    auto pmuName = typeConfig[0];
    if (pmuName.empty())
    {
        pmuName = "core";
    }
    const auto configStr = typeConfig[1];
    if (configStr.empty())
    {
        cerr << "ERROR: empty config description in event description \"" << eventStr << "\"\n";
        return false;
    }
    const auto configArray = split(configStr, ',');
    bool fixed = false;
    for (auto item : configArray)
    {
        if (match(item, "config=", &config.first[0])) {}
        else if (match(item, "config1=", &config.first[1])) {}
        else if (match(item, "config2=", &config.first[2])) {}
        else if (pcm_sscanf(item) >> s_expect("name=") >> setw(255) >> config.second) {}
        else if (item == "fixed")
        {
            fixed = true;
        }
        else
        {
            cerr << "ERROR: unknown token " << item << " in event description \"" << eventStr << "\"\n";
            return false;
        }
    }
    cout << "parsed "<< (fixed?"fixed ":"")<<"event " << pmuName << ": \"" << hex << config.second << "\" : {0x" << hex << config.first[0] << ", 0x" << config.first[1] << ", 0x" << config.first[2] << "}\n" << dec;
    if (fixed)
        allPMUConfigs[pmuName].fixed.push_back(config);
    else
        allPMUConfigs[pmuName].programmable.push_back(config);
    return true;
}

bool show_partial_core_output = false;
bitset<MAX_CORES> ycores;
bool flushLine = false;

void print(PCM* m, vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState, vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState, const CsvOutputType outputType)
{
    printDateForCSV(outputType);
    if (BeforeState.size() > 0 && AfterState.size() > 0)
    {
        choose(outputType,
            []() { cout << ","; },
            []() { cout << "ms,"; },
            [&]() { cout << (1000ULL * getInvariantTSC(BeforeState[0], AfterState[0])) / m->getNominalFrequency() << ","; });
    }
    for (auto typeEvents : allPMUConfigs)
    {
        const auto & type = typeEvents.first;
        const auto & events = typeEvents.second.programmable;
        const auto & fixedEvents = typeEvents.second.fixed;
        if (type == "core")
        {
            for (uint32 core = 0; core < m->getNumCores(); ++core)
            {
                if (m->isCoreOnline(core) == false || (show_partial_core_output && ycores.test(core) == false))
                    continue;

                if (fixedEvents.size())
                {
                    auto print = [&](const char* metric, const uint64 value)
                    {
                        choose(outputType,
                            [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << ","; },
                            [&fixedEvents,&metric]() { cout << metric << fixedEvents[0].second << ","; },
                            [&]() { cout << value << ","; });
                    };
                    print("InstructionsRetired", getInstructionsRetired(BeforeState[core], AfterState[core]));
                    print("Cycles", getCycles(BeforeState[core], AfterState[core]));
                    print("RefCycles", getRefCycles(BeforeState[core], AfterState[core]));
                }
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "COREEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getNumberOfCustomEvents(i, BeforeState[core], AfterState[core]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "m3upi")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 l = 0; l < m->getQPILinksPerSocket(); ++l)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "M3UPIEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getM3UPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "xpi" || type == "upi" || type == "qpi")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 l = 0; l < m->getQPILinksPerSocket(); ++l)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "XPIEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getXPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "imc")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 ch = 0; ch < m->getMCChannelsPerSocket(); ++ch)
                {
                    if (fixedEvents.size())
                    {
                        choose(outputType,
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << ","; },
                            [&fixedEvents]() { cout << "DRAMClocks" << fixedEvents[0].second << ","; },
                            [&]() { cout << getDRAMClocks(ch, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    }
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "IMCEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getMCCounter(ch, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "m2m")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 mc = 0; mc < m->getMCPerSocket(); ++mc)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, mc]() { cout << "SKT" << s << "MC" << mc << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "M2MEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getM2MCounter(mc, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "pcu")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "PCUEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getPCUCounter(i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "ubox")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                if (fixedEvents.size())
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&fixedEvents]() { cout << "UncoreClocks" << fixedEvents[0].second << ","; },
                        [&]() { cout << getUncoreClocks(BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                }
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "UBOXEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getUBOXCounter(i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "cbo" || type == "cha")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 cbo = 0; cbo < m->getMaxNumOfCBoxes(); ++cbo)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, cbo]() { cout << "SKT" << s << "C" << cbo << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "CBOEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getCBOCounter(cbo, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "iio")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 stack = 0; stack < m->getMaxNumOfIIOStacks(); ++stack)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, stack]() { cout << "SKT" << s << "IIO" << stack << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "IIOEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getIIOCounter(stack, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else
        {
            std::cerr << "ERROR: unrecognized PMU type \"" << type << "\"\n";
        }
    }
    if (flushLine)
    {
        cout << endl;
    }
    else
    {
        cout << "\n";
    }
}

void printAll(PCM * m, vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState, vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState)
{
    static bool displayHeader = true;
    if (displayHeader)
    {
        print(m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Header1);
        print(m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Header2);
        displayHeader = false;
    }
    print(m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Data);
}

int main(int argc, char* argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#endif

    cerr << "\n";
    cerr << " Processor Counter Monitor: Core Monitoring Utility \n";
    cerr << "\n";

    double delay = -1.0;
    char* sysCmd = NULL;
    char** sysArgv = NULL;
    MainLoop mainLoop;
    string program = string(argv[0]);

    PCM* m = PCM::getInstance();

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
        else if (
            strncmp(*argv, "-f", 2) == 0 ||
            strncmp(*argv, "/f", 2) == 0)
        {
            flushLine = true;
            continue;
        }
        else if (strncmp(*argv, "--yescores", 10) == 0 ||
            strncmp(*argv, "-yc", 3) == 0 ||
            strncmp(*argv, "/yc", 3) == 0)
        {
            argv++;
            argc--;
            show_partial_core_output = true;
            if (*argv == NULL)
            {
                cerr << "Error: --yescores requires additional argument.\n";
                exit(EXIT_FAILURE);
            }
            std::stringstream ss(*argv);
            while (ss.good())
            {
                string s;
                int core_id;
                std::getline(ss, s, ',');
                if (s.empty())
                    continue;
                core_id = atoi(s.c_str());
                if (core_id > MAX_CORES)
                {
                    cerr << "Core ID:" << core_id << " exceed maximum range " << MAX_CORES << ", program abort\n";
                    exit(EXIT_FAILURE);
                }

                ycores.set(atoi(s.c_str()), true);
            }
            if (m->getNumCores() > MAX_CORES)
            {
                cerr << "Error: --yescores option is enabled, but #define MAX_CORES " << MAX_CORES << " is less than  m->getNumCores() = " << m->getNumCores() << "\n";
                cerr << "There is a potential to crash the system. Please increase MAX_CORES to at least " << m->getNumCores() << " and re-enable this option.\n";
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (strncmp(*argv, "-e", 2) == 0)
        {
            argv++;
            argc--;
            if (addEvent(*argv) == false)
            {
                exit(EXIT_FAILURE);
            }

            continue;
        }
        else
            if (CheckAndForceRTMAbortMode(*argv, m))
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
                std::istringstream is_str_stream(*argv);
                is_str_stream >> noskipws >> delay_input;
                if (is_str_stream.eof() && !is_str_stream.fail()) {
                    delay = delay_input;
                }
                else {
                    cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                    print_usage(program);
                    exit(EXIT_FAILURE);
                }
                continue;
            }
    } while (argc > 1); // end of command line parsing loop

    PCM::ErrorCode status = m->program(allPMUConfigs);
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
        std::cin >> yn;
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


    SystemCounterState SysBeforeState, SysAfterState;
    vector<CoreCounterState> BeforeState, AfterState;
    vector<SocketCounterState> DummySocketStates;
    vector<ServerUncoreCounterState> BeforeUncoreState, AfterUncoreState;
    BeforeUncoreState.resize(m->getNumSockets());
    AfterUncoreState.resize(m->getNumSockets());

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    }
    else {
        m->setBlocked(false);
    }


    if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;

    cerr << "Update every " << delay << " seconds\n";

    std::cout.precision(2);
    std::cout << std::fixed;

    m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);
    for (uint32 s = 0; s < m->getNumSockets(); ++s)
    {
        BeforeUncoreState[s] = m->getServerUncoreCounterState(s);
    }

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
        calibratedSleep(delay, sysCmd, mainLoop, m);

        m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);
        for (uint32 s = 0; s < m->getNumSockets(); ++s)
        {
            AfterUncoreState[s] = m->getServerUncoreCounterState(s);
        }

        //cout << "Time elapsed: " << dec << fixed << AfterTime - BeforeTime << " ms\n";
        //cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";

        printAll(m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState);

        swap(BeforeState, AfterState);
        swap(SysBeforeState, SysAfterState);
        swap(BeforeUncoreState, AfterUncoreState);

        if (m->isBlocked()) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });
    exit(EXIT_SUCCESS);
}
