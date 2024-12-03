// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev,
//            Thomas Willhalm,
//            Patrick Ungerer


/*!     \file pcm.cpp
\brief Example of using CPU counters: implements a simple performance counter monitoring utility
*/
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#include <signal.h>   // for atexit()
#include <sys/time.h> // for gettimeofday()
#endif
#include <math.h>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <cstring>
#include <sstream>
#include <assert.h>
#include <bitset>
#include "cpucounters.h"
#include "utils.h"

#define SIZE (10000000)
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define MAX_CORES 4096

using namespace std;
using namespace pcm;

template <class IntType>
double float_format(IntType n)
{
    return double(n) / 1e6;
}

std::string temp_format(int32 t)
{
    char buffer[1024];
    if (t == PCM_INVALID_THERMAL_HEADROOM)
        return "N/A";

    snprintf(buffer, 1024, "%2d", t);
    return buffer;
}

std::string l3cache_occ_format(uint64 o)
{
    char buffer[1024];
    if (o == PCM_INVALID_QOS_MONITORING_DATA)
        return "N/A";

    snprintf(buffer, 1024, "%6u", (uint32) o);
    return buffer;
}

template <class UncoreStateType>
double getAverageUncoreFrequencyGhz(const UncoreStateType& before, const UncoreStateType& after) // in GHz
{
    return getAverageUncoreFrequency(before, after) / 1e9;
}

void print_help(const string & prog_name)
{
    cout << "\n Usage: \n " << prog_name
        << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cout << "   <delay>                           => time interval to sample performance counters.\n";
    cout << "                                        If not specified, or 0, with external program given\n";
    cout << "                                        will read counters only after external program finishes\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help      | /h           => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  --version                          => print application version\n";
    cout << "  -pid PID | /pid PID                => collect core metrics only for specified process ID\n";
#ifdef _MSC_VER
    cout << "  --uninstallDriver   | --installDriver=> (un)install driver\n";
#endif
    cout << "  -r    | --reset     | /reset       => reset PMU configuration (at your own risk)\n";
    cout << "  -nc   | --nocores   | /nc          => hide core related output\n";
    cout << "  -yc   | --yescores  | /yc          => enable specific cores to output\n";
    cout << "  -ns   | --nosockets | /ns          => hide socket related output\n";
    cout << "  -nsys | --nosystem  | /nsys        => hide system related output\n";
    cout << "  --color                            => use ASCII colors\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
        << "                                        to a file, in case filename is provided\n"
        << "                                        the format used is documented here: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-pcm-column-names-decoder-ring.html\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << "  -m=integer | /m=integer            => metrics version (default = 2)\n";
    print_enforce_flush_option_help();
    print_help_force_rtm_abort_mode(37);
    cout << " Examples:\n";
    cout << "  " << prog_name << " 1 -nc -ns          => print counters every second without core and socket output\n";
    cout << "  " << prog_name << " 1 -i=10            => print counters every second 10 times and exit\n";
    cout << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << prog_name << " /csv 5 2>/dev/null => one sample every 5 seconds, and discard all diagnostic output\n";
    cout << "\n";
}


