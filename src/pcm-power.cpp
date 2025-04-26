// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation

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
#include <variant>
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

double getNormalizedPCUCounter(uint32 unit, uint32 counter, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after)
{
    const auto clk = getPCUClocks(unit, before, after);
    if (clk)
    {
        return double(getUncoreCounter(PCM::PCU_PMU_ID, unit, counter, before, after)) / double(clk);
    }
    return -1.0;
}

double getNormalizedPCUCounter(uint32 unit, uint32 counter, const ServerUncoreCounterState & before, const ServerUncoreCounterState & after, PCM * m)
{
    const uint64 PCUClocks = (m->getPCUFrequency() * getInvariantTSC(before, after)) / m->getNominalFrequency();
    // cout << "PCM Debug: PCU clocks " << PCUClocks << " PCU frequency: " << m->getPCUFrequency() << "\n";
    return double(getUncoreCounter(PCM::PCU_PMU_ID, unit, counter, before, after)) / double(PCUClocks);
}

namespace PERF_LIMIT_REASON_TPMI
{
    // from https://github.com/intel/tpmi_power_management
    //      https://github.com/intel/tpmi_power_management/blob/main/TPMI_Perf_Limit_reasons_rev3.pdf
    const auto PERF_LIMIT_REASON_TPMI_ID = 0xC;
    const auto PERF_LIMIT_REASON_TPMI_HEADER            = 0x0;
    const auto PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE = 0x8;
    const auto PERF_LIMIT_REASON_TPMI_MAILBOX_DATA      = 0x10;
    const auto PERF_LIMIT_REASON_TPMI_DIE_LEVEL         = 0x18;
    const auto PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_COMMAND_WRITE = 1ULL;
    const auto PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_ID_SHIFT = 12ULL;
    const auto PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_RUN_BUSY = (1ULL << 63ULL);

    bool isSupported()
    {
        bool PERF_LIMIT_REASON_TPMI_Supported = false;
        auto numTPMIInstances = TPMIHandle::getNumInstances();
        for (size_t i = 0; i < numTPMIInstances; ++i)
        {
            TPMIHandle h(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_HEADER);
            for (uint32 j = 0; j < h.getNumEntries(); ++j)
            {
                const auto header = h.read64(j);
                if (header == ~0ULL)
                {
                    return false;
                }
                const auto version =      extract_bits_64(header, 7, 0);
                const auto majorVersion = extract_bits_64(header, 7, 5);
                const auto minorVersion = extract_bits_64(header, 4, 0);
                DBG(1, "PLR_HEADER.INTERFACE_VERSION instance ", i, " die ", j, " version ", version, " (major version ", majorVersion, " minor version ", minorVersion, ")");
                PERF_LIMIT_REASON_TPMI_Supported = true;
            }
        }
        return PERF_LIMIT_REASON_TPMI_Supported;
    }

    int getMaxPMModuleID(const PCM * m)
    {
        int max_pm_module_id = -1;
        for (unsigned int core = 0; core < m->getNumCores(); ++core)
        {
            if (m->isCoreOnline(core) == false)
                continue;

            MsrHandle msr(core);
            uint64 val = 0;
            constexpr auto MSR_PM_LOGICAL_ID = 0x54;
            msr.read(MSR_PM_LOGICAL_ID, &val);
            const auto module_id = (int)extract_bits(val, 10, 3);
            max_pm_module_id = (std::max)(max_pm_module_id, module_id);
        }
        return max_pm_module_id;
    }

    void reset(const size_t max_pm_modules)
    {
        auto numTPMIInstances = TPMIHandle::getNumInstances();
        for (size_t i = 0; i < numTPMIInstances; ++i)
        {
            TPMIHandle die_level(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_DIE_LEVEL, false);
            for (uint32 j = 0; j < die_level.getNumEntries(); ++j)
            {
                die_level.write64(j, 0);
            }
            for (size_t module = 0; module < max_pm_modules; ++module)
            {
                TPMIHandle mailbox_data(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_MAILBOX_DATA, false);
                TPMIHandle mailbox_interface(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE, false);
                assert(mailbox_data.getNumEntries() == mailbox_interface.getNumEntries());
                for (uint32 j = 0; j < mailbox_data.getNumEntries(); ++j)
                {
                    mailbox_data.write64(j, 0);
                    const uint64 value =    PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_COMMAND_WRITE |
                                            (module << PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_ID_SHIFT) |
                                            PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_RUN_BUSY ;
                    mailbox_interface.write64(j, value);
                    while (mailbox_interface.read64(j) & PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_RUN_BUSY)
                    {
                        // wait for the command to be processed
                    }
                }
            }
        }
    };

