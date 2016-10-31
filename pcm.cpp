/*
Copyright (c) 2009-2012, Intel Corporation
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
#define HACK_TO_REMOVE_DUPLICATE_ERROR
#include <iostream>
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
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
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration
#define MAX_CORES 4096

using namespace std;

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

    sprintf(buffer, "%2d", t);
    return buffer;
}

std::string l3cache_occ_format(uint64 o)
{
    char buffer[1024];
    if (o == PCM_INVALID_QOS_MONITORING_DATA)
        return "N/A";

    sprintf(buffer, "%6d", (uint32) o);
    return buffer;
}

void print_help(const string prog_name)
{
    cerr << endl << " Usage: " << endl << " " << prog_name
        << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help      | /h           => print this help and exit" << endl;
#ifdef _MSC_VER
    cerr << "  --uninstallDriver   | --installDriver=> (un)install driver" << endl;
#endif
    cerr << "  -r    | --reset     | /reset       => reset PMU configuration (at your own risk)" << endl;
    cerr << "  -nc   | --nocores   | /nc          => hide core related output" << endl;
    cerr << "  -yc   | --yescores  | /yc          => enable specific cores to output" << endl;
    cerr << "  -ns   | --nosockets | /ns          => hide socket related output" << endl;
    cerr << "  -nsys | --nosystem  | /nsys        => hide system related output" << endl;
    cerr << "  -m    | --multiple-instances | /m  => allow multiple PCM instances running in parallel" << endl;
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
        << "                                        to a file, in case filename is provided" << endl;
    cerr << "  -i[=number] | /i[=number]          => allow to determine number of iterations" << endl;
    cerr << " Examples:" << endl;
    cerr << "  " << prog_name << " 1 -nc -ns          => print counters every second without core and socket output" << endl;
    cerr << "  " << prog_name << " 1 -i=10            => print counters every second 10 times and exit" << endl;
    cerr << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format" << endl;
    cerr << "  " << prog_name << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output" << endl;
    cerr << endl;
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
    cout << " EXEC  : instructions per nominal CPU cycle" << "\n";
    cout << " IPC   : instructions per CPU cycle" << "\n";
    cout << " FREQ  : relation to nominal CPU frequency='unhalted clock ticks'/'invariant timer ticks' (includes Intel Turbo Boost)" << "\n";
    if (cpu_model != PCM::ATOM) cout << " AFREQ : relation to nominal CPU frequency while in active state (not in power-saving C state)='unhalted clock ticks'/'invariant timer ticks while in C0-state'  (includes Intel Turbo Boost)" << "\n";
    if (cpu_model != PCM::ATOM) cout << " L3MISS: L3 cache misses " << "\n";
    if (cpu_model == PCM::ATOM)
        cout << " L2MISS: L2 cache misses " << "\n";
    else
        cout << " L2MISS: L2 cache misses (including other core's L2 cache *hits*) " << "\n";
    if (cpu_model != PCM::ATOM) cout << " L3HIT : L3 cache hit ratio (0.00-1.00)" << "\n";
    cout << " L2HIT : L2 cache hit ratio (0.00-1.00)" << "\n";
    if (cpu_model != PCM::ATOM) cout << " L3MPI : number of L3 cache misses per instruction\n";
    if (cpu_model != PCM::ATOM) cout << " L2MPI : number of L2 cache misses per instruction\n";
    if (cpu_model != PCM::ATOM) cout << " READ  : bytes read from memory controller (in GBytes)" << "\n";
    if (cpu_model != PCM::ATOM) cout << " WRITE : bytes written to memory controller (in GBytes)" << "\n";
    if (m->memoryIOTrafficMetricAvailable()) cout << " IO    : bytes read/written due to IO requests to memory controller (in GBytes); this may be an over estimate due to same-cache-line partial requests" << "\n";
    if (m->L3CacheOccupancyMetricAvailable()) cout << " L3OCC : L3 occupancy (in KBytes)" << "\n";
    if (m->CoreLocalMemoryBWMetricAvailable()) cout << " LMB   : L3 cache external bandwidth satisfied by local memory (in MBytes)" << "\n";
    if (m->CoreRemoteMemoryBWMetricAvailable()) cout << " RMB   : L3 cache external bandwidth satisfied by remote memory (in MBytes)" << "\n";
    cout << " TEMP  : Temperature reading in 1 degree Celsius relative to the TjMax temperature (thermal headroom): 0 corresponds to the max temperature" << "\n";
    cout << " energy: Energy in Joules" << "\n";
    cout << "\n";
    cout << "\n";
    const char * longDiv = "---------------------------------------------------------------------------------------------------------------\n";
    cout.precision(2);
    cout << std::fixed;
    if (cpu_model == PCM::ATOM)
        cout << " Core (SKT) | EXEC | IPC  | FREQ | L2MISS | L2HIT | TEMP" << "\n" << "\n";
    else
    {
			cout << " Core (SKT) | EXEC | IPC  | FREQ  | AFREQ | L3MISS | L2MISS | L3HIT | L2HIT | L3MPI | L2MPI |";

			if (m->L3CacheOccupancyMetricAvailable())
					cout << "  L3OCC |";
			if (m->CoreLocalMemoryBWMetricAvailable())
					cout << "   LMB  |";
			if (m->CoreRemoteMemoryBWMetricAvailable())
				cout << "   RMB  |";

            cout << " TEMP" << "\n" << "\n";
    }


    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (m->isCoreOnline(i) == false || (show_partial_core_output && ycores.test(i) == false))
                continue;

            if (cpu_model != PCM::ATOM)
            {
                cout << " " << setw(3) << i << "   " << setw(2) << m->getSocketId(i) <<
                    "     " << getExecUsage(cstates1[i], cstates2[i]) <<
                    "   " << getIPC(cstates1[i], cstates2[i]) <<
                    "   " << getRelativeFrequency(cstates1[i], cstates2[i]) <<
                    "    " << getActiveRelativeFrequency(cstates1[i], cstates2[i]) <<
                    "    " << unit_format(getL3CacheMisses(cstates1[i], cstates2[i])) <<
                    "   " << unit_format(getL2CacheMisses(cstates1[i], cstates2[i])) <<
                    "    " << getL3CacheHitRatio(cstates1[i], cstates2[i]) <<
                    "    " << getL2CacheHitRatio(cstates1[i], cstates2[i]) <<
                    "    " << double(getL3CacheMisses(cstates1[i], cstates2[i])) / getInstructionsRetired(cstates1[i], cstates2[i]) <<
                    "    " << double(getL2CacheMisses(cstates1[i], cstates2[i])) / getInstructionsRetired(cstates1[i], cstates2[i]) ;
                if (m->L3CacheOccupancyMetricAvailable())
                    cout << "   " << setw(6) << l3cache_occ_format(getL3CacheOccupancy(cstates2[i])) ;
				if (m->CoreLocalMemoryBWMetricAvailable())
					cout << "   " << setw(6) << getLocalMemoryBW(cstates1[i], cstates2[i]);
				if (m->CoreRemoteMemoryBWMetricAvailable())
                	cout << "   " << setw(6) << getRemoteMemoryBW(cstates1[i], cstates2[i]) ;
                cout << "     " << temp_format(cstates2[i].getThermalHeadroom()) <<
                    "\n";
            }
            else
                cout << " " << setw(3) << i << "   " << setw(2) << m->getSocketId(i) <<
                "     " << getExecUsage(cstates1[i], cstates2[i]) <<
                "   " << getIPC(cstates1[i], cstates2[i]) <<
                "   " << getRelativeFrequency(cstates1[i], cstates2[i]) <<
                "   " << unit_format(getL2CacheMisses(cstates1[i], cstates2[i])) <<
                "    " << getL2CacheHitRatio(cstates1[i], cstates2[i]) <<
                "     " << temp_format(cstates2[i].getThermalHeadroom()) <<
                "\n";
        }
    }
    if (show_socket_output)
    {
        if (!(m->getNumSockets() == 1 && cpu_model == PCM::ATOM))
        {
            cout << longDiv;
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                cout << " SKT   " << setw(2) << i <<
                    "     " << getExecUsage(sktstate1[i], sktstate2[i]) <<
                    "   " << getIPC(sktstate1[i], sktstate2[i]) <<
                    "   " << getRelativeFrequency(sktstate1[i], sktstate2[i]) <<
                    "    " << getActiveRelativeFrequency(sktstate1[i], sktstate2[i]) <<
                    "    " << unit_format(getL3CacheMisses(sktstate1[i], sktstate2[i])) <<
                    "   " << unit_format(getL2CacheMisses(sktstate1[i], sktstate2[i])) <<
                    "    " << getL3CacheHitRatio(sktstate1[i], sktstate2[i]) <<
                    "    " << getL2CacheHitRatio(sktstate1[i], sktstate2[i]) <<
                    "    " << double(getL3CacheMisses(sktstate1[i], sktstate2[i])) / getInstructionsRetired(sktstate1[i], sktstate2[i]) <<
                    "    " << double(getL2CacheMisses(sktstate1[i], sktstate2[i])) / getInstructionsRetired(sktstate1[i], sktstate2[i]);
                if (m->L3CacheOccupancyMetricAvailable())
                    cout << "   " << setw(6) << l3cache_occ_format(getL3CacheOccupancy(sktstate2[i])) ;
				if (m->CoreLocalMemoryBWMetricAvailable())
					cout << "   " << setw(6) << getLocalMemoryBW(sktstate1[i], sktstate2[i]);
				if (m->CoreRemoteMemoryBWMetricAvailable())
					cout << "   " << setw(6) << getRemoteMemoryBW(sktstate1[i], sktstate2[i]);

                cout << "     " << temp_format(sktstate2[i].getThermalHeadroom()) << "\n";
            }
        }
    }
    cout << longDiv;

    if (show_system_output)
    {
        if (cpu_model != PCM::ATOM)
        {
            cout << " TOTAL  *     " << getExecUsage(sstate1, sstate2) <<
                "   " << getIPC(sstate1, sstate2) <<
                "   " << getRelativeFrequency(sstate1, sstate2) <<
                "    " << getActiveRelativeFrequency(sstate1, sstate2) <<
                "    " << unit_format(getL3CacheMisses(sstate1, sstate2)) <<
                "   " << unit_format(getL2CacheMisses(sstate1, sstate2)) <<
                "    " << getL3CacheHitRatio(sstate1, sstate2) <<
                "    " << getL2CacheHitRatio(sstate1, sstate2) <<
                "    " << double(getL3CacheMisses(sstate1, sstate2)) / getInstructionsRetired(sstate1, sstate2) <<
                "    " << double(getL2CacheMisses(sstate1, sstate2)) / getInstructionsRetired(sstate1, sstate2);
            if (m->L3CacheOccupancyMetricAvailable())
                cout << "    " << " N/A ";
			if (m->CoreLocalMemoryBWMetricAvailable())
				cout << "   " << " N/A ";
			if (m->CoreRemoteMemoryBWMetricAvailable())
				cout << "   " << " N/A ";

            cout << "     N/A\n";
        }
        else
            cout << " TOTAL  *     " << getExecUsage(sstate1, sstate2) <<
            "   " << getIPC(sstate1, sstate2) <<
            "   " << getRelativeFrequency(sstate1, sstate2) <<
            "   " << unit_format(getL2CacheMisses(sstate1, sstate2)) <<
            "    " << getL2CacheHitRatio(sstate1, sstate2) <<
            "     N/A\n";
    }

    if (show_system_output)
    {
        cout << "\n" << " Instructions retired: " << unit_format(getInstructionsRetired(sstate1, sstate2)) << " ; Active cycles: " << unit_format(getCycles(sstate1, sstate2)) << " ; Time (TSC): " << unit_format(getInvariantTSC(cstates1[0], cstates2[0])) << "ticks ; C0 (active,non-halted) core residency: " << (getCoreCStateResidency(0, sstate1, sstate2)*100.) << " %\n";
        cout << "\n";
        for (int s = 1; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            std::cout << " C" << s << " core residency: " << (getCoreCStateResidency(s, sstate1, sstate2)*100.) << " %;";
        cout << "\n";
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            std::cout << " C" << s << " package residency: " << (getPackageCStateResidency(s, sstate1, sstate2)*100.) << " %;";
        cout << "\n";
        if (m->getNumCores() == m->getNumOnlineCores())
        {
            cout << "\n" << " PHYSICAL CORE IPC                 : " << getCoreIPC(sstate1, sstate2) << " => corresponds to " << 100. * (getCoreIPC(sstate1, sstate2) / double(m->getMaxIPC())) << " % utilization for cores in active state";
            cout << "\n" << " Instructions per nominal CPU cycle: " << getTotalExecUsage(sstate1, sstate2) << " => corresponds to " << 100. * (getTotalExecUsage(sstate1, sstate2) / double(m->getMaxIPC())) << " % core utilization over time interval" << "\n";
        }
    }

    if (show_socket_output)
    {
        if (m->getNumSockets() > 1 && m->incomingQPITrafficMetricsAvailable()) // QPI info only for multi socket systems
        {
            cout << "\n" << "Intel(r) QPI data traffic estimation in bytes (data traffic coming to CPU/socket through QPI links):" << "\n" << "\n";


            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            cout << "              ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " QPI" << i << "    ";

            if (m->qpiUtilizationMetricsAvailable())
            {
                cout << "| ";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << " QPI" << i << "  ";
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
            cout << "Total QPI incoming data traffic: " << unit_format(getAllIncomingQPILinkBytes(sstate1, sstate2)) << "     QPI data traffic/Memory controller traffic: " << getQPItoMCTrafficRatio(sstate1, sstate2) << "\n";
    }

    if (show_socket_output)
    {
        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            cout << "\n" << "Intel(r) QPI traffic estimation in bytes (data and non-data traffic outgoing from CPU/socket through QPI links):" << "\n" << "\n";


            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            cout << "              ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " QPI" << i << "    ";


            cout << "| ";
            for (uint32 i = 0; i < qpiLinks; ++i)
                cout << " QPI" << i << "  ";


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
            cout << "Total QPI outgoing data and non-data traffic: " << unit_format(getAllOutgoingQPILinkBytes(sstate1, sstate2)) << "\n";
        }
    }
    if (show_socket_output)
    {
        cout << "\n";
        cout << "          |";
        if (m->memoryTrafficMetricsAvailable())
            cout << "  READ |  WRITE |";
        if (m->memoryIOTrafficMetricAvailable())
            cout << "    IO  |";
        if (m->packageEnergyMetricsAvailable())
            cout << " CPU energy |";
        if (m->dramEnergyMetricsAvailable())
            cout << " DIMM energy";
        cout << "\n";
        cout << longDiv;
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
                cout << " SKT  " << setw(2) << i;
                if (m->memoryTrafficMetricsAvailable())
                    cout << "    " << setw(5) << getBytesReadFromMC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                            "    " << setw(5) << getBytesWrittenToMC(sktstate1[i], sktstate2[i]) / double(1e9);
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
                cout << "\n";
        }
        cout << longDiv;
        if (m->getNumSockets() > 1) {
            cout << "       *";
            if (m->memoryTrafficMetricsAvailable())
                cout << "    " << setw(5) << getBytesReadFromMC(sstate1, sstate2) / double(1e9) <<
                        "    " << setw(5) << getBytesWrittenToMC(sstate1, sstate2) / double(1e9);
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
            cout << "\n";
        }
    }

}

void print_csv_header(PCM * m,
    const int cpu_model,
    const bool show_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
    // print first header line
    cout << "System;;";
    if (show_system_output)
    {
        if (cpu_model != PCM::ATOM)
            {
                cout << ";;;;;;;;;;";
                if (m->memoryTrafficMetricsAvailable())
                   cout << ";;";
            }
        else
            cout << ";;;;;";


        cout << ";;;;;;;";
        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
                cout << ";;";
            if (m->outgoingQPITrafficMetricsAvailable())
                cout << ";";
        }

        cout << "System Core C-States";
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            cout << ";";
        cout << "System Pack C-States";
        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            cout << ";";
        if (m->packageEnergyMetricsAvailable())
            cout << ";";
        if (m->dramEnergyMetricsAvailable())
            cout << ";";
    }


    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
				if (cpu_model == PCM::ATOM)
						cout << "Socket" << i << ";;;;;;;";
				else
				{
                                        cout << "Socket" <<  i << ";;;;;;;;;;;";
 
					if (m->L3CacheOccupancyMetricAvailable())
						cout << ";";
                                        if (m->CoreLocalMemoryBWMetricAvailable())
                                                cout << ";";
                                        if (m->CoreRemoteMemoryBWMetricAvailable())
                                                cout << ";";
                                        if (m->memoryTrafficMetricsAvailable())
                                                 cout << ";;";
 
				}
             }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                cout << "SKT" << s << "dataIn";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << ";";
                if (m->qpiUtilizationMetricsAvailable())
                {
                    cout << "SKT" << s << "dataIn (percent)";
                    for (uint32 i = 0; i < qpiLinks; ++i)
                        cout << ";";
                }
            }
        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                cout << "SKT" << s << "trafficOut";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << ";";
                cout << "SKT" << s << "trafficOut (percent)";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << ";";
            }
        }


        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            cout << "SKT" << i << " Core C-State";
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << ";";
            cout << "SKT" << i << " Package C-State";
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                cout << ";";
        }

        if (m->packageEnergyMetricsAvailable())
        {
            cout << "Proc Energy (Joules)";
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << ";";
        }
        if (m->dramEnergyMetricsAvailable())
        {
            cout << "DRAM Energy (Joules)";
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << ";";
        }
    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
			if (cpu_model == PCM::ATOM)
				cout << "Core" << i << " (Socket" << setw(2) << m->getSocketId(i) << ");;;;;";
			else {

				cout << "Core" << i << " (Socket" << setw(2) << m->getSocketId(i) << ");;;;;;;;;;";
            if (m->L3CacheOccupancyMetricAvailable())
                cout << ';' ;
            if (m->CoreLocalMemoryBWMetricAvailable())
                cout << ';' ;
            if (m->CoreRemoteMemoryBWMetricAvailable())
                cout << ';' ;
                 }
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << ";";
            cout << ";"; // TEMP
 
        }
    }

    // print second header line
    cout << "\nDate;Time;";
    if (show_system_output)
    {
        if (cpu_model != PCM::ATOM)
        {
		cout << "EXEC;IPC;FREQ;AFREQ;L3MISS;L2MISS;L3HIT;L2HIT;L3MPI;L2MPI;";
                if (m->memoryTrafficMetricsAvailable())
		   cout << "READ;WRITE;";
	}
        else
        {
            cout << "EXEC;IPC;FREQ;L2MISS;L2HIT;";
        }


        cout << "INST;ACYC;TIME(ticks);PhysIPC;PhysIPC%;INSTnom;INSTnom%;";
        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
                cout << "TotalQPIin;QPItoMC;";
            if (m->outgoingQPITrafficMetricsAvailable())
                cout << "TotalQPIout;";
        }


        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            cout << "C" << s << "res%;";

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            cout << "C" << s << "res%;";

        if (m->packageEnergyMetricsAvailable())
            cout << "Proc Energy (Joules);";
        if (m->dramEnergyMetricsAvailable())
            cout << "DRAM Energy (Joules);";
    }


    if (show_socket_output)
    {
        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
				if (cpu_model == PCM::ATOM)
						cout << "EXEC;IPC;FREQ;L2MISS;L2HIT;TEMP;";
				else
				{
					
					cout << "EXEC;IPC;FREQ;AFREQ;L3MISS;L2MISS;L3HIT;L2HIT;L3MPI;L2MPI;";
					if (m->L3CacheOccupancyMetricAvailable())
						cout << "L3OCC;";
					if (m->CoreLocalMemoryBWMetricAvailable())
						cout << "LMB;";
					if (m->CoreRemoteMemoryBWMetricAvailable())
						cout << "RMB;";
 					if (m->memoryTrafficMetricsAvailable())
 						cout << "READ;WRITE;";
					cout << "TEMP;";
				}
            }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();

            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << "QPI" << i << ";";

                if (m->qpiUtilizationMetricsAvailable())
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << "QPI" << i << ";";
            }

        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << "QPI" << i << ";";
                for (uint32 i = 0; i < qpiLinks; ++i)
                    cout << "QPI" << i << ";";
            }

        }


        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << "C" << s << "res%;";

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                cout << "C" << s << "res%;";
        }

        if (m->packageEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << "SKT" << i << ";";
        }
        if (m->dramEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << "SKT" << i << ";";
        }

    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (cpu_model == PCM::ATOM)
                cout << "EXEC;IPC;FREQ;L2MISS;L2HIT;";
            else
				{
					cout << "EXEC;IPC;FREQ;AFREQ;L3MISS;L2MISS;L3HIT;L2HIT;L3MPI;L2MPI;";
					if (m->L3CacheOccupancyMetricAvailable())
						cout << "L3OCC;";
					if (m->CoreLocalMemoryBWMetricAvailable())
						cout << "LMB;";
					if (m->CoreRemoteMemoryBWMetricAvailable())
						cout << "RMB;";
				}


            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << "C" << s << "res%;";

            cout << "TEMP;";

        }

    }

}

void print_csv(PCM * m,
    const std::vector<CoreCounterState> & cstates1,
    const std::vector<CoreCounterState> & cstates2,
    const std::vector<SocketCounterState> & sktstate1,
    const std::vector<SocketCounterState> & sktstate2,
    const SystemCounterState& sstate1,
    const SystemCounterState& sstate2,
    const int cpu_model,
    const bool show_core_output,
    const bool show_socket_output,
    const bool show_system_output
    )
{
#ifndef _MSC_VER
    struct timeval timestamp;
    gettimeofday(&timestamp, NULL);
#endif
    time_t t = time(NULL);
    tm *tt = localtime(&t);
    char old_fill = cout.fill('0');
    cout.precision(3);
    cout << endl << setw(4) << 1900 + tt->tm_year << '-' << setw(2) << 1 + tt->tm_mon << '-'
        << setw(2) << tt->tm_mday << ';' << setw(2) << tt->tm_hour << ':'
        << setw(2) << tt->tm_min << ':' << setw(2) << tt->tm_sec
#ifdef _MSC_VER
        << ';';
#else
        << "." << setw(3) << ceil(timestamp.tv_usec / 1000) << ';';
#endif
    cout.fill(old_fill);

    if (show_system_output)
    {
        if (cpu_model != PCM::ATOM)
        {
            cout << getExecUsage(sstate1, sstate2) <<
                ';' << getIPC(sstate1, sstate2) <<
                ';' << getRelativeFrequency(sstate1, sstate2) <<
                ';' << getActiveRelativeFrequency(sstate1, sstate2) <<
                ';' << float_format(getL3CacheMisses(sstate1, sstate2)) <<
                ';' << float_format(getL2CacheMisses(sstate1, sstate2)) <<
                ';' << getL3CacheHitRatio(sstate1, sstate2) <<
                ';' << getL2CacheHitRatio(sstate1, sstate2) <<
                ';' << double(getL3CacheMisses(sstate1, sstate2)) / getInstructionsRetired(sstate1, sstate2) <<
                ';' << double(getL2CacheMisses(sstate1, sstate2)) / getInstructionsRetired(sstate1, sstate2) << ";";
            if (m->memoryTrafficMetricsAvailable())
                cout << getBytesReadFromMC(sstate1, sstate2) / double(1e9) <<
                ';' << getBytesWrittenToMC(sstate1, sstate2) / double(1e9) << ';';
        }
        else
            cout << getExecUsage(sstate1, sstate2) <<
            ';' << getIPC(sstate1, sstate2) <<
            ';' << getRelativeFrequency(sstate1, sstate2) <<
            ';' << float_format(getL2CacheMisses(sstate1, sstate2)) <<
            ';' << getL2CacheHitRatio(sstate1, sstate2) <<
            ';';

        cout << float_format(getInstructionsRetired(sstate1, sstate2)) << ";"
            << float_format(getCycles(sstate1, sstate2)) << ";"
            << float_format(getInvariantTSC(cstates1[0], cstates2[0])) << ";"
            << getCoreIPC(sstate1, sstate2) << ";"
            << 100. * (getCoreIPC(sstate1, sstate2) / double(m->getMaxIPC())) << ";"
            << getTotalExecUsage(sstate1, sstate2) << ";"
            << 100. * (getTotalExecUsage(sstate1, sstate2) / double(m->getMaxIPC())) << ";";

        if (m->getNumSockets() > 1) { // QPI info only for multi socket systems
            if (m->incomingQPITrafficMetricsAvailable())
               cout << float_format(getAllIncomingQPILinkBytes(sstate1, sstate2)) << ";"
                    << getQPItoMCTrafficRatio(sstate1, sstate2) << ";";
            if (m->outgoingQPITrafficMetricsAvailable())
               cout << float_format(getAllOutgoingQPILinkBytes(sstate1, sstate2)) << ";";
        }


        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isCoreCStateResidencySupported(s))
            cout << getCoreCStateResidency(s, sstate1, sstate2) * 100 << ";";

        for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
        if (m->isPackageCStateResidencySupported(s))
            cout << getPackageCStateResidency(s, sstate1, sstate2) * 100 << ";";

        if (m->packageEnergyMetricsAvailable())
            cout << getConsumedJoules(sstate1, sstate2) << ";";
        if (m->dramEnergyMetricsAvailable())
            cout << getDRAMConsumedJoules(sstate1, sstate2) << ";";

    }

    if (show_socket_output)
    {
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                if (cpu_model == PCM::ATOM)
                    cout << getExecUsage(sktstate1[i], sktstate2[i]) <<
                    ';' << getIPC(sktstate1[i], sktstate2[i]) <<
                    ';' << getRelativeFrequency(sktstate1[i], sktstate2[i]) <<
                    ';' << float_format(getL2CacheMisses(sktstate1[i], sktstate2[i])) <<
                    ';' << getL2CacheHitRatio(sktstate1[i], sktstate2[i]);
                else
                    cout << getExecUsage(sktstate1[i], sktstate2[i]) <<
                    ';' << getIPC(sktstate1[i], sktstate2[i]) <<
                    ';' << getRelativeFrequency(sktstate1[i], sktstate2[i]) <<
                    ';' << getActiveRelativeFrequency(sktstate1[i], sktstate2[i]) <<
                    ';' << float_format(getL3CacheMisses(sktstate1[i], sktstate2[i])) <<
                    ';' << float_format(getL2CacheMisses(sktstate1[i], sktstate2[i])) <<
                    ';' << getL3CacheHitRatio(sktstate1[i], sktstate2[i]) <<
                    ';' << getL2CacheHitRatio(sktstate1[i], sktstate2[i]) <<
                    ';' << double(getL3CacheMisses(sktstate1[i], sktstate2[i])) / getInstructionsRetired(sktstate1[i], sktstate2[i]) <<
                    ';' << double(getL2CacheMisses(sktstate1[i], sktstate2[i])) / getInstructionsRetired(sktstate1[i], sktstate2[i]) ;
                if (m->L3CacheOccupancyMetricAvailable())
                    cout << ';' << l3cache_occ_format(getL3CacheOccupancy(sktstate2[i]));
				if (m->CoreLocalMemoryBWMetricAvailable())
					cout << ';' << getLocalMemoryBW(sktstate1[i], sktstate2[i]);
				if (m->CoreRemoteMemoryBWMetricAvailable())
                	cout << ';' << getRemoteMemoryBW(sktstate1[i], sktstate2[i]) ;
		if (m->memoryTrafficMetricsAvailable())
                    cout << ';' << getBytesReadFromMC(sktstate1[i], sktstate2[i]) / double(1e9) <<
                    ';' << getBytesWrittenToMC(sktstate1[i], sktstate2[i]) / double(1e9);
                cout << ';' << temp_format(sktstate2[i].getThermalHeadroom()) << ';';
            }
        }

        if (m->getNumSockets() > 1 && (m->incomingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << float_format(getIncomingQPILinkBytes(i, l, sstate1, sstate2)) << ";";

                if (m->qpiUtilizationMetricsAvailable())
                {
                    for (uint32 l = 0; l < qpiLinks; ++l)
                        cout << setw(3) << std::dec << int(100. * getIncomingQPILinkUtilization(i, l, sstate1, sstate2)) << "%;";
                }
            }
        }

        if (m->getNumSockets() > 1 && (m->outgoingQPITrafficMetricsAvailable())) // QPI info only for multi socket systems
        {
            const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
            {
                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << float_format(getOutgoingQPILinkBytes(i, l, sstate1, sstate2)) << ";";

                for (uint32 l = 0; l < qpiLinks; ++l)
                    cout << setw(3) << std::dec << int(100. * getOutgoingQPILinkUtilization(i, l, sstate1, sstate2)) << "%;";
            }
        }


        for (uint32 i = 0; i < m->getNumSockets(); ++i)
        {
            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << getCoreCStateResidency(s, sktstate1[i], sktstate2[i]) * 100 << ";";

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isPackageCStateResidencySupported(s))
                cout << getPackageCStateResidency(s, sktstate1[i], sktstate2[i]) * 100 << ";";
        }

        if (m->packageEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getConsumedJoules(sktstate1[i], sktstate2[i]) << ";";
        }
        if (m->dramEnergyMetricsAvailable())
        {
            for (uint32 i = 0; i < m->getNumSockets(); ++i)
                cout << getDRAMConsumedJoules(sktstate1[i], sktstate2[i]) << " ;";
        }
    }

    if (show_core_output)
    {
        for (uint32 i = 0; i < m->getNumCores(); ++i)
        {
            if (cpu_model != PCM::ATOM)
                {
                    cout << getExecUsage(cstates1[i], cstates2[i]) <<
                    ';' << getIPC(cstates1[i], cstates2[i]) <<
                    ';' << getRelativeFrequency(cstates1[i], cstates2[i]) <<
                    ';' << getActiveRelativeFrequency(cstates1[i], cstates2[i]) <<
                    ';' << float_format(getL3CacheMisses(cstates1[i], cstates2[i])) <<
                    ';' << float_format(getL2CacheMisses(cstates1[i], cstates2[i])) <<
                    ';' << getL3CacheHitRatio(cstates1[i], cstates2[i]) <<
                    ';' << getL2CacheHitRatio(cstates1[i], cstates2[i]) <<
                    ';' << double(getL3CacheMisses(cstates1[i], cstates2[i])) / getInstructionsRetired(cstates1[i], cstates2[i]) <<
                    ';' << double(getL2CacheMisses(cstates1[i], cstates2[i])) / getInstructionsRetired(cstates1[i], cstates2[i]);
                    if (m->L3CacheOccupancyMetricAvailable())
                        cout << ';' << l3cache_occ_format(getL3CacheOccupancy(cstates2[i])) ;
					if (m->CoreLocalMemoryBWMetricAvailable())
						cout << ';' << getLocalMemoryBW(cstates1[i], cstates2[i]);
					if (m->CoreRemoteMemoryBWMetricAvailable())
						cout <<	';' << getRemoteMemoryBW(cstates1[i], cstates2[i]) ;
		    cout << ';';
                }
            else
                cout << getExecUsage(cstates1[i], cstates2[i]) <<
                ';' << getIPC(cstates1[i], cstates2[i]) <<
                ';' << getRelativeFrequency(cstates1[i], cstates2[i]) <<
                ';' << float_format(getL2CacheMisses(cstates1[i], cstates2[i])) <<
                ';' << getL2CacheHitRatio(cstates1[i], cstates2[i]) <<
                ';';

            for (int s = 0; s <= PCM::MAX_C_STATE; ++s)
            if (m->isCoreCStateResidencySupported(s))
                cout << getCoreCStateResidency(s, cstates1[i], cstates2[i]) * 100 << ";";

            cout << temp_format(cstates2[i].getThermalHeadroom()) << ';';
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

    cerr << endl;
    cerr << " Intel(r) Performance Counter Monitor " << INTEL_PCM_VERSION << endl;
    cerr << endl;
    cerr << INTEL_PCM_COPYRIGHT << endl;
    cerr << endl;

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

    long diff_usec = 0; // deviation of clock is useconds between measurements
    int calibrated = PCM_CALIBRATION_INTERVAL - 2; // keeps track is the clock calibration needed
    unsigned int numberOfIterations = 0; // number of iterations
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
                cerr << "Error: --yescores requires additional argument." << endl;
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
                    cerr << "Core ID:" << core_id << " exceed maximum range " << MAX_CORES << ", program abort" << endl;
                    exit(EXIT_FAILURE);
                }

                ycores.set(atoi(s.c_str()),true);
            }
            if(m->getNumCores() > MAX_CORES)
            {
                cerr << "Error: --yescores option is enabled, but #define MAX_CORES " << MAX_CORES << " is less than  m->getNumCores() = " << m->getNumCores() << endl;
                cerr << "There is a potential to crash the system. Please increase MAX_CORES to at least " << m->getNumCores() << " and re-enable this option." << endl;
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
        if (strncmp(*argv, "--nosystem", 9) == 0 ||
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
        if (strncmp(*argv, "-i", 2) == 0 ||
            strncmp(*argv, "/i", 2) == 0)
        {
            string cmd = string(*argv);
            size_t found = cmd.find('=', 2);
            if (found != string::npos) {
                string tmp = cmd.substr(found + 1);
                if (!tmp.empty()) {
                    numberOfIterations = (unsigned int)atoi(tmp.c_str());
                }
            }
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
            cerr << "msr.sys driver has been uninstalled. You might need to reboot the system to make this effective." << endl;
            exit(EXIT_SUCCESS);
        }
        else
        if (strncmp(*argv, "--installDriver", 15) == 0)
        {
            TCHAR driverPath[1040]; // length for current directory + "\\msr.sys"
            GetCurrentDirectory(1024, driverPath);
            wcscat_s(driverPath, 1040, L"\\msr.sys");
            Driver tmpDrvObject;
            if (!tmpDrvObject.start(driverPath))
            {
                cerr << "Can not access CPU counters" << endl;
                cerr << "You must have signed msr.sys driver in your current directory and have administrator rights to run this program" << endl;
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
                cerr << "Delay: " << delay << endl;
            }
            else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it." << endl;
                print_help(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while (argc > 1); // end of command line partsing loop

    if (disable_JKT_workaround) m->disableJKTWorkaround();

    if (reset_pmu)
    {
        cerr << "\n Resetting PMU configuration" << endl;
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
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access)." << endl;
        exit(EXIT_FAILURE);
    case PCM::PMUBusy:
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU." << endl;
        cerr << "Alternatively you can try running Intel PCM with option -r to reset PMU configuration at your own risk." << endl;
        exit(EXIT_FAILURE);
    default:
        cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error)." << endl;
        exit(EXIT_FAILURE);
    }

    cerr << "\nDetected " << m->getCPUBrandString() << " \"Intel(r) microarchitecture codename " << m->getUArchCodename() << "\"" << endl;

    std::vector<CoreCounterState> cstates1, cstates2;
    std::vector<SocketCounterState> sktstate1, sktstate2;
    SystemCounterState sstate1, sstate2;
    const int cpu_model = m->getCPUModel();
    uint64 TimeAfterSleep = 0;
    PCM_UNUSED(TimeAfterSleep);

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    }
    else {
        m->setBlocked(false);
    }

    if (csv_output) {
        print_csv_header(m, cpu_model, show_core_output, show_socket_output, show_system_output);
        if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;
    }
    else {
        // for non-CSV mode delay < 1.0 does not make a lot of practical sense:
        // hard to read from the screen, or
        // in case delay is not provided in command line => set default
        if (((delay<1.0) && (delay>0.0)) || (delay <= 0.0)) delay = PCM_DELAY_DEFAULT;
    }
    // cerr << "DEBUG: Delay: " << delay << " seconds. Blocked: " << m->isBlocked() << endl;

    m->getAllCounterStates(sstate1, sktstate1, cstates1);

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    unsigned int i = 1;

    while ((i <= numberOfIterations) || (numberOfIterations == 0))
    {
        if (!csv_output) cout << std::flush;
        int delay_ms = int(delay * 1000);
        int calibrated_delay_ms = delay_ms;
#ifdef _MSC_VER
        // compensate slow Windows console output
        if (TimeAfterSleep) delay_ms -= (uint32)(m->getTickCount() - TimeAfterSleep);
        if (delay_ms < 0) delay_ms = 0;
#else
        // compensation of delay on Linux/UNIX
        // to make the sampling interval as monotone as possible
        struct timeval start_ts, end_ts;
        if (calibrated == 0) {
            gettimeofday(&end_ts, NULL);
            diff_usec = (end_ts.tv_sec - start_ts.tv_sec)*1000000.0 + (end_ts.tv_usec - start_ts.tv_usec);
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
        TimeAfterSleep = m->getTickCount();

        m->getAllCounterStates(sstate2, sktstate2, cstates2);

        if (csv_output)
            print_csv(m, cstates1, cstates2, sktstate1, sktstate2, sstate1, sstate2,
            cpu_model, show_core_output, show_socket_output, show_system_output);
        else
            print_output(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
            cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output);

        // sanity checks
        if (cpu_model == PCM::ATOM)
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
            break;
        }

        ++i;
    }

    exit(EXIT_SUCCESS);
}
