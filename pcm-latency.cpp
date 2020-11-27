/*
Copyright (c) 2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
//
// written by Subhiksha Ravisundar
#include "cpucounters.h"
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#endif
#include <fstream>
#include <stdlib.h>
#include <cstdint>
#include <numeric>
#include <bitset>
#include <algorithm>
#include <string.h>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include "lspci.h"
#include "utils.h"
using namespace std;
using namespace pcm;

#define DDR 0
#define PMM 1

#define L1 0
#define L2 1

#define RPQ_OCC 0
#define RPQ_INS 1
#define WPQ_OCC 2
#define WPQ_INS 3

#define FB_OCC_RD 0
#define FB_INS_RD 1

#define PCM_DELAY_DEFAULT 3.0 // in seconds
#define MAX_CORES 4096

EventSelectRegister regs[2];
const uint8_t max_sockets = 64;

struct socket_info_uncore
{
    int socket_id;
    double rlatency;
    double wlatency;
    double rinsert;
    double winsert;
    double roccupancy;
    double woccupancy;
};

struct core_info
{
    int core_id;
    int socket;
    int thread;
    double latency;
    double occ_rd;
    double insert_rd;
    core_info() : core_id(0), socket(0), thread(0), latency(0.), occ_rd(0.), insert_rd(0.) {}
};

struct socket_info_pci
{
    int socket_id;
    uint64_t latency;
};

struct res_uncore
{
    string name;
    vector<struct socket_info_uncore> skt;
} uncore_event[10];

struct res_core
{
    string name;
    vector<struct core_info> core;
    vector<struct core_info> socket;
} core_latency[10];

double DRAMSpeed;

ServerUncoreCounterState * BeforeState;
ServerUncoreCounterState * AfterState;


SystemCounterState SysBeforeState, SysAfterState;
std::vector<CoreCounterState> BeforeState_core, AfterState_core;
std::vector<SocketCounterState> DummySocketStates;

void collect_beforestate_uncore(PCM *m)
{
    for (unsigned int i=0; i<m->getNumSockets(); i++)
    {
        BeforeState[i] = m->getServerUncoreCounterState(i);
    }
}

void collect_afterstate_uncore(PCM *m)
{
    for (unsigned int i=0; i<m->getNumSockets(); i++)
    {
        AfterState[i] = m->getServerUncoreCounterState(i);
    }
}

void store_latency_uncore(PCM *m, bool ddr, int delay_ms)
{
    for (unsigned int i=0; i<m->getNumSockets(); i++)
    {
        uncore_event[ddr].skt[i].socket_id = i;
        const double delay_seconds = double(delay_ms) / 1000.;
        DRAMSpeed = double(getDRAMClocks(0, BeforeState[i], AfterState[i]))/(double(1e9) * delay_seconds);
        uncore_event[ddr].skt[i].rinsert = 0;
        uncore_event[ddr].skt[i].roccupancy = 0;
        uncore_event[ddr].skt[i].winsert = 0;
        uncore_event[ddr].skt[i].woccupancy = 0;
        for (size_t channel = 0; channel < m->getMCChannelsPerSocket(); ++channel)
        {
            uncore_event[ddr].skt[i].rinsert += (double)getMCCounter((uint32)channel, RPQ_INS, BeforeState[i], AfterState[i]);
            uncore_event[ddr].skt[i].roccupancy += (double)getMCCounter((uint32)channel, RPQ_OCC, BeforeState[i], AfterState[i]);
            uncore_event[ddr].skt[i].winsert += (double)getMCCounter((uint32)channel, WPQ_INS, BeforeState[i], AfterState[i]);
            uncore_event[ddr].skt[i].woccupancy += (double)getMCCounter((uint32)channel, WPQ_OCC, BeforeState[i], AfterState[i]);
        }
        if (uncore_event[ddr].skt[i].rinsert == 0.)
        {
            uncore_event[ddr].skt[i].rlatency = 0;
        } else {
            uncore_event[ddr].skt[i].rlatency = uncore_event[ddr].skt[i].roccupancy / uncore_event[ddr].skt[i].rinsert;
        }

        if (uncore_event[ddr].skt[i].winsert == 0.)
        {
            uncore_event[ddr].skt[i].wlatency = 0;
        } else {
            uncore_event[ddr].skt[i].wlatency = uncore_event[ddr].skt[i].woccupancy / uncore_event[ddr].skt[i].winsert;
        }

        swap(BeforeState[i], AfterState[i]);
    }
}

void collect_beforestate_core(PCM *m)
{
    m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState_core);
}

void collect_afterstate_core(PCM *m)
{
    m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState_core);
}

void store_latency_core(PCM *m)
{
    unsigned int extra_clocks_for_L1_miss = 5;
    struct res_core core_event[3];
    for (int k=0; k < 3; k++)
    {
         core_event[k].core.resize(MAX_CORES);
    }
    for (auto & s : core_latency[L1].socket)
    {
        s.occ_rd = 0;
        s.insert_rd = 0;
    }
    for (unsigned int i=0; i<m->getNumCores(); i++)
    {
        const double frequency = (((double)getCycles(BeforeState_core[i], AfterState_core[i]) /
            (double)getRefCycles(BeforeState_core[i], AfterState_core[i])) * (double)m->getNominalFrequency()) / 1000000000;
        for(int j=0; j<2; j++)// 2 events
        {
            core_event[j].core[i].core_id = i;
            core_event[j].core[i].latency = (double)getNumberOfCustomEvents(j, BeforeState_core[i], AfterState_core[i]);
        }
        // L1 latency
        //Adding 5 clocks for L1 Miss
        core_latency[L1].core[i].latency = ((core_event[FB_OCC_RD].core[i].latency/core_event[FB_INS_RD].core[i].latency)+extra_clocks_for_L1_miss)/frequency;
        core_latency[L1].core[i].occ_rd = (core_event[FB_OCC_RD].core[i].latency);
        core_latency[L1].core[i].insert_rd = (core_event[FB_INS_RD].core[i].latency);
        const auto s = m->getSocketId(i);
        core_latency[L1].socket[s].occ_rd += (core_latency[L1].core[i].occ_rd + extra_clocks_for_L1_miss * core_latency[L1].core[i].insert_rd) / frequency;
        core_latency[L1].socket[s].insert_rd += core_latency[L1].core[i].insert_rd;
    }
    for (auto & s : core_latency[L1].socket)
    {
        s.latency = s.occ_rd / s.insert_rd;
    }
    swap(BeforeState_core, AfterState_core);
    swap(SysBeforeState, SysAfterState);
}


void print_verbose(PCM *m, int ddr_ip)
{
    cout << "L1 Cache Latency ============================= \n";
    for (unsigned int i=0; i<m->getNumCores(); i++)
    {
        cout << "Core: " << i << "\n";
        cout << "L1 Occupancy read: " << core_latency[0].core[i].occ_rd << "\n";
        cout << "L1 Inserts read: " << core_latency[0].core[i].insert_rd << "\n";
        cout << "\n";
    }
    if (ddr_ip == DDR)
    {
        cout << "DDR Latency =================================\n";
        cout << "Read Inserts Socket0: " << uncore_event[DDR].skt[0].rinsert << "\n";
        cout << "Read Occupancy Socket0: " << uncore_event[DDR].skt[0].roccupancy << "\n";
        cout << "Read Inserts Socket1: " << uncore_event[DDR].skt[1].rinsert << "\n";
        cout << "Read Occupancy Socket1: " << uncore_event[DDR].skt[1].roccupancy << "\n";
        cout << "\n";
        cout << "Write Inserts Socket0: " << uncore_event[DDR].skt[0].winsert << "\n";
        cout << "Write Occupancy Socket0: " << uncore_event[DDR].skt[0].woccupancy << "\n";
        cout << "Write Inserts Socket1: " << uncore_event[DDR].skt[1].winsert << "\n";
        cout << "Write Occupancy Socket1: " << uncore_event[DDR].skt[1].woccupancy << "\n";
    }

    if (ddr_ip == PMM)
    {
        cout << "PMM Latency =================================\n";
        cout << "Read Inserts Socket0: " << uncore_event[PMM].skt[0].rinsert << "\n";
        cout << "Read Occupancy Socket0: " << uncore_event[PMM].skt[0].roccupancy << "\n";
        cout << "Read Inserts Socket1: " << uncore_event[PMM].skt[1].rinsert << "\n";
        cout << "Read Occupancy Socket1: " << uncore_event[PMM].skt[1].roccupancy << "\n";
        cout << "\n";
        cout << "Write Inserts Socket0: " << uncore_event[PMM].skt[0].winsert << "\n";
        cout << "Write Occupancy Socket0: " << uncore_event[PMM].skt[0].woccupancy << "\n";
        cout << "Write Inserts Socket1: " << uncore_event[PMM].skt[1].winsert << "\n";
        cout << "Write Occupancy Socket1: " << uncore_event[PMM].skt[1].woccupancy << "\n";
    }
}

void print_ddr(PCM *m, int ddr_ip)
{
    if (ddr_ip == PMM)
    {
        if (m->PMMTrafficMetricsAvailable())
        {
            cout << "PMM read Latency(ns)\n";
            for (unsigned int n=0; n<m->getNumSockets(); n++)
            {
                cout << "Socket" << n << ": " << double(uncore_event[PMM].skt[n].rlatency)/DRAMSpeed;
                cout << "\n";
            }
        }
        else
        {
            cout << "PMM metrics are not supported on your processor\n";
        }
    }

    if (ddr_ip == DDR)
    {
        cout << "DDR read Latency(ns)\n";
        for (unsigned int n=0; n<m->getNumSockets(); n++)
        {
            cout << "Socket" << n << ": " << double(uncore_event[DDR].skt[n].rlatency)/DRAMSpeed;
            cout << "\n";
        }
    }
}

void print_core_stats(PCM *m, unsigned int core_size_per_socket, vector<vector<vector<struct core_info >>> &sk_th)
{
    auto printHeader = []()
    {
        cout << "\n\n";
        cout << "L1 Cache Miss Latency(ns) [Adding 5 clocks for L1 Miss]\n\n";;
    };
    printHeader();
    for (unsigned int sid=0; sid<m->getNumSockets(); sid++)
    {
        for (unsigned int tid=0; tid< m->getThreadsPerCore(); tid++)
        {
            cout << "Socket" << sid << " Thread" << tid << "     ";
        }
    }

    cout << "\n-----------------------------------------------------------------------------\n";

    for (unsigned int cid=0; cid<core_size_per_socket; cid++)
    {
        for (unsigned int sid=0; sid<m->getNumSockets(); sid++)
        {
            for (unsigned int tid=0; tid<m->getThreadsPerCore(); tid++)
            {
                cout << "Core" << sk_th[sid][tid][cid].core_id << ": " << fixed << setprecision(2) << sk_th[sid][tid][cid].latency << "        ";
            }
        }
        cout << "\n";
    }
    cout << "\n";

    printHeader();

    for (unsigned int s = 0; s < m->getNumSockets(); ++s)
    {
        cout << "Socket" << s << ": " << core_latency[L1].socket[s].latency << "\n";
    }
}

void print_all_stats(PCM *m, bool enable_pmm, bool enable_verbose)
{
    vector < vector < vector < struct core_info >>> sk_th;
    unsigned int sid, cid, tid;
    unsigned int core_size_per_socket=0;
   //Populate Core info per Socket and thread_id
   //Create 3D vector with Socket as 1D, Thread as 2D and Core info for the 3D
    for (sid = 0; sid < m->getNumSockets(); sid++)
    {
        vector < vector <core_info> > tmp_thread;
        for (tid = 0; tid < m->getThreadsPerCore(); tid++)
        {
            vector <core_info> tmp_core;
            for (cid = 0; cid < m->getNumCores(); cid++)
            {
                if ((sid == (unsigned int)(m->getSocketId(cid))) && (tid == (unsigned int)(m->getThreadId(cid))))
                {
                core_info tmp;
                tmp.core_id = cid;
                tmp.latency = core_latency[L1].core[cid].latency;
                tmp_core.push_back(tmp);
                }
            }
            core_size_per_socket = (unsigned int)tmp_core.size();
            tmp_thread.push_back(tmp_core);
        }
        sk_th.push_back(tmp_thread);
    }

    print_core_stats(m, core_size_per_socket, sk_th);

    if (m->DDRLatencyMetricsAvailable())
    {
        print_ddr(m, enable_pmm);
        if (enable_verbose)
            print_verbose(m, enable_pmm);
    }
}

EventSelectRegister build_core_register(uint64 reg_used, uint64 value, uint64 usr, uint64 os, uint64 enable, uint64 umask, uint64 event_select, uint64 edge)
{
    regs[reg_used].value = value;
    regs[reg_used].fields.usr = usr;
    regs[reg_used].fields.os = os;
    regs[reg_used].fields.enable = enable;
    regs[reg_used].fields.umask = umask;
    regs[reg_used].fields.event_select = event_select;
    regs[reg_used].fields.edge = edge;
    return regs[reg_used];
}

void check_status(PCM *m, PCM::ErrorCode status)
{
    switch (status)
    {
        case PCM::Success:
            break;
        case PCM::MSRAccessDenied:
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access).\n";
            exit(EXIT_FAILURE);
        case PCM::PMUBusy:
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
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
            cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error).\n";
            exit(EXIT_FAILURE);
    }
    print_cpu_details();

    if(!(m->LatencyMetricsAvailable()))
    {
        cerr << "Platform not Supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to " <<(int)max_sockets<< " sockets are supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }
}

void build_registers(PCM *m, PCM::ExtendedCustomCoreEventDescription conf, bool enable_pmm, bool /*enable_verbose*/)
{

//Check if Online Cores = Available Cores. This version only supports available cores = online cores
    if (m->getNumCores() != m->getNumOnlineCores())
    {
        cout << "Number of online cores should be equal to number of available cores\n";
        exit(EXIT_FAILURE);
    }

    //Check for Maximum Custom Core Events
    if (m->getMaxCustomCoreEvents() < 2)
    {
        cout << "System should support a minimum of 2 Custom Core Events to run pcm-latency\n";
        exit(EXIT_FAILURE);
    }
//Creating conf
    conf.fixedCfg = NULL; // default
    conf.nGPCounters = 2;
    conf.gpCounterCfg = regs;
    conf.OffcoreResponseMsrValue[0] = 0;
    conf.OffcoreResponseMsrValue[1] = 0;

// Registers for L1 cache
    regs[FB_OCC_RD] = build_core_register(FB_OCC_RD, 0, 1, 1, 1, 0x01, 0x48, 0); //L1d Fill Buffer Occupancy (Read Only)
    regs[FB_INS_RD] = build_core_register(FB_INS_RD, 0, 1, 1, 1, 0x48, 0xd1, 0); //MEM_LOAD_RETIRED(FB_HIT + L1_MISS)

//Restructuring Counters
    for (int i=0; i <5; i++)
    {
        uncore_event[i].skt.resize(m->getNumSockets());
        core_latency[i].core.resize(m->getNumCores());
        core_latency[i].socket.resize(m->getNumSockets());
    }

//Program Core and Uncore
    m->resetPMU();
    PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
    check_status(m, status);
    m->programServerUncoreLatencyMetrics(enable_pmm);
}

