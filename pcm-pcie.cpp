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


/*!     \file pcm-pcie.cpp
  \brief Example of using uncore CBo counters: implements a performance counter monitoring utility for monitoring PCIe bandwidth
  */
#define HACK_TO_REMOVE_DUPLICATE_ERROR
#include <iostream>
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
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
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration
typedef struct
{
    // PCIe read events (PCI devices reading from memory)
    uint64 PCIeRdCur; // PCIe read current
    uint64 PCIeNSRd;  // PCIe non-snoop read
    // PCIe write events (PCI devices writing to memory)
    uint64 PCIeWiLF;  // PCIe Write (non-allocating)
    uint64 PCIeItoM;  // PCIe Write (allocating)
    uint64 PCIeNSWr;  // PCIe Non-snoop write (partial)
    uint64 PCIeNSWrF; // PCIe Non-snoop write (full)
    // events shared by CPU and IO
    uint64 RFO;       // Demand Data RFO [PCIe write partial cache line]
    uint64 CRd;       // Demand Code Read
    uint64 DRd;       // Demand Data read
    uint64 PRd;       // Partial Reads (UC) [MMIO Read]
    uint64 WiL;       // Write Invalidate Line - partial [MMIO write], PL: Not documented in HSX/IVT
    uint64 ItoM;      // Request Invalidate Line [PCIe write full cache line]
} PCIeEvents_t;

typedef struct
{
    PCIeEvents_t total; 
    PCIeEvents_t miss; 
    PCIeEvents_t hit; 
}sample_t;

PCIeEvents_t aggregate_sample;
uint32 num_events = (sizeof(PCIeEvents_t)/sizeof(uint64));

using namespace std;

const uint32 max_sockets = 4;
void getPCIeEvents(PCM *m, PCM::PCIeEventCode opcode, uint32 delay_ms, sample_t *sample, const uint32 tid=0, const uint32 q=0, const uint32 nc=0);

void print_events()
{
    cerr << " PCIe event definitions (each event counts as a transfer): \n";
    cerr << "   PCIe read events (PCI devices reading from memory - application writes to disk/network/PCIe device):\n";
    cerr << "     PCIePRd   - PCIe UC read transfer (partial cache line)\n";
    cerr << "     PCIeRdCur* - PCIe read current transfer (full cache line)\n";
    cerr << "         On Haswell Server PCIeRdCur counts both full/partial cache lines\n";
    cerr << "     RFO*      - Demand Data RFO\n";
    cerr << "     CRd*      - Demand Code Read\n";
    cerr << "     DRd       - Demand Data Read\n";
    cerr << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cerr << "   PCIe write events (PCI devices writing to memory - application reads from disk/network/PCIe device):\n";
    cerr << "     PCIeWiLF  - PCIe Write transfer (non-allocating) (full cache line)\n";
    cerr << "     PCIeItoM  - PCIe Write transfer (allocating) (full cache line)\n";
    cerr << "     PCIeNSWr  - PCIe Non-snoop write transfer (partial cache line)\n";
    cerr << "     PCIeNSWrF - PCIe Non-snoop write transfer (full cache line)\n";
    cerr << "     ItoM      - PCIe write full cache line\n";
    cerr << "     RFO       - PCIe parial Write\n";
    cerr << "   CPU MMIO events (CPU reading/writing to PCIe devices):\n";
    cerr << "     PRd       - MMIO Read [Haswell Server only] (Partial Cache Line)\n";
    cerr << "     WiL       - MMIO Write (Full/Partial)\n\n";
    cerr << " * - NOTE: Depending on the configuration of your BIOS, this tool may report '0' if the message\n";
    cerr << "           has not been selected.\n\n";
}