    std::vector<std::vector<uint64>> readDies()
    {
        auto numTPMIInstances = TPMIHandle::getNumInstances();
        std::vector<std::vector<uint64>> data;
        for (size_t i = 0; i < numTPMIInstances; ++i)
        {
            std::vector<uint64> instanceData;
            TPMIHandle die_level(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_DIE_LEVEL);
            for (uint32 j = 0; j < die_level.getNumEntries(); ++j)
            {
                instanceData.push_back(die_level.read64(j));
            }
            data.push_back(instanceData);
        }
        return data;
    };

    std::vector<std::vector<std::vector<uint64>>> readModules(const size_t max_pm_modules)
    {
        auto numTPMIInstances = TPMIHandle::getNumInstances();
        std::vector<std::vector<std::vector<uint64>>> data;
        for (size_t i = 0; i < numTPMIInstances; ++i)
        {
            std::vector<std::vector<uint64>> moduleData;
            for (size_t module = 0; module < max_pm_modules; ++module)
            {
                std::vector<uint64> instanceData;
                TPMIHandle mailbox_data(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_MAILBOX_DATA);
                TPMIHandle mailbox_interface(i, PERF_LIMIT_REASON_TPMI_ID, PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE, false);
                assert(mailbox_data.getNumEntries() == mailbox_interface.getNumEntries());
                for (uint32 j = 0; j < mailbox_data.getNumEntries(); ++j)
                {
                    const uint64 value =    (module << PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_ID_SHIFT) |
                                            PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_RUN_BUSY ;
                    mailbox_interface.write64(j, value);
                    while (mailbox_interface.read64(j) & PERF_LIMIT_REASON_TPMI_MAILBOX_INTERFACE_RUN_BUSY)
                    {
                        // wait for the command to be processed
                    }
                    instanceData.push_back(mailbox_data.read64(j));
                }
                moduleData.push_back(instanceData);
            }
            data.push_back(moduleData);
        }
        return data;
    };

