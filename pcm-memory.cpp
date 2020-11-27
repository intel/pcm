/*

   Copyright (c) 2009-2020, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Patrick Lu
// increased max sockets to 256 - Thomas Willhalm


/*!     \file pcm-memory.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for memory controller channels and DIMMs (ranks) + PMM memory traffic
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

#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs

#define DEFAULT_DISPLAY_COLUMNS 2

using namespace std;
using namespace pcm;

const uint32 max_sockets = 256;
uint32 max_imc_channels = ServerUncoreCounterState::maxChannels;
const uint32 max_edc_channels = ServerUncoreCounterState::maxChannels;
const uint32 max_imc_controllers = ServerUncoreCounterState::maxControllers;

typedef struct memdata {
    float iMC_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels];
    float iMC_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels];
    float iMC_PMM_Rd_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels];
    float iMC_PMM_Wr_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels];
    float iMC_PMM_MemoryMode_Miss_socket_chan[max_sockets][ServerUncoreCounterState::maxChannels];
    float iMC_Rd_socket[max_sockets];
    float iMC_Wr_socket[max_sockets];
    float iMC_PMM_Rd_socket[max_sockets];
    float iMC_PMM_Wr_socket[max_sockets];
    float iMC_PMM_MemoryMode_Miss_socket[max_sockets];
    float M2M_NM_read_hit_rate[max_sockets][max_imc_controllers];
    float EDC_Rd_socket_chan[max_sockets][max_edc_channels];
    float EDC_Wr_socket_chan[max_sockets][max_edc_channels];
    float EDC_Rd_socket[max_sockets];
    float EDC_Wr_socket[max_sockets];
    uint64 partial_write[max_sockets];
    bool PMM, PMMMixedMode;
} memdata_t;

bool skipInactiveChannels = true;

void print_help(const string prog_name)
{
    cerr << "\n Usage: \n " << prog_name
         << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cerr << "   <delay>                           => time interval to sample performance counters.\n";
    cerr << "                                        If not specified, or 0, with external program given\n";
    cerr << "                                        will read counters only after external program finishes\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help  | /h               => print this help and exit\n";
    cerr << "  -rank=X | /rank=X                  => monitor DIMM rank X. At most 2 out of 8 total ranks can be monitored simultaneously.\n";
    cerr << "  -pmm | /pmm | -pmem | /pmem        => monitor PMM memory bandwidth and DRAM cache hit rate in Memory Mode (default on systems with PMM support).\n";
    cerr << "  -mixed                             => monitor PMM mixed mode (AppDirect + Memory Mode).\n";
    cerr << "  -partial                           => monitor monitor partial writes instead of PMM (default on systems without PMM support).\n";
    cerr << "  -nc   | --nochannel | /nc          => suppress output for individual channels.\n";
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cerr << "  -columns=X | /columns=X            => Number of columns to display the NUMA Nodes, defaults to 2.\n";
    cerr << "  -all | /all                        => Display all channels (even with no traffic)\n";
    cerr << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
#ifdef _MSC_VER
    cerr << "  --uninstallDriver | --installDriver=> (un)install driver\n";
#endif
    cerr << " Examples:\n";
    cerr << "  " << prog_name << " 1                  => print counters every second without core and socket output\n";
    cerr << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << prog_name << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
}

void printSocketBWHeader(uint32 no_columns, uint32 skt, const bool show_channel_output)
{
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--             Socket " << setw(2) << i << "             --|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << "\n";
    if (show_channel_output) {
       for (uint32 i=skt; i<(no_columns+skt); ++i) {
           cout << "|--     Memory Channel Monitoring     --|";
       }
       cout << "\n";
       for (uint32 i=skt; i<(no_columns+skt); ++i) {
           cout << "|---------------------------------------|";
       }
       cout << "\n";
    }
}

void printSocketRankBWHeader(uint32 no_columns, uint32 skt)
{
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--               Socket " << setw(2) << i << "               --|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--           DIMM Rank Monitoring        --|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << "\n";
}

void printSocketChannelBW(PCM */*m*/, memdata_t *md, uint32 no_columns, uint32 skt)
{
    for (uint32 channel = 0; channel < max_imc_channels; ++channel) {
        // check all the sockets for bad channel "channel"
        unsigned bad_channels = 0;
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            if (md->iMC_Rd_socket_chan[i][channel] < 0.0 || md->iMC_Wr_socket_chan[i][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                ++bad_channels;
        }
        if (bad_channels == no_columns) { // the channel is missing on all sockets in the row
            continue;
        }
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- Mem Ch " << setw(2) << channel << ": Reads (MB/s): " << setw(8) << md->iMC_Rd_socket_chan[i][channel] << " --|";
        }
        cout << "\n";
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|--            Writes(MB/s): " << setw(8) << md->iMC_Wr_socket_chan[i][channel] << " --|";
        }
        cout << "\n";
        if(md->PMM)
        {
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|--      PMM Reads(MB/s)   : " << setw(8) << md->iMC_PMM_Rd_socket_chan[i][channel] << " --|";
            }
            cout << "\n";
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|--      PMM Writes(MB/s)  : " << setw(8) << md->iMC_PMM_Wr_socket_chan[i][channel] << " --|";
            }
            cout << "\n";
        }
    }
}

