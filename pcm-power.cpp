/*
Copyright (c) 2009-2014, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// written by Roman Dementiev
// added PPD cycles by Thomas Willhalm

#define HACK_TO_REMOVE_DUPLICATE_ERROR
#include "cpucounters.h"
#ifdef _MSC_VER
#include <windows.h>
#include "../PCM_Win/windriver.h"
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
#define PCM_CALIBRATION_INTERVAL 50 // calibrate clock only every 50th iteration

using namespace std;

int getFirstRank(int imc_profile)
{
    return imc_profile * 2;
}
int getSecondRank(int imc_profile)
{
    return (imc_profile * 2) + 1;
}

double getCKEOffResidency(uint32 channel, uint32 rank, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    return double(getMCCounter(channel, (rank & 1) ? 2 : 0, before, after)) / double(getDRAMClocks(channel, before, after));
}

int64 getCKEOffAverageCycles(uint32 channel, uint32 rank, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    uint64 div = getMCCounter(channel, (rank & 1) ? 3 : 1, before, after);
    if (div)
        return getMCCounter(channel, (rank & 1) ? 2 : 0, before, after) / div;

    return -1;
}

int64 getCyclesPerTransition(uint32 channel, uint32 rank, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    uint64 div = getMCCounter(channel, (rank & 1) ? 3 : 1, before, after);
    if (div)
        return getDRAMClocks(channel, before, after) / div;

    return -1;
}

uint64 getSelfRefreshCycles(uint32 channel, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    return getMCCounter(channel, 0, before, after);
}

uint64 getSelfRefreshTransitions(uint32 channel, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    return getMCCounter(channel, 1, before, after);
}

uint64 getPPDCycles(uint32 channel, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    return getMCCounter(channel, 2, before, after);
}

double getNormalizedPCUCounter(uint32 counter, const ServerUncorePowerState & before, const ServerUncorePowerState & after)
{
    return double(getPCUCounter(counter, before, after)) / double(getPCUClocks(before, after));
}

double getNormalizedPCUCounter(uint32 counter, const ServerUncorePowerState & before, const ServerUncorePowerState & after, PCM * m)
{
    const uint64 PCUClocks = (m->getPCUFrequency() * getInvariantTSC(before, after)) / m->getNominalFrequency();
    //std::cout << "PCM Debug: PCU clocks "<< PCUClocks << std::endl;
    return double(getPCUCounter(counter, before, after)) / double(PCUClocks);
}

int default_freq_band[3] = { 12, 20, 40 };
int freq_band[3];

void print_usage(const string progname)
{
    cerr << endl << " Usage: " << endl << " " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]" << endl;
    cerr << "   <delay>                           => time interval to sample performance counters." << endl;
    cerr << "                                        If not specified, or 0, with external program given" << endl;
    cerr << "                                        will read counters only after external program finishes" << endl;
    cerr << " Supported <options> are: " << endl;
    cerr << "  -h    | --help  | /h               => print this help and exit" << endl;
//    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or" << endl
//         << "                                        to a file, in case filename is provided" << endl;
    cerr << "  [-m imc_profile] [-p pcu_profile] [-a freq_band0] [-b freq_band1] [-c freq_band2]" << endl << endl;
    cerr << " Where: imc_profile, pcu_profile, freq_band0, freq_band1 and freq_band2 are the following:" << endl;
    cerr << "  <imc_profile>      - profile (counter group) for IMC PMU. Possible values are: 0,1,2,3,4,-1 \n";
    cerr << "                       profile  0 - rank 0 and rank 1 residencies (default) \n";
    cerr << "                       profile  1 - rank 2 and rank 3 residencies \n";
    cerr << "                       profile  2 - rank 4 and rank 5 residencies \n";
    cerr << "                       profile  3 - rank 6 and rank 7 residencies \n";
    cerr << "                       profile  4 - self-refresh residencies \n";
    cerr << "                       profile -1 - omit IMC PMU output\n";
    cerr << "  <pcu_profile>      - profile (counter group) for PCU PMU. Possible values are: 0,1,2,3,4,5,-1 \n";
    cerr << "                       profile  0 - frequency residencies (default) \n";
    cerr << "                       profile  1 - core C-state residencies. The unit is the number of physical cores on the socket who were in C0, C3 or C6 during the measurement interval (e.g. 'C0 residency is 3.5' means on average 3.5 physical cores were resident in C0 state)\n";
    cerr << "                       profile  2 - Prochot (throttled) residencies and thermal frequency limit cycles \n";
    cerr << "                       profile  3 - {Thermal,Power,Clipped} frequency limit cycles \n";
    cerr << "                       profile  4 - {OS,Power,Clipped} frequency limit cycles \n";
    cerr << "                       profile  5 - frequency transition statistics \n";
    cerr << "                       profile  6 - package C-states residency and transition statistics \n";
    cerr << "                       profile  7 - UFS transition statistics (1) \n";
    cerr << "                       profile  8 - UFS transition statistics (2) \n";
    cerr << "                       profile -1 - omit PCU PMU output\n";
    cerr << "  <freq_band0>       - frequency minumum for band 0 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[0] << "= " << 100 * default_freq_band[0] << "MHz)\n";
    cerr << "  <freq_band1>       - frequency minumum for band 1 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[1] << "= " << 100 * default_freq_band[1] << "MHz)\n";
    cerr << "  <freq_band2>       - frequency minumum for band 2 for PCU frequency residency profile [in 100MHz units] (default is " <<
        default_freq_band[2] << "= " << 100 * default_freq_band[2] << "MHz)\n";
    cerr << endl;
}

int main(int argc, char * argv[])
{
    set_signal_handlers();

    std::cerr << "\n Processor Counter Monitor " << PCM_VERSION << std::endl;
    std::cerr << "\n Power Monitoring Utility\n";

    int imc_profile = 0;
    int pcu_profile = 0;
    double delay = -1.0;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;

    freq_band[0] = default_freq_band[0];
    freq_band[1] = default_freq_band[1];
    freq_band[2] = default_freq_band[2];

    bool csv = false;
    long diff_usec = 0;                            // deviation of clock is useconds between measurements
    int calibrated = PCM_CALIBRATION_INTERVAL - 2; // keeps track is the clock calibration needed
    unsigned int numberOfIterations = 0; // number of iterations
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
                print_usage(program);
                exit(EXIT_FAILURE);
            }
            else if (strncmp(*argv, "-csv", 4) == 0 ||
                     strncmp(*argv, "/csv", 4) == 0)
            {
                csv = true;
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
            else if (strncmp(*argv, "-m", 2) == 0)
            {
                argv++;
                argc--;
                imc_profile = atoi(*argv);
                continue;
            }
            else if (strncmp(*argv, "-p", 2) == 0)
            {
                argv++;
                argc--;
                pcu_profile = atoi(*argv);
                continue;
            }
            else if (strncmp(*argv, "-a", 2) == 0)
            {
                argv++;
                argc--;
                freq_band[0] = atoi(*argv);
                continue;
            }
            else if (strncmp(*argv, "-b", 2) == 0)
            {
                argv++;
                argc--;
                freq_band[1] = atoi(*argv);
                continue;
            }
            else if (strncmp(*argv, "-c", 2) == 0)
            {
                argv++;
                argc--;
                freq_band[2] = atoi(*argv);
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
                if (is_str_stream.eof() && !is_str_stream.fail()) {
                    delay = delay_input;
                } else {
                    cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it." << endl;
                    print_usage(program);
                    exit(EXIT_FAILURE);
                }
                continue;
            }
        } while (argc > 1); // end of command line partsing loop

    m->disableJKTWorkaround();

    const int cpu_model = m->getCPUModel();
    if (!(m->hasPCICFGUncore()))
    {
        std::cerr << "Unsupported processor model (" << cpu_model << ")." << std::endl;
        exit(EXIT_FAILURE);
    }

    if (PCM::Success != m->programServerUncorePowerMetrics(imc_profile, pcu_profile, freq_band))
    {
#ifdef _MSC_VER
        std::cerr << "You must have signed msr.sys driver in your current directory and have administrator rights to run this program" << std::endl;
#elif defined(__linux__)
        std::cerr << "You need to be root and loaded 'msr' Linux kernel module to execute the program. You may load the 'msr' module with 'modprobe msr'. \n";
#endif
        exit(EXIT_FAILURE);
    }
    ServerUncorePowerState * BeforeState = new ServerUncorePowerState[m->getNumSockets()];
    ServerUncorePowerState * AfterState = new ServerUncorePowerState[m->getNumSockets()];
    uint64 BeforeTime = 0, AfterTime = 0;

    std::cerr << std::dec << std::endl;
    std::cerr.precision(2);
    std::cerr << std::fixed;
    std::cout << std::dec << std::endl;
    std::cout.precision(2);
    std::cout << std::fixed;
    std::cerr << "\nMC counter group: " << imc_profile << std::endl;
    std::cerr << "PCU counter group: " << pcu_profile << std::endl;
    if (pcu_profile == 0) {
        if (cpu_model == PCM::HASWELLX || cpu_model == PCM::BDX_DE || cpu_model == PCM::SKX)
            std::cerr << "Your processor does not support frequency band statistics" << std::endl;
        else
            std::cerr << "Freq bands [0/1/2]: " << freq_band[0] * 100 << " MHz; " << freq_band[1] * 100 << " MHz; " << freq_band[2] * 100 << " MHz; " << std::endl;
    }
    if (sysCmd != NULL)
        std::cerr << "Update every " << delay << " seconds" << std::endl;

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    } else {
        m->setBlocked(false);
    }

    if (((delay < 1.0) && (delay > 0.0)) || (delay <= 0.0)) delay = PCM_DELAY_DEFAULT;

    uint32 i = 0;

    for (i = 0; i < m->getNumSockets(); ++i)
        BeforeState[i] = m->getServerUncorePowerState(i);

    BeforeTime = m->getTickCount();
    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    unsigned int ic = 1;

    while ((ic <= numberOfIterations) || (numberOfIterations == 0))
    {
        std::cout << "----------------------------------------------------------------------------------------------" << std::endl;

        if (!csv) cout << std::flush;
        int delay_ms = int(delay * 1000);
        int calibrated_delay_ms = delay_ms;
#ifdef _MSC_VER
        // compensate slow Windows console output
        if (AfterTime) delay_ms -= (int)(m->getTickCount() - BeforeTime);
        if (delay_ms < 0) delay_ms = 0;
#else
        // compensation of delay on Linux/UNIX
        // to make the samling interval as monotone as possible
        struct timeval start_ts, end_ts;
        if (calibrated == 0) {
            gettimeofday(&end_ts, NULL);
            diff_usec = (end_ts.tv_sec - start_ts.tv_sec) * 1000000.0 + (end_ts.tv_usec - start_ts.tv_usec);
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

        AfterTime = m->getTickCount();
        for (i = 0; i < m->getNumSockets(); ++i)
            AfterState[i] = m->getServerUncorePowerState(i);

        std::cout << "Time elapsed: " << AfterTime - BeforeTime << " ms\n";
        std::cout << "Called sleep function for " << delay_ms << " ms\n";
        for (uint32 socket = 0; socket < m->getNumSockets(); ++socket)
        {
            for (uint32 port = 0; port < m->getQPILinksPerSocket(); ++port)
            {
                std::cout << "S" << socket << "P" << port
                          << "; QPIClocks: " << getQPIClocks(port, BeforeState[socket], AfterState[socket])
                          << "; L0p Tx Cycles: " << 100. * getNormalizedQPIL0pTxCycles(port, BeforeState[socket], AfterState[socket]) << "%"
                          << "; L1 Cycles: " << 100. * getNormalizedQPIL1Cycles(port, BeforeState[socket], AfterState[socket]) << "%"
                          << "\n";
            }
            for (uint32 channel = 0; channel < m->getMCChannelsPerSocket(); ++channel)
            {
                if (imc_profile <= 3 && imc_profile >= 0)
                {
                    std::cout << "S" << socket << "CH" << channel << "; DRAMClocks: " << getDRAMClocks(channel, BeforeState[socket], AfterState[socket])
                              << "; Rank" << getFirstRank(imc_profile) << " CKE Off Residency: " << std::setw(3) <<
                        100. * getCKEOffResidency(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket]) << "%"
                              << "; Rank" << getFirstRank(imc_profile) << " CKE Off Average Cycles: " <<
                        getCKEOffAverageCycles(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "; Rank" << getFirstRank(imc_profile) << " Cycles per transition: " <<
                        getCyclesPerTransition(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "\n";

                    std::cout << "S" << socket << "CH" << channel << "; DRAMClocks: " << getDRAMClocks(channel, BeforeState[socket], AfterState[socket])
                              << "; Rank" << getSecondRank(imc_profile) << " CKE Off Residency: " << std::setw(3) <<
                        100. * getCKEOffResidency(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket]) << "%"
                              << "; Rank" << getSecondRank(imc_profile) << " CKE Off Average Cycles: " <<
                        getCKEOffAverageCycles(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "; Rank" << getSecondRank(imc_profile) << " Cycles per transition: " <<
                        getCyclesPerTransition(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket])
                              << "\n";
                } else if (imc_profile == 4)
                {
                    std::cout << "S" << socket << "CH" << channel
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
                std::cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Freq band 0/1/2 cycles: " << 100. * getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) << "%"
                          << "; " << 100. * getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) << "%"
                          << "; " << 100. * getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) << "%"
                          << "\n";
                break;

            case 1:
                std::cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << ((cpu_model == PCM::SKX)?"; core C0_1/C3/C6_7-state residency: ":"; core C0/C3/C6-state residency: ") 
                          << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket])
                          << "; " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket])
                          << "; " << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket])
                          << "\n";
                break;

            case 2:
                std::cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Internal prochot cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; External prochot cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Thermal freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "\n";
                break;

            case 3:
                std::cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; Thermal freq limit cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Power freq limit cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %";
                if(cpu_model != PCM::SKX)
                    std::cout << "; Clipped freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %";
                std::cout << "\n";
                break;

            case 4:
                if(cpu_model == PCM::SKX)
                {
                    std::cout << "This PCU profile is not supported on your processor\n";
                    break;
                }
                std::cout << "S" << socket
                          << "; PCUClocks: " << getPCUClocks(BeforeState[socket], AfterState[socket])
                          << "; OS freq limit cycles: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Power freq limit cycles:" << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "; Clipped freq limit cycles:" << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket]) * 100. << " %"
                          << "\n";
                break;
            case 5:
                std::cout << "S" << socket
                          << "; Frequency transition count: " << getPCUCounter(1, BeforeState[socket], AfterState[socket]) << " "
                          << "; Cycles spent changing frequency: " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket], m) * 100. << " %";
                if (PCM::HASWELLX == cpu_model) {
                    std::cout << "; UFS transition count: " << getPCUCounter(3, BeforeState[socket], AfterState[socket]) << " ";
                    std::cout << "; UFS transition cycles: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %";
                }
                std::cout << "\n";
                break;
            case 6:
                std::cout << "S" << socket;

                if (cpu_model == PCM::HASWELLX || PCM::BDX_DE == cpu_model)
                    std::cout << "; PC1e+ residency: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                        "; PC1e+ transition count: " << getPCUCounter(1, BeforeState[socket], AfterState[socket]) << " ";

                if (cpu_model == PCM::IVYTOWN || cpu_model == PCM::HASWELLX || PCM::BDX_DE == cpu_model || PCM::SKX == cpu_model)
                {
                    std::cout << "; PC2 residency: " << getPackageCStateResidency(2, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    std::cout << "; PC2 transitions: " << getPCUCounter(2, BeforeState[socket], AfterState[socket]) << " ";
                    std::cout << "; PC3 residency: " << getPackageCStateResidency(3, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    std::cout << "; PC6 residency: " << getPackageCStateResidency(6, BeforeState[socket], AfterState[socket]) * 100. << " %";
                    std::cout << "; PC6 transitions: " << getPCUCounter(3, BeforeState[socket], AfterState[socket]) << " ";
                }

                std::cout << "\n";
                break;
            case 7:
                if (PCM::HASWELLX == cpu_model || PCM::BDX_DE == cpu_model || PCM::BDX == cpu_model) {
                    std::cout << "S" << socket
                              << "; UFS_TRANSITIONS_PERF_P_LIMIT: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_IO_P_LIMIT: " << getNormalizedPCUCounter(1, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_UP_RING_TRAFFIC: " << getNormalizedPCUCounter(2, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "; UFS_TRANSITIONS_UP_STALL_CYCLES: " << getNormalizedPCUCounter(3, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "\n";
                }
                break;
            case 8:
                if (PCM::HASWELLX == cpu_model || PCM::BDX_DE == cpu_model || PCM::BDX == cpu_model) {
                    std::cout << "S" << socket
                              << "; UFS_TRANSITIONS_DOWN: " << getNormalizedPCUCounter(0, BeforeState[socket], AfterState[socket], m) * 100. << " %"
                              << "\n";
                }
                break;
            }

            std::cout << "S" << socket
                      << "; Consumed energy units: " << getConsumedEnergy(BeforeState[socket], AfterState[socket])
                      << "; Consumed Joules: " << getConsumedJoules(BeforeState[socket], AfterState[socket])
                      << "; Watts: " << 1000. * getConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime)
                      << "; Thermal headroom below TjMax: " << AfterState[socket].getPackageThermalHeadroom()
                      << "\n";
            std::cout << "S" << socket
                      << "; Consumed DRAM energy units: " << getDRAMConsumedEnergy(BeforeState[socket], AfterState[socket])
                      << "; Consumed DRAM Joules: " << getDRAMConsumedJoules(BeforeState[socket], AfterState[socket])
                      << "; DRAM Watts: " << 1000. * getDRAMConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime)
                      << "\n";
        }
        std::swap(BeforeState, AfterState);
        std::swap(BeforeTime, AfterTime);

        if (m->isBlocked()) {
            std::cout << "----------------------------------------------------------------------------------------------" << std::endl;
            // in case PCM was blocked after spawning child application: break monitoring loop here
            break;
        }
	++ic;
    }

    delete[] BeforeState;
    delete[] AfterState;
    exit(EXIT_SUCCESS);
}