template <class State>
void print_basic_metrics(const PCM * m, const State & state1, const State & state2, const int metricVersion)
{
    switch (metricVersion)
    {
        case 2:
            if (m->isCoreCStateResidencySupported(0))
            {
                cout << setNextColor() << "     " << getCoreCStateResidency(0, state1, state2);
            }
            cout << setNextColor() <<  "   " << getIPC(state1, state2);
            if (m->isActiveRelativeFrequencyAvailable())
            {
                cout << setNextColor() <<  "    " << getActiveAverageFrequency(state1, state2)/1e9;
            }
            break;
        default:
            cout << setNextColor() << "     " << getExecUsage(state1, state2) <<
                setNextColor() << "   " << getIPC(state1, state2) <<
                setNextColor() << "   " << getRelativeFrequency(state1, state2);
            if (m->isActiveRelativeFrequencyAvailable())
                cout << setNextColor() << "    " << getActiveRelativeFrequency(state1, state2);
    }
    if (m->isL3CacheMissesAvailable())
        cout << setNextColor() << "    " << unit_format(getL3CacheMisses(state1, state2));
    if (m->isL2CacheMissesAvailable())
        cout << setNextColor() << "   " << unit_format(getL2CacheMisses(state1, state2));
    if (m->isL3CacheHitRatioAvailable())
        cout << setNextColor() << "    " << getL3CacheHitRatio(state1, state2);
    if (m->isL2CacheHitRatioAvailable())
        cout << setNextColor() << "    " << getL2CacheHitRatio(state1, state2);
    cout.precision(4);
    if (m->isL3CacheMissesAvailable())
        cout << setNextColor() << "  " << double(getL3CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    if (m->isL2CacheMissesAvailable())
        cout << setNextColor() << "  " << double(getL2CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    cout.precision(2);
}

template <class State>
void print_other_metrics(const PCM * m, const State & state1, const State & state2)
{
    if (m->L3CacheOccupancyMetricAvailable())
        cout << setNextColor() << "   " << setw(6) << l3cache_occ_format(getL3CacheOccupancy(state2));
    if (m->CoreLocalMemoryBWMetricAvailable())
        cout << setNextColor() << "   " << setw(6) << getLocalMemoryBW(state1, state2);
    if (m->CoreRemoteMemoryBWMetricAvailable())
        cout << setNextColor() << "   " << setw(6) << getRemoteMemoryBW(state1, state2);
    cout << setNextColor() <<  "     " << temp_format(state2.getThermalHeadroom()) << "\n";
}

void print_output(PCM * m,
    const std::vector<CoreCounterState> & cstates1,
    const std::vector<CoreCounterState> & cstates2,
    const std::vector<SocketCounterState> & sktstate1,
    const std::vector<SocketCounterState> & sktstate2,
    const std::bitset<MAX_CORES> & ycores,
    const SystemCounterState& sstate1,
    const SystemCounterState& sstate2,
    const int cpu_family_model,
    const bool show_core_output,
    const bool show_partial_core_output,
    const bool show_socket_output,
    const bool show_system_output,
    const int metricVersion
    )
{
    cout << "\n";

    switch (metricVersion)
    {
        case 2:
            if (m->isCoreCStateResidencySupported(0))
            {
                cout << " UTIL  : utlization (same as core C0 state active state residency, the value is in 0..1) \n";
            }
            cout << " IPC   : instructions per CPU cycle\n";
            if (m->isActiveRelativeFrequencyAvailable())
            {
                cout << " CFREQ : core frequency in Ghz\n";
            }
            break;
        default:
            cout << " EXEC  : instructions per nominal CPU cycle\n";
            cout << " IPC   : instructions per CPU cycle\n";
            cout << " FREQ  : relation to nominal CPU frequency='unhalted clock ticks'/'invariant timer ticks' (includes Intel Turbo Boost)\n";
            if (m->isActiveRelativeFrequencyAvailable())
                cout << " AFREQ : relation to nominal CPU frequency while in active state (not in power-saving C state)='unhalted clock ticks'/'invariant timer ticks while in C0-state'  (includes Intel Turbo Boost)\n";
    };
    if (m->isL3CacheMissesAvailable())
        cout << " L3MISS: L3 (read) cache misses \n";
    if (m->isL2CacheHitsAvailable())
    {
        if (m->isAtom() || cpu_family_model == PCM::KNL)
            cout << " L2MISS: L2 (read) cache misses \n";
        else
            cout << " L2MISS: L2 (read) cache misses (including other core's L2 cache *hits*) \n";
    }
    if (m->isL3CacheHitRatioAvailable())
        cout << " L3HIT : L3 (read) cache hit ratio (0.00-1.00)\n";
    if (m->isL2CacheHitRatioAvailable())
        cout << " L2HIT : L2 cache hit ratio (0.00-1.00)\n";
    if (m->isL3CacheMissesAvailable())
        cout << " L3MPI : number of L3 (read) cache misses per instruction\n";
    if (m->isL2CacheMissesAvailable())
        cout << " L2MPI : number of L2 (read) cache misses per instruction\n";
    if (m->memoryTrafficMetricsAvailable()) cout << " READ  : bytes read from main memory controller (in GBytes)\n";
    if (m->memoryTrafficMetricsAvailable()) cout << " WRITE : bytes written to main memory controller (in GBytes)\n";
    if (m->localMemoryRequestRatioMetricAvailable()) cout << " LOCAL : ratio of local memory requests to memory controller in %\n";
    if (m->LLCReadMissLatencyMetricsAvailable()) cout << "LLCRDMISSLAT: average latency of last level cache miss for reads and prefetches (in ns)\n";
    if (m->PMMTrafficMetricsAvailable()) cout << " PMM RD : bytes read from PMM memory (in GBytes)\n";
    if (m->PMMTrafficMetricsAvailable()) cout << " PMM WR : bytes written to PMM memory (in GBytes)\n";
    if (m->HBMmemoryTrafficMetricsAvailable()) cout << " HBM READ  : bytes read from HBM controller (in GBytes)\n";
    if (m->HBMmemoryTrafficMetricsAvailable()) cout << " HBM WRITE : bytes written to HBM controller (in GBytes)\n";
    if (m->memoryIOTrafficMetricAvailable()) {
        cout << " IO    : bytes read/written due to IO requests to memory controller (in GBytes); this may be an over estimate due to same-cache-line partial requests\n";
        cout << " IA    : bytes read/written due to IA requests to memory controller (in GBytes); this may be an over estimate due to same-cache-line partial requests\n";
        cout << " GT    : bytes read/written due to GT requests to memory controller (in GBytes); this may be an over estimate due to same-cache-line partial requests\n";
    }
    if (m->L3CacheOccupancyMetricAvailable()) cout << " L3OCC : L3 occupancy (in KBytes)\n";
    if (m->CoreLocalMemoryBWMetricAvailable()) cout << " LMB   : L3 cache external bandwidth satisfied by local memory (in MBytes)\n";
    if (m->CoreRemoteMemoryBWMetricAvailable()) cout << " RMB   : L3 cache external bandwidth satisfied by remote memory (in MBytes)\n";
    cout << " TEMP  : Temperature reading in 1 degree Celsius relative to the TjMax temperature (thermal headroom): 0 corresponds to the max temperature\n";
    cout << " energy: Energy in Joules\n";
    cout << "\n";
    cout << "\n";
    const char * longDiv = "---------------------------------------------------------------------------------------------------------------\n";
    cout.precision(2);
    cout << std::fixed;
    if (cpu_family_model == PCM::KNL)
        cout << " Proc Tile Core Thread |";
    else
        cout << " Core (SKT) |";

    switch (metricVersion)
    {
        case 2:
            if (m->isCoreCStateResidencySupported(0))
            {
                cout << setNextColor() << " UTIL |";
            }
            cout << setNextColor() << " IPC  |";
            if (m->isActiveRelativeFrequencyAvailable())
            {
                cout << setNextColor() << " CFREQ |";
            }
            break;
        default:
            cout << setNextColor() << " EXEC |" << setNextColor() << " IPC  |" << setNextColor() <<" FREQ  |";
            if (m->isActiveRelativeFrequencyAvailable())
                cout << setNextColor() << " AFREQ |";
    }
    if (m->isL3CacheMissesAvailable())
        cout << setNextColor() << " L3MISS |";
    if (m->isL2CacheMissesAvailable())
        cout << setNextColor() << " L2MISS |";
    if (m->isL3CacheHitRatioAvailable())
        cout << setNextColor() << " L3HIT |";
    if (m->isL2CacheHitRatioAvailable())
        cout << setNextColor() << " L2HIT |";
    if (m->isL3CacheMissesAvailable())
        cout << setNextColor() << " L3MPI |";
    if (m->isL2CacheMissesAvailable())
        cout << setNextColor() << " L2MPI | ";
    if (m->L3CacheOccupancyMetricAvailable())
        cout << setNextColor() << "  L3OCC |";
    if (m->CoreLocalMemoryBWMetricAvailable())
        cout << setNextColor() << "   LMB  |";
    if (m->CoreRemoteMemoryBWMetricAvailable())
        cout << setNextColor() << "   RMB  |";

    cout << setNextColor() << " TEMP\n\n";

    cout << resetColor();

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (m->isCoreOnline(i) == false || (show_partial_core_output && ycores.test(i) == false))
                continue;

            if (cpu_family_model == PCM::KNL)
                cout << setfill(' ') << internal << setw(5) << i
                << setw(5) << m->getTileId(i) << setw(5) << m->getCoreId(i)
                << setw(7) << m->getThreadId(i);
            else
                cout << " " << setw(3) << i << "   " << setw(2) << m->getSocketId(i);

            print_basic_metrics(m, cstates1[i], cstates2[i], metricVersion);
            print_other_metrics(m, cstates1[i], cstates2[i]);
            cout << resetColor();
        }
    }
    if (show_socket_output)
    {
        if (!(m->getNumSockets() == 1 && (m->isAtom() || cpu_family_model == PCM::KNL)))
        {
            cout << longDiv;
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                cout << " SKT   " << setw(2) << i;
                print_basic_metrics(m, sktstate1[i], sktstate2[i], metricVersion);
                print_other_metrics(m, sktstate1[i], sktstate2[i]);
                cout << resetColor();
            }
        }
    }
    cout << longDiv;

    if (show_system_output)
    {
        if (cpu_family_model == PCM::KNL)
            cout << setw(22) << left << " TOTAL" << internal << setw(7-5);
        else
            cout << " TOTAL  *";

        print_basic_metrics(m, sstate1, sstate2, metricVersion);

        if (m->L3CacheOccupancyMetricAvailable())
            cout << setNextColor() <<"     N/A ";
        if (m->CoreLocalMemoryBWMetricAvailable())
            cout << setNextColor() <<"    N/A ";
        if (m->CoreRemoteMemoryBWMetricAvailable())
            cout << setNextColor() <<"    N/A ";

        cout << setNextColor() << "     N/A\n";
        cout << resetColor();
        cout << setNextColor() << "\n Instructions retired: " << unit_format(getInstructionsRetired(sstate1, sstate2)) << " ;"
            << setNextColor() << " Active cycles: " << unit_format(getCycles(sstate1, sstate2)) << " ;"
            << setNextColor() << " Time (TSC): " << unit_format(getInvariantTSC(cstates1[0], cstates2[0])) << "ticks;";
        if (m->systemEnergyMetricAvailable() && systemEnergyStatusValid(sstate1) && systemEnergyStatusValid(sstate2))
        {
            cout << setNextColor() << " SYS energy: " << getSystemConsumedJoules(sstate1, sstate2) << " J;";
        }
        cout << "\n\n";

        cout << resetColor() << setNextColor() << " Core C-state residencies: "<< setNextColor() << "C0 (active,non-halted): " << (getCoreCStateResidency(0, sstate1, sstate2)*100.) << " %;";
        for (int s = 1; s <= PCM::MAX_C_STATE; ++s)
        {
            if (m->isCoreCStateResidencySupported(s))
            {
                std::cout << setNextColor() << " C" << s << ": " << (getCoreCStateResidency(s, sstate1, sstate2)*100.) << " %;";
            }
        }
        cout << "\n" ;
        cout << resetColor() << setNextColor() << " Package C-state residencies: ";
        std::vector<StackedBarItem> CoreCStateStackedBar, PackageCStateStackedBar;
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        {
            std::ostringstream sstr(std::ostringstream::out);
            sstr << std::hex << s;
            const char fill = sstr.str().c_str()[0];
            if (m->isCoreCStateResidencySupported(s))
            {
                CoreCStateStackedBar.push_back(StackedBarItem(getCoreCStateResidency(s, sstate1, sstate2), "", fill));
            }
            if (m->isPackageCStateResidencySupported(s))
            {
                std::cout << setNextColor() << " C" << s << ": " << (getPackageCStateResidency(s, sstate1, sstate2)*100.) << " %;";
                PackageCStateStackedBar.push_back(StackedBarItem(getPackageCStateResidency(s, sstate1, sstate2), "", fill));
            }
        }
        cout << "\n" << resetColor() << setColor(ASCII_BRIGHT_GREEN);

        drawStackedBar(" Core    C-state distribution", CoreCStateStackedBar, 80);
        cout << setColor(ASCII_GREEN);
        drawStackedBar(" Package C-state distribution", PackageCStateStackedBar, 80);

        cout << resetColor();

        if (m->getNumCores() == m->getNumOnlineCores() && false)
        {
            cout << "\n PHYSICAL CORE IPC                 : " << getCoreIPC(sstate1, sstate2) << " => corresponds to " << 100. * (getCoreIPC(sstate1, sstate2) / double(m->getMaxIPC())) << " % utilization for cores in active state";
            cout << "\n Instructions per nominal CPU cycle: " << getTotalExecUsage(sstate1, sstate2) << " => corresponds to " << 100. * (getTotalExecUsage(sstate1, sstate2) / double(m->getMaxIPC())) << " % core utilization over time interval\n";
        }
        if (m->isHWTMAL2Supported())
        {
            cout << setColor(ASCII_BRIGHT_MAGENTA);
            cout << " Pipeline stalls: " << setColor(ASCII_BRIGHT_CYAN) << "Frontend (fetch latency: " << int(100. * getFetchLatencyBound(sstate1, sstate2)) <<" %, fetch bandwidth: " << int(100. * getFetchBandwidthBound(sstate1, sstate2)) <<
                " %)\n                  " << setColor(ASCII_BRIGHT_RED) << "bad Speculation (branch misprediction: " << int(100. * getBranchMispredictionBound(sstate1, sstate2)) <<
                " %, machine clears: " << int(100. * getMachineClearsBound(sstate1, sstate2)) <<
                " %)\n                  " << setColor(ASCII_BRIGHT_YELLOW) << "Backend (buffer/cache/memory: " << int(100. * getMemoryBound(sstate1, sstate2)) <<
                " %, core: " << int(100. * getCoreBound(sstate1, sstate2)) <<
                " %)\n                  " << setColor(ASCII_BRIGHT_GREEN) << "Retiring (heavy operations: " << int(100. * getHeavyOperationsBound(sstate1, sstate2)) <<
                " %, light operations: " << int(100. * getLightOperationsBound(sstate1, sstate2)) << " %)\n";
        }
        else if (m->isHWTMAL1Supported())
        {
            cout << setColor(ASCII_BRIGHT_MAGENTA);
            cout << " Pipeline stalls: " << setColor(ASCII_BRIGHT_CYAN) << "Frontend bound: " << int(100. * getFrontendBound(sstate1, sstate2)) <<
                " %, " << setColor(ASCII_BRIGHT_RED) << "bad Speculation: " << int(100. * getBadSpeculation(sstate1, sstate2)) <<
                " %, " << setColor(ASCII_BRIGHT_YELLOW) << "Backend bound: " << int(100. * getBackendBound(sstate1, sstate2)) <<
                " %, " << setColor(ASCII_BRIGHT_GREEN) << "Retiring: " << int(100. * getRetiring(sstate1, sstate2)) << " %\n";
        }

        if (m->isHWTMAL1Supported())
        {
            cout << setColor(ASCII_BRIGHT_MAGENTA);
            std::vector<StackedBarItem> TMAStackedBar;
            TMAStackedBar.push_back(StackedBarItem(getFrontendBound(sstate1, sstate2), "", 'F'));
            TMAStackedBar.push_back(StackedBarItem(getBadSpeculation(sstate1, sstate2), "", 'S'));
            TMAStackedBar.push_back(StackedBarItem(getBackendBound(sstate1, sstate2), "", 'B'));
            TMAStackedBar.push_back(StackedBarItem(getRetiring(sstate1, sstate2), "", 'R'));
            drawStackedBar(" Pipeline stall distribution ", TMAStackedBar, 80);
            cout << resetColor() << "\n";
        }

#if 0
        cout << " SMI count: " << getSMICount(sstate1, sstate2) << "\n";
#endif
    }

    cout << setColor(ASCII_CYAN);

    if (show_socket_output)
    {
        if (m->getNumSockets() > 1 && m->incomingQPITrafficMetricsAvailable()) // QPI info only for multi socket systems
        {
            cout << "Intel(r) " << m->xPI() << " data traffic estimation in bytes (data traffic coming to CPU/socket through " << m->xPI() << " links):\n\n";

            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            cout << "              ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " " << m->xPI() << i << "    ";

            if (m->qpiUtilizationMetricsAvailable())
            {
                cout << "| ";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << " " << m->xPI() << i << "  ";
            }

            cout << "\n" << longDiv;


            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                cout << " SKT   " << setw(2) << i << "     ";
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << unit_format(getIncomingQPILinkBytes(i, l, sstate1, sstate2)) << "   ";

                if (m->qpiUtilizationMetricsAvailable())
                {
                    cout << "|  ";
                    for (uint32 l = 0; l < qpiLinks; ++l)
                        cout << setw(3) << std::dec << int(100. * getIncomingQPILinkUtilization(i, l, sstate1, sstate2)) << "%   ";
                }

                cout << "\n";
            }
        }
    }

    if (show_system_output)
    {
        cout << longDiv;

        if (m->getNumSockets() > 1 && m->incomingQPITrafficMetricsAvailable()) // QPI info only for multi socket systems
            cout << "Total " << m->xPI() << " incoming data traffic: " << unit_format(getAllIncomingQPILinkBytes(sstate1, sstate2)) << "     " << m->xPI() << " data traffic/Memory controller traffic: " << getQPItoMCTrafficRatio(sstate1, sstate2) << "\n";
    }

    cout << setColor(ASCII_BRIGHT_CYAN);

    if (show_socket_output)
    {
        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            cout << "\nIntel(r) " << m->xPI() << " traffic estimation in bytes (data and non-data traffic outgoing from CPU/socket through " << m->xPI() << " links):\n\n";

            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            cout << "              ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " " << m->xPI() << i << "    ";


            cout << "| ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " " << m->xPI() << i << "  ";

            cout << "\n" << longDiv;

            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                cout << " SKT   " << setw(2) << i << "     ";
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << unit_format(getOutgoingQPILinkBytes(i, l, sstate1, sstate2)) << "   ";

                cout << "|  ";
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << setw(3) << std::dec << int(100. * getOutgoingQPILinkUtilization(i, l, sstate1, sstate2)) << "%   ";

                cout << "\n";
            }

            cout << longDiv;
            cout << "Total " << m->xPI() << " outgoing data and non-data traffic: " << unit_format(getAllOutgoingQPILinkBytes(sstate1, sstate2)) << "\n";
        }
    }
    cout << resetColor();

    if (show_socket_output)
    {
        cout << "\nMEM (GB)->|";
        if (m->memoryTrafficMetricsAvailable())
            cout << setNextColor() << "  READ |  WRITE |";
        if (m->localMemoryRequestRatioMetricAvailable())
            cout << setNextColor() << " LOCAL |";
        if (m->PMMTrafficMetricsAvailable())
            cout << setNextColor() << " PMM RD | PMM WR |";
        if (m->HBMmemoryTrafficMetricsAvailable())
            cout << setNextColor() << " HBM READ | HBM WRITE |";
        if (m->memoryIOTrafficMetricAvailable())
            cout << setNextColor() << "   IO   |";
        if (m->memoryIOTrafficMetricAvailable())
            cout << setNextColor() << "   IA   |";
        if (m->memoryIOTrafficMetricAvailable())
            cout << setNextColor() << "   GT   |";
        if (m->packageEnergyMetricsAvailable())
            cout << setNextColor() << " CPU energy |";
        if (m->ppEnergyMetricsAvailable())
        {
            cout << setNextColor() << " PP0 energy |";
            cout << setNextColor() << " PP1 energy |";
        }
        if (m->dramEnergyMetricsAvailable())
            cout << setNextColor() << " DIMM energy |";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << setNextColor() << " LLCRDMISSLAT (ns)|";
        if (m->uncoreFrequencyMetricAvailable())
            cout << setNextColor() << " UncFREQ (Ghz)|";

        auto printCentered = [](const std::string& str, int width)
        {
            int len = str.length();
            if(width < len) {
                std::cout << str;
            } else {
                int diff = width - len;
                int pad1 = diff/2;
                int pad2 = diff - pad1;
                std::cout << std::string(pad1, ' ') << str << std::string(pad2, ' ');
            }
        };
        const std::vector<uint64> uncoreDieTypes{getUncoreDieTypes(sktstate2[0])};
        if (uncoreDieTypes.empty() == false)
        {
            cout << setNextColor() << " Unc(Ghz) ";
            for (auto & d: uncoreDieTypes)
            {
                cout << setNextColor();
                printCentered(UncoreCounterState::getDieTypeStr(d), 7);
                cout << " ";
            }
            std::cout << "|" ;
        }
        cout << resetColor() << "\n";

        cout << longDiv;
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
                cout << " SKT  " << setw(2) << i;
                if (m->memoryTrafficMetricsAvailable())
                    cout << setNextColor() << "    " << setw(5) << getBytesReadFromMC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "    " << setw(5) << getBytesWrittenToMC(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->localMemoryRequestRatioMetricAvailable())
                    cout << setNextColor() << "  " << setw(3) << int(100.* getLocalMemoryRequestRatio(sktstate1[i], sktstate2[i])) << " %";
                if (m->PMMTrafficMetricsAvailable())
                    cout << setNextColor() << "     " << setw(5) << getBytesReadFromPMM(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "     " << setw(5) << getBytesWrittenToPMM(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->HBMmemoryTrafficMetricsAvailable())
                    cout << setNextColor() << "   " << setw(11) << getBytesReadFromEDC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "    " << setw(11) << getBytesWrittenToEDC(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->memoryIOTrafficMetricAvailable()) {
                    cout << setNextColor() << "    " << setw(5) << getIORequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9);
                    cout << setNextColor() << "    " << setw(5) << getIARequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9);
                    cout << setNextColor() << "    " << setw(5) << getGTRequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9);
                }
                if(m->packageEnergyMetricsAvailable()) {
                    cout << setNextColor() << "     ";
                    cout << setw(6) << getConsumedJoules(sktstate1[i], sktstate2[i]);
                }
                if (m->ppEnergyMetricsAvailable()) {
                    cout << setNextColor() << "     ";
                    cout << setw(6) << getConsumedJoules(0, sktstate1[i], sktstate2[i]);
                    cout << setNextColor() << "     ";
                    cout << setw(6) << getConsumedJoules(1, sktstate1[i], sktstate2[i]);
                }
                if(m->dramEnergyMetricsAvailable()) {
                    cout << setNextColor() << "     ";
                    cout << setw(6) << getDRAMConsumedJoules(sktstate1[i], sktstate2[i]);
                }
                if (m->LLCReadMissLatencyMetricsAvailable()) {
                    cout << setNextColor() << "         ";
                    cout << setw(6) << getLLCReadMissLatency(sktstate1[i], sktstate2[i]);
                }
                if (m->uncoreFrequencyMetricAvailable()) {
                    cout << setNextColor() << "             ";
                    cout << setw(4) << getAverageUncoreFrequencyGhz(sktstate1[i], sktstate2[i]);
                }
                const std::vector<double> uncoreFrequencies{getUncoreFrequency(sktstate2[i])};
                assert(uncoreFrequencies.size() == uncoreDieTypes.size());

                if (uncoreFrequencies.empty() == false)
                {
                    cout << setNextColor() << "                ";
                    for (auto & d: uncoreFrequencies)
                    {
                        cout << setNextColor() << "  " << std::setw(4) << d/1e9 << "  ";
                    }
                }
                cout << resetColor() << "\n";
        }
        cout << longDiv;
        if (m->getNumSockets() > 1) {
            cout << "       *";
            if (m->memoryTrafficMetricsAvailable())
                cout << setNextColor() << "    " << setw(5) << getBytesReadFromMC(sstate1, sstate2) / double(1e9) <<
                        "    " << setw(5) << getBytesWrittenToMC(sstate1, sstate2) / double(1e9);
            if (m->localMemoryRequestRatioMetricAvailable())
                cout << setNextColor() << "  " << setw(3) << int(100.* getLocalMemoryRequestRatio(sstate1, sstate2)) << " %";
            if (m->PMMTrafficMetricsAvailable())
                cout << setNextColor() << "     " << setw(5) << getBytesReadFromPMM(sstate1, sstate2) / double(1e9) <<
                        "     " << setw(5) << getBytesWrittenToPMM(sstate1, sstate2) / double(1e9);
            if (m->memoryIOTrafficMetricAvailable())
                cout << setNextColor() << "    " << setw(5) << getIORequestBytesFromMC(sstate1, sstate2) / double(1e9);
            if (m->packageEnergyMetricsAvailable()) {
                cout << setNextColor() << "     ";
                cout << setw(6) << getConsumedJoules(sstate1, sstate2);
            }
            if (m->ppEnergyMetricsAvailable()) {
                cout << setNextColor() << "     ";
                cout << setw(6) << getConsumedJoules(0, sstate1, sstate2);
                cout << setNextColor() << "     ";
                cout << setw(6) << getConsumedJoules(1, sstate1, sstate2);
            }
            if (m->dramEnergyMetricsAvailable()) {
                cout << setNextColor() << "     ";
                cout << setw(6) << getDRAMConsumedJoules(sstate1, sstate2);
            }
            if (m->LLCReadMissLatencyMetricsAvailable()) {
                cout << setNextColor() << "         ";
                cout << setw(6) << getLLCReadMissLatency(sstate1, sstate2);
            }
            if (m->uncoreFrequencyMetricAvailable()) {
                cout << setNextColor() << "             ";
                cout << setw(4) << getAverageUncoreFrequencyGhz(sstate1, sstate2);
            }
            cout << resetColor() << "\n";
        }
    }

}