void printSocketChannelBW(uint32 no_columns, uint32 skt, uint32 num_imc_channels, const ServerUncoreCounterState * uncState1, const ServerUncoreCounterState * uncState2, uint64 elapsedTime, int rankA, int rankB)
{
    for (uint32 channel = 0; channel < num_imc_channels; ++channel) {
        if(rankA >= 0) {
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|-- Mem Ch " << setw(2) << channel << " R " << setw(1) << rankA << ": Reads (MB/s): " << setw(8) << (float) (getMCCounter(channel,ServerPCICFGUncore::EventPosition::READ_RANK_A,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0)) << " --|";
          }
          cout << "\n";
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|--                Writes(MB/s): " << setw(8) << (float) (getMCCounter(channel,ServerPCICFGUncore::EventPosition::WRITE_RANK_A,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0)) << " --|";
          }
          cout << "\n";
        }
        if(rankB >= 0) {
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|-- Mem Ch " << setw(2) << channel << " R " << setw(1) << rankB << ": Reads (MB/s): " << setw(8) << (float) (getMCCounter(channel,ServerPCICFGUncore::EventPosition::READ_RANK_B,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0)) << " --|";
          }
          cout << "\n";
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|--                Writes(MB/s): " << setw(8) << (float) (getMCCounter(channel,ServerPCICFGUncore::EventPosition::WRITE_RANK_B,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0)) << " --|";
          }
          cout << "\n";
        }
    }
}

float AD_BW(const memdata_t *md, const uint32 skt)
{
    const auto totalPMM = md->iMC_PMM_Rd_socket[skt] + md->iMC_PMM_Wr_socket[skt];
    return (max)(totalPMM - md->iMC_PMM_MemoryMode_Miss_socket[skt], float(0.0));
}

float PMM_MM_Ratio(const memdata_t *md, const uint32 skt)
{
    const auto dram = md->iMC_Rd_socket[skt] + md->iMC_Wr_socket[skt];
    return md->iMC_PMM_MemoryMode_Miss_socket[skt] / dram;
}

void printSocketBWFooter(uint32 no_columns, uint32 skt, const memdata_t *md)
{
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE" << setw(2) << i << " Mem Read (MB/s) : " << setw(8) << md->iMC_Rd_socket[i] << " --|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE" << setw(2) << i << " Mem Write(MB/s) : " << setw(8) << md->iMC_Wr_socket[i] << " --|";
    }
    cout << "\n";
    if (md->PMM || md->PMMMixedMode)
    {
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE" << setw(2) << i << " PMM Read (MB/s):  " << setw(8) << md->iMC_PMM_Rd_socket[i] << " --|";
        }
        cout << "\n";
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE" << setw(2) << i << " PMM Write(MB/s):  " << setw(8) << md->iMC_PMM_Wr_socket[i] << " --|";
        }
        cout << "\n";
    }
    if (md->PMMMixedMode)
    {
        for (uint32 i = skt; i < (skt + no_columns); ++i)
        {
            cout << "|-- NODE" << setw(2) << i << " PMM AD Bw(MB/s):  " << setw(8) << AD_BW(md, i) << " --|";
        }
        cout << "\n";
        for (uint32 i = skt; i < (skt + no_columns); ++i)
        {
            cout << "|-- NODE" << setw(2) << i << " PMM MM Bw(MB/s):  " << setw(8) << md->iMC_PMM_MemoryMode_Miss_socket[i] << " --|";
        }
        cout << "\n";
        for (uint32 i = skt; i < (skt + no_columns); ++i)
        {
            cout << "|-- NODE" << setw(2) << i << " PMM MM Bw/DRAM Bw:" << setw(8) << PMM_MM_Ratio(md, i) << " --|";
        }
        cout << "\n";
    }
    if (md->PMM)
    {
        for (uint32 ctrl = 0; ctrl < max_imc_controllers; ++ctrl)
        {
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|-- NODE" << setw(2) << i << "." << ctrl << " NM read hit rate :" << setw(6) << md->M2M_NM_read_hit_rate[i][ctrl] << " --|";
            }
            cout << "\n";
        }
    }
    if (md->PMM == false && md->PMMMixedMode == false)
    {
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE" << setw(2) << i << " P. Write (T/s): " << dec << setw(10) << md->partial_write[i] << " --|";
        }
        cout << "\n";
    }
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE" << setw(2) << i << " Memory (MB/s): " << setw(11) << right << (md->iMC_Rd_socket[i]+md->iMC_Wr_socket[i]+
              md->iMC_PMM_Rd_socket[i]+md->iMC_PMM_Wr_socket[i]) << " --|";
    }
    cout << "\n";
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << "\n";
}

