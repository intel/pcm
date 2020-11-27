/*
Copyright (c) 2009-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
* Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev,
//            Thomas Willhalm,
//            Patrick Ungerer


/*!     \file pcm.cpp
\brief Example of using CPU counters: implements a simple performance counter monitoring utility
*/
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
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

    snprintf(buffer, 1024, "%6d", (uint32) o);
    return buffer;
}

void print_help(const string prog_name)
{
    cerr << "\n Usage: \n " << prog_name
        << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cerr << "   <delay>                           => time interval to sample performance counters.\n";
    cerr << "                                        If not specified, or 0, with external program given\n";
    cerr << "                                        will read counters only after external program finishes\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help      | /h           => print this help and exit\n";
#ifdef _MSC_VER
    cerr << "  --uninstallDriver   | --installDriver=> (un)install driver\n";
#endif
    cerr << "  -r    | --reset     | /reset       => reset PMU configuration (at your own risk)\n";
    cerr << "  -nc   | --nocores   | /nc          => hide core related output\n";
    cerr << "  -yc   | --yescores  | /yc          => enable specific cores to output\n";
    cerr << "  -ns   | --nosockets | /ns          => hide socket related output\n";
    cerr << "  -nsys | --nosystem  | /nsys        => hide system related output\n";
    cerr << "  -m    | --multiple-instances | /m  => allow multiple PCM instances running in parallel\n";
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
        << "                                        to a file, in case filename is provided\n"
        << "                                        the format used is documented here: https://software.intel.com/content/www/us/en/develop/blogs/intel-pcm-column-names-decoder-ring.html\n";
    cerr << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    print_help_force_rtm_abort_mode(37);
    cerr << " Examples:\n";
    cerr << "  " << prog_name << " 1 -nc -ns          => print counters every second without core and socket output\n";
    cerr << "  " << prog_name << " 1 -i=10            => print counters every second 10 times and exit\n";
    cerr << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << prog_name << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
}