void print_basic_metrics_csv_header(const PCM * m)
{
    cout << "EXEC,IPC,FREQ,";
    if (m->isActiveRelativeFrequencyAvailable())
        cout << "AFREQ,CFREQ,";
    if (m->isL3CacheMissesAvailable())
        cout << "L3MISS,";
    if (m->isL2CacheMissesAvailable())
        cout << "L2MISS,";
    if (m->isL3CacheHitRatioAvailable())
        cout << "L3HIT,";
    if (m->isL2CacheHitRatioAvailable())
        cout << "L2HIT,";
    if (m->isL3CacheMissesAvailable())
        cout << "L3MPI,";
    if (m->isL2CacheMissesAvailable())
        cout << "L2MPI,";
    if (m->isHWTMAL1Supported())
        cout << "Frontend_bound(%),Bad_Speculation(%),Backend_Bound(%),Retiring(%),";
    if (m->isHWTMAL2Supported())
    {
        cout << "Fetch_latency_bound(%),Fetch_bandwidth_bound(%),Branch_misprediction_bound(%),Machine_clears_bound(%),"
             << "Buffer_Cache_Memory_bound(%),Core_bound(%),Heavy_operations_bound(%),Light_operations_bound(%),";
    }
}

void print_csv_header_helper(const string & header, int count=1){
  for(int i = 0; i < count; i++){
    cout << header << ",";
  }
}