    enum Coarse_Grained_PLR_Bit_Definition
    {
        FREQUENCY = 0,
        CURRENT = 1,
        POWER = 2,
        THERMAL = 3,
        PLATFORM = 4,
        MCP = 5,
        RAS = 6,
        MISC = 7,
        QOS = 8,
        DFC = 9,
        MAX = 10
    };
    const char * Coarse_Grained_PLR_Bit_Definition_Strings[] = {
        "FREQUENCY", // Limitation due to Turbo Ratio Limit (TRL)
        "CURRENT",   // Package ICCmax or MT-Pmax
        "POWER",     // Socket or Platform RAPL
        "THERMAL",   // Thermal Throttling
        "PLATFORM",  // Prochot or Hot VR
        "MCP",       // freq limit due to a companion die like PCH
        "RAS",       // freq limit due to RAS
        "MISC",      // Freq limit from out-of-band SW (e.g. BMC)
        "QOS",       // SST-CP, SST-BF, SST-TF
        "DFC"        // Freq limitation due to Dynamic Freq Capping
    };
    enum Fine_Grained_PLR_Bit_Definition
    {
        CDYN0 = 0,
        CDYN1 = 1,
        CDYN2 = 2,
        CDYN3 = 3,
        CDYN4 = 4,
        CDYN5 = 5,
        FCT = 6,
        PCS_TRL = 7,
        MTPMAX = 8,
        FAST_RAPL = 9,
        PKG_PL1_MSR_TPMI = 10,
        PKG_PL1_MMIO = 11,
        PKG_PL1_PCS = 12,
        PKG_PL2_MSR_TPMI = 13,
        PKG_PL2_MMIO = 14,
        PKG_PL2_PCS = 15,
        PLATFORM_PL1_MSR_TPMI = 16,
        PLATFORM_PL1_MMIO = 17,
        PLATFORM_PL1_PCS = 18,
        PLATFORM_PL2_MSR_TPMI = 19,
        PLATFORM_PL2_MMIO = 20,
        PLATFORM_PL2_PCS = 21,
        RSVD = 22,
        PER_CORE_THERMAL = 23,
        UFS_DFC = 24,
        XXPROCHOT = 25,
        HOT_VR = 26,
        RSVD2 = 27,
        RSVD3 = 28,
        PCS_PSTATE = 29,
        MAX_FINE = 30
    };
    struct FGData
    {
        const char * name;
        int coarse_grained_mapping;
        FGData(const char * n, int c) : name(n), coarse_grained_mapping(c) {}
    };
    const FGData Fine_Grained_PLR_Bit_Definition_Data[] = {
        FGData("TRL/CDYN0", FREQUENCY), // Turbo Ratio Limit 0
        FGData("TRL/CDYN1", FREQUENCY), // Turbo Ratio Limit 1
        FGData("TRL/CDYN2", FREQUENCY), // Turbo Ratio Limit 2
        FGData("TRL/CDYN3", FREQUENCY), // Turbo Ratio Limit 3
        FGData("TRL/CDYN4", FREQUENCY), // Turbo Ratio Limit 4
        FGData("TRL/CDYN5", FREQUENCY), // Turbo Ratio Limit 5
        FGData("FCT", FREQUENCY),       // Favored Core Turbo
        FGData("PCS_TRL", FREQUENCY),   // Turbo Ratio Limit from out-of-band (BMC)
        FGData("MTPMAX", CURRENT),
        FGData("FAST_RAPL", POWER),
        FGData("PKG_PL1_MSR_TPMI", POWER),
        FGData("PKG_PL1_MMIO", POWER),
        FGData("PKG_PL1_PCS", POWER),
        FGData("PKG_PL2_MSR_TPMI", POWER),
        FGData("PKG_PL2_MMIO", POWER),
        FGData("PKG_PL2_PCS", POWER),
        FGData("PLATFORM_PL1_MSR_TPMI", POWER),
        FGData("PLATFORM_PL1_MMIO", POWER),
        FGData("PLATFORM_PL1_PCS", POWER),
        FGData("PLATFORM_PL2_MSR_TPMI", POWER),
        FGData("PLATFORM_PL2_MMIO", POWER),
        FGData("PLATFORM_PL2_PCS", POWER),
        FGData("RSVD", POWER),
        FGData("PER_CORE_THERMAL", THERMAL), // Thermal Throttling
        FGData("UFS_DFC", DFC),              // Dynamic Freq Capping
        FGData("XXPROCHOT", PLATFORM),
        FGData("HOT_VR", PLATFORM),
        FGData("RSVD2", PLATFORM),
        FGData("RSVD3", PLATFORM),
        FGData("PCS_PSTATE", MISC)
    };
};

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
    cout << "  --version                          => print application version\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
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

struct Metric
{
    typedef std::variant<double, uint64, int64, int32, bool> ValueType;
    std::string name{};
    ValueType value{};
    std::string unit{};
    Metric(const std::string & n, const ValueType & v, const std::string & u) : name(n), value(v), unit(u) {}
    Metric() = default;
};

bool csv = false;
bool csv_header = false;