void print_usage(const string progname)
{
    cerr << endl << " Usage: " << endl << " " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help  | /h               => print this help and exit" << endl;
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
         << "                                        to a file, in case filename is provided" << endl;
    cerr << "  -B                                 => Estimate PCIe B/W (in Bytes/sec) by multiplying" << endl;
    cerr << "                                        the number of transfers by the cache line size (=64 bytes)." << endl; 
    cerr << " It overestimates the bandwidth under traffic with many partial cache line transfers." << endl;
    cerr << endl;
    print_events();
    cerr << endl;
    cerr << " Examples:" << endl;
    cerr << "  " << progname << " 1                  => print counters every second without core and socket output" << endl;
    cerr << "  " << progname << " 0.5 -csv=test.log  => twice a second save counter values to test.log in CSV format" << endl;
    cerr << "  " << progname << " /csv 5 2>/dev/null => one sampe every 5 seconds, and discard all diagnostic output" << endl;
    cerr << endl;
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
    cerr << " Processor Counter Monitor: PCIe Bandwidth Monitoring Utility "<< endl;
    cerr << " This utility measures PCIe bandwidth in real-time" << endl;
    cerr << endl;
    print_events();

    double delay = -1.0;
    bool csv = false;
    bool print_bandwidth = false;
	bool print_additional_info = false;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;
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
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else
        if (strncmp(*argv, "-csv",4) == 0 ||
            strncmp(*argv, "/csv",4) == 0)
        {
            csv = true;
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
        if (strncmp(*argv, "-B", 2) == 0 ||
            strncmp(*argv, "/b", 2) == 0)
        {
            print_bandwidth = true;
            continue;
        }
		else
		if (strncmp(*argv, "-e", 2) == 0 )
		{
			print_additional_info = true;
			continue;
		}
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
                print_usage(program);
                exit(EXIT_FAILURE);
            }
            continue;
        }
    } while(argc > 1); // end of command line partsing loop

    m->disableJKTWorkaround();
    PCM::ErrorCode status = m->program();
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
    if(!(m->hasPCICFGUncore()))
    {
        cerr << "Jaketown, Ivytown, Haswell, Broadwell-DE Server CPU is required for this tool! Program aborted" << endl;
        exit(EXIT_FAILURE);
    }

    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to "<<max_sockets<<" sockets are supported! Program aborted" << endl;
        exit(EXIT_FAILURE);
    }
  
    if(m->isSomeCoreOfflined())
    {
        cerr << "Core offlining is not supported. Program aborted" << endl;
        exit(EXIT_FAILURE);
    }

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

