// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2017-2025, Intel Corporation

// written by Patrick Lu,
//            Aaron Cruz,
//            Alexander Antonov
//            and others

#include "pcm-iio-pmu.h"

void print_PCIeMapping(const std::vector<struct iio_stacks_on_socket>& iios, const PCIDB & pciDB, std::ostream& stream)
{
    uint32_t header_width = 100;
    string row;
    vector<string> buffer;

    for (auto it = iios.begin(); it != iios.end(); ++it) {
        row = "Socket " + std::to_string((*it).socket_id);
        buffer.push_back(row);
        for (auto & stack : it->stacks) {
            std::stringstream ss;
            ss << "\t" << stack.stack_name << " domain 0x" << std::hex << std::setfill('0') << std::setw(4) << stack.domain
               << "; root bus: 0x" << std::setfill('0') << std::setw(2) << (int)stack.busno << "\tflipped: " << (stack.flipped ? "true" : "false");
            buffer.push_back(ss.str());
            for (auto& part : stack.parts) {
                vector<struct pci> pp = part.child_pci_devs;
                uint8_t level = 5;
                for (std::vector<struct pci>::const_iterator iunit = pp.begin(); iunit != pp.end(); ++iunit) {
                    row = build_pci_header(pciDB, header_width, *iunit, -1, level);
                    buffer.push_back(row);
                    if (iunit->hasChildDevices()) {
                        build_pci_tree(buffer, pciDB, header_width, *iunit, -1, level + 1);
                    } else if (iunit->header_type == 1) {
                        level++;
                    }
                }
            }
        }
    }
    display(buffer, stream);
}

void print_usage(const string& progname)
{
    cout << "\n Usage: \n " << progname << " --help | [interval] [options] \n";
    cout << "   <interval>                           => time interval in seconds (floating point number is accepted)\n";
    cout << "                                        to sample performance counters.\n";
    cout << "                                        If not specified - 3.0 is used\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help  | /h               => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  --version                          => print application version\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cout << "  -csv-delimiter=<value>  | /csv-delimiter=<value>   => set custom csv delimiter\n";
    cout << "  -human-readable | /human-readable  => use human readable format for output (for csv only)\n";
    cout << "  -root-port | /root-port            => add root port devices to output (for csv only)\n";
    cout << "  -list | --list                     => provide platform topology info\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << " Examples:\n";
    cout << "  " << progname << " 1.0 -i=10             => print counters every second 10 times and exit\n";
    cout << "  " << progname << " 0.5 -csv=test.log     => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << progname << " -csv -human-readable  => every 3 second print counters in human-readable CSV format\n";
    cout << "\n";
}

void parse_arguments(int argc, char * argv[], struct pcm_iio_config& config, MainLoop& mainLoop)
{
    const string program = string(argv[0]);
    while (argc > 1) {
        argv++;
        argc--;
        std::string arg_value;
        if (check_argument_equals(*argv, {"--help", "-h", "/h"})) {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"})) {
            // handled in check_and_set_silent
            continue;
        }
        else if (extract_argument_value(*argv, {"-csv-delimiter", "/csv-delimiter"}, arg_value)) {
            config.display.csv_delimiter = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"})) {
            config.display.csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value)) {
            config.display.csv = true;
            config.display.output_file = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-human-readable", "/human-readable"})) {
            config.display.human_readable = true;
        }
        else if (check_argument_equals(*argv, {"-list", "--list"})) {
            config.display.list = true;
        }
        else if (check_argument_equals(*argv, {"-root-port", "/root-port"})) {
            config.display.show_root_port = true;
        }
        else if (mainLoop.parseArg(*argv)) {
            continue;
        }
        else {
            config.pmu_config.delay = parse_delay(*argv, program, (print_usage_func)print_usage);
            continue;
        }
    }
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if (print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n This utility measures IIO information\n\n";

    struct pcm_iio_config config;
    MainLoop mainLoop;

    parse_arguments(argc, argv, config, mainLoop);

    set_signal_handlers();

    print_cpu_details();

    std::ostream* output = &std::cout;
    std::fstream file_stream;
    if (!config.display.output_file.empty()) {
        file_stream.open(config.display.output_file.c_str(), std::ios_base::out);
        output = &file_stream;
    }

    if (!initializePCIeBWCounters(config.pmu_config))
        exit(EXIT_FAILURE);

    load_PCIDB(config.pciDB);

    if (config.display.list) {
        print_PCIeMapping(config.pmu_config.iios, config.pciDB, *output);
        return 0;
    }

#ifdef PCM_DEBUG
    print_nameMap(config.pmu_config.nameMap);
#endif

    auto displayBuilder = getDisplayBuilder(config);
    auto collector = std::make_unique<PcmIioDataCollector>(config.pmu_config);

    mainLoop([&]()
    {
        collector->collectData();
        vector<string> display_buffer = displayBuilder->buildDisplayBuffer();
        display(display_buffer, *output);
        return true;
    });

    file_stream.close();

    exit(EXIT_SUCCESS);
}
