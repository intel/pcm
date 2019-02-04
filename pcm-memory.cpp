/*

   Copyright (c) 2009-2018, Intel Corporation
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

//Programmable iMC counter
#define READ 0
#define WRITE 1
#define READ_RANK_A 0
#define WRITE_RANK_A 1
#define READ_RANK_B 2
#define WRITE_RANK_B 3
#define PARTIAL 2
#define PMM_READ 2
#define PMM_WRITE 3
#define NM_HIT 0  // NM :  Near Memory (DRAM cache) in Memory Mode
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration

#define DEFAULT_DISPLAY_COLUMNS 2

using namespace std;

const uint32 max_sockets = 256;
const uint32 max_imc_channels = ServerUncorePowerState::maxChannels;
const uint32 max_edc_channels = ServerUncorePowerState::maxChannels;
const uint32 max_imc_controllers = ServerUncorePowerState::maxControllers;

typedef struct memdata {
    float iMC_Rd_socket_chan[max_sockets][max_imc_channels];
    float iMC_Wr_socket_chan[max_sockets][max_imc_channels];
    float iMC_PMM_Rd_socket_chan[max_sockets][max_imc_channels];
    float iMC_PMM_Wr_socket_chan[max_sockets][max_imc_channels];
    float iMC_Rd_socket[max_sockets];
    float iMC_Wr_socket[max_sockets];
    float iMC_PMM_Rd_socket[max_sockets];
    float iMC_PMM_Wr_socket[max_sockets];
    float M2M_NM_read_hit_rate[max_sockets][max_imc_controllers];
    float EDC_Rd_socket_chan[max_sockets][max_edc_channels];
    float EDC_Wr_socket_chan[max_sockets][max_edc_channels];
    float EDC_Rd_socket[max_sockets];
    float EDC_Wr_socket[max_sockets];
    uint64 partial_write[max_sockets];
    bool PMM;
} memdata_t;

void print_help(const string prog_name)
{
    cerr << endl << " Usage: " << endl << " " << prog_name
         << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help  | /h               => print this help and exit" << endl;
    cerr << "  -rank=X | /rank=X                  => monitor DIMM rank X. At most 2 out of 8 total ranks can be monitored simultaneously." << endl;
    cerr << "  -pmm                               => monitor PMM memory bandwidth (instead of partial writes)." << endl;
    cerr << "  -nc   | --nochannel | /nc          => suppress output for individual channels." << endl;
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
         << "                                        to a file, in case filename is provided" << endl;
    cerr << "  -columns=X | /columns=X            => Number of columns to display the NUMA Nodes, defaults to 2." << endl;
#ifdef _MSC_VER
    cerr << "  --uninstallDriver | --installDriver=> (un)install driver" << endl;
#endif
    cerr << " Examples:" << endl;
    cerr << "  " << prog_name << " 1                  => print counters every second without core and socket output" << endl;
    cerr << "  " << prog_name << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format" << endl;
    cerr << "  " << prog_name << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output" << endl;
    cerr << endl;
}

void printSocketBWHeader(uint32 no_columns, uint32 skt, const bool show_channel_output)
{
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--             Socket "<<setw(2)<<i<<"             --|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << endl;
    if (show_channel_output) {
       for (uint32 i=skt; i<(no_columns+skt); ++i) {
           cout << "|--     Memory Channel Monitoring     --|";
       }
       cout << endl;
       for (uint32 i=skt; i<(no_columns+skt); ++i) {
           cout << "|---------------------------------------|";
       }
       cout << endl;
    }
}

void printSocketRankBWHeader(uint32 no_columns, uint32 skt)
{
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--               Socket "<<setw(2)<<i<<"               --|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|--           DIMM Rank Monitoring        --|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|-------------------------------------------|";
    }
    cout << endl;
}

void printSocketChannelBW(PCM *m, memdata_t *md, uint32 no_columns, uint32 skt)
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
            cout << "|-- Mem Ch "<<setw(2)<<channel<<": Reads (MB/s): "<<setw(8)<<md->iMC_Rd_socket_chan[i][channel]<<" --|";
        }
        cout << endl;
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|--            Writes(MB/s): "<<setw(8)<<md->iMC_Wr_socket_chan[i][channel]<<" --|";
        }
        cout << endl;
        if(md->PMM)
        {
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|--      PMM Reads(MB/s)   : "<<setw(8)<<md->iMC_PMM_Rd_socket_chan[i][channel]<<" --|";
            }
            cout << endl;
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|--      PMM Writes(MB/s)  : "<<setw(8)<<md->iMC_PMM_Wr_socket_chan[i][channel]<<" --|";
            }
            cout << endl;
        }
    }
}

void printSocketChannelBW(uint32 no_columns, uint32 skt, uint32 num_imc_channels, const ServerUncorePowerState * uncState1, const ServerUncorePowerState * uncState2, uint64 elapsedTime, int rankA, int rankB)
{
    for (uint32 channel = 0; channel < num_imc_channels; ++channel) {
        if(rankA >= 0) {
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|-- Mem Ch "<<setw(2)<<channel<<" R " << setw(1) << rankA <<": Reads (MB/s): "<<setw(8)<<(float) (getMCCounter(channel,READ_RANK_A,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0))<<" --|";
          }
          cout << endl;
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|--                Writes(MB/s): "<<setw(8)<<(float) (getMCCounter(channel,WRITE_RANK_A,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0))<<" --|";
          }
          cout << endl;
        }
        if(rankB >= 0) {
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|-- Mem Ch "<<setw(2) << channel<<" R " << setw(1) << rankB <<": Reads (MB/s): "<<setw(8)<<(float) (getMCCounter(channel,READ_RANK_B,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0))<<" --|";
          }
          cout << endl;
          for (uint32 i=skt; i<(skt+no_columns); ++i) {
              cout << "|--                Writes(MB/s): "<<setw(8)<<(float) (getMCCounter(channel,WRITE_RANK_B,uncState1[i],uncState2[i]) * 64 / 1000000.0 / (elapsedTime/1000.0))<<" --|";
          }
          cout << endl;
        }
    }
}

void printSocketBWFooter(uint32 no_columns, uint32 skt, const memdata_t *md)
{
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE"<<setw(2)<<i<<" Mem Read (MB/s) : "<<setw(8)<<md->iMC_Rd_socket[i]<<" --|";
    }
    cout << endl;
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE"<<setw(2)<<i<<" Mem Write(MB/s) : "<<setw(8)<<md->iMC_Wr_socket[i]<<" --|";
    }
    cout << endl;
    if (md->PMM)
    {
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE"<<setw(2)<<i<<" PMM Read (MB/s):  "<<setw(8)<<md->iMC_PMM_Rd_socket[i]<<" --|";
        }
        cout << endl;
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE"<<setw(2)<<i<<" PMM Write(MB/s):  "<<setw(8)<<md->iMC_PMM_Wr_socket[i]<<" --|";
        }
        cout << endl;
        for (uint32 ctrl = 0; ctrl < max_imc_controllers; ++ctrl)
        {
            for (uint32 i=skt; i<(skt+no_columns); ++i) {
                cout << "|-- NODE"<<setw(2)<<i<<"."<<ctrl<<" NM read hit rate :"<<setw(6)<<md->M2M_NM_read_hit_rate[i][ctrl]<<" --|";
            }
            cout << endl;
        }
    }
    else
    {
        for (uint32 i=skt; i<(skt+no_columns); ++i) {
            cout << "|-- NODE"<<setw(2)<<i<<" P. Write (T/s): "<<dec<<setw(10)<<md->partial_write[i]<<" --|";
        }
        cout << endl;
    }
    for (uint32 i=skt; i<(skt+no_columns); ++i) {
        cout << "|-- NODE"<<setw(2)<<i<<" Memory (MB/s): "<<setw(11)<<std::right<<(md->iMC_Rd_socket[i]+md->iMC_Wr_socket[i]+
              md->iMC_PMM_Rd_socket[i]+md->iMC_PMM_Wr_socket[i])<<" --|";
    }
    cout << endl;
    for (uint32 i=skt; i<(no_columns+skt); ++i) {
        cout << "|---------------------------------------|";
    }
    cout << endl;
}

void display_bandwidth(PCM *m, memdata_t *md, uint32 no_columns, const bool show_channel_output)
{
    float sysReadDRAM = 0.0, sysWriteDRAM = 0.0, sysReadPMM = 0.0, sysWritePMM = 0.0;
    uint32 numSockets = m->getNumSockets();
    uint32 skt = 0;
    cout.setf(ios::fixed);
    cout.precision(2);

    while(skt < numSockets)
    {
        // Full row
        if ( (skt+no_columns) <= numSockets )
        {
            printSocketBWHeader (no_columns, skt, show_channel_output);
	    if (show_channel_output)
                printSocketChannelBW(m, md, no_columns, skt);
            printSocketBWFooter (no_columns, skt, md);
            for (uint32 i=skt; i<(skt+no_columns); i++) {
                sysReadDRAM += md->iMC_Rd_socket[i];
                sysWriteDRAM += md->iMC_Wr_socket[i];
                sysReadPMM += md->iMC_PMM_Rd_socket[i];
                sysWritePMM += md->iMC_PMM_Wr_socket[i];
            }
            skt += no_columns;
        }
        else //Display one socket in this row
        {
            if (m->MCDRAMmemoryTrafficMetricsAvailable())
            {
                cout << "\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r|--                              Processor socket " << skt << "                            --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r|--       DDR4 Channel Monitoring     --||--      MCDRAM Channel Monitoring    --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r";
                uint32 max_channels = max_imc_channels <= max_edc_channels ? max_edc_channels : max_imc_channels;
                if (show_channel_output) {
	   float iMC_Rd, iMC_Wr, EDC_Rd, EDC_Wr;
                   for(uint64 channel = 0; channel < max_channels; ++channel)
                   {
                    if (channel <= max_imc_channels) {
                        iMC_Rd = md->iMC_Rd_socket_chan[skt][channel];
                        iMC_Wr = md->iMC_Wr_socket_chan[skt][channel];
		    }
		    else
		    {
		    	iMC_Rd = -1.0;
		    	iMC_Wr = -1.0;
		    }
		    if (channel <= max_edc_channels) {
                        EDC_Rd = md->EDC_Rd_socket_chan[skt][channel];
                        EDC_Wr = md->EDC_Wr_socket_chan[skt][channel];
		    }
		    else
		    {
		    	EDC_Rd = -1.0;
		    	EDC_Rd = -1.0;
		    }

		    if (iMC_Rd >= 0.0 && iMC_Wr >= 0.0 && EDC_Rd >= 0.0 && EDC_Wr >= 0.0)
		    	cout << "|-- DDR4 Ch " << channel <<": Reads (MB/s):" << setw(9)  << iMC_Rd
		    	     << " --||-- EDC Ch " << channel <<": Reads (MB/s):" << setw(10)  << EDC_Rd
		    	     << " --|\n|--            Writes(MB/s):" << setw(9) << iMC_Wr
		    	     << " --||--           Writes(MB/s):" << setw(10)  << EDC_Wr
		    	     <<" --|\n";
		    else if ((iMC_Rd < 0.0 || iMC_Wr < 0.0) && EDC_Rd >= 0.0 && EDC_Wr >= 0.0)
		    	cout << "|--                                  "
		    	     << " --||-- EDC Ch " << channel <<": Reads (MB/s):" << setw(10)  << EDC_Rd
		    	     << " --|\n|--                                  "
		    	     << " --||--           Writes(MB/s):" << setw(10)  << EDC_Wr
		    	     <<" --|\n";

		    else if (iMC_Rd >= 0.0 && iMC_Wr >= 0.0 && (EDC_Rd < 0.0 || EDC_Wr < 0.0))
		    	cout << "|-- DDR4 Ch " << channel <<": Reads (MB/s):" << setw(9)  << iMC_Rd
		    	     << " --||--                                  "
		    	     << " --|\n|--            Writes(MB/s):" << setw(9) << iMC_Wr
		    	     << " --||--                                  "
		    	     <<" --|\n";
		    else
		    	continue;
	   }
                }
                cout << "\
                    \r|-- DDR4 Mem Read  (MB/s):"<<setw(11)<<md->iMC_Rd_socket[skt]<<" --||-- MCDRAM Read (MB/s):"<<setw(14)<<md->EDC_Rd_socket[skt]<<" --|\n\
                    \r|-- DDR4 Mem Write (MB/s):"<<setw(11)<<md->iMC_Wr_socket[skt]<<" --||-- MCDRAM Write(MB/s):"<<setw(14)<<md->EDC_Wr_socket[skt]<<" --|\n\
                    \r|-- DDR4 Memory (MB/s)   :"<<setw(11)<<md->iMC_Rd_socket[skt]+md->iMC_Wr_socket[skt]<<" --||-- MCDRAM (MB/s)     :"<<setw(14)<<md->EDC_Rd_socket[skt]+md->EDC_Wr_socket[skt]<<" --|\n\
                    \r|---------------------------------------||---------------------------------------|\n\
                    \r";

                sysReadDRAM  += (md->iMC_Rd_socket[skt]+md->EDC_Rd_socket[skt]);
                sysWriteDRAM += (md->iMC_Wr_socket[skt]+md->EDC_Wr_socket[skt]);
                skt += 1;
            }
	    else
	    {
                cout << "\
                    \r|---------------------------------------|\n\
                    \r|--             Socket "<<skt<<"              --|\n\
                    \r|---------------------------------------|\n";
                if (show_channel_output) {
	  cout << "\
                    \r|--     Memory Channel Monitoring     --|\n\
                    \r|---------------------------------------|\n\
                    \r"; 
                  for(uint64 channel = 0; channel < max_imc_channels; ++channel)
                  {
                    if(md->iMC_Rd_socket_chan[skt][channel] < 0.0 && md->iMC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
                        continue;
                    cout << "|--  Mem Ch " << channel <<": Reads (MB/s):" << setw(8)  << md->iMC_Rd_socket_chan[skt][channel]
                        <<"  --|\n|--            Writes(MB/s):" << setw(8) << md->iMC_Wr_socket_chan[skt][channel]
                        <<"  --|\n";
                    if (md->PMM)
                    {
                        cout << "|--      PMM Reads (MB/s):" << setw(8) << md->iMC_PMM_Rd_socket_chan[skt][channel] << "  --|\n";
                        cout << "|--      PMM Writes(MB/s):" << setw(8) << md->iMC_PMM_Wr_socket_chan[skt][channel] << "  --|\n";
                    }
                  }
	}
                cout << "\
                    \r|-- NODE"<<skt<<" Mem Read (MB/s)  :"<<setw(8)<<md->iMC_Rd_socket[skt]<<"  --|\n\
                    \r|-- NODE"<<skt<<" Mem Write (MB/s) :"<<setw(8)<<md->iMC_Wr_socket[skt]<<"  --|\n";
                if(md->PMM)
                {
                    cout << "\
                        \r|-- NODE"<<skt<<" PMM Read (MB/s):"<<setw(8)<<md->iMC_PMM_Rd_socket[skt]<<"  --|\n\
                        \r|-- NODE"<<skt<<" PMM Write(MB/s):"<<setw(8)<<md->iMC_PMM_Wr_socket[skt]<<"  --|\n";
                    for (uint32 ctrl = 0; ctrl < max_imc_controllers; ++ctrl)
                    {
                        cout << "\r|-- NODE"<<setw(2)<<skt<<"."<<ctrl<<" NM read hit rate :"<<setw(6)<<md->M2M_NM_read_hit_rate[skt][ctrl]<<" --|\n";
                    }
                }
                else
                {
                    cout <<
                       "\r|-- NODE"<<skt<<" P. Write (T/s) :"<<setw(10)<<dec<<md->partial_write[skt]<<"  --|\n";
                }
                cout <<
                   "\r|-- NODE"<<skt<<" Memory (MB/s): "<<setw(8)<<md->iMC_Rd_socket[skt]+md->iMC_Wr_socket[skt]+
                    md->iMC_PMM_Rd_socket[skt]+md->iMC_PMM_Wr_socket[skt]<<"     --|\n\
                    \r|---------------------------------------|\n\
                    \r";

                sysReadDRAM += md->iMC_Rd_socket[skt];
                sysWriteDRAM += md->iMC_Wr_socket[skt];
                sysReadPMM += md->iMC_PMM_Rd_socket[skt];
                sysWritePMM += md->iMC_PMM_Wr_socket[skt];
                skt += 1;
            }
        }
    }
    {
        cout << "\
            \r|---------------------------------------||---------------------------------------|\n";
	if(md->PMM)
           cout << "\
            \r|--            System DRAM Read Throughput(MB/s):"<<setw(14)<<sysReadDRAM<<"                --|\n\
            \r|--           System DRAM Write Throughput(MB/s):"<<setw(14)<<sysWriteDRAM<<"                --|\n\
            \r|--             System PMM Read Throughput(MB/s):"<<setw(14)<<sysReadPMM<<"                --|\n\
            \r|--            System PMM Write Throughput(MB/s):"<<setw(14)<<sysWritePMM<<"                --|\n";
        cout << "\
            \r|--                 System Read Throughput(MB/s):"<<setw(14)<<sysReadDRAM+sysReadPMM<<"                --|\n\
            \r|--                System Write Throughput(MB/s):"<<setw(14)<<sysWriteDRAM+sysWritePMM<<"                --|\n\
            \r|--               System Memory Throughput(MB/s):"<<setw(14)<<sysReadDRAM+sysReadPMM+sysWriteDRAM+sysWritePMM<<"                --|\n\
            \r|---------------------------------------||---------------------------------------|" << endl;
    }
}

void display_bandwidth_csv_header(PCM *m, memdata_t *md, const bool show_channel_output)
{
    uint32 numSockets = m->getNumSockets();
    cout << ";;" ; // Time

    for (uint32 skt=0; skt < numSockets; ++skt)
    {
      if (show_channel_output) {
         for(uint64 channel = 0; channel < max_imc_channels; ++channel)
         {
	     if(md->iMC_Rd_socket_chan[skt][channel] < 0.0 && md->iMC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	        continue;
	     cout << "SKT" << skt << ";SKT" << skt << ';';
             if (md->PMM)
             {
                 cout << "SKT" << skt << ";SKT" << skt << ';';
             }
         }
      }
      cout << "SKT"<<skt<<";"
	   << "SKT"<<skt<<";"
	   << "SKT"<<skt<<";";
      if (m->getCPUModel() != PCM::KNL) {
          if (md->PMM)
	      cout << "SKT"<<skt<<";" << "SKT"<<skt<<";";
          else
              cout << "SKT"<<skt<<";";
      }

      if (m->MCDRAMmemoryTrafficMetricsAvailable()) {
	  if (show_channel_output) {
             for(uint64 channel = 0; channel < max_edc_channels; ++channel)
             {
	         if(md->EDC_Rd_socket_chan[skt][channel] < 0.0 && md->EDC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	             continue;
	         cout << "SKT" << skt << ";SKT" << skt << ';';
	     }
	  }
          cout << "SKT"<<skt<<";"
               << "SKT"<<skt<<";"
	       << "SKT"<<skt<<";";
      }

    }
    if (md->PMM)
        cout << "System;System;System;System;";
    cout << "System;System;System\n";

    cout << "Date;Time;" ;
    for (uint32 skt=0; skt < numSockets; ++skt)
    {
      if (show_channel_output) {
         for(uint64 channel = 0; channel < max_imc_channels; ++channel)
         {
	     if(md->iMC_Rd_socket_chan[skt][channel] < 0.0 && md->iMC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	         continue;
	     cout << "Ch" <<channel <<"Read;"
	          << "Ch" <<channel <<"Write;";
             if(md->PMM)
             {
                 cout << "Ch" <<channel <<"PMM_Read;"
                      << "Ch" <<channel <<"PMM_Write;";
             }
	 }
      }
      if (m->getCPUModel() == PCM::KNL)
          cout << "DDR4 Read (MB/s); DDR4 Write (MB/s); DDR4 Memory (MB/s);";
      else
      {
          if(md->PMM)
              cout << "Mem Read (MB/s);Mem Write (MB/s); PMM_Read; PMM_Write; Memory (MB/s);";
          else
              cout << "Mem Read (MB/s);Mem Write (MB/s); P. Write (T/s); Memory (MB/s);";
      }

      if (m->MCDRAMmemoryTrafficMetricsAvailable()) {
         if (show_channel_output) {
            for(uint64 channel = 0; channel < max_edc_channels; ++channel)
            {
	         if(md->EDC_Rd_socket_chan[skt][channel] < 0.0 && md->EDC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	             continue;
	         cout << "EDC_Ch" <<channel <<"Read;"
	              << "EDC_Ch" <<channel <<"Write;";
	       }
	 }
         cout << "MCDRAM Read (MB/s); MCDRAM Write (MB/s); MCDRAM (MB/s);";
      }
    }

    if (md->PMM)
	cout << "DRAMRead;DRAMWrite;PMMREAD;PMMWrite;";
    cout << "Read;Write;Memory" << endl;
}

void display_bandwidth_csv(PCM *m, memdata_t *md, uint64 elapsedTime, const bool show_channel_output)
{
    uint32 numSockets = m->getNumSockets();
    tm tt = pcm_localtime();
    cout.precision(3);
    cout << 1900+tt.tm_year << '-' << 1+tt.tm_mon << '-' << tt.tm_mday << ';'
         << tt.tm_hour << ':' << tt.tm_min << ':' << tt.tm_sec << ';';


    float sysReadDRAM = 0.0, sysWriteDRAM = 0.0, sysReadPMM = 0.0, sysWritePMM = 0.0;

    cout.setf(ios::fixed);
    cout.precision(2);

    for (uint32 skt=0; skt < numSockets; ++skt)
    {
	if (show_channel_output) {
           for(uint64 channel = 0; channel < max_imc_channels; ++channel)
           {
	      if(md->iMC_Rd_socket_chan[skt][channel] < 0.0 && md->iMC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	         continue;
	      cout <<setw(8) << md->iMC_Rd_socket_chan[skt][channel] << ';'
	           <<setw(8) << md->iMC_Wr_socket_chan[skt][channel] << ';';
              if(md->PMM)
              {
                  cout <<setw(8) << md->iMC_PMM_Rd_socket_chan[skt][channel] << ';'
                       <<setw(8) << md->iMC_PMM_Wr_socket_chan[skt][channel] << ';';
              }
	   }
	 }
         cout <<setw(8) << md->iMC_Rd_socket[skt] <<';'
	      <<setw(8) << md->iMC_Wr_socket[skt] <<';';
         if(md->PMM)
         {
             cout <<setw(8) << md->iMC_PMM_Rd_socket[skt] <<';'
                  <<setw(8) << md->iMC_PMM_Wr_socket[skt] <<';';
         }
	 if (m->getCPUModel() != PCM::KNL)
         {
             if (!md->PMM)
             {
                 cout <<setw(10) << dec << md->partial_write[skt] <<';';
             }
         }
         cout << setw(8) << md->iMC_Rd_socket[skt]+md->iMC_Wr_socket[skt] <<';';

	 sysReadDRAM += md->iMC_Rd_socket[skt];
         sysWriteDRAM += md->iMC_Wr_socket[skt];
         sysReadPMM += md->iMC_PMM_Rd_socket[skt];
         sysWritePMM += md->iMC_PMM_Wr_socket[skt];

	 if (m->MCDRAMmemoryTrafficMetricsAvailable()) {
            if (show_channel_output) {
	       for(uint64 channel = 0; channel < max_edc_channels; ++channel)
	       {
                  if(md->EDC_Rd_socket_chan[skt][channel] < 0.0 && md->EDC_Wr_socket_chan[skt][channel] < 0.0) //If the channel read neg. value, the channel is not working; skip it.
	               continue;
                  cout <<setw(8) << md->EDC_Rd_socket_chan[skt][channel] << ';'
	               <<setw(8) << md->EDC_Wr_socket_chan[skt][channel] << ';';
	
	       }
	    }
             cout <<setw(8) << md->EDC_Rd_socket[skt] <<';'
	          <<setw(8) << md->EDC_Wr_socket[skt] <<';'
                  <<setw(8) << md->EDC_Rd_socket[skt]+md->EDC_Wr_socket[skt] <<';';

             sysReadDRAM += md->EDC_Rd_socket[skt];
             sysWriteDRAM += md->EDC_Wr_socket[skt];
	 }
    }

    if (md->PMM)
        cout <<setw(10) <<sysReadDRAM <<';'
             <<setw(10) <<sysWriteDRAM <<';'
             <<setw(10) <<sysReadPMM <<';'
             <<setw(10) <<sysWritePMM <<';';

    cout <<setw(10) <<sysReadDRAM+sysReadPMM <<';'
	 <<setw(10) <<sysWriteDRAM+sysWritePMM <<';'
	 <<setw(10) <<sysReadDRAM+sysReadPMM+sysWriteDRAM+sysWritePMM << endl;
}

void calculate_bandwidth(PCM *m, const ServerUncorePowerState uncState1[], const ServerUncorePowerState uncState2[], uint64 elapsedTime, bool csv, bool & csvheader, uint32 no_columns, bool PMM, const bool show_channel_output)
{
    //const uint32 num_imc_channels = m->getMCChannelsPerSocket();
    //const uint32 num_edc_channels = m->getEDCChannelsPerSocket();
    memdata_t md;
    md.PMM = PMM;

    for(uint32 skt = 0; skt < m->getNumSockets(); ++skt)
    {
        md.iMC_Rd_socket[skt] = 0.0;
        md.iMC_Wr_socket[skt] = 0.0;
        md.iMC_PMM_Rd_socket[skt] = 0.0;
        md.iMC_PMM_Wr_socket[skt] = 0.0;
        md.EDC_Rd_socket[skt] = 0.0;
        md.EDC_Wr_socket[skt] = 0.0;
        md.partial_write[skt] = 0;
        for(uint32 i=0; i < max_imc_controllers; ++i)
        {
            md.M2M_NM_read_hit_rate[skt][i] = 0.;
        }
        const uint32 numChannels1 = m->getMCChannels(skt, 0); // number of channels in the first controller

	switch(m->getCPUModel()) {
	case PCM::KNL:
            for(uint32 channel = 0; channel < max_edc_channels; ++channel)
            {
                if(getEDCCounter(channel,READ,uncState1[skt],uncState2[skt]) == 0.0 && getEDCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) == 0.0)
                {
                    md.EDC_Rd_socket_chan[skt][channel] = -1.0;
                    md.EDC_Wr_socket_chan[skt][channel] = -1.0;
                    continue;
                }

                md.EDC_Rd_socket_chan[skt][channel] = (float) (getEDCCounter(channel,READ,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
                md.EDC_Wr_socket_chan[skt][channel] = (float) (getEDCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

                md.EDC_Rd_socket[skt] += md.EDC_Rd_socket_chan[skt][channel];
                md.EDC_Wr_socket[skt] += md.EDC_Wr_socket_chan[skt][channel];
	    }
        default:
            for(uint32 channel = 0; channel < max_imc_channels; ++channel)
            {
                if(getMCCounter(channel,READ,uncState1[skt],uncState2[skt]) == 0.0 && getMCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) == 0.0) //In case of JKT-EN, there are only three channels. Skip one and continue.
                {
                    if (!PMM || (getMCCounter(channel,PMM_READ,uncState1[skt],uncState2[skt]) == 0.0 && getMCCounter(channel,PMM_WRITE,uncState1[skt],uncState2[skt]) == 0.0))
                    {
                        md.iMC_Rd_socket_chan[skt][channel] = -1.0;
                        md.iMC_Wr_socket_chan[skt][channel] = -1.0;
                        continue;
                    }
                }

                md.iMC_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,READ,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
                md.iMC_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,WRITE,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

                md.iMC_Rd_socket[skt] += md.iMC_Rd_socket_chan[skt][channel];
                md.iMC_Wr_socket[skt] += md.iMC_Wr_socket_chan[skt][channel];

                if(PMM)
                {
                    md.iMC_PMM_Rd_socket_chan[skt][channel] = (float) (getMCCounter(channel,PMM_READ,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));
                    md.iMC_PMM_Wr_socket_chan[skt][channel] = (float) (getMCCounter(channel,PMM_WRITE,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0));

                    md.iMC_PMM_Rd_socket[skt] += md.iMC_PMM_Rd_socket_chan[skt][channel];
                    md.iMC_PMM_Wr_socket[skt] += md.iMC_PMM_Wr_socket_chan[skt][channel];

                    md.M2M_NM_read_hit_rate[skt][(channel < numChannels1)?0:1] += (float)getMCCounter(channel,READ,uncState1[skt],uncState2[skt]);
                }
                else
                {
                    md.partial_write[skt] += (uint64) (getMCCounter(channel,PARTIAL,uncState1[skt],uncState2[skt]) / (elapsedTime/1000.0));
                }
            }
	}
        if (PMM)
        {
            for(uint32 c = 0; c < max_imc_controllers; ++c)
            {
                if(md.M2M_NM_read_hit_rate[skt][c] != 0.0)
                {
                    md.M2M_NM_read_hit_rate[skt][c] = ((float)getM2MCounter(c, NM_HIT, uncState1[skt],uncState2[skt]))/ md.M2M_NM_read_hit_rate[skt][c];
                }
            }
        }
    }

    if (csv) {
      if (csvheader) {
	display_bandwidth_csv_header(m, &md, show_channel_output);
	csvheader = false;
      }
      display_bandwidth_csv(m, &md, elapsedTime, show_channel_output);
    } else {
      display_bandwidth(m, &md, no_columns, show_channel_output);
    }
}

void calculate_bandwidth(PCM *m, const ServerUncorePowerState uncState1[], const ServerUncorePowerState uncState2[], uint64 elapsedTime, bool csv, bool & csvheader, uint32 no_columns, int rankA, int rankB)
{
    uint32 skt = 0;
    cout.setf(ios::fixed);
    cout.precision(2);
    uint32 numSockets = m->getNumSockets();

    while(skt < numSockets)
    {
        // Full row
        if ( (skt+no_columns) <= numSockets )
        {
            printSocketRankBWHeader(no_columns, skt);
            printSocketChannelBW(no_columns, skt, max_imc_channels, uncState1, uncState2, elapsedTime, rankA, rankB);
            for (uint32 i=skt; i<(no_columns+skt); ++i) {
              cout << "|-------------------------------------------|";
            }
            cout << endl;
            skt += no_columns;
        }
        else //Display one socket in this row
        {
            cout << "\
                \r|-------------------------------------------|\n\
                \r|--               Socket "<<skt<<"                --|\n\
                \r|-------------------------------------------|\n\
                \r|--           DIMM Rank Monitoring        --|\n\
                \r|-------------------------------------------|\n\
                \r";
            for(uint32 channel = 0; channel < max_imc_channels; ++channel)
            {
                if(rankA >=0)
                  cout << "|-- Mem Ch "
                      << setw(2) << channel
                      << " R " << setw(1) << rankA
                      <<": Reads (MB/s):"
                      <<setw(8)
                      <<(float) (getMCCounter(channel,READ_RANK_A,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0))
                      <<"  --|\n|--                Writes(MB/s):"
                      <<setw(8)
                      <<(float) (getMCCounter(channel,WRITE_RANK_A,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0))
                      <<"  --|\n";
                if(rankB >=0)
                  cout << "|-- Mem Ch "
                      << setw(2) << channel
                      << " R " << setw(1) << rankB
                      <<": Reads (MB/s):"
                      <<setw(8)
                      <<(float) (getMCCounter(channel,READ_RANK_B,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0))
                      <<"  --|\n|--                Writes(MB/s):"
                      <<setw(8)
                      <<(float) (getMCCounter(channel,WRITE_RANK_B,uncState1[skt],uncState2[skt]) * 64 / 1000000.0 / (elapsedTime/1000.0))
                      <<"  --|\n";
            }
            cout << "\
                \r|-------------------------------------------|\n\
                \r";
	
            skt += 1;
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

#ifdef _MSC_VER
    TCHAR driverPath[1040]; // length for current directory + "\\msr.sys"
    GetCurrentDirectory(1024, driverPath);
    wcscat_s(driverPath, 1040, L"\\msr.sys");
#endif

    cerr << endl;
    cerr << " Processor Counter Monitor: Memory Bandwidth Monitoring Utility " << PCM_VERSION << endl;
    cerr << endl;
    
    cerr << " This utility measures memory bandwidth per channel or per DIMM rank in real-time" << endl;
    cerr << endl;

    double delay = -1.0;
    bool csv = false, csvheader=false, show_channel_output=true;
    uint32 no_columns = DEFAULT_DISPLAY_COLUMNS; // Default number of columns is 2
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
#ifndef _MSC_VER
    long diff_usec = 0; // deviation of clock is useconds between measurements
    int calibrated = PCM_CALIBRATION_INTERVAL - 2; // keeps track is the clock calibration needed
#endif
    int rankA = -1, rankB = -1;
    bool PMM = false;
    unsigned int numberOfIterations = 0; // number of iterations

    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    if(argc > 1) do
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
                  std::cerr << "At most two DIMM ranks can be monitored "<< std::endl;
                  exit(EXIT_FAILURE);
                }
                else
                {
                  if(rank > 7) {
                      std::cerr << "Invalid rank number "<<rank << std::endl;
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

        if (strncmp(*argv, "-pmm", 6) == 0 ||
            strncmp(*argv, "/pmm", 6) == 0)
        {
            PMM = true;
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
            if(is_str_stream.eof() && !is_str_stream.fail()) {
                delay = delay_input;
            } else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it." << endl;
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
        std::cerr << "Unsupported processor model (" << m->getCPUModel() << ")." << std::endl;
        if (m->memoryTrafficMetricsAvailable())
            cerr << "For processor-level memory bandwidth statistics please use pcm.x" << endl;
        exit(EXIT_FAILURE);
    }
    if(PMM && (m->PMMTrafficMetricsAvailable() == false))
    {
        cerr << "PMM traffic metrics are not available on your processor." << endl;
        exit(EXIT_FAILURE);
    }
    if((rankA >= 0 || rankB >= 0) && PMM)
    {
        cerr << "PMM traffic metrics are not available on rank level" << endl;
        exit(EXIT_FAILURE);
    }
    if((rankA >= 0 || rankB >= 0) && !show_channel_output)
    {
        cerr << "Rank level output requires channel output" << endl;
        exit(EXIT_FAILURE);
    }
    PCM::ErrorCode status = m->programServerUncoreMemoryMetrics(rankA, rankB, PMM);
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

    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to "<<max_sockets<<" sockets are supported! Program aborted" << endl;
        exit(EXIT_FAILURE);
    }

    ServerUncorePowerState * BeforeState = new ServerUncorePowerState[m->getNumSockets()];
    ServerUncorePowerState * AfterState = new ServerUncorePowerState[m->getNumSockets()];
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

    cerr << "Update every "<<delay<<" seconds"<< endl;

    for(uint32 i=0; i<m->getNumSockets(); ++i)
        BeforeState[i] = m->getServerUncorePowerState(i); 

    BeforeTime = m->getTickCount();

    if( sysCmd != NULL ) {
        MySystem(sysCmd, sysArgv);
    }

    unsigned int i = 1;

    while ((i <= numberOfIterations) || (numberOfIterations == 0))
    {
        if(!csv) cout << std::flush;
        int delay_ms = int(delay * 1000);
        int calibrated_delay_ms = delay_ms;
#ifdef _MSC_VER
        // compensate slow Windows console output
        if(AfterTime) delay_ms -= (int)(m->getTickCount() - BeforeTime);
        if(delay_ms < 0) delay_ms = 0;
#else
        // compensation of delay on Linux/UNIX
        // to make the samling interval as monotone as possible
        struct timeval start_ts, end_ts;
        if(calibrated == 0) {
            gettimeofday(&end_ts, NULL);
            diff_usec = (end_ts.tv_sec-start_ts.tv_sec)*1000000.0+(end_ts.tv_usec-start_ts.tv_usec);
            calibrated_delay_ms = delay_ms - diff_usec/1000.0;
        }
#endif

        MySleepMs(calibrated_delay_ms);

#ifndef _MSC_VER
        calibrated = (calibrated + 1) % PCM_CALIBRATION_INTERVAL;
        if(calibrated == 0) {
            gettimeofday(&start_ts, NULL);
        }
#endif

        AfterTime = m->getTickCount();
        for(uint32 i=0; i<m->getNumSockets(); ++i)
            AfterState[i] = m->getServerUncorePowerState(i);

	if (!csv) {
	  //cout << "Time elapsed: "<<dec<<fixed<<AfterTime-BeforeTime<<" ms\n";
	  //cout << "Called sleep function for "<<dec<<fixed<<delay_ms<<" ms\n";
	}

        if(rankA >= 0 || rankB >= 0)
          calculate_bandwidth(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader, no_columns, rankA, rankB);
        else
          calculate_bandwidth(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader, no_columns, PMM, show_channel_output);

        swap(BeforeTime, AfterTime);
        swap(BeforeState, AfterState);

        if ( m->isBlocked() ) {
        // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
	++i;
    }

    delete[] BeforeState;
    delete[] AfterState;

    exit(EXIT_SUCCESS);
}
