// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2014, Intel Corporation

// written by Roman Dementiev
// added PPD cycles by Thomas Willhalm

#include "cpucounters.h"
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/time.h> // for gettimeofday()
#endif
#include <iostream>
#include <stdlib.h>
#include <iomanip>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif
#include "utils.h"

#define PCM_DELAY_DEFAULT 1.0       // in seconds
#define PCM_DELAY_MIN 0.015         // 15 milliseconds is practical on most modern CPUs

using namespace std;
using namespace pcm;

int getFirstRank(int imc_profile)
{
    return imc_profile * 2;
}
int getSecondRank(int imc_profile)
{
    return (imc_profile * 2) + 1;
}

double getCKEOffResidency(uint32 channel, uint32 rank, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    return double(getMCCounter(channel, (rank & 1) ? 2 : 0, before, after)) / double(getDRAMClocks(channel, before, after));
}

int64 getCKEOffAverageCycles(uint32 channel, uint32 rank, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    uint64 div = getMCCounter(channel, (rank & 1) ? 3 : 1, before, after);
    if (div)
        return getMCCounter(channel, (rank & 1) ? 2 : 0, before, after) / div;

    return -1;
}

int64 getCyclesPerTransition(uint32 channel, uint32 rank, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    uint64 div = getMCCounter(channel, (rank & 1) ? 3 : 1, before, after);
    if (div)
        return getDRAMClocks(channel, before, after) / div;

    return -1;
}

uint64 getSelfRefreshCycles(uint32 channel, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    return getMCCounter(channel, 0, before, after);
}

uint64 getSelfRefreshTransitions(uint32 channel, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    return getMCCounter(channel, 1, before, after);
}

uint64 getPPDCycles(uint32 channel, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    return getMCCounter(channel, 2, before, after);
}

double getNormalizedPCUCounter(uint32 counter, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    return double(getPCUCounter(counter, before, after)) / double(getPCUClocks(before, after));
}

double getNormalizedPCUCounter(uint32 counter, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after, PCM * m)
{
    const uint64 PCUClocks = (m->getPCUFrequency() * getInvariantTSC(before, after)) / m->getNominalFrequency();
    // cout << "PCM Debug: PCU clocks " << PCUClocks << " PCU frequency: " << m->getPCUFrequency() << "\n";
    return double(getPCUCounter(counter, before, after)) / double(PCUClocks);
}

int default_freq_band[3] = { 12, 20, 40 };
int freq_band[3];

void print_usage(const string & progname)
{
    cout << "\n Usage: \n " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cout << "   <delay>                           => time interval to sample performance counters.\n";
    cout << "                                        If not specified, or 0, with external program given\n";
    cout << "                                        will read counters only after external program finishes\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help  | /h               => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
//    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
//         << "                                        to a file, in case filename is provided\n";
    cout << "  [-m imc_profile] [-p pcu_profile] [-a freq_band0] [-b freq_band1] [-c freq_band2]\n\n";
    cout << " Where: imc_profile, pcu_profile, freq_band0, freq_band1 and freq_band2 are the following:\n";
    cout << "  <imc_profile>      - profile (counter group) for IMC PMU. Possible values are: 0,1,2,3,4,-1 \n";
    cout << "                       profile  0 - rank 0 and rank 1 residencies (default) \n";
    cout << "                       profile  1 - rank 2 and rank 3 residencies \n";
    cout << "                       profile  2 - rank 4 and rank 5 residencies \n";
    cout << "                       profile  3 - rank 6 and rank 7 residencies \n";
    cout << "                       profile  4 - self-refresh residencies \n";
    cout << "                       profile -1 - omit IMC PMU output\n";
    cout << "  <pcu_profile>      - profile (counter group) for PCU PMU. Possible values are: 0,1,2,3,4,5,-1 \n";
    cout << "                       profile  0 - frequency residencies (default) \n";
    cout << "                       profile  1 - core C-state residencies. The unit is the number of physical cores on the socket who were in C0, C3 or C6 during the measurement interval (e.g. 'C0 residency is 3.5' means on average 3.5 physical cores were resident in C0 state)\n";
    cout << "                       profile  2 - Prochot (throttled) residencies and thermal frequency limit cycles \n";
    cout << "                       profile  3 - {Thermal,Power,Clipped} frequency limit cycles \n";
    cout << "                       profile  4 - {OS,Power,Clipped} frequency limit cycles \n";
    cout << "                       profile  5 - frequency transition statistics \n";
    cout << "                       profile  6 - package C-states residency and transition statistics \n";
    cout << "                       profile  7 - UFS transition statistics (1) \n";
    cout << "                       profile  8 - UFS transition statistics (2) \n";
    cout << "                       profile -1 - omit PCU PMU output\n";
    cout << "  <freq_band0>       - frequency minimum for band 0 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[0] << "= " << 100 * default_freq_band[0] << "MHz)\n";
    cout << "  <freq_band1>       - frequency minimum for band 1 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[1] << "= " << 100 * default_freq_band[1] << "MHz)\n";
    cout << "  <freq_band2>       - frequency minimum for band 2 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[2] << "= " << 100 * default_freq_band[2] << "MHz)\n";
    cout << "\n";
}

