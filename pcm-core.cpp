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


/*!     \file pcm-core.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for Intel Core, Offcore events
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

void build_event(const char * argv, EventSelectRegister *reg, int idx);

struct CoreEvent
{
	char name[256];
	uint64 value;
	uint64 msr_value;
	char * description;
} events[PERF_MAX_CUSTOM_COUNTERS];

extern "C" {
	SystemCounterState SysBeforeState, SysAfterState;
	std::vector<CoreCounterState> BeforeState, AfterState;
	std::vector<SocketCounterState> DummySocketStates;
	EventSelectRegister regs[PERF_MAX_COUNTERS];
	PCM::ExtendedCustomCoreEventDescription conf;

	int pcm_c_build_core_event(uint8_t idx, const char * argv)
	{
		if(idx > 3)
			return -1;

		cout << "building core event " << argv << " " << idx << "\n";
		build_event(argv, &regs[idx], idx);
		return 0;
	}

	int pcm_c_init()
	{
		PCM * m = PCM::getInstance();
		conf.fixedCfg = NULL; // default
		conf.nGPCounters = m->getMaxCustomCoreEvents();
		conf.gpCounterCfg = regs;
		conf.OffcoreResponseMsrValue[0] = events[0].msr_value;
		conf.OffcoreResponseMsrValue[1] = events[1].msr_value;

		m->resetPMU();
		PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
		if(status == PCM::Success)
			return 0;
		else
			return -1;
	}

	void pcm_c_start()
	{
		PCM * m = PCM::getInstance();
		m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);
	}

	void pcm_c_stop()
	{
		PCM * m = PCM::getInstance();
		m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);
	}

	uint64_t pcm_c_get_cycles(uint32_t core_id)
	{
		return getCycles(BeforeState[core_id], AfterState[core_id]);
	}

	uint64_t pcm_c_get_instr(uint32_t core_id)
	{
		return getInstructionsRetired(BeforeState[core_id], AfterState[core_id]);
	}

	uint64_t pcm_c_get_core_event(uint32_t core_id, uint32_t event_id)
	{
		return getNumberOfCustomEvents(event_id, BeforeState[core_id], AfterState[core_id]);
	}
}

void print_usage(const string progname)
{
	cerr << "\n Usage: \n " << progname
		<< " --help | [delay] [options] [-- external_program [external_program_options]]\n";
	cerr << "   <delay>                               => time interval to sample performance counters.\n";
	cerr << "                                            If not specified, or 0, with external program given\n";
	cerr << "                                            will read counters only after external program finishes\n";
	cerr << " Supported <options> are: \n";
	cerr << "  -h    | --help      | /h               => print this help and exit\n";
	cerr << "  -c    | /c                             => print CPU Model name and exit (used for pmu-query.py)\n";
	cerr << "  -csv[=file.csv]     | /csv[=file.csv]  => output compact CSV format to screen or\n"
		<< "                                            to a file, in case filename is provided\n";
    cerr << "  [-e event1] [-e event2] [-e event3] .. => optional list of custom events to monitor\n";
	cerr << "  event description example: cpu/umask=0x01,event=0x05,name=MISALIGN_MEM_REF.LOADS/ \n";
	cerr << "  -yc   | --yescores  | /yc              => enable specific cores to output\n";
	cerr << "  -i[=number] | /i[=number]              => allow to determine number of iterations\n";
    print_help_force_rtm_abort_mode(41);
	cerr << " Examples:\n";
	cerr << "  " << progname << " 1                   => print counters every second without core and socket output\n";
	cerr << "  " << progname << " 0.5 -csv=test.log   => twice a second save counter values to test.log in CSV format\n";
	cerr << "  " << progname << " /csv 5 2>/dev/null  => one sampe every 5 seconds, and discard all diagnostic output\n";
	cerr << "\n";
}

	template <class StateType>
void print_custom_stats(const StateType & BeforeState, const StateType & AfterState ,bool csv, uint64 txn_rate)
{
    const uint64 cycles = getCycles(BeforeState, AfterState);
    const uint64 refCycles = getRefCycles(BeforeState, AfterState);
    const uint64 instr = getInstructionsRetired(BeforeState, AfterState);
	if(!csv)
	{
		cout << double(instr)/double(cycles);
		if(txn_rate == 1)
		{
			cout << setw(14) << unit_format(instr);
			cout << setw(11) << unit_format(cycles);
			cout << setw(12) << unit_format(refCycles);
		} else {
			cout << setw(14) << double(instr)/double(txn_rate);
			cout << setw(11) << double(cycles)/double(txn_rate);
			cout << setw(12) << double(refCycles) / double(txn_rate);
		}
	}
	else
	{
		cout << double(instr)/double(cycles) << ",";
		cout << double(instr)/double(txn_rate) << ",";
		cout << double(cycles)/double(txn_rate) << ",";
		cout << double(refCycles) / double(txn_rate) << ",";
	}
    const auto max_ctr = PCM::getInstance()->getMaxCustomCoreEvents();
    for (int i = 0; i < max_ctr; ++i)
		if(!csv) {
			cout << setw(10);
			if(txn_rate == 1)
				cout << unit_format(getNumberOfCustomEvents(i, BeforeState, AfterState));
			else
				cout << double(getNumberOfCustomEvents(i, BeforeState, AfterState))/double(txn_rate);
		}
		else
			cout << double(getNumberOfCustomEvents(i, BeforeState, AfterState))/double(txn_rate) << ",";

	cout << "\n";
}

// emulates scanf %i for hex 0x prefix otherwise assumes dec (no oct support)
bool match(const char * subtoken, const char * name, int * result)
{
    std::string sname(name);
    if (pcm_sscanf(subtoken) >> s_expect(sname + "0x") >> std::hex >> *result)
        return true;

    if (pcm_sscanf(subtoken) >> s_expect(sname) >> std::dec >> *result)
        return true;

    return false;
}

#define EVENT_SIZE 256
void build_event(const char * argv, EventSelectRegister *reg, int idx)
{
	char *token, *subtoken, *saveptr1, *saveptr2;
	char name[EVENT_SIZE], *str1, *str2;
	int j, tmp;
	uint64 tmp2;
	reg->value = 0;
	reg->fields.usr = 1;
	reg->fields.os = 1;
	reg->fields.enable = 1;

	memset(name,0,EVENT_SIZE);
#ifdef _MSC_VER
    strncpy_s(name, argv, EVENT_SIZE - 1);
#else
	strncpy(name,argv,EVENT_SIZE-1); 
#endif
	/*
	   uint64 apic_int : 1;

	   offcore_rsp=2,period=10000
	   */
	for (j = 1, str1 = name; ; j++, str1 = NULL) {
		token = strtok_r(str1, "/", &saveptr1);
		if (token == NULL)
			break;
		printf("%d: %s\n", j, token);
		if(strncmp(token,"cpu",3) == 0)
			continue;

		for (str2 = token; ; str2 = NULL) {
			tmp = -1;
			subtoken = strtok_r(str2, ",", &saveptr2);
			if (subtoken == NULL)
				break;
			if(match(subtoken,"event=",&tmp))
				reg->fields.event_select = tmp;
			else if(match(subtoken,"umask=",&tmp))
				reg->fields.umask = tmp;
			else if(strcmp(subtoken,"edge") == 0)
				reg->fields.edge = 1;
			else if(match(subtoken,"any=",&tmp))
				reg->fields.any_thread = tmp;
			else if(match(subtoken,"inv=",&tmp))
				reg->fields.invert = tmp;
			else if(match(subtoken,"cmask=",&tmp))
				reg->fields.cmask = tmp;
			else if(match(subtoken,"in_tx=",&tmp))
				reg->fields.in_tx = tmp;
			else if(match(subtoken,"in_tx_cp=",&tmp))
				reg->fields.in_txcp = tmp;
			else if(match(subtoken,"pc=",&tmp))
				reg->fields.pin_control = tmp;
			else if(pcm_sscanf(subtoken) >> s_expect("offcore_rsp=") >> std::hex >> tmp2) {
				if(idx >= 2)
				{
					cerr << "offcore_rsp must specify in first or second event only. idx=" << idx << "\n";
					throw idx;
				}
				events[idx].msr_value = tmp2;
			}
			else if(pcm_sscanf(subtoken) >> s_expect("name=") >> setw(255) >> events[idx].name) ;
			else
			{
				cerr << "Event '" << subtoken << "' is not supported. See the list of supported events\n";
				throw subtoken;
			}

		}
	}
	events[idx].value = reg->value;
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
	cerr << " Processor Counter Monitor: Core Monitoring Utility \n";
	cerr << "\n";

	double delay = -1.0;
	char *sysCmd = NULL;
	char **sysArgv = NULL;
	uint32 cur_event = 0;
	bool csv = false;
	uint64 txn_rate = 1;
	MainLoop mainLoop;
	string program = string(argv[0]);
	EventSelectRegister regs[PERF_MAX_COUNTERS];
	PCM::ExtendedCustomCoreEventDescription conf;
	bool show_partial_core_output = false;
	std::bitset<MAX_CORES> ycores;


        PCM * m = PCM::getInstance();

	conf.fixedCfg = NULL; // default
	conf.nGPCounters = m->getMaxCustomCoreEvents();
	conf.gpCounterCfg = regs;

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
		else if (strncmp(*argv, "-csv",4) == 0 ||
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
		if (mainLoop.parseArg(*argv))
		{
			continue;
		}
		else if (strncmp(*argv, "-c",2) == 0 ||
				strncmp(*argv, "/c",2) == 0)
		{
			cout << m->getCPUFamilyModelString() << "\n";
			exit(EXIT_SUCCESS);
		}
		else if (strncmp(*argv, "-txn",4) == 0 ||
				strncmp(*argv, "/txn",4) == 0)
		{
			argv++;
			argc--;
			txn_rate = strtoull(*argv,NULL,10);
			cout << "txn_rate set to " << txn_rate << "\n";
			continue;
		}
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

				ycores.set(atoi(s.c_str()),true);
			}
			if(m->getNumCores() > MAX_CORES)
			{
				cerr << "Error: --yescores option is enabled, but #define MAX_CORES " << MAX_CORES << " is less than  m->getNumCores() = " << m->getNumCores() << "\n";
				cerr << "There is a potential to crash the system. Please increase MAX_CORES to at least " << m->getNumCores() << " and re-enable this option.\n";
				exit(EXIT_FAILURE);
			}
			continue;
		}
		else if (strncmp(*argv, "-e",2) == 0)
		{
			argv++;
			argc--;
			if(cur_event >= conf.nGPCounters) {
				cerr << "At most " << conf.nGPCounters << " events are allowed\n";
				exit(EXIT_FAILURE);
			}
			try {
				build_event(*argv,&regs[cur_event],cur_event);
				cur_event++;
			} catch (const char * /* str */) {
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
			if(is_str_stream.eof() && !is_str_stream.fail()) {
				delay = delay_input;
			} else {
				cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
				print_usage(program);
				exit(EXIT_FAILURE);
			}
			continue;
		}
	} while(argc > 1); // end of command line parsing loop

	if ( cur_event == 0 )
		cerr << "WARNING: you did not provide any custom events, is this intentional?\n";

	conf.OffcoreResponseMsrValue[0] = events[0].msr_value;
	conf.OffcoreResponseMsrValue[1] = events[1].msr_value;

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

	uint64 BeforeTime = 0, AfterTime = 0;
	SystemCounterState SysBeforeState, SysAfterState;
	const uint32 ncores = m->getNumCores();
	std::vector<CoreCounterState> BeforeState, AfterState;
	std::vector<SocketCounterState> DummySocketStates;

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

	std::cout.precision(2);
	std::cout << std::fixed; 

	BeforeTime = m->getTickCount();
	m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);

	if( sysCmd != NULL ) {
		MySystem(sysCmd, sysArgv);
	}


	mainLoop([&]()
	{
		if(!csv) cout << std::flush;

		calibratedSleep(delay, sysCmd, mainLoop, m);

		AfterTime = m->getTickCount();
		m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);

		cout << "Time elapsed: " << dec << fixed << AfterTime-BeforeTime << " ms\n";
		cout << "txn_rate: " << txn_rate << "\n";
		//cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";

		for(uint32 i=0;i<cur_event;++i)
		{
			cout << "Event" << i << ": " << events[i].name << " (raw 0x" <<
				std::hex << (uint32)events[i].value;

			if(events[i].msr_value)
				cout << ", offcore_rsp 0x" << (uint64) events[i].msr_value;

			cout << std::dec << ")\n";
		}
		cout << "\n";
        if (csv)
        {
            cout << "Core,IPC,Instructions,Cycles,RefCycles";
            for (unsigned i = 0; i < conf.nGPCounters; ++i)
            {
                cout << ",Event" << i;
            }
            cout << "\n";
        }
        else
        {
            cout << "Core | IPC | Instructions  |  Cycles  | RefCycles ";
            for (unsigned i = 0; i < conf.nGPCounters; ++i)
            {
                cout << "| Event" << i << "  ";
            }
            cout << "\n";
        }

		for(uint32 i = 0; i<ncores ; ++i)
		{
			if(m->isCoreOnline(i) == false || (show_partial_core_output && ycores.test(i) == false))
				continue;
			if(csv)
				cout << i << ",";
			else
				cout << " " << setw(3) << i << "   " << setw(2) ;
			print_custom_stats(BeforeState[i], AfterState[i], csv, txn_rate);
		}
		if(csv)
			cout << "*,";
		else
		{
			cout << "---------------------------------------------------------------------------------------------------------------------------------\n";
			cout << "   *   ";
		}
		print_custom_stats(SysBeforeState, SysAfterState, csv, txn_rate);

		std::cout << "\n";

		swap(BeforeTime, AfterTime);
		swap(BeforeState, AfterState);
		swap(SysBeforeState, SysAfterState);

		if ( m->isBlocked() ) {
			// in case PCM was blocked after spawning child application: break monitoring loop here
			return false;
		}
		return true;
	});
	exit(EXIT_SUCCESS);
}
