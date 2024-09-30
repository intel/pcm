#define UNIT_TEST 1

#include "../src/pcm-memory.cpp"

#undef UNIT_TEST


extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
        size_t size_int = size / sizeof(int);
        const auto ints_used = 9;
        if (size_int < ints_used)
        {
                return 0;
        }
        print_help("");

        auto m = PCM::getInstance();
        const int *data_int = reinterpret_cast<const int *>(data);
        int pos = 0;

        bool csv = data_int[pos++] % 2;
        bool csvheader = data_int[pos++] % 2;
        bool show_channel_output = data_int[pos++] % 2;
        bool print_update = data_int[pos++] % 2;
        uint32 no_columns = DEFAULT_DISPLAY_COLUMNS; // Default number of columns is 2
        int delay = data_int[pos++] % 4;
        int rankA = data_int[pos++] % 11;
        int rankB = data_int[pos++] % 11;
        bool use_rank = data_int[pos++] % 2;
        if (!use_rank)
        {
                rankA = -1;
                rankB = -1;
        }


        ServerUncoreMemoryMetrics metrics;
        switch (data_int[pos++] % 4)
        {
        case 0:
                metrics = PartialWrites;
                break;
        case 1:
                metrics = Pmem;
                break;
        case 2:
                metrics = PmemMemoryMode;
                break;
        case 3:
                metrics = PmemMixedMode;
                break;
        }

        assert(pos == ints_used);

        m->resetPMU();
        m->disableJKTWorkaround();

        const auto cpu_family_model = m->getCPUFamilyModel();
        if (!m->hasPCICFGUncore())
        {
                cerr << "Unsupported processor model (0x" << std::hex << cpu_family_model << std::dec << ").\n";
                if (m->memoryTrafficMetricsAvailable())
                        cerr << "For processor-level memory bandwidth statistics please use 'pcm' utility\n";
                return 0;
        }
        if (anyPmem(metrics) && (m->PMMTrafficMetricsAvailable() == false))
        {
                cerr << "PMM/Pmem traffic metrics are not available on your processor.\n";
                return 0;
        }
        if (metrics == PmemMemoryMode && m->PMMMemoryModeMetricsAvailable() == false)
        {
                cerr << "PMM Memory Mode metrics are not available on your processor.\n";
                return 0;
        }
        if (metrics == PmemMixedMode && m->PMMMixedModeMetricsAvailable() == false)
        {
                cerr << "PMM Mixed Mode metrics are not available on your processor.\n";
                return 0;
        }
        if((rankA >= 0 || rankB >= 0) && anyPmem(metrics))
        {
                cerr << "PMM/Pmem traffic metrics are not available on rank level\n";
                return 0;
        }
        if((rankA >= 0 || rankB >= 0) && !show_channel_output)
        {
                cerr << "Rank level output requires channel output\n";
                return 0;
        }
        std::cerr << "programServerUncoreMemoryMetrics parameters:" << metrics << ";" << rankA << ";" << rankB << "\n";
        PCM::ErrorCode status = m->programServerUncoreMemoryMetrics(metrics, rankA, rankB);
        m->checkError(status);

        max_imc_channels = (pcm::uint32)m->getMCChannelsPerSocket();

        std::vector<ServerUncoreCounterState> BeforeState(m->getNumSockets());
        std::vector<ServerUncoreCounterState> AfterState(m->getNumSockets());
        uint64 BeforeTime = 0, AfterTime = 0;

        readState(BeforeState);
        BeforeTime = m->getTickCount();
        MySleepMs(delay);
        AfterTime = m->getTickCount();
        readState(AfterState);

        if(rankA >= 0 || rankB >= 0)
          calculate_bandwidth_rank(m,BeforeState, AfterState, AfterTime - BeforeTime, csv, csvheader, no_columns, rankA, rankB);
        else
          calculate_bandwidth(m,BeforeState,AfterState,AfterTime-BeforeTime,csv,csvheader, no_columns, metrics,
                show_channel_output, print_update, 0);

        return 0;
}