void printMetrics(const std::string & header, const std::vector<Metric> & metrics, const CsvOutputType outputType, const bool skipZeroValues = false)
{
    if (csv)
    {
        const auto numMetrics = metrics.size();
        choose(outputType,
            [&]() {
                for (size_t i = 0; i < numMetrics; ++i)
                {
                    cout << header << ',';
                }
            },
            [&]() {
                for (size_t i = 0; i < numMetrics; ++i)
                {
                    cout << metrics[i].name;
                    if (!metrics[i].unit.empty())
                    {
                        cout << '(' << metrics[i].unit << ')';
                    }
                    cout << ',';
                }
            },
            [&]() {
                for (size_t i = 0; i < numMetrics; ++i)
                {
                    if (std::holds_alternative<uint64>(metrics[i].value))
                    {
                        cout << std::get<uint64>(metrics[i].value);
                    }
                    else if (std::holds_alternative<double>(metrics[i].value))
                    {
                        cout << std::get<double>(metrics[i].value);
                    }
                    else if (std::holds_alternative<int64>(metrics[i].value))
                    {
                        cout << std::get<int64>(metrics[i].value);
                    }
                    else if (std::holds_alternative<int32>(metrics[i].value))
                    {
                        cout << std::get<int32>(metrics[i].value);
                    }
                    else if (std::holds_alternative<bool>(metrics[i].value))
                    {
                        cout << (std::get<bool>(metrics[i].value) ? '1' : '0');
                    }
                    else
                    {
                        assert(false && "Unknown metric type");
                    }
                    cout << ',';
                }
            });
        return;
    }
    cout << header << "; ";
    for (const auto & metric : metrics)
    {
        if (skipZeroValues)
        {
            if (std::holds_alternative<uint64>(metric.value) && std::get<uint64>(metric.value) == 0)
            {
                continue;
            }
            if (std::holds_alternative<double>(metric.value) && std::get<double>(metric.value) == 0.0)
            {
                continue;
            }
            if (std::holds_alternative<int64>(metric.value) && std::get<int64>(metric.value) == 0)
            {
                continue;
            }
            if (std::holds_alternative<int32>(metric.value) && std::get<int32>(metric.value) == 0)
            {
                continue;
            }
        }
        if (std::holds_alternative<bool>(metric.value))
        {
            if (std::get<bool>(metric.value))
            {
                cout << metric.name << ";";
            }
            continue;
        }
        cout << metric.name << ": ";
        if (std::holds_alternative<uint64>(metric.value))
        {
            cout << std::get<uint64>(metric.value);
        }
        else if (std::holds_alternative<double>(metric.value))
        {
            cout << std::get<double>(metric.value);
        }
        else if (std::holds_alternative<int64>(metric.value))
        {
            cout << std::get<int64>(metric.value);
        }
        else if (std::holds_alternative<int32>(metric.value))
        {
            cout << std::get<int32>(metric.value);
        }
        else
        {
            assert(false && "Unknown metric type");
        }
        if (!metric.unit.empty())
        {
            cout << " " << metric.unit;
        }
        cout << "; ";
    }
    cout << "\n";
}