#define NUM_SAMPLES (1)

    uint32 i;
    uint32 delay_ms = uint32(delay * 1000 / num_events / NUM_SAMPLES);
    if(delay_ms * num_events * NUM_SAMPLES < delay * 1000) ++delay_ms; //Adjust the delay_ms if it's less than delay time
    sample_t sample[max_sockets];
    cerr << "delay_ms: " << delay_ms << endl;
    
    if( sysCmd != NULL ) {
        MySystem(sysCmd, sysArgv);
    }

	// ================================== Begin Printing Output ==================================
	
	
	// additional info case


	if ( print_additional_info == true)
	{

    unsigned int ic = 1;
    while ((ic <= numberOfIterations) || (numberOfIterations == 0))
    {
        MySleepMs(delay_ms);
        memset(sample,0,sizeof(sample));
        memset(&aggregate_sample,0,sizeof(aggregate_sample));
        
        if(!(m->getCPUModel() == PCM::JAKETOWN) && !(m->getCPUModel() == PCM::IVYTOWN))
        {
            for(i=0;i<NUM_SAMPLES;i++)
            {
                if(m->getCPUModel() == PCM::SKX)
                {
                    getPCIeEvents(m, m->SKX_RdCur, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_RFO, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_CRd, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_DRd, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_ItoM, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_PRd, delay_ms, sample, 0, m->IRQ, 1);
                    getPCIeEvents(m, m->SKX_WiL, delay_ms, sample, 0, m->IRQ, 1);
                }
                else
                {
                    getPCIeEvents(m, m->PCIeRdCur, delay_ms, sample);
                    getPCIeEvents(m, m->RFO, delay_ms, sample,m->RFOtid);
                    getPCIeEvents(m, m->CRd, delay_ms, sample);
                    getPCIeEvents(m, m->DRd, delay_ms, sample);
                    getPCIeEvents(m, m->ItoM, delay_ms, sample,m->ItoMtid);
                    getPCIeEvents(m, m->PRd, delay_ms, sample);
                    getPCIeEvents(m, m->WiL, delay_ms, sample);
                }
            }
            
            if(csv)
                if(print_bandwidth)
                    cout << "Skt,PCIeRdCur,RFO,CRd,DRd,ItoM,PRd,WiL,PCIe Rd (B),PCIe Wr (B)\n";
                else
                    cout << "Skt,PCIeRdCur,RFO,CRd,DRd,ItoM,PRd,WiL\n";
            else
                if(print_bandwidth)
                    cout << "Skt | PCIeRdCur |  RFO  |  CRd  |  DRd  |  ItoM  |  PRd  |  WiL  | PCIe Rd (B) | PCIe Wr (B)\n";
                else
                    cout << "Skt | PCIeRdCur |  RFO  |  CRd  |  DRd  |  ItoM  |  PRd  |  WiL\n";

            //report extrapolated read and write PCIe bandwidth per socket using the data from the sample
            for(i=0; i<m->getNumSockets(); ++i)
            {
                if(csv)
                {
                    cout << i;
                    cout << "," << sample[i].total.PCIeRdCur;
                    cout << "," << sample[i].total.RFO;
                    cout << "," << sample[i].total.CRd;
                    cout << "," << sample[i].total.DRd;
                    cout << "," << sample[i].total.ItoM;
                    cout << "," << sample[i].total.PRd;
                    cout << "," << sample[i].total.WiL;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].total.PCIeRdCur + sample[i].total.RFO + sample[i].total.CRd + sample[i].total.DRd)*64ULL);
                        cout << "," << ((sample[i].total.ItoM + sample[i].total.RFO)*64ULL);
                    }
                    cout << "	(Total)\n";
					
					cout << i;
                    cout << "," << sample[i].miss.PCIeRdCur;
                    cout << "," << sample[i].miss.RFO;
                    cout << "," << sample[i].miss.CRd;
                    cout << "," << sample[i].miss.DRd;
                    cout << "," << sample[i].miss.ItoM;
                    cout << "," << sample[i].miss.PRd;
                    cout << "," << sample[i].miss.WiL;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].miss.PCIeRdCur + sample[i].miss.RFO + sample[i].miss.CRd + sample[i].miss.DRd)*64ULL);
                        cout << "," << ((sample[i].miss.ItoM + sample[i].miss.RFO)*64ULL);
                    }
                    cout << "	(Miss)\n";
					
					cout << i;
                    cout << "," << sample[i].hit.PCIeRdCur;
                    cout << "," << sample[i].hit.RFO;
                    cout << "," << sample[i].hit.CRd;
                    cout << "," << sample[i].hit.DRd;
                    cout << "," << sample[i].hit.ItoM;
                    cout << "," << sample[i].hit.PRd;
                    cout << "," << sample[i].hit.WiL;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].hit.PCIeRdCur + sample[i].hit.RFO + sample[i].hit.CRd + sample[i].hit.DRd)*64ULL);
                        cout << "," << ((sample[i].hit.ItoM + sample[i].hit.RFO)*64ULL);
                    }
                    cout << "	(Hit)\n";
					
					
                }
                else
                {
                    cout << " " << i;
                    cout << "    " << unit_format(sample[i].total.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].total.RFO);
                    cout << "  " << unit_format(sample[i].total.CRd);
                    cout << "  " << unit_format(sample[i].total.DRd);
                    cout << "   " << unit_format(sample[i].total.ItoM);
                    cout << "  " << unit_format(sample[i].total.PRd);
                    cout << "  " << unit_format(sample[i].total.WiL);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].total.PCIeRdCur + sample[i].total.RFO + sample[i].total.CRd + sample[i].total.DRd)*64ULL);
                        cout << "        " << unit_format((sample[i].total.ItoM + sample[i].total.RFO)*64ULL);
                    }
                    cout << "	(Total)\n";
					
					cout << " " << i;
                    cout << "    " << unit_format(sample[i].miss.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].miss.RFO);
                    cout << "  " << unit_format(sample[i].miss.CRd);
                    cout << "  " << unit_format(sample[i].miss.DRd);
                    cout << "   " << unit_format(sample[i].miss.ItoM);
                    cout << "  " << unit_format(sample[i].miss.PRd);
                    cout << "  " << unit_format(sample[i].miss.WiL);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].miss.PCIeRdCur + sample[i].miss.RFO + sample[i].miss.CRd + sample[i].miss.DRd)*64ULL);
                        cout << "        " << unit_format((sample[i].miss.ItoM + sample[i].miss.RFO)*64ULL);
                    }
                    cout << "	(Miss)\n";
					
					cout << " " << i;
                    cout << "    " << unit_format(sample[i].hit.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].hit.RFO);
                    cout << "  " << unit_format(sample[i].hit.CRd);
                    cout << "  " << unit_format(sample[i].hit.DRd);
                    cout << "   " << unit_format(sample[i].hit.ItoM);
                    cout << "  " << unit_format(sample[i].hit.PRd);
                    cout << "  " << unit_format(sample[i].hit.WiL);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].hit.PCIeRdCur + sample[i].hit.RFO + sample[i].hit.CRd + sample[i].hit.DRd)*64ULL);
                        cout << "        " << unit_format((sample[i].hit.ItoM + sample[i].hit.RFO)*64ULL);
                    }
                    cout << "	(Hit)\n";
                }
            }
            if(!csv)
            {
                if(print_bandwidth)
                    cout << "----------------------------------------------------------------------------------------------------\n";
                else
                    cout << "-----------------------------------------------------------------------\n";
                cout << " * ";
                cout << "   " << unit_format(aggregate_sample.PCIeRdCur);
                cout << "      " << unit_format(aggregate_sample.RFO);
                cout << "  " << unit_format(aggregate_sample.CRd);
                cout << "  " << unit_format(aggregate_sample.DRd);
                cout << "   " << unit_format(aggregate_sample.ItoM);
                cout << "  " << unit_format(aggregate_sample.PRd);
                cout << "  " << unit_format(aggregate_sample.WiL);
                if(print_bandwidth)
                {
                    cout << "        " << unit_format((aggregate_sample.PCIeRdCur + aggregate_sample.CRd + aggregate_sample.DRd + aggregate_sample.RFO)*64ULL);
                    cout << "        " << unit_format((aggregate_sample.ItoM + aggregate_sample.RFO)*64ULL);
                }
                cout << "	(Aggregate)\n\n";
            }
        }
        else // Ivytown and Older Architectures
        {
            for(i=0;i<NUM_SAMPLES;i++)
            {
                getPCIeEvents(m, m->PCIeRdCur, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSRd, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeWiLF, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeItoM, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSWr, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSWrF, delay_ms, sample,0);
            }
            
            if(csv)
                if(print_bandwidth)
                    cout << "Skt,PCIeRdCur,PCIeNSRd,PCIeWiLF,PCIeItoM,PCIeNSWr,PCIeNSWrF,PCIe Rd (B),PCIe Wr (B)\n";
                else
                    cout << "Skt,PCIeRdCur,PCIeNSRd,PCIeWiLF,PCIeItoM,PCIeNSWr,PCIeNSWrF\n";
            else
                if(print_bandwidth)
                    cout << "Skt | PCIeRdCur | PCIeNSRd  | PCIeWiLF | PCIeItoM | PCIeNSWr | PCIeNSWrF | PCIe Rd (B) | PCIe Wr (B)\n";
                else
                    cout << "Skt | PCIeRdCur | PCIeNSRd  | PCIeWiLF | PCIeItoM | PCIeNSWr | PCIeNSWrF\n";

            //report extrapolated read and write PCIe bandwidth per socket using the data from the sample
            for(i=0; i<m->getNumSockets(); ++i)
            {
                if(csv)
                {
                    cout << i;
                    cout << "," << sample[i].total.PCIeRdCur;
                    cout << "," << sample[i].total.PCIeNSWr;
                    cout << "," << sample[i].total.PCIeWiLF;
                    cout << "," << sample[i].total.PCIeItoM;
                    cout << "," << sample[i].total.PCIeNSWr;
                    cout << "," << sample[i].total.PCIeNSWrF;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].total.PCIeRdCur+ sample[i].total.PCIeNSWr)*64ULL);
                        cout << "," << ((sample[i].total.PCIeWiLF+sample[i].total.PCIeItoM+sample[i].total.PCIeNSWr+sample[i].total.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Total)\n";
					
					cout << i;
                    cout << "," << sample[i].miss.PCIeRdCur;
                    cout << "," << sample[i].miss.PCIeNSWr;
                    cout << "," << sample[i].miss.PCIeWiLF;
                    cout << "," << sample[i].miss.PCIeItoM;
                    cout << "," << sample[i].miss.PCIeNSWr;
                    cout << "," << sample[i].miss.PCIeNSWrF;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].miss.PCIeRdCur+ sample[i].miss.PCIeNSWr)*64ULL);
                        cout << "," << ((sample[i].miss.PCIeWiLF+sample[i].miss.PCIeItoM+sample[i].miss.PCIeNSWr+sample[i].miss.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Miss)\n";
					
					cout << i;
                    cout << "," << sample[i].hit.PCIeRdCur;
                    cout << "," << sample[i].hit.PCIeNSWr;
                    cout << "," << sample[i].hit.PCIeWiLF;
                    cout << "," << sample[i].hit.PCIeItoM;
                    cout << "," << sample[i].hit.PCIeNSWr;
                    cout << "," << sample[i].hit.PCIeNSWrF;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].hit.PCIeRdCur+ sample[i].hit.PCIeNSWr)*64ULL);
                        cout << "," << ((sample[i].hit.PCIeWiLF+sample[i].hit.PCIeItoM+sample[i].hit.PCIeNSWr+sample[i].hit.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Hit)\n";
					
					
					
					
                }
                else
                {
                    cout << " " << i;
                    cout << "      " << unit_format(sample[i].total.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].total.PCIeNSWr);
                    cout << "      " << unit_format(sample[i].total.PCIeWiLF);
                    cout << "     " << unit_format(sample[i].total.PCIeItoM);
                    cout << "     " << unit_format(sample[i].total.PCIeNSWr);
                    cout << "     " << unit_format(sample[i].total.PCIeNSWrF);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].total.PCIeRdCur+ sample[i].total.PCIeNSWr)*64ULL);
                        cout << "         " << unit_format((sample[i].total.PCIeWiLF+sample[i].total.PCIeItoM+sample[i].total.PCIeNSWr+sample[i].total.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Total)\n";
					
					cout << " " << i;
                    cout << "      " << unit_format(sample[i].miss.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].miss.PCIeNSWr);
                    cout << "      " << unit_format(sample[i].miss.PCIeWiLF);
                    cout << "     " << unit_format(sample[i].miss.PCIeItoM);
                    cout << "     " << unit_format(sample[i].miss.PCIeNSWr);
                    cout << "     " << unit_format(sample[i].miss.PCIeNSWrF);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].miss.PCIeRdCur+ sample[i].miss.PCIeNSWr)*64ULL);
                        cout << "         " << unit_format((sample[i].miss.PCIeWiLF+sample[i].miss.PCIeItoM+sample[i].miss.PCIeNSWr+sample[i].miss.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Miss)\n";
					
					cout << " " << i;
                    cout << "      " << unit_format(sample[i].hit.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].hit.PCIeNSWr);
                    cout << "      " << unit_format(sample[i].hit.PCIeWiLF);
                    cout << "     " << unit_format(sample[i].hit.PCIeItoM);
                    cout << "     " << unit_format(sample[i].hit.PCIeNSWr);
                    cout << "     " << unit_format(sample[i].hit.PCIeNSWrF);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].hit.PCIeRdCur+ sample[i].hit.PCIeNSWr)*64ULL);
                        cout << "         " << unit_format((sample[i].hit.PCIeWiLF+sample[i].hit.PCIeItoM+sample[i].hit.PCIeNSWr+sample[i].hit.PCIeNSWrF)*64ULL);
                    }
                    cout << "	(Hit)\n";
                }
            }
            if(!csv)
            {
                if(print_bandwidth)
                    cout << "----------------------------------------------------------------------------------------------------------------\n";
                else
                    cout << "-----------------------------------------------------------------------------------\n";
                cout << " * ";
                cout << "      " << unit_format(aggregate_sample.PCIeRdCur);
                cout << "      " << unit_format(aggregate_sample.PCIeNSWr);
                cout << "      " << unit_format(aggregate_sample.PCIeWiLF);
                cout << "     " << unit_format(aggregate_sample.PCIeItoM);
                cout << "     " << unit_format(aggregate_sample.PCIeNSWr);
                cout << "     " << unit_format(aggregate_sample.PCIeNSWrF);
                if(print_bandwidth)
                {
                    cout << "        " << unit_format((aggregate_sample.PCIeRdCur+ aggregate_sample.PCIeNSWr)*64ULL);
                    cout << "         " << unit_format((aggregate_sample.PCIeWiLF+aggregate_sample.PCIeItoM+aggregate_sample.PCIeNSWr+aggregate_sample.PCIeNSWrF)*64ULL);
                }
            }
            cout << "	(Aggregate)\n\n";
        }
        if ( m->isBlocked() ) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
	++ic;
    }
	
	}
	
	
	// default case
	else if ( print_additional_info == false)
	{
	
    unsigned int ic = 1;
    while ((ic <= numberOfIterations) || (numberOfIterations == 0))
    {
        MySleepMs(delay_ms);
        memset(sample,0,sizeof(sample));
        memset(&aggregate_sample,0,sizeof(aggregate_sample));
        
        if(!(m->getCPUModel() == PCM::JAKETOWN) && !(m->getCPUModel() == PCM::IVYTOWN))
        {
            for(i=0;i<NUM_SAMPLES;i++)
            {
                if(m->getCPUModel() == PCM::SKX)
                {
                    getPCIeEvents(m, m->SKX_RdCur, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_RFO, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_CRd, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_DRd, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_ItoM, delay_ms, sample, 0, m->PRQ);
                    getPCIeEvents(m, m->SKX_PRd, delay_ms, sample, 0, m->IRQ, 1);
                    getPCIeEvents(m, m->SKX_WiL, delay_ms, sample, 0, m->IRQ, 1);
                }
                 else 
                {
                    getPCIeEvents(m, m->PCIeRdCur, delay_ms, sample);
                    getPCIeEvents(m, m->RFO, delay_ms, sample,m->RFOtid);
                    getPCIeEvents(m, m->CRd, delay_ms, sample);
                    getPCIeEvents(m, m->DRd, delay_ms, sample);
                    getPCIeEvents(m, m->ItoM, delay_ms, sample,m->ItoMtid);
                    getPCIeEvents(m, m->PRd, delay_ms, sample);
                    getPCIeEvents(m, m->WiL, delay_ms, sample);
                }
            }
            
            if(csv)
                if(print_bandwidth)
                    cout << "Skt,PCIeRdCur,RFO,CRd,DRd,ItoM,PRd,WiL,PCIe Rd (B),PCIe Wr (B)\n";
                else
                    cout << "Skt,PCIeRdCur,RFO,CRd,DRd,ItoM,PRd,WiL\n";
            else
                if(print_bandwidth)
                    cout << "Skt | PCIeRdCur |  RFO  |  CRd  |  DRd  |  ItoM  |  PRd  |  WiL  | PCIe Rd (B) | PCIe Wr (B)\n";
                else
                    cout << "Skt | PCIeRdCur |  RFO  |  CRd  |  DRd  |  ItoM  |  PRd  |  WiL\n";

            //report extrapolated read and write PCIe bandwidth per socket using the data from the sample
            for(i=0; i<m->getNumSockets(); ++i)
            {
                if(csv)
                {
                    cout << i;
                    cout << "," << sample[i].total.PCIeRdCur;
                    cout << "," << sample[i].total.RFO;
                    cout << "," << sample[i].total.CRd;
                    cout << "," << sample[i].total.DRd;
                    cout << "," << sample[i].total.ItoM;
                    cout << "," << sample[i].total.PRd;
                    cout << "," << sample[i].total.WiL;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].total.PCIeRdCur + sample[i].total.RFO + sample[i].total.CRd + sample[i].total.DRd)*64ULL);
                        cout << "," << ((sample[i].total.ItoM + sample[i].total.RFO)*64ULL);
                    }
                    cout << "\n";
                }
                else
                {
                    cout << " " << i;
                    cout << "    " << unit_format(sample[i].total.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].total.RFO);
                    cout << "  " << unit_format(sample[i].total.CRd);
                    cout << "  " << unit_format(sample[i].total.DRd);
                    cout << "   " << unit_format(sample[i].total.ItoM);
                    cout << "  " << unit_format(sample[i].total.PRd);
                    cout << "  " << unit_format(sample[i].total.WiL);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].total.PCIeRdCur + sample[i].total.RFO + sample[i].total.CRd + sample[i].total.DRd)*64ULL);
                        cout << "        " << unit_format((sample[i].total.ItoM + sample[i].total.RFO)*64ULL);
                    }
                    cout << "\n";
                }
            }
            if(!csv)
            {
                if(print_bandwidth)
                    cout << "----------------------------------------------------------------------------------------------------\n";
                else
                    cout << "-----------------------------------------------------------------------\n";
                cout << " * ";
                cout << "   " << unit_format(aggregate_sample.PCIeRdCur);
                cout << "      " << unit_format(aggregate_sample.RFO);
                cout << "  " << unit_format(aggregate_sample.CRd);
                cout << "  " << unit_format(aggregate_sample.DRd);
                cout << "   " << unit_format(aggregate_sample.ItoM);
                cout << "  " << unit_format(aggregate_sample.PRd);
                cout << "  " << unit_format(aggregate_sample.WiL);
                if(print_bandwidth)
                {
                    cout << "        " << unit_format((aggregate_sample.PCIeRdCur + aggregate_sample.CRd + aggregate_sample.DRd + aggregate_sample.RFO)*64ULL);
                    cout << "        " << unit_format((aggregate_sample.ItoM + aggregate_sample.RFO)*64ULL);
                }
                cout << "\n\n";
            }
        }
        else // Ivytown and Older Architectures
        {
            for(i=0;i<NUM_SAMPLES;i++)
            {
                getPCIeEvents(m, m->PCIeRdCur, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSRd, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeWiLF, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeItoM, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSWr, delay_ms, sample,0);
                getPCIeEvents(m, m->PCIeNSWrF, delay_ms, sample,0);
            }
            
            if(csv)
                if(print_bandwidth)
                    cout << "Skt,PCIeRdCur,PCIeNSRd,PCIeWiLF,PCIeItoM,PCIeNSWr,PCIeNSWrF,PCIe Rd (B),PCIe Wr (B)\n";
                else
                    cout << "Skt,PCIeRdCur,PCIeNSRd,PCIeWiLF,PCIeItoM,PCIeNSWr,PCIeNSWrF\n";
            else
                if(print_bandwidth)
                    cout << "Skt | PCIeRdCur | PCIeNSRd  | PCIeWiLF | PCIeItoM | PCIeNSWr | PCIeNSWrF | PCIe Rd (B) | PCIe Wr (B)\n";
                else
                    cout << "Skt | PCIeRdCur | PCIeNSRd  | PCIeWiLF | PCIeItoM | PCIeNSWr | PCIeNSWrF\n";

            //report extrapolated read and write PCIe bandwidth per socket using the data from the sample
            for(i=0; i<m->getNumSockets(); ++i)
            {
                if(csv)
                {
                    cout << i;
                    cout << "," << sample[i].total.PCIeRdCur;
                    cout << "," << sample[i].total.PCIeNSWr;
                    cout << "," << sample[i].total.PCIeWiLF;
                    cout << "," << sample[i].total.PCIeItoM;
                    cout << "," << sample[i].total.PCIeNSWr;
                    cout << "," << sample[i].total.PCIeNSWrF;
                    if(print_bandwidth)
                    {
                        cout << "," << ((sample[i].total.PCIeRdCur+ sample[i].total.PCIeNSWr)*64ULL);
                        cout << "," << ((sample[i].total.PCIeWiLF+sample[i].total.PCIeItoM+sample[i].total.PCIeNSWr+sample[i].total.PCIeNSWrF)*64ULL);
                    }
                    cout << "\n";
                }
                else
                {
                    cout << " " << i;
                    cout << "      " << unit_format(sample[i].total.PCIeRdCur);
                    cout << "      " << unit_format(sample[i].total.PCIeNSWr);
                    cout << "      " << unit_format(sample[i].total.PCIeWiLF);
                    cout << "     " << unit_format(sample[i].total.PCIeItoM);
                    cout << "     " << unit_format(sample[i].total.PCIeNSWr);
                    cout << "     " << unit_format(sample[i].total.PCIeNSWrF);
                    if(print_bandwidth)
                    {
                        cout << "        " << unit_format((sample[i].total.PCIeRdCur+ sample[i].total.PCIeNSWr)*64ULL);
                        cout << "         " << unit_format((sample[i].total.PCIeWiLF+sample[i].total.PCIeItoM+sample[i].total.PCIeNSWr+sample[i].total.PCIeNSWrF)*64ULL);
                    }
                    cout << "\n";
                }
            }
            if(!csv)
            {
                if(print_bandwidth)
                    cout << "----------------------------------------------------------------------------------------------------------------\n";
                else
                    cout << "-----------------------------------------------------------------------------------\n";
                cout << " * ";
                cout << "      " << unit_format(aggregate_sample.PCIeRdCur);
                cout << "      " << unit_format(aggregate_sample.PCIeNSWr);
                cout << "      " << unit_format(aggregate_sample.PCIeWiLF);
                cout << "     " << unit_format(aggregate_sample.PCIeItoM);
                cout << "     " << unit_format(aggregate_sample.PCIeNSWr);
                cout << "     " << unit_format(aggregate_sample.PCIeNSWrF);
                if(print_bandwidth)
                {
                    cout << "        " << unit_format((aggregate_sample.PCIeRdCur+ aggregate_sample.PCIeNSWr)*64ULL);
                    cout << "         " << unit_format((aggregate_sample.PCIeWiLF+aggregate_sample.PCIeItoM+aggregate_sample.PCIeNSWr+aggregate_sample.PCIeNSWrF)*64ULL);
                }
            }
            cout << "\n\n";
        }
        if ( m->isBlocked() ) {
            // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
	++ic;
    }
	
	}
	
	// ================================== End Printing Output ==================================
	
    exit(EXIT_SUCCESS);
}

