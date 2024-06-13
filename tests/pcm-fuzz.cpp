#define UNIT_TEST 1

#include "../src/pcm.cpp"

#undef UNIT_TEST


extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
       auto m = PCM::getInstance();
       const int *data_int = reinterpret_cast<const int *>(data);
       size_t size_int = size / sizeof(int);

       if (size_int < 1) {
               return 0;
       }

       int pid = data_int[0];

       m->resetPMU();

       m->disableJKTWorkaround();

       const PCM::ErrorCode status = m->program(PCM::DEFAULT_EVENTS, nullptr, false, pid);

       switch (status)
       {
        case PCM::Success:
                break;
        case PCM::UnknownError: // expected for invalid pid
                return 0;
        case PCM::MSRAccessDenied:
                cerr << "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access).\n";
                exit(EXIT_FAILURE);
        case PCM::PMUBusy:
                cerr << "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
                cerr << "Alternatively you can try running PCM with option -r to reset PMU.\n";
                exit(EXIT_FAILURE);
        default:
                cerr << "Access to Intel(r) Performance Counter Monitor has denied (Unknown error).\n";
                exit(EXIT_FAILURE);
        }

        if (size_int < 6)
        {
                return 0;
        }

        print_cpu_details();

        std::vector<CoreCounterState> cstates1, cstates2;
        std::vector<SocketCounterState> sktstate1, sktstate2;
        SystemCounterState sstate1, sstate2;
        bitset<MAX_CORES> ycores;
        const auto cpu_model = m->getCPUModel();

        print_pid_collection_message(pid);
        bool show_partial_core_output = false; // TODO: add support for partial core output
        bool csv_output = data_int[1] % 2;
        int metricVersion = data_int[2];
        bool show_socket_output = data_int[3] % 2;
        bool show_system_output = data_int[4] % 2;
        bool show_core_output = data_int[5] % 2;

        m->getAllCounterStates(sstate1, sktstate1, cstates1);
        m->getAllCounterStates(sstate2, sktstate2, cstates2);
        if (csv_output)
                print_csv(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
                cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output);
        else
                print_output(m, cstates1, cstates2, sktstate1, sktstate2, ycores, sstate1, sstate2,
                        cpu_model, show_core_output, show_partial_core_output, show_socket_output, show_system_output,
                        metricVersion);

       return 0;
}