void display_bandwidth(PCM *m, memdata_t *md, const uint32 no_columns, const bool show_channel_output)
{
    float sysReadDRAM = 0.0, sysWriteDRAM = 0.0, sysReadPMM = 0.0, sysWritePMM = 0.0;
    uint32 numSockets = m->getNumSockets();
    uint32 skt = 0;
    cout.setf(ios::fixed);
    cout.precision(2);

    while (skt < numSockets)
    {
        auto printRow = [&skt,&show_channel_output,&m,&md,&sysReadDRAM,&sysWriteDRAM, &sysReadPMM, &sysWritePMM](const uint32 no_columns)
        {
            printSocketBWHeader(no_columns, skt, show_channel_output);
            if (show_channel_output)
                printSocketChannelBW(m, md, no_columns, skt);
            printSocketBWFooter(no_columns, skt, md);
            for (uint32 i = skt; i < (skt + no_columns); i++)
            {
                sysReadDRAM += md->iMC_Rd_socket[i];
                sysWriteDRAM += md->iMC_Wr_socket[i];
                sysReadPMM += md->iMC_PMM_Rd_socket[i];
                sysWritePMM += md->iMC_PMM_Wr_socket[i];
            }
            skt += no_columns;
        };
        // Full row
        if ((skt + no_columns) <= numSockets)
        {
            printRow(no_columns);
        }
        else //Display the remaining sockets in this row
        {
            if (m->MCDRAMmemoryTrafficMetricsAvailable() == false)
            {
                printRow(numSockets - skt);
            }
            else
            {
                cout << "\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r|--                              Processor socket "
                     << skt << "                            --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r|--       DDR4 Channel Monitoring     --||--      MCDRAM Channel Monitoring    --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r";
                uint32 max_channels = max_imc_channels <= max_edc_channels ? max_edc_channels : max_imc_channels;
                if (show_channel_output)
                {
                    float iMC_Rd, iMC_Wr, EDC_Rd, EDC_Wr;
                    for (uint64 channel = 0; channel < max_channels; ++channel)
                    {
                        if (channel <= max_imc_channels)
                        {
                            iMC_Rd = md->iMC_Rd_socket_chan[skt][channel];
                            iMC_Wr = md->iMC_Wr_socket_chan[skt][channel];
                        }
                        else
                        {
                            iMC_Rd = -1.0;
                            iMC_Wr = -1.0;
                        }
                        if (channel <= max_edc_channels)
                        {
                            EDC_Rd = md->EDC_Rd_socket_chan[skt][channel];
                            EDC_Wr = md->EDC_Wr_socket_chan[skt][channel];
                        }
                        else
                        {
                            EDC_Rd = -1.0;
                            EDC_Rd = -1.0;
                        }

                        if (iMC_Rd >= 0.0 && iMC_Wr >= 0.0 && EDC_Rd >= 0.0 && EDC_Wr >= 0.0)
                            cout << "|-- DDR4 Ch " << channel << ": Reads (MB/s):" << setw(9) << iMC_Rd
                                 << " --||-- EDC Ch " << channel << ": Reads (MB/s):" << setw(10) << EDC_Rd
                                 << " --|\n|--            Writes(MB/s):" << setw(9) << iMC_Wr
                                 << " --||--           Writes(MB/s):" << setw(10) << EDC_Wr
                                 << " --|\n";
                        else if ((iMC_Rd < 0.0 || iMC_Wr < 0.0) && EDC_Rd >= 0.0 && EDC_Wr >= 0.0)
                            cout << "|--                                  "
                                 << " --||-- EDC Ch " << channel << ": Reads (MB/s):" << setw(10) << EDC_Rd
                                 << " --|\n|--                                  "
                                 << " --||--           Writes(MB/s):" << setw(10) << EDC_Wr
                                 << " --|\n";

                        else if (iMC_Rd >= 0.0 && iMC_Wr >= 0.0 && (EDC_Rd < 0.0 || EDC_Wr < 0.0))
                            cout << "|-- DDR4 Ch " << channel << ": Reads (MB/s):" << setw(9) << iMC_Rd
                                 << " --||--                                  "
                                 << " --|\n|--            Writes(MB/s):" << setw(9) << iMC_Wr
                                 << " --||--                                  "
                                 << " --|\n";
                        else
                            continue;
                    }
                }
                cout << "\
                    \r|-- DDR4 Mem Read  (MB/s):"
                     << setw(11) << md->iMC_Rd_socket[skt] << " --||-- MCDRAM Read (MB/s):" << setw(14) << md->EDC_Rd_socket[skt] << " --|\n\
                    \r|-- DDR4 Mem Write (MB/s):"
                     << setw(11) << md->iMC_Wr_socket[skt] << " --||-- MCDRAM Write(MB/s):" << setw(14) << md->EDC_Wr_socket[skt] << " --|\n\
                    \r|-- DDR4 Memory (MB/s)   :"
                     << setw(11) << md->iMC_Rd_socket[skt] + md->iMC_Wr_socket[skt] << " --||-- MCDRAM (MB/s)     :" << setw(14) << md->EDC_Rd_socket[skt] + md->EDC_Wr_socket[skt] << " --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r";

                sysReadDRAM += (md->iMC_Rd_socket[skt] + md->EDC_Rd_socket[skt]);
                sysWriteDRAM += (md->iMC_Wr_socket[skt] + md->EDC_Wr_socket[skt]);
                skt += 1;
            }
        }
    }
    {
        cout << "\
            \r|---------------------------------------||---------------------------------------|\n";
        if(md->PMM || md->PMMMixedMode)
            cout << "\
            \r|--            System DRAM Read Throughput(MB/s):" << setw(14) << sysReadDRAM <<                                     "                --|\n\
            \r|--           System DRAM Write Throughput(MB/s):" << setw(14) << sysWriteDRAM <<                                    "                --|\n\
            \r|--             System PMM Read Throughput(MB/s):" << setw(14) << sysReadPMM <<                                      "                --|\n\
            \r|--            System PMM Write Throughput(MB/s):" << setw(14) << sysWritePMM <<                                     "                --|\n";
        cout << "\
            \r|--                 System Read Throughput(MB/s):" << setw(14) << sysReadDRAM+sysReadPMM <<                          "                --|\n\
            \r|--                System Write Throughput(MB/s):" << setw(14) << sysWriteDRAM+sysWritePMM <<                        "                --|\n\
            \r|--               System Memory Throughput(MB/s):" << setw(14) << sysReadDRAM+sysReadPMM+sysWriteDRAM+sysWritePMM << "                --|\n\
            \r|---------------------------------------||---------------------------------------|\n";
    }
}