void print_basic_metrics_csv_semicolons(const PCM * m, const string & header)
{
    print_csv_header_helper(header, 3);    // EXEC;IPC;FREQ;
    if (m->isActiveRelativeFrequencyAvailable())
        print_csv_header_helper(header, 2);  // AFREQ;CFREQ;
    if (m->isL3CacheMissesAvailable())
        print_csv_header_helper(header);  // L3MISS;
    if (m->isL2CacheMissesAvailable())
        print_csv_header_helper(header);  // L2MISS;
    if (m->isL3CacheHitRatioAvailable())
        print_csv_header_helper(header);  // L3HIT
    if (m->isL2CacheHitRatioAvailable())
        print_csv_header_helper(header);  // L2HIT;
    if (m->isL3CacheMissesAvailable())
        print_csv_header_helper(header);  // L3MPI;
    if (m->isL2CacheMissesAvailable())
        print_csv_header_helper(header);  // L2MPI;
    if (m->isHWTMAL1Supported())
        print_csv_header_helper(header, 4); // Frontend_bound(%),Bad_Speculation(%),Backend_Bound(%),Retiring(%)
    if (m->isHWTMAL2Supported())
        print_csv_header_helper(header, 8);
}

void print_csv_header(PCM * m,
    const std::bitset<MAX_CORES> & ycores,
    const bool show_core_output,
    const bool show_partial_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
    // print first header line
    string header;
    header = "System";
    print_csv_header_helper(header,2);
    if (show_system_output)
    {
        print_basic_metrics_csv_semicolons(m,header);

        if (m->memoryTrafficMetricsAvailable())
            print_csv_header_helper(header,2);

        if (m->localMemoryRequestRatioMetricAvailable())
            print_csv_header_helper(header);

        if (m->PMMTrafficMetricsAvailable())
            print_csv_header_helper(header,2);

        if (m->HBMmemoryTrafficMetricsAvailable())
            print_csv_header_helper(header,2);

        print_csv_header_helper(header,7);
        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
                print_csv_header_helper(header,2);
            if (m->outgoingQPITrafficMetricsAvailable())
                print_csv_header_helper(header);
        }

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                print_csv_header_helper("System Core C-States");
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                print_csv_header_helper("System Pack C-States");
        if (m->packageEnergyMetricsAvailable())
            print_csv_header_helper(header);
        if (m->ppEnergyMetricsAvailable())
            print_csv_header_helper(header, 2);
        if (m->dramEnergyMetricsAvailable())
            print_csv_header_helper(header);
        if (m->systemEnergyMetricAvailable())
            print_csv_header_helper(header);
        if (m->LLCReadMissLatencyMetricsAvailable())
            print_csv_header_helper(header);
        if (m->uncoreFrequencyMetricAvailable())
            print_csv_header_helper(header);
    }

    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            header = "Socket " + std::to_string(i);
            print_basic_metrics_csv_semicolons(m,header);
            if (m->L3CacheOccupancyMetricAvailable())
                print_csv_header_helper(header);
            if (m->CoreLocalMemoryBWMetricAvailable())
                print_csv_header_helper(header);
            if (m->CoreRemoteMemoryBWMetricAvailable())
                print_csv_header_helper(header);
            if (m->memoryTrafficMetricsAvailable())
                print_csv_header_helper(header,2);
            if (m->localMemoryRequestRatioMetricAvailable())
                print_csv_header_helper(header);
            if (m->PMMTrafficMetricsAvailable())
                print_csv_header_helper(header,2);
            if (m->HBMmemoryTrafficMetricsAvailable())
                print_csv_header_helper(header,2);
            if (m->memoryIOTrafficMetricAvailable())
                print_csv_header_helper(header,3);
            print_csv_header_helper(header, 8); //TEMP,INST,ACYC,TIME(ticks),PhysIPC,PhysIPC%,INSTnom,INSTnom%,
        }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                header = "SKT" + std::to_string(s) + "dataIn";
                print_csv_header_helper(header,qpiLinks);
                if (m->qpiUtilizationMetricsAvailable())
                {
                    header = "SKT" + std::to_string(s) + "dataIn (percent)";
                    print_csv_header_helper(header,qpiLinks);
                }
            }
        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                header = "SKT" + std::to_string(s) + "trafficOut";
                print_csv_header_helper(header,qpiLinks);
                header = "SKT" + std::to_string(s) + "trafficOut (percent)";
                print_csv_header_helper(header,qpiLinks);
            }
        }


        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            header = "SKT" + std::to_string(i) + " Core C-State";
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                print_csv_header_helper(header);
            header = "SKT" + std::to_string(i) + " Package C-State";
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                print_csv_header_helper(header);
        }

        if (m->packageEnergyMetricsAvailable())
        {
            header = "Proc Energy (Joules)";
            print_csv_header_helper(header,m->getNumSockets());
        }
        if (m->ppEnergyMetricsAvailable())
        {
            header = "Power Plane 0 Energy (Joules)";
            print_csv_header_helper(header, m->getNumSockets());
            header = "Power Plane 1 Energy (Joules)";
            print_csv_header_helper(header, m->getNumSockets());
        }
        if (m->dramEnergyMetricsAvailable())
        {
            header = "DRAM Energy (Joules)";
            print_csv_header_helper(header,m->getNumSockets());
        }
        if (m->LLCReadMissLatencyMetricsAvailable())
        {
            header = "LLCRDMISSLAT (ns)";
            print_csv_header_helper(header,m->getNumSockets());
        }
        if (m->uncoreFrequencyMetricAvailable())
        {
            header = "UncFREQ (Ghz)";
            print_csv_header_helper(header, m->getNumSockets());
        }
        for (uint32 s = 0; s < m->getNumSockets(); ++s)
        {
            for (size_t die = 0; die < m->getNumUFSDies(); ++die)
            {
                header = "UncFREQ Die " + std::to_string(die) + " (Ghz)";
                print_csv_header_helper(header);
            }
        }
    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (show_partial_core_output && ycores.test(i) == false)
                continue;

            std::stringstream hstream;
            hstream << "Core" << i << " (Socket" << setw(2) << m->getSocketId(i) << ")";
            header = hstream.str();
            print_basic_metrics_csv_semicolons(m,header);
            if (m->L3CacheOccupancyMetricAvailable())
                print_csv_header_helper(header);
            if (m->CoreLocalMemoryBWMetricAvailable())
                print_csv_header_helper(header);
            if (m->CoreRemoteMemoryBWMetricAvailable())
                print_csv_header_helper(header);

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
                if (m->isCoreCStateResidencySupported(s))
                    print_csv_header_helper(header);
            print_csv_header_helper(header);// TEMP
            print_csv_header_helper(header,7); //ACYC,TIME(ticks),PhysIPC,PhysIPC%,INSTnom,INSTnom%,
        }
    }

    // print second header line
    cout << "\n";
    printDateForCSV(Header2);
    if (show_system_output)
    {
        print_basic_metrics_csv_header(m);

        if (m->memoryTrafficMetricsAvailable())
                cout << "READ,WRITE,";

        if (m->localMemoryRequestRatioMetricAvailable())
            cout << "LOCAL,";

        if (m->PMMTrafficMetricsAvailable())
            cout << "PMM_RD,PMM_WR,";

        if (m->HBMmemoryTrafficMetricsAvailable())
                cout << "HBM_READ,HBM_WRITE,";

        cout << "INST,ACYC,TIME(ticks),PhysIPC,PhysIPC%,INSTnom,INSTnom%,";
        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
                cout << "Total" << m->xPI() << "in," << m->xPI() << "toMC,";
            if (m->outgoingQPITrafficMetricsAvailable())
                cout << "Total" << m->xPI() << "out,";
        }

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            cout << "C" << s << "res%,";

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            cout << "C" << s << "res%,";

        if (m->packageEnergyMetricsAvailable())
            cout << "Proc Energy (Joules),";
        if (m->ppEnergyMetricsAvailable())
        {
            cout << "Power Plane 0 Energy (Joules),";
            cout << "Power Plane 1 Energy (Joules),";
        }
        if (m->dramEnergyMetricsAvailable())
            cout << "DRAM Energy (Joules),";
        if (m->systemEnergyMetricAvailable())
            cout << "SYSTEM Energy (Joules),";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << "LLCRDMISSLAT (ns),";
        if (m->uncoreFrequencyMetricAvailable())
            cout << "UncFREQ (Ghz),";
    }


    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
             print_basic_metrics_csv_header(m);
             if (m->L3CacheOccupancyMetricAvailable())
                 cout << "L3OCC,";
             if (m->CoreLocalMemoryBWMetricAvailable())
                 cout << "LMB,";
             if (m->CoreRemoteMemoryBWMetricAvailable())
                 cout << "RMB,";
             if (m->memoryTrafficMetricsAvailable())
                 cout << "READ,WRITE,";
             if (m->localMemoryRequestRatioMetricAvailable())
                 cout << "LOCAL,";
             if (m->PMMTrafficMetricsAvailable())
                 cout << "PMM_RD,PMM_WR,";
             if (m->HBMmemoryTrafficMetricsAvailable())
                 cout << "HBM_READ,HBM_WRITE,";
             if (m->memoryIOTrafficMetricAvailable())
                 cout << "IO,IA,GT,";
             cout << "TEMP,INST,ACYC,TIME(ticks),PhysIPC,PhysIPC%,INSTnom,INSTnom%,";
        }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << m->xPI() << i << ",";

                if (m->qpiUtilizationMetricsAvailable())
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << m->xPI() << i << ",";
            }
        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << m->xPI() << i << ",";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << m->xPI() << i << ",";
            }
        }

        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << "C" << s << "res%,";

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                cout << "C" << s << "res%,";
        }

        auto printSKT = [] (const uint32 i, const uint32 count = 1)
        {
            for (uint32 j = 0; j < count; ++j)
            {
                cout << "SKT" << i << ",";
            }
        };
        if (m->packageEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                printSKT(i);
        }
        if (m->ppEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                printSKT(i, 2);
        }
        if (m->dramEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                printSKT(i);
        }
        if (m->LLCReadMissLatencyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                printSKT(i);
        }
        if (m->uncoreFrequencyMetricAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                printSKT(i);
        }
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            printSKT(i, m->getNumUFSDies());
        }
    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (show_partial_core_output && ycores.test(i) == false)
                continue;

            print_basic_metrics_csv_header(m);
            if (m->L3CacheOccupancyMetricAvailable())
                cout << "L3OCC,";
            if (m->CoreLocalMemoryBWMetricAvailable())
                cout << "LMB,";
            if (m->CoreRemoteMemoryBWMetricAvailable())
                cout << "RMB,";

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
                if (m->isCoreCStateResidencySupported(s))
                    cout << "C" << s << "res%,";

            cout << "TEMP,";
            cout << "INST,ACYC,TIME(ticks),PhysIPC,PhysIPC%,INSTnom,INSTnom%,";
        }
    }
}

