// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Patrick Lu


/*!     \file pcm-core.cpp
  \brief Example of using CPU counters: implements a performance counter monitoring utility for Intel Core, Offcore events
  */
#include <iostream>
#ifdef _MSC_VER
#define strtok_r strtok_s
#include <windows.h>
#include "windows/windriver.h"
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

#ifdef PCM_SHARED_LIBRARY

extern "C" {
	static std::shared_ptr<SystemCounterState> globalSysBeforeState, globalSysAfterState;
	static std::shared_ptr<std::vector<CoreCounterState> > globalBeforeState, globalAfterState;
	static std::shared_ptr<std::vector<SocketCounterState> > globalDummySocketStates;
	static EventSelectRegister globalRegs[PERF_MAX_COUNTERS];
	static PCM::ExtendedCustomCoreEventDescription globalConf;

	int pcm_c_build_core_event(uint8_t idx, const char * argv)
	{
		if(idx > 3)
			return -1;

		cout << "building core event " << argv << " " << idx << "\n";
		build_event(argv, &globalRegs[idx], idx);
		return 0;
	}

	int pcm_c_init()
	{
		PCM * m = PCM::getInstance();
		globalSysBeforeState = std::make_shared<SystemCounterState>();
		globalSysAfterState = std::make_shared<SystemCounterState>();
		globalBeforeState = std::make_shared<std::vector<CoreCounterState> >();
		globalAfterState = std::make_shared<std::vector<CoreCounterState> >();
		globalDummySocketStates = std::make_shared<std::vector<SocketCounterState> >();
		globalConf.fixedCfg = NULL; // default
		globalConf.nGPCounters = m->getMaxCustomCoreEvents();
		globalConf.gpCounterCfg = globalRegs;
		globalConf.OffcoreResponseMsrValue[0] = events[0].msr_value;
		globalConf.OffcoreResponseMsrValue[1] = events[1].msr_value;

		m->resetPMU();
		PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &globalConf);
		if(status == PCM::Success)
			return 0;
		else
			return -1;
	}

	void pcm_c_start()
	{
		PCM * m = PCM::getInstance();
		m->getAllCounterStates(*globalSysBeforeState.get(), *globalDummySocketStates.get(), *globalBeforeState.get());
	}

	void pcm_c_stop()
	{
		PCM * m = PCM::getInstance();
		m->getAllCounterStates(*globalSysAfterState.get(), *globalDummySocketStates.get(), *globalAfterState.get());
	}

	uint64_t pcm_c_get_cycles(uint32_t core_id)
	{
		return getCycles((*globalBeforeState.get())[core_id], (*globalAfterState.get())[core_id]);
	}

	uint64_t pcm_c_get_instr(uint32_t core_id)
	{
		return getInstructionsRetired((*globalBeforeState.get())[core_id], (*globalAfterState.get())[core_id]);
	}

	uint64_t pcm_c_get_core_event(uint32_t core_id, uint32_t event_id)
	{
		return getNumberOfCustomEvents(event_id, (*globalBeforeState.get())[core_id], (*globalAfterState.get())[core_id]);
	}
}

#endif // PCM_SHARED_LIBRARY

void print_usage(const string & progname)
{
	cout << "\n Usage: \n " << progname
		 << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
	cout << "   <delay>                               => time interval to sample performance counters.\n";
	cout << "                                            If not specified, or 0, with external program given\n";
	cout << "                                            will read counters only after external program finishes\n";
	cout << " Supported <options> are: \n";
	cout << "  -h    | --help      | /h               => print this help and exit\n";
	cout << "  -silent                                => silence information output and print only measurements\n";
	cout << "  --version                              => print application version\n";
	cout << "  -c    | /c                             => print CPU Model name and exit (used for pmu-query.py)\n";
	cout << "  -csv[=file.csv]     | /csv[=file.csv]  => output compact CSV format to screen or\n"
		<< "                                            to a file, in case filename is provided\n";
    cout << "  [-e event1] [-e event2] [-e event3] .. => optional list of custom events to monitor\n";
	cout << "  event description example: cpu/umask=0x01,event=0x05,name=MISALIGN_MEM_REF.LOADS/ \n";
	cout << "  -yc   | --yescores  | /yc              => enable specific cores to output\n";
	cout << "  -i[=number] | /i[=number]              => allow to determine number of iterations\n";
    print_help_force_rtm_abort_mode(41);
	cout << " Examples:\n";
	cout << "  " << progname << " 1                   => print counters every second without core and socket output\n";
	cout << "  " << progname << " 0.5 -csv=test.log   => twice a second save counter values to test.log in CSV format\n";
	cout << "  " << progname << " /csv 5 2>/dev/null  => one sample every 5 seconds, and discard all diagnostic output\n";
	cout << "\n";
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

void build_event(const char * argv, EventSelectRegister *reg, int idx)
{
	char *token, *subtoken, *saveptr1, *saveptr2;
	char *str1, *str2;
	int j, tmp;
	uint64 tmp2;
	reg->value = 0;
	reg->fields.usr = 1;
	reg->fields.os = 1;
	reg->fields.enable = 1;

	/*
	   uint64 apic_int : 1;

	   offcore_rsp=2,period=10000
	   */
	for (j = 1, str1 = (char*) argv; ; j++, str1 = NULL) {
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
			else if(pcm_sscanf(subtoken) >> s_expect("name=") >> setw(255) >> events[idx].name) {
				if (check_for_injections(events[idx].name))
					throw events[idx].name;
			}
			else
			{
				cerr << "Event '" << subtoken << "' is not supported. See the list of supported events\n";
				throw subtoken;
			}

		}
	}
	events[idx].value = reg->value;
}

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
	cerr << " Intel(r) Performance Counter Monitor: Core Monitoring Utility \n";
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
		string arg_value;

		if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
		{
			print_usage(program);
			exit(EXIT_FAILURE);
		}
		else if (check_argument_equals(*argv, {"-silent", "/silent"}))
		{
			// handled in check_and_set_silent
			continue;
		}
		else if (check_argument_equals(*argv, {"-csv", "/csv"}))
		{
			csv = true;
		}
		else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
		{
			csv = true;
			if (!arg_value.empty()) {
				m->setOutput(arg_value);
			}
			continue;
		}
		else if (mainLoop.parseArg(*argv))
		{
			continue;
		}
		else if (check_argument_equals(*argv, {"-c", "/c"}))
		{
			cout << m->getCPUFamilyModelString() << "\n";
			exit(EXIT_SUCCESS);
		}
		else if (check_argument_equals(*argv, {"-txn", "/txn"}))
		{
			argv++;
			argc--;
			txn_rate = strtoull(*argv,NULL,10);
			cout << "txn_rate set to " << txn_rate << "\n";
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
		else if (check_argument_equals(*argv, {"-e"}))
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
			} catch (...) {
				exit(EXIT_FAILURE);
			}
			continue;
		}
		else if (CheckAndForceRTMAbortMode(*argv, m))
		{
			continue;
		}
		else if (check_argument_equals(*argv, {"--"}))
		{
			argv++;
			sysCmd = *argv;
			sysArgv = argv;
			break;
		}
		else
		{
			delay = parse_delay(*argv, program, (print_usage_func)print_usage);
			continue;
		}
	} while(argc > 1); // end of command line parsing loop

	if ( cur_event == 0 )
		cerr << "WARNING: you did not provide any custom events, is this intentional?\n";

	conf.OffcoreResponseMsrValue[0] = events[0].msr_value;
	conf.OffcoreResponseMsrValue[1] = events[1].msr_value;

	PCM::ErrorCode status = m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
    m->checkError(status);

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