void display_bandwidth_csv(PCM *m, memdata_t *md, uint64 /*elapsedTime*/, const bool show_channel_output, const CsvOutputType outputType)
{
    const uint32 numSockets = m->getNumSockets();
    printDateForCSV(outputType);

    float sysReadDRAM = 0.0, sysWriteDRAM = 0.0, sysReadPMM = 0.0, sysWritePMM = 0.0;

    for (uint32 skt = 0; skt < numSockets; ++skt)
    {
        auto printSKT = [skt](int c = 1) {
            for (int i = 0; i < c; ++i)
                cout << "SKT" << skt << ',';
        };
        if (show_channel_output)
        {
            for (uint64 channel = 0; channel < max_imc_channels; ++channel)
            {
                if (md->iMC_Rd_socket_chan[skt][channel] < 0.0 && md->iMC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                    continue;

                choose(outputType,
                       [printSKT]() {
                           printSKT(2);
                       },
                       [&channel]() {
                           cout << "Ch" << channel << "Read,"
                                << "Ch" << channel << "Write,";
                       },
                       [&md, &skt, &channel]() {
                           cout << setw(8) << md->iMC_Rd_socket_chan[skt][channel] << ','
                                << setw(8) << md->iMC_Wr_socket_chan[skt][channel] << ',';
                       });

                if (md->PMM)
                {
                    choose(outputType,
                           [printSKT]() {
                               printSKT(2);
                           },
                           [&channel]() {
                               cout << "Ch" << channel << "PMM_Read,"
                                    << "Ch" << channel << "PMM_Write,";
                           },
                           [&skt, &md, &channel]() {
                               cout << setw(8) << md->iMC_PMM_Rd_socket_chan[skt][channel] << ','
                                    << setw(8) << md->iMC_PMM_Wr_socket_chan[skt][channel] << ',';
                           });
                }
            }
        }
        choose(outputType,
               [printSKT]() {
                   printSKT(2);
               },
               []() {
                   cout << "Mem Read (MB/s),Mem Write (MB/s),";
               },
               [&md, &skt]() {
                   cout << setw(8) << md->iMC_Rd_socket[skt] << ','
                        << setw(8) << md->iMC_Wr_socket[skt] << ',';
               });

        if (md->PMM || md->PMMMixedMode)
        {
            choose(outputType,
                   [printSKT]() {
                       printSKT(2);
                   },
                   []() {
                       cout << "PMM_Read (MB/s), PMM_Write (MB/s),";
                   },
                   [&md, &skt]() {
                       cout << setw(8) << md->iMC_PMM_Rd_socket[skt] << ','
                            << setw(8) << md->iMC_PMM_Wr_socket[skt] << ',';
                   });
        }
        if (md->PMM)
        {
            for (uint32 c = 0; c < max_imc_controllers; ++c)
            {
                choose(outputType,
                    [printSKT]() {
                        printSKT();
                    },
                    [c]() {
                        cout << "iMC" << c << " NM read hit rate,";
                    },
                    [&md, &skt, c]() {
                        cout << setw(8) << md->M2M_NM_read_hit_rate[skt][c] << ',';
                    });
            }
        }
        if (md->PMMMixedMode)
        {
            choose(outputType,
                   [printSKT]() {
                       printSKT(3);
                   },
                   []() {
                       cout << "PMM_AD (MB/s), PMM_MM (MB/s), PMM_MM_Bw/DRAM_Bw,";
                   },
                   [&md, &skt]() {
                       cout << setw(8) << AD_BW(md, skt) << ','
                            << setw(8) << md->iMC_PMM_MemoryMode_Miss_socket[skt] << ','
                            << setw(8) << PMM_MM_Ratio(md, skt) << ',';
                   });
        }
        if (m->getCPUModel() != PCM::KNL)
        {
            if (md->PMM == false && md->PMMMixedMode == false)
            {
                choose(outputType,
                       [printSKT]() {
                           printSKT();
                       },
                       []() {
                           cout << "P. Write (T/s),";
                       },
                       [&md, &skt]() {
                           cout << setw(10) << dec << md->partial_write[skt] << ',';
                       });
            }
        }

        choose(outputType,
               [printSKT]() {
                   printSKT();
               },
               []() {
                   cout << "Memory (MB/s),";
               },
               [&]() {
                   cout << setw(8) << md->iMC_Rd_socket[skt] + md->iMC_Wr_socket[skt] << ',';

                   sysReadDRAM += md->iMC_Rd_socket[skt];
                   sysWriteDRAM += md->iMC_Wr_socket[skt];
                   sysReadPMM += md->iMC_PMM_Rd_socket[skt];
                   sysWritePMM += md->iMC_PMM_Wr_socket[skt];
               });

        if (m->MCDRAMmemoryTrafficMetricsAvailable())
        {
            if (show_channel_output)
            {
                for (uint64 channel = 0; channel < max_edc_channels; ++channel)
                {
                    if (md->EDC_Rd_socket_chan[skt][channel] < 0.0 && md->EDC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                        continue;

                    choose(outputType,
                           [printSKT]() {
                               printSKT(2);
                           },
                           [&channel]() {
                               cout << "EDC_Ch" << channel << "Read,"
                                    << "EDC_Ch" << channel << "Write,";
                           },
                           [&md, &skt, &channel]() {
                               cout << setw(8) << md->EDC_Rd_socket_chan[skt][channel] << ','
                                    << setw(8) << md->EDC_Wr_socket_chan[skt][channel] << ',';
                           });
                }
            }

            choose(outputType,
                   [printSKT]() {
                       printSKT(3);
                   },
                   []() {
                       cout << "MCDRAM Read (MB/s), MCDRAM Write (MB/s), MCDRAM (MB/s),";
                   },
                   [&]() {
                       cout << setw(8) << md->EDC_Rd_socket[skt] << ','
                            << setw(8) << md->EDC_Wr_socket[skt] << ','
                            << setw(8) << md->EDC_Rd_socket[skt] + md->EDC_Wr_socket[skt] << ',';

                       sysReadDRAM += md->EDC_Rd_socket[skt];
                       sysWriteDRAM += md->EDC_Wr_socket[skt];
                   });
        }
    }

    if (md->PMM || md->PMMMixedMode)
    {
        choose(outputType,
               []() {
                   cout << "System,System,System,System,";
               },
               []() {
                   cout << "DRAMRead,DRAMWrite,PMMREAD,PMMWrite,";
               },
               [&]() {
                   cout << setw(10) << sysReadDRAM << ','
                        << setw(10) << sysWriteDRAM << ','
                        << setw(10) << sysReadPMM << ','
                        << setw(10) << sysWritePMM << ',';
               });
    }

    choose(outputType,
           []() {
               cout << "System,System,System\n";
           },
           []() {
               cout << "Read,Write,Memory\n";
           },
           [&]() {
               cout << setw(10) << sysReadDRAM + sysReadPMM << ','
                    << setw(10) << sysWriteDRAM + sysWritePMM << ','
                    << setw(10) << sysReadDRAM + sysReadPMM + sysWriteDRAM + sysWritePMM << "\n";
           });
}

void calculate_bandwidth(PCM *m, const ServerUncoreCounterState uncState1[], const ServerUncoreCounterState uncState2[], const uint64 elapsedTime, const bool csv, bool & csvheader, uint32 no_columns, const bool PMM, const bool show_channel_output, const bool PMMMixedMode)
{
    //const uint32 num_imc_channels = m->getMCChannelsPerSocket();
    //const uint32 num_edc_channels = m->getEDCChannelsPerSocket();
    memdata_t md;
    md.PMM = PMM;
    md.PMMMixedMode = PMMMixedMode;

    for(uint32 skt = 0; skt < m->getNumSockets(); ++skt)
    {
        md.iMC_Rd_socket[skt] = 0.0;
        md.iMC_Wr_socket[skt] = 0.0;
        md.iMC_PMM_Rd_socket[skt] = 0.0;
        md.iMC_PMM_Wr_socket[skt] = 0.0;
        md.iMC_PMM_MemoryMode_Miss_socket[skt] = 0.0;
        md.EDC_Rd_socket[skt] = 0.0;
        md.EDC_Wr_socket[skt] = 0.0;
        md.partial_write[skt] = 0;
		for (uint32 i = 0; i < max_imc_controllers; ++i)
		{
			md.M2M_NM_read_hit_rate[skt][i] = 0.;
		}
		const uint32 numChannels1 = (uint32)m->getMCChannels(skt, 0); // number of channels in the first controller

		auto toBW = [&elapsedTime](const uint64 nEvents)
		{
			return (float)(nEvents * 64 / 1000000.0 / (elapsedTime / 1000.0));
		};

        switch (m->getCPUModel())
        {
        case PCM::KNL:
            for (uint32 channel = 0; channel < max_edc_channels; ++channel)
            {
                if (skipInactiveChannels && getEDCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]) == 0.0 && getEDCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]) == 0.0)
                {
                    md.EDC_Rd_socket_chan[skt][channel] = -1.0;
                    md.EDC_Wr_socket_chan[skt][channel] = -1.0;
                    continue;
                }

                md.EDC_Rd_socket_chan[skt][channel] = toBW(getEDCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]));
                md.EDC_Wr_socket_chan[skt][channel] = toBW(getEDCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]));

                md.EDC_Rd_socket[skt] += md.EDC_Rd_socket_chan[skt][channel];
                md.EDC_Wr_socket[skt] += md.EDC_Wr_socket_chan[skt][channel];
            }
            /* fall-through */
        default:
            for (uint32 channel = 0; channel < max_imc_channels; ++channel)
            {
                uint64 reads = 0, writes = 0, pmmReads = 0, pmmWrites = 0, pmmMemoryModeCleanMisses = 0, pmmMemoryModeDirtyMisses = 0;
                reads = getMCCounter(channel, ServerPCICFGUncore::EventPosition::READ, uncState1[skt], uncState2[skt]);
                writes = getMCCounter(channel, ServerPCICFGUncore::EventPosition::WRITE, uncState1[skt], uncState2[skt]);
                if (PMM)
                {
                    pmmReads = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_READ, uncState1[skt], uncState2[skt]);
                    pmmWrites = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_WRITE, uncState1[skt], uncState2[skt]);
                }
                if (PMMMixedMode)
                {
                    pmmMemoryModeCleanMisses = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_MM_MISS_CLEAN, uncState1[skt], uncState2[skt]);
                    pmmMemoryModeDirtyMisses = getMCCounter(channel, ServerPCICFGUncore::EventPosition::PMM_MM_MISS_DIRTY, uncState1[skt], uncState2[skt]);
                }
                if (skipInactiveChannels && (reads + writes == 0))
                {
                    if ((PMM == false) || (pmmReads + pmmWrites == 0))
                    {
                        if ((PMMMixedMode == false) || (pmmMemoryModeCleanMisses + pmmMemoryModeDirtyMisses == 0))
                        {

                            md.iMC_Rd_socket_chan[skt][channel] = -1.0;
                            md.iMC_Wr_socket_chan[skt][channel] = -1.0;
                            continue;
                        }
                    }
                }

                md.iMC_Rd_socket_chan[skt][channel] = toBW(reads);
                md.iMC_Wr_socket_chan[skt][channel] = toBW(writes);

                md.iMC_Rd_socket[skt] += md.iMC_Rd_socket_chan[skt][channel];
                md.iMC_Wr_socket[skt] += md.iMC_Wr_socket_chan[skt][channel];

                if (PMM)
                {
                    md.iMC_PMM_Rd_socket_chan[skt][channel] = toBW(pmmReads);
                    md.iMC_PMM_Wr_socket_chan[skt][channel] = toBW(pmmWrites);

                    md.iMC_PMM_Rd_socket[skt] += md.iMC_PMM_Rd_socket_chan[skt][channel];
                    md.iMC_PMM_Wr_socket[skt] += md.iMC_PMM_Wr_socket_chan[skt][channel];

                    md.M2M_NM_read_hit_rate[skt][(channel < numChannels1) ? 0 : 1] += (float)reads;
                }
                else if (PMMMixedMode)
                {
                    md.iMC_PMM_MemoryMode_Miss_socket_chan[skt][channel] = toBW(pmmMemoryModeCleanMisses + 2 * pmmMemoryModeDirtyMisses);
                    md.iMC_PMM_MemoryMode_Miss_socket[skt] += md.iMC_PMM_MemoryMode_Miss_socket_chan[skt][channel];
                }
                else
                {
                    md.partial_write[skt] += (uint64)(getMCCounter(channel, ServerPCICFGUncore::EventPosition::PARTIAL, uncState1[skt], uncState2[skt]) / (elapsedTime / 1000.0));
                }
            }
        }
        if (PMMMixedMode)
        {
            for(uint32 c = 0; c < max_imc_controllers; ++c)
            {
                md.iMC_PMM_Rd_socket[skt] += toBW(getM2MCounter(c, ServerPCICFGUncore::EventPosition::PMM_READ, uncState1[skt],uncState2[skt]));
                md.iMC_PMM_Wr_socket[skt] += toBW(getM2MCounter(c, ServerPCICFGUncore::EventPosition::PMM_WRITE, uncState1[skt],uncState2[skt]));;
            }
        }
        if (PMM)
        {
            for(uint32 c = 0; c < max_imc_controllers; ++c)
            {
                if(md.M2M_NM_read_hit_rate[skt][c] != 0.0)
                {
                    md.M2M_NM_read_hit_rate[skt][c] = ((float)getM2MCounter(c, ServerPCICFGUncore::EventPosition::NM_HIT, uncState1[skt],uncState2[skt]))/ md.M2M_NM_read_hit_rate[skt][c];
                }
            }
        }
    }

    if (csv)
    {
        if (csvheader)
        {
            display_bandwidth_csv(m, &md, elapsedTime, show_channel_output, Header1);
            display_bandwidth_csv(m, &md, elapsedTime, show_channel_output, Header2);
            csvheader = false;
        }
        display_bandwidth_csv(m, &md, elapsedTime, show_channel_output, Data);
    }
    else
    {
        display_bandwidth(m, &md, no_columns, show_channel_output);
    }
}