void collect_data(PCM *m, bool enable_pmm, bool enable_verbose, int delay_ms, MainLoop & mainLoop)
{

    BeforeState = new ServerUncoreCounterState[m->getNumSockets()];
    AfterState = new ServerUncoreCounterState[m->getNumSockets()];

    mainLoop([&]()
    {
        collect_beforestate_uncore(m);
        collect_beforestate_core(m);

	MySleepMs(delay_ms);

        collect_afterstate_uncore(m);
        collect_afterstate_core(m);

	store_latency_uncore(m, enable_pmm, delay_ms);// 0 for DDR
	store_latency_core(m);

        print_all_stats(m, enable_pmm, enable_verbose);
        std::cout << std::flush;
        return true;
    });

    delete[] BeforeState;
    delete[] AfterState;
}

void print_usage()
{
    cerr << "\nUsage: \n";
    cerr << " -h | --help | /h          => print this help and exit\n";
    cerr << " --PMM | -pmm              => to enable PMM (Default DDR uncore latency)\n";
    cerr << " -i[=number] | /i[=number] => allow to determine number of iterations\n";
    cerr << " -v | --verbose            => verbose Output\n";
    cerr << "\n";
}

int main(int argc, char * argv[])
{
    set_signal_handlers();
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n This utility measures Latency information\n\n";
    bool enable_pmm = false;
    bool enable_verbose = false;
    int delay_ms = 1000;
    MainLoop mainLoop;
    if(argc > 1) do
    {
        argv++;
        argc--;

        if (strncmp(*argv, "--help", 6) == 0 ||
                                strncmp(*argv, "-h", 2) == 0 ||
                                strncmp(*argv, "/h", 2) == 0)
        {
            print_usage();
            exit(EXIT_FAILURE);
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else if (strncmp(*argv, "--PMM",6) == 0 || strncmp(*argv, "-pmm", 5) == 0)
        {
            argv++;
            argc--;
            enable_pmm = true;
            continue;
        }

        else if (strncmp(*argv, "--verbose", 9) == 0 ||
                             strncmp(*argv, "-v", 2) == 0 ||
                             strncmp(*argv, "/v", 2) == 0)
        {
            argv++;
            argc--;
            enable_verbose = true;
            continue;
        }
    } while(argc > 1);

    PCM::ExtendedCustomCoreEventDescription conf;
    PCM * m = PCM::getInstance();

    build_registers(m, conf, enable_pmm, enable_verbose);
    collect_data(m, enable_pmm, enable_verbose, delay_ms, mainLoop);

    exit(EXIT_SUCCESS);
}