void getPCIeEvents(PCM *m, PCM::PCIeEventCode opcode, uint32 delay_ms, sample_t *sample, const uint32 tid, const uint32 q, const uint32 nc)
{
    PCIeCounterState * before = new PCIeCounterState[m->getNumSockets()];
    PCIeCounterState * after = new PCIeCounterState[m->getNumSockets()];
    PCIeCounterState * before2 = new PCIeCounterState[m->getNumSockets()];
    PCIeCounterState * after2 = new PCIeCounterState[m->getNumSockets()];
    uint32 i;

    m->programPCIeCounters(opcode, tid, 0, q, nc);
    for(i=0; i<m->getNumSockets(); ++i)
        before[i] = m->getPCIeCounterState(i);
    MySleepUs(delay_ms*1000);
    for(i=0; i<m->getNumSockets(); ++i)
        after[i] = m->getPCIeCounterState(i);

    m->programPCIeMissCounters(opcode, tid, q, nc);
    for(i=0; i<m->getNumSockets(); ++i)
        before2[i] = m->getPCIeCounterState(i);
    MySleepUs(delay_ms*1000);
    for(i=0; i<m->getNumSockets(); ++i)
        after2[i] = m->getPCIeCounterState(i);

    for(i=0; i<m->getNumSockets(); ++i)
    {
        switch(opcode)
        {
            case PCM::PCIeRdCur:
            case PCM::SKX_RdCur:
                sample[i].total.PCIeRdCur += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeRdCur += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeRdCur += (sample[i].total.PCIeRdCur > sample[i].miss.PCIeRdCur) ? sample[i].total.PCIeRdCur - sample[i].miss.PCIeRdCur : 0;
                aggregate_sample.PCIeRdCur += sample[i].total.PCIeRdCur;
                break;
            case PCM::PCIeNSRd:
                sample[i].total.PCIeNSRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeNSRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeNSRd += (sample[i].total.PCIeNSRd > sample[i].miss.PCIeNSRd) ? sample[i].total.PCIeNSRd - sample[i].miss.PCIeNSRd : 0;
                aggregate_sample.PCIeNSRd += sample[i].total.PCIeNSRd;
                break;
            case PCM::PCIeWiLF:
                sample[i].total.PCIeWiLF += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeWiLF += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeWiLF += (sample[i].total.PCIeWiLF > sample[i].miss.PCIeWiLF) ? sample[i].total.PCIeWiLF - sample[i].miss.PCIeWiLF : 0;
                aggregate_sample.PCIeWiLF += sample[i].total.PCIeWiLF;
                break;
            case PCM::PCIeItoM:
                sample[i].total.PCIeItoM += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeItoM += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeItoM += (sample[i].total.PCIeItoM > sample[i].miss.PCIeItoM) ? sample[i].total.PCIeItoM - sample[i].miss.PCIeItoM : 0;
                aggregate_sample.PCIeItoM += sample[i].total.PCIeItoM;
                break;
            case PCM::PCIeNSWr:
                sample[i].total.PCIeNSWr += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeNSWr += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeNSWr += (sample[i].total.PCIeNSWr > sample[i].miss.PCIeNSWr) ? sample[i].total.PCIeNSWr - sample[i].miss.PCIeNSWr : 0;
                aggregate_sample.PCIeNSWr += sample[i].total.PCIeNSWr;
                break;
            case PCM::PCIeNSWrF:
                sample[i].total.PCIeNSWrF += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PCIeNSWrF += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PCIeNSWrF += (sample[i].total.PCIeNSWrF > sample[i].miss.PCIeNSWrF) ? sample[i].total.PCIeNSWrF - sample[i].miss.PCIeNSWrF : 0;
                aggregate_sample.PCIeNSWrF += sample[i].total.PCIeNSWrF;
                break;
            case PCM::SKX_RFO:
            case PCM::RFO:
                if(opcode == PCM::SKX_RFO || tid == PCM::RFOtid) //Use tid to filter only PCIe traffic
                {
                    sample[i].total.RFO += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                    sample[i].miss.RFO += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                    sample[i].hit.RFO += (sample[i].total.RFO > sample[i].miss.RFO) ? sample[i].total.RFO - sample[i].miss.RFO : 0;
                    aggregate_sample.RFO += sample[i].total.RFO;
                }
                break;
            case PCM::SKX_ItoM:
            case PCM::ItoM:
                if(opcode == PCM::SKX_ItoM || tid == PCM::ItoMtid) //Use tid to filter only PCIe traffic
                {
                    sample[i].total.ItoM += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                    sample[i].miss.ItoM += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                    sample[i].hit.ItoM += (sample[i].total.ItoM > sample[i].miss.ItoM) ? sample[i].total.ItoM - sample[i].miss.ItoM : 0;
                    aggregate_sample.ItoM += sample[i].total.ItoM;
                }
                break;
            case PCM::SKX_WiL:
            case PCM::WiL:
                sample[i].total.WiL += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.WiL += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.WiL += (sample[i].total.WiL > sample[i].miss.WiL) ? sample[i].total.WiL - sample[i].miss.WiL : 0;
                aggregate_sample.WiL += sample[i].total.WiL;
                break;
            case PCM::SKX_PRd:
            case PCM::PRd:
                sample[i].total.PRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.PRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.PRd += (sample[i].total.PRd > sample[i].miss.PRd) ? sample[i].total.PRd - sample[i].miss.PRd : 0;
                aggregate_sample.PRd += sample[i].total.PRd;
                break;
            case PCM::SKX_CRd:
            case PCM::CRd:
                sample[i].total.CRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.CRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.CRd += (sample[i].total.CRd > sample[i].miss.CRd) ? sample[i].total.CRd - sample[i].miss.CRd : 0;
                aggregate_sample.CRd += sample[i].total.CRd;
                break;
            case PCM::SKX_DRd:
            case PCM::DRd:
                sample[i].total.DRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before[i], after[i]);
                sample[i].miss.DRd += (sizeof(PCIeEvents_t)/sizeof(uint64)) * getNumberOfEvents(before2[i], after2[i]);
                sample[i].hit.DRd += (sample[i].total.DRd > sample[i].miss.DRd) ? sample[i].total.DRd - sample[i].miss.DRd : 0;
                aggregate_sample.DRd += sample[i].total.DRd;
                break;
        }
    }

    delete[] before;
    delete[] after;
	delete[] before2;
	delete[] after2;
}