void printMetrics(const std::string & header, const std::initializer_list<Metric> & metrics, const CsvOutputType outputType, const bool skipZeroValues = false)
{
    std::vector<Metric> metricsVec;
    for (const auto & metric : metrics)
    {
        metricsVec.push_back(metric);
    }
    printMetrics(header, metricsVec, outputType, skipZeroValues);
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);

    set_signal_handlers();

    cerr << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";
    cerr << "\n Power Monitoring Utility\n";

    int imc_profile = 0;
    int pcu_profile = 0;
    double delay = -1.0;
    char * sysCmd = NULL;
    char ** sysArgv = NULL;

    freq_band[0] = default_freq_band[0];
    freq_band[1] = default_freq_band[1];
    freq_band[2] = default_freq_band[2];

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
                csv_header = true;
            }
            else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
            {
                csv = true;
                csv_header = true;
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

    const int cpu_family_model = m->getCPUFamilyModel();
    if (!(m->hasPCICFGUncore()))
    {
        cerr << "Unsupported processor model (0x" << std::hex << cpu_family_model << std::dec << ").\n";
        exit(EXIT_FAILURE);
    }

    EventSelectRegister regs[PERF_MAX_CUSTOM_COUNTERS];
    PCM::ExtendedCustomCoreEventDescription conf;
    int32 nCorePowerLicenses = 0;
    std::vector<std::string> licenseStr;
    switch (cpu_family_model)
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

    cerr << dec;
    cerr.precision(2);
    cerr << fixed;
    cout << dec;
    cout.precision(2);
    cout << fixed;
    cerr << "\nMC counter group: " << imc_profile << "\n";
    cerr << "PCU counter group: " << pcu_profile << "\n";
    if (pcu_profile == 0) {
        if (cpu_family_model == PCM::HASWELLX || cpu_family_model == PCM::BDX_DE || cpu_family_model == PCM::SKX)
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

    const bool PERF_LIMIT_REASON_TPMI_Supported = PERF_LIMIT_REASON_TPMI::isSupported();

    const size_t max_pm_modules = PERF_LIMIT_REASON_TPMI_Supported ? (size_t)(PERF_LIMIT_REASON_TPMI::getMaxPMModuleID(m) + 1) : 0;
    DBG(1, "max_pm_modules = ", max_pm_modules);
    std::vector<std::vector<uint64>> PERF_LIMIT_REASON_TPMI_dies_data;
    std::vector<std::vector<std::vector<uint64>>> PERF_LIMIT_REASON_TPMI_modules_data;

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

    if (PERF_LIMIT_REASON_TPMI_Supported) PERF_LIMIT_REASON_TPMI::reset(max_pm_modules);

    auto printAll = [&](const int delay_ms, const CsvOutputType outputType)
    {
        if (csv)
        {
            printDateForCSV(outputType);
        }
        else
        {
            cout << "Time elapsed: " << AfterTime - BeforeTime << " ms\n";
            cout << "Called sleep function for " << delay_ms << " ms\n";
        }
        for (uint32 socket = 0; socket < numSockets; ++socket)
        {
            if (nCorePowerLicenses)
            {
                std::vector<Metric> metrics;
                metrics.push_back(Metric("Uncore Freq", getAverageUncoreFrequency(BeforeState[socket], AfterState[socket]) * uncoreFreqFactor / 1e9, "Ghz"));
                metrics.push_back(Metric("Core Freq", getActiveAverageFrequency(beforeSocketState[socket], afterSocketState[socket]) / 1e9, "Ghz"));
                for (int32 l = 0; l < nCorePowerLicenses; ++l)
                {
                    metrics.push_back(Metric("Core Power License " + std::to_string(l), getPowerLicenseResidency(l, beforeSocketState[socket], afterSocketState[socket]), "%"));
                }
                printMetrics("S" + std::to_string(socket), metrics, outputType);
            }
            for (uint32 port = 0; port < m->getQPILinksPerSocket(); ++port)
            {
                printMetrics("S" + std::to_string(socket) + "P" + std::to_string(port),
                    {
                        Metric(std::string(m->xPI()) + " Clocks", getQPIClocks(port, BeforeState[socket], AfterState[socket]), ""),
                        Metric("L0p Tx Cycles", 100. * getNormalizedQPIL0pTxCycles(port, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("L1 Cycles", 100. * getNormalizedQPIL1Cycles(port, BeforeState[socket], AfterState[socket]), "%")
                    }, outputType);
            }
            for (uint32 channel = 0; channel < m->getMCChannelsPerSocket(); ++channel)
            {
                if (imc_profile <= 3 && imc_profile >= 0)
                {
                    printMetrics("S" + std::to_string(socket) + "CH" + std::to_string(channel),
                    {
                        Metric("DRAMClocks", getDRAMClocks(channel, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Rank" + std::to_string(getFirstRank(imc_profile)) + " CKE Off Residency", 
                               100. * getCKEOffResidency(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Rank" + std::to_string(getFirstRank(imc_profile)) + " CKE Off Average Cycles", 
                               getCKEOffAverageCycles(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket]), ""),
                        Metric("Rank" + std::to_string(getFirstRank(imc_profile)) + " Cycles per transition", 
                               getCyclesPerTransition(channel, getFirstRank(imc_profile), BeforeState[socket], AfterState[socket]), "")
                    }, outputType);

                    printMetrics("S" + std::to_string(socket) + "CH" + std::to_string(channel),
                    {
                        Metric("DRAMClocks", getDRAMClocks(channel, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Rank" + std::to_string(getSecondRank(imc_profile)) + " CKE Off Residency", 
                               100. * getCKEOffResidency(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Rank" + std::to_string(getSecondRank(imc_profile)) + " CKE Off Average Cycles", 
                               getCKEOffAverageCycles(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket]), ""),
                        Metric("Rank" + std::to_string(getSecondRank(imc_profile)) + " Cycles per transition", 
                               getCyclesPerTransition(channel, getSecondRank(imc_profile), BeforeState[socket], AfterState[socket]), "")
                    }, outputType);
                } else if (imc_profile == 4)
                {
                    printMetrics("S" + std::to_string(socket) + "CH" + std::to_string(channel),
                    {
                        Metric("DRAMClocks", getDRAMClocks(channel, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Self-refresh cycles", getSelfRefreshCycles(channel, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Self-refresh transitions", getSelfRefreshTransitions(channel, BeforeState[socket], AfterState[socket]), ""),
                        Metric("PPD cycles", getPPDCycles(channel, BeforeState[socket], AfterState[socket]), "")
                    }, outputType);
                }
            }

            for (uint32 u = 0; u < m->getMaxNumOfUncorePMUs(PCM::PCU_PMU_ID); ++u)
            {
                std::string header = "S" + std::to_string(socket);
                if (m->getMaxNumOfUncorePMUs(PCM::PCU_PMU_ID) > 1)
                {
                    header += "U" + std::to_string(u);
                }
                std::vector<Metric> metrics;
                switch (pcu_profile)
                {
                case 0:
                    if (cpu_family_model == PCM::HASWELLX || cpu_family_model == PCM::BDX_DE || cpu_family_model == PCM::SKX)
                        break;

                    printMetrics(header,
                    {
                        Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Freq band 0 cycles", 100. * getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Freq band 1 cycles", 100. * getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Freq band 2 cycles", 100. * getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket]), "%")
                    }, outputType);
                    break;

                case 1:
                    printMetrics(header,
                    {
                        Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""),
                        Metric(cpu_family_model == PCM::SKX ? "C0_1-state residency" : "C0-state residency", getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket]), ""),
                        Metric("C3-state residency", getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket]), ""),
                        Metric(cpu_family_model == PCM::SKX ? "C6_7-state residency" : "C6-state residency", getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket]), "")
                    }, outputType);
                    break;

                case 2:
                    printMetrics(header,
                    {
                        Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""),
                        Metric("Internal prochot cycles", 100. * getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("External prochot cycles", 100. * getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Thermal freq limit cycles", 100. * getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket]), "%")
                    }, outputType);
                    break;

                case 3:
                    metrics.push_back(Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""));
                    metrics.push_back(Metric("Thermal freq limit cycles", 100. * getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket]), "%"));
                    metrics.push_back(Metric("Power freq limit cycles", 100. * getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket]), "%"));
                    if (cpu_family_model != PCM::SKX
                        && cpu_family_model != PCM::ICX
                        && cpu_family_model != PCM::SNOWRIDGE
                        && cpu_family_model != PCM::SPR
                        && cpu_family_model != PCM::EMR
                        && cpu_family_model != PCM::SRF
                        && cpu_family_model != PCM::GNR
                        && cpu_family_model != PCM::GNR_D)
                    {
                        metrics.push_back(Metric("Clipped freq limit cycles", 100. * getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket]), "%"));
                    }
                    printMetrics(header, metrics, outputType);
                    break;

                case 4:
                    if (    cpu_family_model == PCM::SKX
                         || cpu_family_model == PCM::ICX
                         || cpu_family_model == PCM::SNOWRIDGE
                         || cpu_family_model == PCM::SPR
                         || cpu_family_model == PCM::EMR
                         || cpu_family_model == PCM::SRF
                         || cpu_family_model == PCM::GNR
                         || cpu_family_model == PCM::GNR_D
                         )
                    {
                        cout << "This PCU profile is not supported on your processor\n";
                        break;
                    }
                    printMetrics(header,
                    {
                        Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""),
                        Metric("OS freq limit cycles", 100. * getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Power freq limit cycles", 100. * getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket]), "%"),
                        Metric("Clipped freq limit cycles", 100. * getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket]), "%")
                    }, outputType);
                    break;
                case 5:
                    metrics.push_back(Metric("PCUClocks", getPCUClocks(u, BeforeState[socket], AfterState[socket]), ""));
                    metrics.push_back(Metric("Frequency transition count", getUncoreCounter(PCM::PCU_PMU_ID, u, 1, BeforeState[socket], AfterState[socket]), ""));
                    metrics.push_back(Metric("Cycles spent changing frequency", 100. * getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket], m), "%"));
                    if (PCM::HASWELLX == cpu_family_model)
                    {
                        metrics.push_back(Metric("UFS transition count", getUncoreCounter(PCM::PCU_PMU_ID, u, 3, BeforeState[socket], AfterState[socket]), ""));
                        metrics.push_back(Metric("UFS transition cycles", 100. * getNormalizedPCUCounter(u, 0, BeforeState[socket], AfterState[socket], m), "%"));
                    }
                    printMetrics(header, metrics, outputType);
                    break;
                case 6:
                    if (cpu_family_model == PCM::HASWELLX || PCM::BDX_DE == cpu_family_model)
                    {
                        metrics.push_back(Metric("PC1e+ residency", getNormalizedPCUCounter(u, 0, BeforeState[socket], AfterState[socket], m) * 100., "%"));
                        metrics.push_back(Metric("PC1e+ transition count", getUncoreCounter(PCM::PCU_PMU_ID, u, 1, BeforeState[socket], AfterState[socket]), ""));
                    }
                    switch (cpu_family_model)
                    {
                    case PCM::IVYTOWN:
                    case PCM::HASWELLX:
                    case PCM::BDX_DE:
                    case PCM::SKX:
                    case PCM::ICX:
                    case PCM::SNOWRIDGE:
                    case PCM::SPR:
                    case PCM::EMR:
                    case PCM::SRF:
                    case PCM::GNR:
                    case PCM::GNR_D:
                        metrics.push_back(Metric("PC2 residency", getPackageCStateResidency(2, BeforeState[socket], AfterState[socket]) * 100., "%"));
                        metrics.push_back(Metric("PC2 transitions", getUncoreCounter(PCM::PCU_PMU_ID, u, 2, BeforeState[socket], AfterState[socket]), ""));
                        metrics.push_back(Metric("PC3 residency", getPackageCStateResidency(3, BeforeState[socket], AfterState[socket]) * 100., "%"));
                        metrics.push_back(Metric("PC6 residency", getPackageCStateResidency(6, BeforeState[socket], AfterState[socket]) * 100., "%"));
                        metrics.push_back(Metric("PC6 transitions", getUncoreCounter(PCM::PCU_PMU_ID, u, 3, BeforeState[socket], AfterState[socket]), ""));
                        break;
                    }
                    printMetrics(header, metrics, outputType);
                    break;
                case 7:
                    if (PCM::HASWELLX == cpu_family_model || PCM::BDX_DE == cpu_family_model || PCM::BDX == cpu_family_model) {
                        printMetrics(header,
                        {
                            Metric("UFS_TRANSITIONS_PERF_P_LIMIT", getNormalizedPCUCounter(u, 0, BeforeState[socket], AfterState[socket], m) * 100., "%"),
                            Metric("UFS_TRANSITIONS_IO_P_LIMIT", getNormalizedPCUCounter(u, 1, BeforeState[socket], AfterState[socket], m) * 100., "%"),
                            Metric("UFS_TRANSITIONS_UP_RING_TRAFFIC", getNormalizedPCUCounter(u, 2, BeforeState[socket], AfterState[socket], m) * 100., "%"),
                            Metric("UFS_TRANSITIONS_UP_STALL_CYCLES", getNormalizedPCUCounter(u, 3, BeforeState[socket], AfterState[socket], m) * 100., "%")
                        }, outputType);
                    }
                    break;
                case 8:
                    if (PCM::HASWELLX == cpu_family_model || PCM::BDX_DE == cpu_family_model || PCM::BDX == cpu_family_model) {
                        printMetrics(header,
                        {
                            Metric("UFS_TRANSITIONS_DOWN", getNormalizedPCUCounter(u, 0, BeforeState[socket], AfterState[socket], m) * 100., "%")
                        }, outputType);
                    }
                    break;
                }
            }

            printMetrics("S" + std::to_string(socket),
                {
                    Metric("Consumed energy units", getConsumedEnergy(BeforeState[socket], AfterState[socket]), ""),
                    Metric("Consumed Joules", getConsumedJoules(BeforeState[socket], AfterState[socket]), ""),
                    Metric("Watts", 1000. * getConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime), ""),
                    Metric("Thermal headroom below TjMax", AfterState[socket].getPackageThermalHeadroom(), "°C")
                }, outputType);
            printMetrics("S" + std::to_string(socket),
                {
                    Metric("Consumed DRAM energy units", getDRAMConsumedEnergy(BeforeState[socket], AfterState[socket]), ""),
                    Metric("Consumed DRAM Joules", getDRAMConsumedJoules(BeforeState[socket], AfterState[socket]), ""),
                    Metric("DRAM Watts", 1000. * getDRAMConsumedJoules(BeforeState[socket], AfterState[socket]) / double(AfterTime - BeforeTime), "")
                }, outputType);
        }
        for (auto instance = 0ULL; instance < PERF_LIMIT_REASON_TPMI_dies_data.size(); ++instance)
        {
            for (auto die = 0ULL; die < PERF_LIMIT_REASON_TPMI_dies_data[instance].size(); ++die)
            {
                std::vector<Metric> metrics;
                const auto data = PERF_LIMIT_REASON_TPMI_dies_data[instance][die];
                for (auto l = 0; l < PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition::MAX; ++l)
                {
                    metrics.push_back(Metric(PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition_Strings[l], extract_bits(data, l, l) ? true : false, ""));
                }
                printMetrics("S" + std::to_string(instance) + "D" + std::to_string(die) + " PERF LIMIT REASONS (DIE LEVEL)", metrics, outputType);
            }
        }
        for (auto instance = 0ULL; instance < PERF_LIMIT_REASON_TPMI_modules_data.size(); ++instance)
        {
            assert(PERF_LIMIT_REASON_TPMI_modules_data[instance].size());
            std::vector<std::array<uint64, 32>> coarseGrainedData, fineGrainedData;
            std::array<uint64, 32> empty;
            empty.fill(0);
            coarseGrainedData.resize(PERF_LIMIT_REASON_TPMI_modules_data[instance][0].size(), empty);
            fineGrainedData.resize(coarseGrainedData.size(), empty);

            for (auto module = 0ULL; module < PERF_LIMIT_REASON_TPMI_modules_data[instance].size(); ++module)
            {
                assert(PERF_LIMIT_REASON_TPMI_modules_data[instance][module].size() == coarseGrainedData.size());
                for (auto die = 0ULL; die < PERF_LIMIT_REASON_TPMI_modules_data[instance][module].size(); ++die)
                {
                    const auto data = PERF_LIMIT_REASON_TPMI_modules_data[instance][module][die];
                    DBG(1, "S", instance, "M", module, "D", die, " data = ", data);
                    for (auto l = 0; l < PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition::MAX; ++l)
                    {
                        if (extract_bits(data, l, l))  ++(coarseGrainedData[die][l]);
                    }
                    for (auto l = 0; l < PERF_LIMIT_REASON_TPMI::Fine_Grained_PLR_Bit_Definition::MAX_FINE; ++l)
                    {
                        if (extract_bits(data, 32 + l, 32 + l))  ++(fineGrainedData[die][l]);
                    }
                }
            }
            for (auto die = 0ULL; die < coarseGrainedData.size(); ++die)
            {
                std::vector<Metric> metrics;
                for (auto l = 0; l < PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition::MAX; ++l)
                {
                    metrics.push_back(Metric(PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition_Strings[l], coarseGrainedData[die][l], ""));
                }
                for (auto l = 0; l < PERF_LIMIT_REASON_TPMI::Fine_Grained_PLR_Bit_Definition::MAX_FINE; ++l)
                {
                    metrics.push_back(Metric(std::string(PERF_LIMIT_REASON_TPMI::Coarse_Grained_PLR_Bit_Definition_Strings[PERF_LIMIT_REASON_TPMI::Fine_Grained_PLR_Bit_Definition_Data[l].coarse_grained_mapping]) + "." +
                        PERF_LIMIT_REASON_TPMI::Fine_Grained_PLR_Bit_Definition_Data[l].name, fineGrainedData[die][l], ""));
                }
                printMetrics("S" + std::to_string(instance) + "D" + std::to_string(die) + " PERF LIMIT REASONS (#CORE MODULES)", metrics, outputType, true);
            }
        }
        if (csv)
        {
            cout << "\n";
        }
    };

    auto printLine = []()
    {
        cout << "----------------------------------------------------------------------------------------------\n";
    };

    mainLoop([&]()
    {
        if (!csv)
        {
            printLine();
            cout << flush;
        }

        const auto delay_ms = calibratedSleep(delay, sysCmd, mainLoop, m);

        AfterTime = m->getTickCount();
        for (i = 0; i < numSockets; ++i)
            AfterState[i] = m->getServerUncoreCounterState(i);

        m->getAllCounterStates(dummySystemState, afterSocketState, dummyCoreStates, false);

        if (PERF_LIMIT_REASON_TPMI_Supported)
        {
            PERF_LIMIT_REASON_TPMI_dies_data = PERF_LIMIT_REASON_TPMI::readDies();
            PERF_LIMIT_REASON_TPMI_modules_data = PERF_LIMIT_REASON_TPMI::readModules(max_pm_modules);
            PERF_LIMIT_REASON_TPMI::reset(max_pm_modules);
        }

        if (csv_header)
        {
            printAll(delay_ms, Header1);
            printAll(delay_ms, Header2);
            csv_header = false;
        }
        printAll(delay_ms, Data);

        swap(BeforeState, AfterState);
        swap(BeforeTime, AfterTime);
        swap(beforeSocketState, afterSocketState);

        if (m->isBlocked()) {
            if (!csv)
            {
                printLine();
            }
            // in case PCM was blocked after spawning child application: break monitoring loop here
            return false;
        }
        return true;
    });

    exit(EXIT_SUCCESS);
}