template <class State>
void print_basic_metrics_csv(const PCM * m, const State & state1, const State & state2, const bool print_last_semicolon = true)
{
    cout << getExecUsage(state1, state2) <<
        ',' << getIPC(state1, state2) <<
        ',' << getRelativeFrequency(state1, state2);

    if (m->isActiveRelativeFrequencyAvailable())
        cout << ',' << getActiveRelativeFrequency(state1, state2) << ',' << getActiveAverageFrequency(state1, state2)/1e9;
    if (m->isL3CacheMissesAvailable())
        cout << ',' << float_format(getL3CacheMisses(state1, state2));
    if (m->isL2CacheMissesAvailable())
        cout << ',' << float_format(getL2CacheMisses(state1, state2));
    if (m->isL3CacheHitRatioAvailable())
        cout << ',' << getL3CacheHitRatio(state1, state2);
    if (m->isL2CacheHitRatioAvailable())
        cout << ',' << getL2CacheHitRatio(state1, state2);
    cout.precision(4);
    if (m->isL3CacheMissesAvailable())
        cout << ',' << double(getL3CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    if (m->isL2CacheMissesAvailable())
        cout << ',' << double(getL2CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    cout.precision(2);
    if (m->isHWTMAL1Supported())
    {
        cout << ',' << int(100. * getFrontendBound(state1, state2));
        cout << ',' << int(100. * getBadSpeculation(state1, state2));
        cout << ',' << int(100. * getBackendBound(state1, state2));
        cout << ',' << int(100. * getRetiring(state1, state2));
    }
    if (m->isHWTMAL2Supported())
    {
        cout << ',' << int(100. * getFetchLatencyBound(state1, state2));
        cout << ',' << int(100. * getFetchBandwidthBound(state1, state2));
        cout << ',' << int(100. * getBranchMispredictionBound(state1, state2));
        cout << ',' << int(100. * getMachineClearsBound(state1, state2));
        cout << ',' << int(100. * getMemoryBound(state1, state2));
        cout << ',' << int(100. * getCoreBound(state1, state2));
        cout << ',' << int(100. * getHeavyOperationsBound(state1, state2));
        cout << ',' << int(100. * getLightOperationsBound(state1, state2));
    }
    if (print_last_semicolon)
        cout << ",";
}

template <class State>
void print_other_metrics_csv(const PCM * m, const State & state1, const State & state2)
{
    if (m->L3CacheOccupancyMetricAvailable())
        cout << ',' << l3cache_occ_format(getL3CacheOccupancy(state2));
    if (m->CoreLocalMemoryBWMetricAvailable())
        cout << ',' << getLocalMemoryBW(state1, state2);
    if (m->CoreRemoteMemoryBWMetricAvailable())
        cout << ',' << getRemoteMemoryBW(state1, state2);
}

void print_csv(PCM * m,
    const std::vector<CoreCounterState> & cstates1,
    const std::vector<CoreCounterState> & cstates2,
    const std::vector<SocketCounterState> & sktstate1,
    const std::vector<SocketCounterState> & sktstate2,
    const std::bitset<MAX_CORES> & ycores,
    const SystemCounterState& sstate1,
    const SystemCounterState& sstate2,
    const bool show_core_output,
    const bool show_partial_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
    cout << "\n";
    printDateForCSV(CsvOutputType::Data);

    if (show_system_output)
    {
        print_basic_metrics_csv(m, sstate1, sstate2);

        if (m->memoryTrafficMetricsAvailable())
                cout << getBytesReadFromMC(sstate1, sstate2) / double(1e9) <<
                ',' << getBytesWrittenToMC(sstate1, sstate2) / double(1e9) << ',';

        if (m->localMemoryRequestRatioMetricAvailable())
            cout << int(100. * getLocalMemoryRequestRatio(sstate1, sstate2)) << ',';

        if (m->PMMTrafficMetricsAvailable())
            cout << getBytesReadFromPMM(sstate1, sstate2) / double(1e9) <<
            ',' << getBytesWrittenToPMM(sstate1, sstate2) / double(1e9) << ',';

        if (m->HBMmemoryTrafficMetricsAvailable())
                cout << getBytesReadFromEDC(sstate1, sstate2) / double(1e9) <<
                ',' << getBytesWrittenToEDC(sstate1, sstate2) / double(1e9) << ',';

        cout << float_format(getInstructionsRetired(sstate1, sstate2)) << ","
            << float_format(getCycles(sstate1, sstate2)) << ","
            << float_format(getInvariantTSC(cstates1[0], cstates2[0])) << ","
            << getCoreIPC(sstate1, sstate2) << ","
            << 100. * (getCoreIPC(sstate1, sstate2) / double(m->getMaxIPC())) << ","
            << getTotalExecUsage(sstate1, sstate2) << ","
            << 100. * (getTotalExecUsage(sstate1, sstate2) / double(m->getMaxIPC())) << ",";

        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
               cout << float_format(getAllIncomingQPILinkBytes(sstate1, sstate2)) << ","
                    << getQPItoMCTrafficRatio(sstate1, sstate2) << ",";
            if (m->outgoingQPITrafficMetricsAvailable())
               cout << float_format(getAllOutgoingQPILinkBytes(sstate1, sstate2)) << ",";
        }

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            cout << getCoreCStateResidency(s, sstate1, sstate2) * 100 << ",";

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            cout << getPackageCStateResidency(s, sstate1, sstate2) * 100 << ",";

        if (m->packageEnergyMetricsAvailable())
            cout << getConsumedJoules(sstate1, sstate2) << ",";
        if (m->ppEnergyMetricsAvailable())
            cout << getConsumedJoules(0, sstate1, sstate2) << "," << getConsumedJoules(1, sstate1, sstate2) << ",";
        if (m->dramEnergyMetricsAvailable())
            cout << getDRAMConsumedJoules(sstate1, sstate2) << ",";
        if (m->systemEnergyMetricAvailable())
            cout << getSystemConsumedJoules(sstate1, sstate2) << ",";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << getLLCReadMissLatency(sstate1, sstate2) << ",";
        if (m->uncoreFrequencyMetricAvailable())
            cout << getAverageUncoreFrequencyGhz(sstate1, sstate2) << ",";
    }

    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            print_basic_metrics_csv(m, sktstate1[i], sktstate2[i], false);
            print_other_metrics_csv(m, sktstate1[i], sktstate2[i]);
            if (m->memoryTrafficMetricsAvailable())
                cout << ',' << getBytesReadFromMC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                    ',' << getBytesWrittenToMC(sktstate1[i], sktstate2[i]) / double(1e9);
            if (m->localMemoryRequestRatioMetricAvailable())
                cout << ',' << int(100. * getLocalMemoryRequestRatio(sktstate1[i], sktstate2[i]));
            if (m->PMMTrafficMetricsAvailable())
                cout << ',' << getBytesReadFromPMM(sktstate1[i], sktstate2[i]) / double(1e9) <<
                ',' << getBytesWrittenToPMM(sktstate1[i], sktstate2[i]) / double(1e9);
            if (m->HBMmemoryTrafficMetricsAvailable())
                cout << ',' << getBytesReadFromEDC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                ',' << getBytesWrittenToEDC(sktstate1[i], sktstate2[i]) / double(1e9);
            if (m->memoryIOTrafficMetricAvailable()) {
                cout << ',' << getIORequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9)
                     << ',' << getIARequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9)
                     << ',' << getGTRequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9);
            }
            cout << ',' << temp_format(sktstate2[i].getThermalHeadroom()) << ',';

            cout << float_format(getInstructionsRetired(sktstate1[i], sktstate2[i])) << ","
                << float_format(getCycles(sktstate1[i], sktstate2[i])) << ","
                << float_format(getInvariantTSC(cstates1[0], cstates2[0])) << ","
                << getCoreIPC(sktstate1[i], sktstate2[i]) << ","
                << 100. * (getCoreIPC(sktstate1[i], sktstate2[i]) / double(m->getMaxIPC())) << ","
                << getTotalExecUsage(sktstate1[i], sktstate2[i]) << ","
                << 100. * (getTotalExecUsage(sktstate1[i], sktstate2[i]) / double(m->getMaxIPC())) << ",";

        }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << float_format(getIncomingQPILinkBytes(i, l, sstate1, sstate2)) << ",";

                if (m->qpiUtilizationMetricsAvailable())
                {
                    for (uint32 l = 0; l < qpiLinks; ++l)
                        cout << setw(3) << std::dec << int(100. * getIncomingQPILinkUtilization(i, l, sstate1, sstate2)) << "%,";
                }
            }
        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << float_format(getOutgoingQPILinkBytes(i, l, sstate1, sstate2)) << ",";

                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << setw(3) << std::dec << int(100. * getOutgoingQPILinkUtilization(i, l, sstate1, sstate2)) << "%,";
            }
        }

        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
                if (m->isCoreCStateResidencySupported(s))
                    cout << getCoreCStateResidency(s, sktstate1[i], sktstate2[i]) * 100 << ",";

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
                if (m->isPackageCStateResidencySupported(s))
                    cout << getPackageCStateResidency(s, sktstate1[i], sktstate2[i]) * 100 << ",";
        }

        if (m->packageEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getConsumedJoules(sktstate1[i], sktstate2[i]) << ",";
        }
        if (m->ppEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getConsumedJoules(0, sktstate1[i], sktstate2[i]) << "," << getConsumedJoules(1, sktstate1[i], sktstate2[i]) << ",";
        }
        if (m->dramEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getDRAMConsumedJoules(sktstate1[i], sktstate2[i]) << " ,";
        }
        if (m->LLCReadMissLatencyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getLLCReadMissLatency(sktstate1[i], sktstate2[i]) << " ,";
        }
        if (m->uncoreFrequencyMetricAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getAverageUncoreFrequencyGhz(sktstate1[i], sktstate2[i]) << ",";
        }
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            const auto freqs = getUncoreFrequency(sktstate2[i]);
            assert(freqs.size() == (size_t)m->getNumUFSDies());
            for (auto & f : freqs)
            {
                cout << f/1e9 << ",";
            }
        }
    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (show_partial_core_output && ycores.test(i) == false)
                continue;

            print_basic_metrics_csv(m, cstates1[i], cstates2[i], false);
            print_other_metrics_csv(m, cstates1[i], cstates2[i]);
            cout << ',';

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
                if (m->isCoreCStateResidencySupported(s))
                    cout << getCoreCStateResidency(s, cstates1[i], cstates2[i]) * 100 << ",";

            cout << temp_format(cstates2[i].getThermalHeadroom()) << ',';

            cout << float_format(getInstructionsRetired(cstates1[i], cstates2[i])) << ","
                << float_format(getCycles(cstates1[i], cstates2[i])) << ","
                << float_format(getInvariantTSC(cstates1[0], cstates2[0])) << ","
                << getCoreIPC(cstates1[i], cstates2[i]) << ","
                << 100. * (getCoreIPC(cstates1[i], cstates2[i]) / double(m->getMaxIPC())) << ","
                << getTotalExecUsage(cstates1[i], cstates2[i]) << ","
                << 100. * (getTotalExecUsage(cstates1[i], cstates2[i]) / double(m->getMaxIPC())) << ",";
        }
    }
}