template <class State>
void print_basic_metrics(const PCM * m, const State & state1, const State & state2)
{
    cout << "     " << getExecUsage(state1, state2) <<
        "   " << getIPC(state1, state2) <<
        "   " << getRelativeFrequency(state1, state2);
    if (m->isActiveRelativeFrequencyAvailable())
        cout << "    " << getActiveRelativeFrequency(state1, state2);
    if (m->isL3CacheMissesAvailable())
        cout << "    " << unit_format(getL3CacheMisses(state1, state2));
    if (m->isL2CacheMissesAvailable())
        cout << "   " << unit_format(getL2CacheMisses(state1, state2));
    if (m->isL3CacheHitRatioAvailable())
        cout << "    " << getL3CacheHitRatio(state1, state2);
    if (m->isL2CacheHitRatioAvailable())
        cout << "    " << getL2CacheHitRatio(state1, state2);
    if (m->isL3CacheMissesAvailable())
        cout << "    " << double(getL3CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    if (m->isL2CacheMissesAvailable())
        cout << "    " << double(getL2CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
}

template <class State>
void print_other_metrics(const PCM * m, const State & state1, const State & state2)
{
    if (m->L3CacheOccupancyMetricAvailable())
        cout << "   " << setw(6) << l3cache_occ_format(getL3CacheOccupancy(state2));
    if (m->CoreLocalMemoryBWMetricAvailable())
        cout << "   " << setw(6) << getLocalMemoryBW(state1, state2);
    if (m->CoreRemoteMemoryBWMetricAvailable())
        cout << "   " << setw(6) << getRemoteMemoryBW(state1, state2);
    cout << "     " << temp_format(state2.getThermalHeadroom()) << "\n";
}

void print_output(PCM * m,
    const std::vector<CoreCounterState> & cstates1,
    const std::vector<CoreCounterState> & cstates2,
    const std::vector<SocketCounterState> & sktstate1,
    const std::vector<SocketCounterState> & sktstate2,
    const std::bitset<MAX_CORES> & ycores,
    const SystemCounterState& sstate1,
    const SystemCounterState& sstate2,
    const int cpu_model,
    const bool show_core_output,
    const bool show_partial_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
    cout << "\n";
    cout << " EXEC  : instructions per nominal CPU cycle\n";
    cout << " IPC   : instructions per CPU cycle\n";
    cout << " FREQ  : relation to nominal CPU frequency='unhalted clock ticks'/'invariant timer ticks' (includes Intel Turbo Boost)\n";
    if (m->isActiveRelativeFrequencyAvailable())
        cout << " AFREQ : relation to nominal CPU frequency while in active state (not in power-saving C state)='unhalted clock ticks'/'invariant timer ticks while in C0-state'  (includes Intel Turbo Boost)\n";
    if (m->isL3CacheMissesAvailable())
        cout << " L3MISS: L3 (read) cache misses \n";
    if (m->isL2CacheHitsAvailable())
    {
        if (m->isAtom() || cpu_model == PCM::KNL)
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
    if (m->MCDRAMmemoryTrafficMetricsAvailable()) cout << " MCDRAM READ  : bytes read from MCDRAM controller (in GBytes)\n";
    if (m->MCDRAMmemoryTrafficMetricsAvailable()) cout << " MCDRAM WRITE : bytes written to MCDRAM controller (in GBytes)\n";
    if (m->memoryIOTrafficMetricAvailable()) cout << " IO    : bytes read/written due to IO requests to memory controller (in GBytes); this may be an over estimate due to same-cache-line partial requests\n";
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
    if (cpu_model == PCM::KNL)
        cout << " Proc Tile Core Thread |";
    else
        cout << " Core (SKT) |";

    cout << " EXEC | IPC  | FREQ  |";

    if (m->isActiveRelativeFrequencyAvailable())
        cout << " AFREQ |";
    if (m->isL3CacheMissesAvailable())
        cout << " L3MISS |";
    if (m->isL2CacheMissesAvailable())
        cout << " L2MISS |";
    if (m->isL3CacheHitRatioAvailable())
        cout << " L3HIT |";
    if (m->isL2CacheHitRatioAvailable())
        cout << " L2HIT |";
    if (m->isL3CacheMissesAvailable())
        cout << " L3MPI |";
    if (m->isL2CacheMissesAvailable())
        cout << " L2MPI | ";
    if (m->L3CacheOccupancyMetricAvailable())
        cout << "  L3OCC |";
    if (m->CoreLocalMemoryBWMetricAvailable())
        cout << "   LMB  |";
    if (m->CoreRemoteMemoryBWMetricAvailable())
        cout << "   RMB  |";

    cout << " TEMP\n\n";

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (m->isCoreOnline(i) == false || (show_partial_core_output && ycores.test(i) == false))
                continue;

            if (cpu_model == PCM::KNL)
                cout << setfill(' ') << internal << setw(5) << i
                << setw(5) << m->getTileId(i) << setw(5) << m->getCoreId(i)
                << setw(7) << m->getThreadId(i);
            else
                cout << " " << setw(3) << i << "   " << setw(2) << m->getSocketId(i);

            print_basic_metrics(m, cstates1[i], cstates2[i]);
            print_other_metrics(m, cstates1[i], cstates2[i]);
        }
    }
    if (show_socket_output)
    {
        if (!(m->getNumSockets() == 1 && (m->isAtom() || cpu_model == PCM::KNL)))
        {
            cout << longDiv;
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                cout << " SKT   " << setw(2) << i;
                print_basic_metrics(m, sktstate1[i], sktstate2[i]);
                print_other_metrics(m, sktstate1[i], sktstate2[i]);
            }
        }
    }
    cout << longDiv;

    if (show_system_output)
    {
        if (cpu_model == PCM::KNL)
            cout << setw(22) << left << " TOTAL" << internal << setw(7-5);
        else
            cout << " TOTAL  *";

        print_basic_metrics(m, sstate1, sstate2);

        if (m->L3CacheOccupancyMetricAvailable())
            cout << "     N/A ";
        if (m->CoreLocalMemoryBWMetricAvailable())
            cout << "    N/A ";
        if (m->CoreRemoteMemoryBWMetricAvailable())
            cout << "    N/A ";

        cout << "     N/A\n";
        cout << "\n Instructions retired: " << unit_format(getInstructionsRetired(sstate1, sstate2)) << " ; Active cycles: " << unit_format(getCycles(sstate1, sstate2)) << " ; Time (TSC): " << unit_format(getInvariantTSC(cstates1[0], cstates2[0])) << "ticks ; C0 (active,non-halted) core residency: " << (getCoreCStateResidency(0, sstate1, sstate2)*100.) << " %\n";
        cout << "\n";
        for (int s = 1; s <= PCM::MAX_C_STATE; ++s)
        {
            if (m->isCoreCStateResidencySupported(s))
            {
                std::cout << " C" << s << " core residency: " << (getCoreCStateResidency(s, sstate1, sstate2)*100.) << " %;";
            }
        }
        cout << "\n";
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
                std::cout << " C" << s << " package residency: " << (getPackageCStateResidency(s, sstate1, sstate2)*100.) << " %;";
                PackageCStateStackedBar.push_back(StackedBarItem(getPackageCStateResidency(s, sstate1, sstate2), "", fill));
            }
        }
        cout << "\n";

        drawStackedBar(" Core    C-state distribution", CoreCStateStackedBar, 80);
        drawStackedBar(" Package C-state distribution", PackageCStateStackedBar, 80);

        if (m->getNumCores() == m->getNumOnlineCores())
        {
            cout << "\n PHYSICAL CORE IPC                 : " << getCoreIPC(sstate1, sstate2) << " => corresponds to " << 100. * (getCoreIPC(sstate1, sstate2) / double(m->getMaxIPC())) << " % utilization for cores in active state";
            cout << "\n Instructions per nominal CPU cycle: " << getTotalExecUsage(sstate1, sstate2) << " => corresponds to " << 100. * (getTotalExecUsage(sstate1, sstate2) / double(m->getMaxIPC())) << " % core utilization over time interval\n";
        }
        cout << " SMI count: " << getSMICount(sstate1, sstate2) << "\n";
    }

    if (show_socket_output)
    {
        if (m->getNumSockets() > 1 && m->incomingQPITrafficMetricsAvailable()) // QPI info only for multi socket systems
        {
            cout << "\nIntel(r) " << m->xPI() << " data traffic estimation in bytes (data traffic coming to CPU/socket through " << m->xPI() << " links):\n\n";

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
    if (show_socket_output)
    {
        cout << "MEM (GB)->|";
        if (m->memoryTrafficMetricsAvailable())
            cout << "  READ |  WRITE |";
        if (m->localMemoryRequestRatioMetricAvailable())
            cout << " LOCAL |";
        if (m->PMMTrafficMetricsAvailable())
            cout << " PMM RD | PMM WR |";
        if (m->MCDRAMmemoryTrafficMetricsAvailable())
            cout << " MCDRAM READ | MCDRAM WRITE |";
        if (m->memoryIOTrafficMetricAvailable())
            cout << "   IO   |";
        if (m->packageEnergyMetricsAvailable())
            cout << " CPU energy |";
        if (m->dramEnergyMetricsAvailable())
            cout << " DIMM energy |";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << " LLCRDMISSLAT (ns)";
        cout << "\n";
        cout << longDiv;
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
                cout << " SKT  " << setw(2) << i;
                if (m->memoryTrafficMetricsAvailable())
                    cout << "    " << setw(5) << getBytesReadFromMC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "    " << setw(5) << getBytesWrittenToMC(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->localMemoryRequestRatioMetricAvailable())
                    cout << "  " << setw(3) << int(100.* getLocalMemoryRequestRatio(sktstate1[i], sktstate2[i])) << " %";
                if (m->PMMTrafficMetricsAvailable())
                    cout << "     " << setw(5) << getBytesReadFromPMM(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "     " << setw(5) << getBytesWrittenToPMM(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->MCDRAMmemoryTrafficMetricsAvailable())
                    cout << "   " << setw(11) << getBytesReadFromEDC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "    " << setw(11) << getBytesWrittenToEDC(sktstate1[i], sktstate2[i]) / double(1e9);
                if (m->memoryIOTrafficMetricAvailable())
                    cout << "    " << setw(5) << getIORequestBytesFromMC(sktstate1[i], sktstate2[i]) / double(1e9);
                cout << "     ";
                if(m->packageEnergyMetricsAvailable()) {
                  cout << setw(6) << getConsumedJoules(sktstate1[i], sktstate2[i]);
                }
                cout << "     ";
                if(m->dramEnergyMetricsAvailable()) {
                  cout << setw(6) << getDRAMConsumedJoules(sktstate1[i], sktstate2[i]);
                }
                cout << "         ";
                if (m->LLCReadMissLatencyMetricsAvailable()) {
                  cout << setw(6) << getLLCReadMissLatency(sktstate1[i], sktstate2[i]);
                }
                cout << "\n";
        }
        cout << longDiv;
        if (m->getNumSockets() > 1) {
            cout << "       *";
            if (m->memoryTrafficMetricsAvailable())
                cout << "    " << setw(5) << getBytesReadFromMC(sstate1, sstate2) / double(1e9) <<
                        "    " << setw(5) << getBytesWrittenToMC(sstate1, sstate2) / double(1e9);
            if (m->localMemoryRequestRatioMetricAvailable())
                cout << "  " << setw(3) << int(100.* getLocalMemoryRequestRatio(sstate1, sstate2)) << " %";
            if (m->PMMTrafficMetricsAvailable())
                cout << "     " << setw(5) << getBytesReadFromPMM(sstate1, sstate2) / double(1e9) <<
                        "     " << setw(5) << getBytesWrittenToPMM(sstate1, sstate2) / double(1e9);
            if (m->memoryIOTrafficMetricAvailable())
                cout << "    " << setw(5) << getIORequestBytesFromMC(sstate1, sstate2) / double(1e9);
            cout << "     ";
            if (m->packageEnergyMetricsAvailable()) {
                cout << setw(6) << getConsumedJoules(sstate1, sstate2);
            }
            cout << "     ";
            if (m->dramEnergyMetricsAvailable()) {
                cout << setw(6) << getDRAMConsumedJoules(sstate1, sstate2);
            }
            cout << "         ";
            if (m->LLCReadMissLatencyMetricsAvailable()) {
                cout << setw(6) << getLLCReadMissLatency(sstate1, sstate2);
            }
            cout << "\n";
        }
    }

}


void print_basic_metrics_csv_header(const PCM * m)
{
    cout << "EXEC,IPC,FREQ,";
    if (m->isActiveRelativeFrequencyAvailable())
        cout << "AFREQ,";
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
}

void print_csv_header_helper(string header, int count=1){
  for(int i = 0; i < count; i++){
    cout << header << ",";
  }
}

void print_basic_metrics_csv_semicolons(const PCM * m, string header)
{
    print_csv_header_helper(header, 3);    // EXEC;IPC;FREQ;
    if (m->isActiveRelativeFrequencyAvailable())
        print_csv_header_helper(header);  // AFREQ;
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
}

void print_csv_header(PCM * m,
    const std::bitset<MAX_CORES> & ycores,
    const int /*cpu_model*/,
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

        if (m->MCDRAMmemoryTrafficMetricsAvailable())
            print_csv_header_helper(header,2);

        print_csv_header_helper(header,7);
        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
                print_csv_header_helper(header,2);
            if (m->outgoingQPITrafficMetricsAvailable())
                print_csv_header_helper(header);
        }

        header = "System Core C-States";
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                print_csv_header_helper(header);
        header = "System Pack C-States";
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                print_csv_header_helper(header);
        if (m->packageEnergyMetricsAvailable())
            print_csv_header_helper(header);
        if (m->dramEnergyMetricsAvailable())
            print_csv_header_helper(header);
        if (m->LLCReadMissLatencyMetricsAvailable())
            print_csv_header_helper(header);
    }

    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            header = "Socket " + std::to_string(i);
            print_csv_header_helper(header);
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
            if (m->MCDRAMmemoryTrafficMetricsAvailable())
                print_csv_header_helper(header,2);
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
        }
    }

    // print second header line
    cout << "\nDate,Time,";
    if (show_system_output)
    {
        print_basic_metrics_csv_header(m);

        if (m->memoryTrafficMetricsAvailable())
                cout << "READ,WRITE,";

        if (m->localMemoryRequestRatioMetricAvailable())
            cout << "LOCAL,";

        if (m->PMMTrafficMetricsAvailable())
            cout << "PMM_RD,PMM_WR,";

        if (m->MCDRAMmemoryTrafficMetricsAvailable())
                cout << "MCDRAM_READ,MCDRAM_WRITE,";

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
        if (m->dramEnergyMetricsAvailable())
            cout << "DRAM Energy (Joules),";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << "LLCRDMISSLAT (ns),";
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
             if (m->MCDRAMmemoryTrafficMetricsAvailable())
                 cout << "MCDRAM_READ,MCDRAM_WRITE,";
             cout << "TEMP,";
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

        if (m->packageEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << "SKT" << i << ",";
        }
        if (m->dramEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << "SKT" << i << ",";
        }
        if (m->LLCReadMissLatencyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << "SKT" << i << ",";
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
        cout << ',' << getActiveRelativeFrequency(state1, state2);
    if (m->isL3CacheMissesAvailable())
        cout << ',' << float_format(getL3CacheMisses(state1, state2));
    if (m->isL2CacheMissesAvailable())
        cout << ',' << float_format(getL2CacheMisses(state1, state2));
    if (m->isL3CacheHitRatioAvailable())
        cout << ',' << getL3CacheHitRatio(state1, state2);
    if (m->isL2CacheHitRatioAvailable())
        cout << ',' << getL2CacheHitRatio(state1, state2);
    if (m->isL3CacheMissesAvailable())
        cout << ',' << double(getL3CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
    if (m->isL2CacheMissesAvailable())
        cout << ',' << double(getL2CacheMisses(state1, state2)) / getInstructionsRetired(state1, state2);
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
    const int /*cpu_model*/,
    const bool show_core_output,
    const bool show_partial_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
#ifndef _MSC_VER
    struct timeval timestamp;
    gettimeofday(&timestamp, NULL);
#endif
    tm tt = pcm_localtime();
    char old_fill = cout.fill('0');
    cout.precision(3);
    cout << "\n" << setw(4) << 1900 + tt.tm_year << '-' << setw(2) << 1 + tt.tm_mon << '-'
        << setw(2) << tt.tm_mday << ',' << setw(2) << tt.tm_hour << ':'
        << setw(2) << tt.tm_min << ':' << setw(2) << tt.tm_sec
#ifdef _MSC_VER
        << ',';
#else
        << "." << setw(3) << ceil(timestamp.tv_usec / 1000) << ',';
#endif
    cout.fill(old_fill);

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

        if (m->MCDRAMmemoryTrafficMetricsAvailable())
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
        if (m->dramEnergyMetricsAvailable())
            cout << getDRAMConsumedJoules(sstate1, sstate2) << ",";
        if (m->LLCReadMissLatencyMetricsAvailable())
            cout << getLLCReadMissLatency(sstate1, sstate2) << ",";
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
            if (m->MCDRAMmemoryTrafficMetricsAvailable())
                cout << ',' << getBytesReadFromEDC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                ',' << getBytesWrittenToEDC(sktstate1[i], sktstate2[i]) / double(1e9);
            cout << ',' << temp_format(sktstate2[i].getThermalHeadroom()) << ',';
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
        }
    }
}