int main(int argc, char * argv[])
{
    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);

    set_signal_handlers();

    cerr << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    cerr << "\n Power Monitoring Utility\n";

    int imc_profile = 0;
    int pcu_profile = 0;
    double delay = -1.0;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;

    freq_band[0] = default_freq_band[0];
    freq_band[1] = default_freq_band[1];
    freq_band[2] = default_freq_band[2];

    bool csv = false;
    MainLoop mainLoop;
    string program = string(argv[0]);

    PCM * m = PCM::getInstance();

    if (argc > 1) do
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
            else if (check_argument_equals(*argv, {"-m"}))
            {
                argv++;
                argc--;
                imc_profile = atoi(*argv);
                continue;
            }
            else if (check_argument_equals(*argv, {"-p"}))
            {
                argv++;
                argc--;
                pcu_profile = atoi(*argv);
                continue;
            }
            else if (check_argument_equals(*argv, {"-a"}))
            {
                argv++;
                argc--;
                freq_band[0] = atoi(*argv);
                continue;
            }
            else if (check_argument_equals(*argv, {"-b"}))
            {
                argv++;
                argc--;
                freq_band[1] = atoi(*argv);
                continue;
            }
            else if (check_argument_equals(*argv, {"-c"}))
            {
                argv++;
                argc--;
                freq_band[2] = atoi(*argv);
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
        } while (argc > 1); // end of command line partsing loop

    m->disableJKTWorkaround();

    const int cpu_model = m->getCPUModel();
    if (!(m->hasPCICFGUncore()))
    {
        cerr << "Unsupported processor model (" << cpu_model << ").\n";
        exit(EXIT_FAILURE);
    }

    EventSelectRegister regs[PERF_MAX_CUSTOM_COUNTERS];
    PCM::ExtendedCustomCoreEventDescription conf;
    int32 nCorePowerLicenses = 0;
    std::vector<std::string> licenseStr;
    switch (cpu_model)
    {
    case PCM::SKX:
    case PCM::ICX:
        {
            std::vector<std::string> skxLicenseStr = { "Core cycles where the core was running with power-delivery for baseline license level 0.  This includes non-AVX codes, SSE, AVX 128-bit, and low-current AVX 256-bit codes.",
                                          "Core cycles where the core was running with power-delivery for license level 1.  This includes high current AVX 256-bit instructions as well as low current AVX 512-bit instructions.",
                                          "Core cycles where the core was running with power-delivery for license level 2 (introduced in Skylake Server michroarchtecture). This includes high current AVX 512-bit instructions." };
            licenseStr = skxLicenseStr;
            regs[0].fields.event_select = 0x28; // CORE_POWER.LVL0_TURBO_LICENSE
            regs[0].fields.umask = 0x07;        // CORE_POWER.LVL0_TURBO_LICENSE
            regs[1].fields.event_select = 0x28; // CORE_POWER.LVL1_TURBO_LICENSE
            regs[1].fields.umask = 0x18;        // CORE_POWER.LVL1_TURBO_LICENSE
            regs[2].fields.event_select = 0x28; // CORE_POWER.LVL2_TURBO_LICENSE
            regs[2].fields.umask = 0x20;        // CORE_POWER.LVL2_TURBO_LICENSE
            conf.nGPCounters = 3;
            nCorePowerLicenses = 3;
            conf.gpCounterCfg = regs;
        }
        break;
    }

    for (size_t l = 0; l < licenseStr.size(); ++l)
    {
        cout << "Core Power License " << std::to_string(l) << ": " << licenseStr[l] << "\n";
    }

    if (conf.gpCounterCfg)
    {
        m->checkError(m->program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf));
    }

    m->checkError(m->programServerUncorePowerMetrics(imc_profile, pcu_profile, freq_band));

    const auto numSockets = m->getNumSockets();
    std::vector<ServerUncoreCounterState> BeforeState(numSockets);
    std::vector<ServerUncoreCounterState> AfterState(numSockets);
    SystemCounterState dummySystemState;
    std::vector<CoreCounterState> dummyCoreStates;
    std::vector<SocketCounterState> beforeSocketState, afterSocketState;
    uint64 BeforeTime = 0, AfterTime = 0;

    cerr << dec << "\n";
    cerr.precision(2);
    cerr << fixed;
    cout << dec << "\n";
    cout.precision(2);
    cout << fixed;
    cerr << "\nMC counter group: " << imc_profile << "\n";
    cerr << "PCU counter group: " << pcu_profile << "\n";
    if (pcu_profile == 0) {
        if (cpu_model == PCM::HASWELLX || cpu_model == PCM::BDX_DE || cpu_model == PCM::SKX)
            cerr << "Your processor does not support frequency band statistics\n";
        else
            cerr << "Freq bands [0/1/2]: " << freq_band[0] * 100 << " MHz; " << freq_band[1] * 100 << " MHz; " << freq_band[2] * 100 << " MHz; \n";
    }
    if (sysCmd != NULL)
        cerr << "Update every " << delay << " seconds\n";

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;

    uint32 i = 0;

    for (i = 0; i < numSockets; ++i)
        BeforeState[i] = m->getServerUncoreCounterState(i);

    m->getAllCounterStates(dummySystemState, beforeSocketState, dummyCoreStates, false);

    BeforeTime = m->getTickCount();
    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    auto getPowerLicenseResidency = [nCorePowerLicenses](const int32 license, const SocketCounterState & before, const SocketCounterState& after)
    {
        uint64 all = 0;
        for (int32 l = 0; l < nCorePowerLicenses; ++l)
        {
            all += getNumberOfCustomEvents(l, before, after);
        }
        assert(license < nCorePowerLicenses);
	if (all > 0)
            return 100.0 * double(getNumberOfCustomEvents(license, before, after)) / double(all);
        return -1.;
    };

    const auto uncoreFreqFactor = double(m->getNumOnlineSockets()) / double(m->getNumOnlineCores());

    mainLoop([&]()
    {
        cout << "----------------------------------------------------------------------------------------------\n";

        if (!csv) cout << flush;

        const auto delay_ms = calibratedSleep(delay, sysCmd, mainLoop, m);

        AfterTime = m->getTickCount();
        for (i = 0; i < numSockets; ++i)
            AfterState[i] = m->getServerUncoreCounterState(i);

        m->getAllCounterStates(dummySystemState, afterSocketState, dummyCoreStates, false);

        cout << "Time elapsed: " << AfterTime - BeforeTime << " ms\n";
        cout << "Called sleep function for " << delay_ms << " ms\n";
        for (uint32 socket = 0; socket < numSockets; ++socket)
        {
            if (nCorePowerLicenses)
            {
                cout << "S" << socket << "; " <<
                    "Uncore Freq: " << getAverageUncoreFrequency(BeforeState[socket], AfterState[socket]) * uncoreFreqFactor / 1e9 << " Ghz; "
                    "Core Freq: " << getActiveAverageFrequency(beforeSocketState[socket], afterSocketState[socket]) / 1e9 << " Ghz; ";
                for (int32 l = 0; l < nCorePowerLicenses; ++l)
                {
                    cout << "Core Power License " << std::to_string(l) << ": " << getPowerLicenseResidency(l, beforeSocketState[socket], afterSocketState[socket]) << "%; ";
                }
                cout << "\n";
            }
            for (uint32 port = 0; port < m->getQPILinksPerSocket(); ++port)
            {
                cout << "S" << socket << "P" << port
                          << "; " + std::string(m->xPI()) + " Clocks: " << getQPIClocks(port, BeforeState[socket], AfterState[socket])
                          << "; L0p Tx Cycles: " << 100. * getNormalizedQPIL0pTxCycles(port, BeforeState[socket], AfterState[socket]) << "%"
                          << "; L1 Cycles: " << 100. * getNormalizedQPIL1Cycles(port, BeforeState[socket], AfterState[socket]) << "%"
                          << "\n";
            }
            for (uint32 channel = 0; channel < m->getMCChannelsPerSocket(); ++channel)
            {
                if (imc_profile <= 3 && imc_profile >= 0)
                {
                    cout << "S" << socket << "CH" << channel << "; DRAMClocks: " << getDRAMClocks(channel, BeforeState[socket], AfterState[socket])
                              << "; Rank" << getFirstRank(imc_profile) << " CKE Off Residency: " << setw(3) <<
                        100. * getCKEOffResidency(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket]) << "%"
                              << "; Rank" << getFirstRank(imc_profile) << " CKE Off Average Cycles: " <<
                        getCKEOffAverageCycles(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "; Rank" << getFirstRank(imc_profile) << " Cycles per transition: " <<
                        getCyclesPerTransition(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "\n";

                    cout << "S" << socket << "CH" << channel << "; DRAMClocks: " << getDRAMClocks(channel, BeforeState[socket], AfterState[socket])
                              << "; Rank" << getSecondRank(imc_profile) << " CKE Off Residency: " << setw(3) <<
                        100. * getCKEOffResidency(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket]) << "%"
                              << "; Rank" << getSecondRank(imc_profile) << " CKE Off Average Cycles: " <<
                        getCKEOffAverageCycles(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "; Rank" << getSecondRank(imc_profile) << " Cycles per transition: " <<
                        getCyclesPerTransition(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "\n";
                } else if (imc_profile == 4)
                {
                    cout << "S" << socket << "CH" << channel
                              << "; DRAMClocks: " << getDRAMClocks(channel, BeforeState[socket], AfterState[socket])
                              << "; Self-refresh cycles: " << getSelfRefreshCycles(channel, BeforeState[socket], AfterState[socket])
                              << "; Self-refresh transitions: " << getSelfRefreshTransitions(channel, BeforeState[socket], AfterState[socket])
                              << "; PPD cycles: " << getPPDCycles(channel, BeforeState[socket], AfterState[socket])
                              << "\n";
                }
            }
            switch (pcu_profile)
            {
            case 0:
                if (cpu_model == PCM::HASWELLX || cpu_model == PCM::BDX_DE || cpu_model == PCM::SKX)
                    break;
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Freq band 0/1/2 cycles: " << 100. * getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) << "%"
                          << "; " << 100. * getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) << "%"
                          << "; " << 100. * getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) << "%"
                          << "\n";
                break;

            case 1:
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << ((cpu_model == PCM::SKX)?"; core C0_1/C3/C6_7-state residency: ":"; core C0/C3/C6-state residency: ") 
                          << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket])
                          << "; " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket])
                          << "; " << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket])
                          << "\n";
                break;

            case 2:
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Internal prochot cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; External prochot cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Thermal freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "\n";
                break;

            case 3:
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Thermal freq limit cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Power freq limit cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %";
                if(cpu_model != PCM::SKX && cpu_model != PCM::ICX && cpu_model != PCM::SNOWRIDGE)
                    cout << "; Clipped freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %";
                cout << "\n";
                break;

            case 4:
                if (cpu_model == PCM::SKX || cpu_model == PCM::ICX || cpu_model == PCM::SNOWRIDGE)
                {
                    cout << "This PCU profile is not supported on your processor\n";
                    break;
                }
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; OS freq limit cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Power freq limit cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Clipped freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "\n";
                break;
            case 5:
                cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Frequency transition count: " << getPCUCounter(1, BeforeState[socket], AfterState[socket]) << " "
                          << "; Cycles spent changing frequency: " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket], m) * 100. << " %";
                if (PCM::HASWELLX == cpu_model) {
                    cout << "; UFS transition count: " << getPCUCounter(3, BeforeState[socket], AfterState[socket]) << " ";
                    cout << "; UFS transition cycles: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %";
                }
                cout << "\n";
                break;
            case 6:
                cout << "S" << socket;

                if (cpu_model == PCM::HASWELLX || PCM::BDX_DE == cpu_model)
                    cout << "; PC1e+ residency: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                        "; PC1e+ transition count: " << getPCUCounter(1, BeforeState[socket], AfterState[socket]) << " ";

                if (cpu_model == PCM::IVYTOWN || cpu_model == PCM::HASWELLX || PCM::BDX_DE == cpu_model || PCM::SKX == cpu_model || PCM::ICX == cpu_model || cpu_model == PCM::SNOWRIDGE)
                {
                    cout << "; PC2 residency: " << getPackageCStateResidency(2, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    cout << "; PC2 transitions: " << getPCUCounter(2, BeforeState[socket], AfterState[socket]) << " ";
                    cout << "; PC3 residency: " << getPackageCStateResidency(3, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    cout << "; PC6 residency: " << getPackageCStateResidency(6, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    cout << "; PC6 transitions: " << getPCUCounter(3, BeforeState[socket], AfterState[socket]) << " ";
                }

                cout << "\n";
                break;
            case 7:
                if (PCM::HASWELLX == cpu_model || PCM::BDX_DE == cpu_model || PCM::BDX == cpu_model) {
                    cout << "S" << socket
                              << "; UFS_TRANSITIONS_PERF_P_LIMIT: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_IO_P_LIMIT: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_UP_RING_TRAFFIC: " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_UP_STALL_CYCLES: " << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "\n";
                }
                break;
            case 8:
                if (PCM::HASWELLX == cpu_model || PCM::BDX_DE == cpu_model || PCM::BDX == cpu_model) {
                    cout << "S" << socket
                              << "; UFS_TRANSITIONS_DOWN: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "\n";
                }
                break;
            }

            cout << "S" << socket
                      << "; Consumed energy units: " << getConsumedEnergy(BeforeState[socket], AfterState[socket])
                      << "; Consumed Joules: " << getConsumedJoules(BeforeState[socket], AfterState[socket])
                      << "; Watts: " << 1000. * getConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime)
                      << "; Thermal headroom below TjMax: " << AfterState[socket].getPackageThermalHeadroom()
                      << "\n";
            cout << "S" << socket
                      << "; Consumed DRAM energy units: " << getDRAMConsumedEnergy(BeforeState[socket], AfterState[socket])
                      << "; Consumed DRAM Joules: " << getDRAMConsumedJoules(BeforeState[socket], AfterState[socket])
                      << "; DRAM Watts: " << 1000. * getDRAMConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime)
                      << "\n";
        }
        swap(BeforeState, AfterState);
        swap(BeforeTime, AfterTime);
        swap(beforeSocketState, afterSocketState);

        if (m->isBlocked()) {
            cout << "----------------------------------------------------------------------------------------------\n";
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    exit(EXIT_SUCCESS);
}