#ifndef UNIT_TEST

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream2;
#ifdef PCM_FORCE_SILENT
    null_stream nullStream1;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#else
    check_and_set_silent(argc, argv, nullStream2);
#endif

    set_signal_handlers();

    cerr << "\n";
    cerr << " Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";
    cerr << "\n";
    
    cerr << "\n";

    // if delay is not specified: use either default (1 second),
    // or only read counters before or after PCM started: keep PCM blocked
    double delay = -1.0;
    int pid{-1};
    char *sysCmd = NULL;
    char **sysArgv = NULL;
    bool show_core_output = true;
    bool show_partial_core_output = false;
    bool show_socket_output = true;
    bool show_system_output = true;
    bool csv_output = false;
    bool reset_pmu = false;
    bool disable_JKT_workaround = false; // as per http://software.intel.com/en-us/articles/performance-impact-when-sampling-certain-llc-events-on-snb-ep-with-vtune
    bool enforceFlush = false;
    int metricVersion = 2;

    parsePID(argc, argv, pid);

    MainLoop mainLoop;
    std::bitset<MAX_CORES> ycores;
    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    if (argc > 1) do
    {
        argv++;
        argc--;
        std::string arg_value;

        if (*argv == nullptr)
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
        {
            print_help(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            // handled in check_and_set_silent
            continue;
        }
        else if (check_argument_equals(*argv, {"--yescores", "-yc", "/yc"}))
        {
            argv++;
            argc--;
            show_partial_core_output = true;
            if(*argv == NULL)
            {
                cerr << "Error: --yescores requires additional argument.\n";
                exit(EXIT_FAILURE);
            }
            std::stringstream ss(*argv);
            while(ss.good())
            {
                string s;
                int core_id;
                std::getline(ss, s, ',');
                if(s.empty())
                    continue;
                core_id = atoi(s.c_str());
                if(core_id > MAX_CORES)
                {
                    cerr << "Core ID:" << core_id << " exceed maximum range " << MAX_CORES << ", program abort\n";
                    exit(EXIT_FAILURE);
                }

                ycores.set(core_id, true);
            }
            if(m->getNumCores() > MAX_CORES)
            {
                cerr << "Error: --yescores option is enabled, but #define MAX_CORES " << MAX_CORES << " is less than  m->getNumCores() = " << m->getNumCores() << "\n";
                cerr << "There is a potential to crash the system. Please increase MAX_CORES to at least " << m->getNumCores() << " and re-enable this option.\n";
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (check_argument_equals(*argv, {"--nocores", "-nc", "/nc"}))
        {
            show_core_output = false;
            continue;
        }
        else if (check_argument_equals(*argv, {"--nosockets", "-ns", "/ns"}))
        {
            show_socket_output = false;
            continue;
        }
        else if (check_argument_equals(*argv, {"--nosystem", "-nsys", "/nsys"}))
        {
            show_system_output = false;
            continue;
        }
        else if (check_argument_equals(*argv, {"--color"}))
        {
            setColorEnabled();
            continue;
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"}))
        {
            csv_output = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
        {
            csv_output = true;
            if (!arg_value.empty()) {
                m->setOutput(arg_value);
            }
            continue;
        }
        else if (extract_argument_value(*argv, {"-m", "/m"}, arg_value))
        {
            if (!arg_value.empty()) {
                metricVersion = atoi(arg_value.c_str());
            }
            if (metricVersion == 0)
            {
                metricVersion = 2;
            }
            continue;
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
        else if (check_argument_equals(*argv, {"-reset", "/reset", "-r"}))
        {
            reset_pmu = true;
            continue;
        }
        else if (CheckAndForceRTMAbortMode(*argv, m))
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"--noJKTWA"}))
        {
            disable_JKT_workaround = true;
            continue;
        }
        PCM_ENFORCE_FLUSH_OPTION
#ifdef _MSC_VER
        else if (check_argument_equals(*argv, {"--uninstallDriver"}))
        {
            Driver tmpDrvObject;
            tmpDrvObject.uninstall();
            cerr << "msr.sys driver has been uninstalled. You might need to reboot the system to make this effective.\n";
            exit(EXIT_SUCCESS);
        }
        else if (check_argument_equals(*argv, {"--installDriver"}))
        {
            Driver tmpDrvObject = Driver(Driver::msrLocalPath());
            if (!tmpDrvObject.start())
            {
                tcerr << "Can not access CPU counters\n";
                tcerr << "You must have a signed  driver at " << tmpDrvObject.driverPath() << " and have administrator rights to run this program\n";
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
#endif
        else if (check_argument_equals(*argv, {"--"}))
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            delay = parse_delay(*argv, program, (print_usage_func)print_help);
            continue;
        }
    } while (argc > 1); // end of command line partsing loop

    if (disable_JKT_workaround) m->disableJKTWorkaround();

    if (reset_pmu)
    {
        cerr << "\n Resetting PMU configuration\n";
        m->resetPMU();
    }

    // program() creates common semaphore for the singleton, so ideally to be called before any other references to PCM
    const PCM::ErrorCode status = m->program(PCM::DEFAULT_EVENTS, nullptr, false, pid);

    switch (status)
    {
    case PCM::Success:
        break;
    case PCM::MSRAccessDenied:
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access).\n";
        exit(EXIT_FAILURE);
    case PCM::PMUBusy:
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
        cerr << "Alternatively you can try running PCM with option -r to reset PMU.\n";
        exit(EXIT_FAILURE);
    default:
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error).\n";
        exit(EXIT_FAILURE);
    }

    print_cpu_details();

    std::vector<CoreCounterState> cstates1, cstates2;
    std::vector<SocketCounterState> sktstate1, sktstate2;
    SystemCounterState sstate1, sstate2;
    const auto cpu_family_model = m->getCPUFamilyModel();

    print_pid_collection_message(pid);

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    }
    else {
        m->setBlocked(false);
    }
    // in case delay is not provided in command line => set default
    if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;
    // cerr << "DEBUG: Delay: " << delay << " seconds. Blocked: " << m->isBlocked() << "\n";

    if (csv_output) {
        print_csv_header(m, ycores, show_core_output, show_partial_core_output, show_socket_output, show_system_output);
    }

    m->getAllCounterStates(sstate1, sktstate1, cstates1);

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
        if (enforceFlush || !csv_output) cout << std::flush;

        calibratedSleep(delay, sysCmd, mainLoop, m);

        m->getAllCounterStates(sstate2, sktstate2, cstates2);

        if (csv_output)
            print_csv(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
                show_core_output, show_partial_core_output, show_socket_output, show_system_output);
        else
            print_output(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
                cpu_family_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output,
                metricVersion);

        std::swap(sstate1, sstate2);
        std::swap(sktstate1, sktstate2);
        std::swap(cstates1, cstates2);

        if (m->isBlocked()) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    exit(EXIT_SUCCESS);
}

#endif // UNIT_TEST