void calculate_bandwidth_rank(PCM *m, const ServerUncoreCounterState uncState1[], const ServerUncoreCounterState uncState2[], const uint64 elapsedTime, const bool /*csv*/, bool & /*csvheader*/, const uint32 no_columns, const int rankA, const int rankB)
{
    uint32 skt = 0;
    cout.setf(ios::fixed);
    cout.precision(2);
    uint32 numSockets = m->getNumSockets();

    while(skt < numSockets)
    {
        auto printRow = [&skt, &uncState1, &uncState2, &elapsedTime, &rankA, &rankB](const uint32 no_columns) {
            printSocketRankBWHeader(no_columns, skt);
            printSocketChannelBW(no_columns, skt, max_imc_channels, uncState1, uncState2, elapsedTime, rankA, rankB);
            for (uint32 i = skt; i < (no_columns + skt); ++i)
            {
                cout << "|-------------------------------------------|";
            }
            cout << "\n";
            skt += no_columns;
        };
        // Full row
        if ((skt + no_columns) <= numSockets)
        {
            printRow(no_columns);
        }
        else //Display the remaining sockets in this row
        {
            printRow(numSockets - skt);
        }
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
    cerr << " Processor Counter Monitor: Memory Bandwidth Monitoring Utility " << PCM_VERSION << "\n";
    cerr << "\n";

    cerr << " This utility measures memory bandwidth per channel or per DIMM rank in real-time\n";
    cerr << "\n";

    double delay = -1.0;
    bool csv = false, csvheader=false, show_channel_output=true;
    uint32 no_columns = DEFAULT_DISPLAY_COLUMNS; // Default number of columns is 2
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
    int rankA = -1, rankB = -1;
    MainLoop mainLoop;

    string program = string(argv[0]);

    PCM * m = PCM::getInstance();
    bool PMM = m->PMMTrafficMetricsAvailable();
    bool PMMMixedMode = false;

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
        if (strncmp(*argv, "-csv",4) == 0 ||
            strncmp(*argv, "/csv",4) == 0)
        {
            csv = true;
			csvheader = true;
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
        if (strncmp(*argv, "-columns", 8) == 0 ||
            strncmp(*argv, "/columns", 8) == 0)
        {
            string cmd = string(*argv);
            size_t found = cmd.find('=',2);
            if (found != string::npos) {
                no_columns = atoi(cmd.substr(found+1).c_str());
                if (no_columns == 0)
                    no_columns = DEFAULT_DISPLAY_COLUMNS;
                if (no_columns > m->getNumSockets())
                    no_columns = m->getNumSockets();
            }
            continue;
        }
        if (strncmp(*argv, "-rank", 5) == 0 ||
            strncmp(*argv, "/rank", 5) == 0)
        {
            string cmd = string(*argv);
            size_t found = cmd.find('=',2);
            if (found != string::npos) {
                int rank = atoi(cmd.substr(found+1).c_str());
                if (rankA >= 0 && rankB >= 0)
                {
                  cerr << "At most two DIMM ranks can be monitored \n";
                  exit(EXIT_FAILURE);
                }
                else
                {
                  if(rank > 7) {
                      cerr << "Invalid rank number " << rank << "\n";
                      exit(EXIT_FAILURE);
                  }
                  if(rankA < 0) rankA = rank;
                  else if(rankB < 0) rankB = rank;
                }
            }
            continue;
        }
        if (strncmp(*argv, "--nochannel", 11) == 0 ||
            strncmp(*argv, "-nc", 3) == 0 ||
            strncmp(*argv, "/nc", 3) == 0)
        {
            show_channel_output = false;
            continue;
        }

        if (strncmp(*argv, "-pmm", 4) == 0 ||
            strncmp(*argv, "/pmm", 4) == 0 ||
            strncmp(*argv, "-pmem", 5) == 0 ||
            strncmp(*argv, "/pmem", 5) == 0 )
        {
            PMM = true;
            continue;
        }

        if (strncmp(*argv, "-all", 4) == 0 ||
            strncmp(*argv, "/all", 4) == 0)
        {
            skipInactiveChannels = false;
            continue;
        }

        if (strncmp(*argv, "-mixed", 6) == 0 ||
            strncmp(*argv, "/mixed", 6) == 0)
        {
            PMMMixedMode = true;
            continue;
        }

        if (strncmp(*argv, "-partial", 8) == 0 ||
            strncmp(*argv, "/partial", 8) == 0)
        {
            PMM = false;
            PMMMixedMode =false;
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
            istringstream is_str_stream(*argv);
            is_str_stream >> noskipws >> delay_input;
            if(is_str_stream.eof() && !is_str_stream.fail()) {
                delay = delay_input;
            } else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                print_help(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while(argc > 1); // end of command line partsing loop

    m->disableJKTWorkaround();
    print_cpu_details();
    if (!m->hasPCICFGUncore())
    {
        cerr << "Unsupported processor model (" << m->getCPUModel() << ").\n";
        if (m->memoryTrafficMetricsAvailable())
            cerr << "For processor-level memory bandwidth statistics please use pcm.x\n";
        exit(EXIT_FAILURE);
    }
    if ((PMM || PMMMixedMode) && (m->PMMTrafficMetricsAvailable() == false))
    {
        cerr << "PMM traffic metrics are not available on your processor.\n";
        exit(EXIT_FAILURE);
    }
    if((rankA >= 0 || rankB >= 0) && PMM)
    {
        cerr << "PMM traffic metrics are not available on rank level\n";
        exit(EXIT_FAILURE);
    }
    if((rankA >= 0 || rankB >= 0) && !show_channel_output)
    {
        cerr << "Rank level output requires channel output\n";
        exit(EXIT_FAILURE);
    }
    PCM::ErrorCode status = m->programServerUncoreMemoryMetrics(rankA, rankB, PMM || PMMMixedMode, PMMMixedMode);
    if (PMMMixedMode)
    {
        PMM = false; // to distinguish between PMM and PMMMixedMode later
    }
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

    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to " << max_sockets << " sockets are supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    max_imc_channels = m->getMCChannelsPerSocket();

    ServerUncoreCounterState * BeforeState = new ServerUncoreCounterState[m->getNumSockets()];
    ServerUncoreCounterState * AfterState = new ServerUncoreCounterState[m->getNumSockets()];
    uint64 BeforeTime = 0, AfterTime = 0;

    if ( (sysCmd != NULL) && (delay<=0.0) ) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (csv) {
        if( delay<=0.0 ) delay = PCM_DELAY_DEFAULT;
    } else {
        // for non-CSV mode delay < 1.0 does not make a lot of practical sense:
        // hard to read from the screen, or
        // in case delay is not provided in command line => set default
        if( ((delay<1.0) && (delay>0.0)) || (delay<=0.0) ) delay = PCM_DELAY_DEFAULT;
    }

    cerr << "Update every " << delay << " seconds\n";

    for(uint32 i=0; i<m->getNumSockets(); ++i)
        BeforeState[i] = m->getServerUncoreCounterState(i);

    BeforeTime = m->getTickCount();

    if( sysCmd != NULL ) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
        if(!csv) cout << flush;

        calibratedSleep(delay, sysCmd, mainLoop, m);

        AfterTime = m->getTickCount();
        for(uint32 i=0; i<m->getNumSockets(); ++i)
            AfterState[i] = m->getServerUncoreCounterState(i);

        if (!csv) {
          //cout << "Time elapsed: " << dec << fixed << AfterTime-BeforeTime << " ms\n";
          //cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";
        }

        if(rankA >= 0 || rankB >= 0)
          calculate_bandwidth_rank(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader, no_columns, rankA, rankB);
        else
          calculate_bandwidth(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader, no_columns, PMM, show_channel_output, PMMMixedMode);

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);

        if ( m->isBlocked() ) {
        // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    delete[] BeforeState;
    delete[] AfterState;

    exit(EXIT_SUCCESS);
}