int main(int argc, char * argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#endif

    cerr << "\n";
    cerr << " Processor Counter Monitor " << PCM_VERSION << "\n";
    cerr << "\n";
    
    cerr << "\n";

    // if delay is not specified: use either default (1 second),
    // or only read counters before or after PCM started: keep PCM blocked
    double delay = -1.0;

    char *sysCmd = NULL;
    char **sysArgv = NULL;
    bool show_core_output = true;
    bool show_partial_core_output = false;
    bool show_socket_output = true;
    bool show_system_output = true;
    bool csv_output = false;
    bool reset_pmu = false;
    bool allow_multiple_instances = false;
    bool disable_JKT_workaround = false; // as per http://software.intel.com/en-us/articles/performance-impact-when-sampling-certain-llc-events-on-snb-ep-with-vtune

    MainLoop mainLoop;
    std::bitset<MAX_CORES> ycores;
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
            print_help(program);
            exit(EXIT_FAILURE);
        }
        else
        if (strncmp(*argv, "--yescores", 10) == 0 ||
            strncmp(*argv, "-yc", 3) == 0 ||
            strncmp(*argv, "/yc", 3) == 0)
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
        if (strncmp(*argv, "--nocores", 9) == 0 ||
            strncmp(*argv, "-nc", 3) == 0 ||
            strncmp(*argv, "/nc", 3) == 0)
        {
            show_core_output = false;
            continue;
        }
        else
        if (strncmp(*argv, "--nosockets", 11) == 0 ||
            strncmp(*argv, "-ns", 3) == 0 ||
            strncmp(*argv, "/ns", 3) == 0)
        {
            show_socket_output = false;
            continue;
        }
        else
        if (strncmp(*argv, "--nosystem", 10) == 0 ||
            strncmp(*argv, "-nsys", 5) == 0 ||
            strncmp(*argv, "/nsys", 5) == 0)
        {
            show_system_output = false;
            continue;
        }
        else
        if (strncmp(*argv, "--multiple-instances", 20) == 0 ||
            strncmp(*argv, "-m", 2) == 0 ||
            strncmp(*argv, "/m", 2) == 0)
        {
            allow_multiple_instances = true;
            continue;
        }
        else
        if (strncmp(*argv, "-csv", 4) == 0 ||
            strncmp(*argv, "/csv", 4) == 0)
        {
            csv_output = true;
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
        else
        if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else
        if (strncmp(*argv, "-reset", 6) == 0 ||
            strncmp(*argv, "-r", 2) == 0 ||
            strncmp(*argv, "/reset", 6) == 0)
        {
            reset_pmu = true;
            continue;
        }
        else
        if (CheckAndForceRTMAbortMode(*argv, m))
        {
            continue;
        }
        else
        if (strncmp(*argv, "--noJKTWA", 9) == 0)
        {
            disable_JKT_workaround = true;
            continue;
        }
#ifdef _MSC_VER
        else
        if (strncmp(*argv, "--uninstallDriver", 17) == 0)
        {
            Driver tmpDrvObject;
            tmpDrvObject.uninstall();
            cerr << "msr.sys driver has been uninstalled. You might need to reboot the system to make this effective.\n";
            exit(EXIT_SUCCESS);
        }
        else
        if (strncmp(*argv, "--installDriver", 15) == 0)
        {
            Driver tmpDrvObject = Driver(Driver::msrLocalPath());
            if (!tmpDrvObject.start())
            {
                wcerr << "Can not access CPU counters\n";
                wcerr << "You must have a signed  driver at " << tmpDrvObject.driverPath() << " and have administrator rights to run this program\n";
                exit(EXIT_FAILURE);
            }
            exit(EXIT_SUCCESS);
        }
#endif
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
            std::istringstream is_str_stream(*argv);
            is_str_stream >> noskipws >> delay_input;
            if (is_str_stream.eof() && !is_str_stream.fail() && delay == -1) {
                delay = delay_input;
                cerr << "Delay: " << delay << "\n";
            }
            else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                print_help(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while (argc > 1); // end of command line partsing loop

    if (disable_JKT_workaround) m->disableJKTWorkaround();

    if (reset_pmu)
    {
        cerr << "\n Resetting PMU configuration\n";
        m->resetPMU();
    }

    if (allow_multiple_instances)
    {
        m->allowMultipleInstances();
    }

    // program() creates common semaphore for the singleton, so ideally to be called before any other references to PCM
    PCM::ErrorCode status = m->program();

    switch (status)
    {
    case PCM::Success:
        break;
    case PCM::MSRAccessDenied:
        cerr << "Access to Processor Counter Monitor has denied (no MSR or PCI CFG space access).\n";
        exit(EXIT_FAILURE);
    case PCM::PMUBusy:
        cerr << "Access to Processor Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
        cerr << "Alternatively you can try running PCM with option -r to reset PMU configuration at your own risk.\n";
        exit(EXIT_FAILURE);
    default:
        cerr << "Access to Processor Counter Monitor has denied (Unknown error).\n";
        exit(EXIT_FAILURE);
    }

    print_cpu_details();

    std::vector<CoreCounterState> cstates1, cstates2;
    std::vector<SocketCounterState> sktstate1, sktstate2;
    SystemCounterState sstate1, sstate2;
    const auto cpu_model = m->getCPUModel();

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
        print_csv_header(m, ycores, cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output);
    }

    m->getAllCounterStates(sstate1, sktstate1, cstates1);

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
        if (!csv_output) cout << std::flush;

        calibratedSleep(delay, sysCmd, mainLoop, m);

        m->getAllCounterStates(sstate2, sktstate2, cstates2);

        if (csv_output)
            print_csv(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
            cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output);
        else
            print_output(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
            cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output);

        // sanity checks
        if (m->isAtom() || cpu_model == PCM::KNL)
        {
            assert(getNumberOfCustomEvents(0, sstate1, sstate2) == getL2CacheMisses(sstate1, sstate2));
            assert(getNumberOfCustomEvents(1, sstate1, sstate2) == getL2CacheMisses(sstate1, sstate2) + getL2CacheHits(sstate1, sstate2));
        }
        else
        {
            assert(getNumberOfCustomEvents(0, sstate1, sstate2) == getL3CacheMisses(sstate1, sstate2));
            if (m->useSkylakeEvents()) {
                assert(getNumberOfCustomEvents(1, sstate1, sstate2) == getL3CacheHits(sstate1, sstate2));
                assert(getNumberOfCustomEvents(2, sstate1, sstate2) == getL2CacheMisses(sstate1, sstate2));
            }
            else {
                assert(getNumberOfCustomEvents(1, sstate1, sstate2) == getL3CacheHitsNoSnoop(sstate1, sstate2));
                assert(getNumberOfCustomEvents(2, sstate1, sstate2) == getL3CacheHitsSnoop(sstate1, sstate2));
            }
            assert(getNumberOfCustomEvents(3, sstate1, sstate2) == getL2CacheHits(sstate1, sstate2));
        }

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
