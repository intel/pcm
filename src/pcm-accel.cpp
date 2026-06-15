// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2024, Intel Corporation
// written by White.Hu

#include "pcm-accel-common.h"
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#endif
#include <memory>
#include <fstream>
#include <stdlib.h>
#include <stdexcept>      // std::length_error
#include <cstdint>
#include <numeric>
#include <algorithm>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif
#include "lspci.h"
#include "utils.h"
using namespace pcm;
accel_content accel_results(ACCEL_MAX, dev_content(ACCEL_IP_DEV_COUNT_MAX, ctr_data()));
std::vector<std::string> build_counter_names(std::string dev_name, std::vector<struct accel_counter>& ctrs, const ACCEL_DEV_LOC_MAPPING loc_map)
{
    std::vector<std::string> v;
    std::map<uint32_t,std::map<uint32_t,struct accel_counter*>> v_sort;

    v.push_back(dev_name);
    
    switch (loc_map)
    {
        case SOCKET_MAP:
            v.push_back("Socket");
            break;
        case NUMA_MAP:
            v.push_back("NUMA Node");
            break;
        default:
            break;
    }

    //re-organize data collection to be row wise
    for (std::vector<struct accel_counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter)
    {
        v_sort[counter->h_id][counter->v_id] = &(*counter);
    }
    
    for (std::map<uint32_t,std::map<uint32_t,struct accel_counter*>>::const_iterator hunit = v_sort.cbegin(); hunit != v_sort.cend(); ++hunit)
    {
       std::map<uint32_t, struct accel_counter*> v_array = hunit->second;

       //std::cout << "hunit: hhid=" << hh_id << "\n";
       for (std::map<uint32_t,struct accel_counter*>::const_iterator vunit = v_array.cbegin(); vunit != v_array.cend(); ++vunit)
       {
           std::string v_name = vunit->second->v_event_name;
           v.push_back(v_name);
       }
   }

   return v;
}

void print_usage(const std::string& progname)
{
    std::cout << "\n Usage: \n " << progname << " --help | [interval] [options] \n";
    std::cout << "   <interval>                           => time interval in seconds (floating point number is accepted)\n";
    std::cout << "                                        to sample performance counters.\n";
    std::cout << "                                        If not specified - 3.0 is used\n";
    std::cout << " Supported <options> are: \n";
    std::cout << "  -h    | --help  | /h               => print this help and exit\n";
    std::cout << "  -silent                            => silence information output and print only measurements\n";
    std::cout << "  -iaa | /iaa                        => print IAA accel device measurements(default)\n";
    std::cout << "  -dsa | /dsa                        => print DSA accel device measurements\n";
#ifdef __linux__
    std::cout << "  -qat | /qat                        => print QAT accel device measurements\n";
    std::cout << "  -numa | /numa                      => print accel device numa node mapping(for linux only)\n";
#endif
    std::cout << "  -evt[=cfg.txt] | /evt[=cfg.txt]    => specify the event cfg file to cfg.txt \n";
    std::cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    std::cout << "  -csv-delimiter=<value>  | /csv-delimiter=<value>   => set custom csv delimiter\n";
    std::cout << "  -human-readable | /human-readable  => use human readable format for output (for csv only)\n";
    std::cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    std::cout << " Examples:\n";
    std::cout << "  " << progname << " -iaa 1.0 -i=10             => print IAA counters every second 10 times and exit\n";
    std::cout << "  " << progname << " -iaa 0.5 -csv=test.log     => twice a second save IAA counter values to test.log in CSV format\n";
    std::cout << "  " << progname << " -iaa -csv -human-readable  => every 3 second print IAA counters in human-readable CSV format\n";
    std::cout << "\n";
}

std::vector<std::string> build_csv(const ACCEL_IP accel, std::vector<struct accel_counter>& ctrs,
    const bool human_readable, const std::string& csv_delimiter, accel_content& sample_data, const ACCEL_DEV_LOC_MAPPING loc_map)
{
    AcceleratorCounterState *accs_;
    accs_ = AcceleratorCounterState::getInstance();
    std::vector<std::string> result;
    std::vector<std::string> current_row;
    auto header = build_counter_names("Accelerator", ctrs, loc_map);
    result.push_back(build_csv_row(header, csv_delimiter));
    std::map<uint32_t,std::map<uint32_t,struct accel_counter*>> v_sort;
    uint32_t dev_count = accs_->getNumOfAccelDevs();

    for (uint32_t dev = 0; dev != dev_count; ++dev) 
    {
        //Re-organize data collection to be row wise
        std::map<uint32_t,std::map<uint32_t,struct accel_counter*>> v_sort;
        size_t max_name_width = 0;
        for (std::vector<struct accel_counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter)
        {
            max_name_width = (std::max)(max_name_width, counter->v_event_name.size());
            v_sort[counter->h_id][counter->v_id] = &(*counter);
            //std::cout << "v_sort: h_id=" << std::hex << counter->h_id << ", v_id=" << std::hex << counter->v_id << "\n" << std::dec;
        }

        //Print data
        for (std::map<uint32_t,std::map<uint32_t,struct accel_counter*>>::const_iterator hunit = v_sort.cbegin(); hunit != v_sort.cend(); ++hunit)
        {
            std::map<uint32_t, struct accel_counter*> v_array = hunit->second;
            uint32_t hh_id = hunit->first;
            std::vector<uint64_t> v_data;
            std::string h_name = v_array[0]->h_event_name + "#" + std::to_string(dev);
            uint32 location = 0xff;

            current_row.clear();
            current_row.push_back(h_name); //dev name
			
            if (accs_->getAccelDevLocation( dev, loc_map, location) == true)
            {
                current_row.push_back(std::to_string(location)); //location info
            }

            //std::cout << "location mapping=" << loc_map << ", data=" << location << "\n";
            //std::cout << "hunit: hhid=" << hh_id << "\n";
            for (std::map<uint32_t,struct accel_counter*>::const_iterator vunit = v_array.cbegin(); vunit != v_array.cend(); ++vunit)
            {
                uint32_t vv_id = vunit->first;
                uint64_t raw_data = sample_data[accel][dev][std::pair<h_id,v_id>(hh_id,vv_id)];
                //std::cout << "vunit: hhid=" << hh_id << ", vname=" << vunit->second->v_event_name << ", data=" << raw_data << "\n";
                current_row.push_back(human_readable ? unit_format(raw_data) : std::to_string(raw_data)); //counter data
            }
            result.push_back(build_csv_row(current_row, csv_delimiter));
        }
    }
    
    return result;
}

std::vector<std::string> build_display(const ACCEL_IP accel, std::vector<struct accel_counter>& ctrs, accel_content& sample_data, const ACCEL_DEV_LOC_MAPPING loc_map)
{
    std::vector<std::string> buffer;
    std::vector<std::string> headers;
    std::vector<struct data> data;
    std::string row;
    AcceleratorCounterState *accs_;
    accs_ = AcceleratorCounterState::getInstance();
    uint32_t dev_count = accs_->getNumOfAccelDevs();

    headers = build_counter_names("Accelerator", ctrs, loc_map);
    //Print first row
    row = std::accumulate(headers.begin(), headers.end(), std::string(" "), a_header_footer);
    buffer.push_back(row);
    //Print a_title
    row = std::accumulate(headers.begin(), headers.end(), std::string("|"), a_title);
    buffer.push_back(row);
    //Print deliminator
    row = std::accumulate(headers.begin(), headers.end(), std::string("|"), a_header_footer);
    buffer.push_back(row);

    for (uint32_t dev = 0; dev != dev_count; ++dev)
    {
        //Print data
        std::map<uint32_t,std::map<uint32_t,struct accel_counter*>> v_sort;

        //re-organize data collection to be row wise
        for (std::vector<struct accel_counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter)
        {
            v_sort[counter->h_id][counter->v_id] = &(*counter);
            //std::cout << "v_sort: h_id=" << std::hex << counter->h_id << ", v_id=" << std::hex << counter->v_id << "\n" << std::dec;
        }
        
        for (std::map<uint32_t,std::map<uint32_t,struct accel_counter*>>::const_iterator hunit = v_sort.cbegin(); hunit != v_sort.cend(); ++hunit)
        {
            std::map<uint32_t, struct accel_counter*> v_array = hunit->second;
            uint32_t hh_id = hunit->first;
            std::vector<uint64_t> v_data;
            std::string h_name = v_array[0]->h_event_name;
            uint32 location = 0xff;
            
            if (accs_->getAccelDevLocation(dev, loc_map, location) == true)
            {
                v_data.push_back(location); //location info
            }

            //std::cout << "location mapping=" << loc_map << ", data=" << location << "\n";
            //std::cout << "hunit: hhid=" << hh_id << "\n";
            for (std::map<uint32_t,struct accel_counter*>::const_iterator vunit = v_array.cbegin(); vunit != v_array.cend(); ++vunit)
            {
                uint32_t vv_id = vunit->first;
                uint64_t raw_data = sample_data[accel][dev][std::pair<h_id,v_id>(hh_id,vv_id)];
                //std::cout << "vunit: hhid=" << hh_id << ", vname=" << vunit->second->v_event_name << ", data=" << raw_data << "\n";
                v_data.push_back(raw_data); //counter data
            }
            data = prepare_data(v_data, headers);

            row = "| " + h_name + "#" + std::to_string(dev); //dev name
            row += std::string(abs(int(headers[0].size() - (row.size() - 1))), ' ');
            row += std::accumulate(data.begin(), data.end(), std::string("|"), a_data);

            buffer.push_back(row);
        }

    }

    //Print deliminator
    row = std::accumulate(headers.begin(), headers.end(), std::string("|"), a_header_footer);
    buffer.push_back(row);
    //Print footer
    row = std::accumulate(headers.begin(), headers.end(), std::string(" "), a_header_footer);
    buffer.push_back(row);

    return buffer;
}

void collect_data(PCM *m, const double delay, const ACCEL_IP accel, std::vector<struct accel_counter>& ctrs)
{
    const uint32_t delay_ms = uint32_t(delay * 1000);
    SimpleCounterState *before, *after;
    AcceleratorCounterState *accs_;
    accs_ = AcceleratorCounterState::getInstance();
    const uint32_t dev_count = accs_->getNumOfAccelDevs();
    const uint32_t counter_nb = ctrs.size();
    uint32_t ctr_index = 0;

    before = new SimpleCounterState[dev_count*counter_nb];
    after = new SimpleCounterState[dev_count*counter_nb];

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
                ctr_index = 0;
                for (auto pctr = ctrs.begin(); pctr != ctrs.end(); ++pctr)
                {
                    before[dev*counter_nb + ctr_index] = accs_->getAccelCounterState(dev, ctr_index);
                    ctr_index++;
                }
            }
            MySleepMs(delay_ms);
            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
                ctr_index = 0;
                for (auto pctr = ctrs.begin();pctr != ctrs.end(); ++pctr)
                {
                    after[dev*counter_nb + ctr_index] = accs_->getAccelCounterState(dev, ctr_index);
                    uint64_t raw_result = getNumberOfEvents(before[dev*counter_nb + ctr_index], after[dev*counter_nb + ctr_index]);
                    uint64_t trans_result = uint64_t (raw_result * pctr->multiplier / (double) pctr->divider * (1000 / (double) delay_ms));
                    accel_results[accel][dev][std::pair<h_id,v_id>(pctr->h_id,pctr->v_id)] = trans_result;
                    //std::cout << "collect_data: accel=" << accel << " dev=" << dev << " h_id=" << pctr->h_id << " v_id=" << pctr->v_id << " data=" << std::hex << trans_result << "\n" << std::dec;
                    ctr_index++;
                }
            }
            break;

        case ACCEL_QAT:
            MySleepMs(delay_ms);

            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
               m->controlQATTelemetry(dev, PCM::QAT_TLM_REFRESH);
               ctr_index = 0;
               for (auto pctr = ctrs.begin();pctr != ctrs.end(); ++pctr)
               {
                   after[dev*counter_nb + ctr_index] = accs_->getAccelCounterState(dev, ctr_index);

                   uint64_t raw_result = after[dev*counter_nb + ctr_index].getRawData();
                   uint64_t trans_result = uint64_t (raw_result * pctr->multiplier / (double) pctr->divider );

                   accel_results[accel][dev][std::pair<h_id,v_id>(pctr->h_id,pctr->v_id)] = trans_result;
                   //std::cout << "collect_data: accel=" << accel << " dev=" << dev << " h_id=" << pctr->h_id << " v_id=" << pctr->v_id << " data=" << std::hex << trans_result << "\n" << std::dec;
                   ctr_index++;
               }
            }
            break;

        default:
            break;
    }

    deleteAndNullifyArray(before);
    deleteAndNullifyArray(after);
}





PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);
    set_signal_handlers();

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION ;
    std::cout << "\n This utility measures Sapphire Rapids-SP accelerators information.\n";

    std::string program = std::string(argv[0]);
    bool csv = false;
    bool human_readable = false;
    std::string csv_delimiter = ",";
    std::string output_file;
    double delay = PCM_DELAY_DEFAULT;
    ACCEL_IP accel=ACCEL_IAA; //default is IAA
    bool evtfile = false;
    std::string specify_evtfile;
    ACCEL_DEV_LOC_MAPPING loc_map = SOCKET_MAP; //default is socket mapping
    MainLoop mainLoop;
    PCM * m;
    AcceleratorCounterState *accs_;
    accs_ = AcceleratorCounterState::getInstance();

    std::string ev_file_name;

    while (argc > 1) 
    {
        argv++;
        argc--;
        std::string arg_value;
        if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
        {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            //handled in check_and_set_silent
            continue;
        }
        else if (extract_argument_value(*argv, {"-csv-delimiter", "/csv-delimiter"}, arg_value))
        {
            csv_delimiter = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"}))
        {
            csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
        {
            csv = true;
            output_file = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-human-readable", "/human-readable"}))
        {
            human_readable = true;
        }
        else if (check_argument_equals(*argv, {"-iaa", "/iaa"}))
        {
            accel = ACCEL_IAA;
        }
        else if (check_argument_equals(*argv, {"-dsa", "/dsa"}))
        {
            accel = ACCEL_DSA;
        }
#ifdef __linux__
        else if (check_argument_equals(*argv, {"-qat", "/qat"}))
        {
            accel = ACCEL_QAT;
        }
        else if (check_argument_equals(*argv, {"-numa", "/numa"}))
        {
            loc_map = NUMA_MAP;
        }
#endif
        else if (extract_argument_value(*argv, {"-evt", "/evt"}, arg_value))
        {
            evtfile = true;
            specify_evtfile = std::move(arg_value);
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else
        {
            delay = parse_delay(*argv, program, (print_usage_func)print_usage);
            continue;
        }
    }

    print_cpu_details();

#ifdef __linux__
    // check kernel version for driver dependency.
    std::cout << "Info: IDX - Please ensure the required driver(e.g idxd driver for iaa/dsa, qat driver and etc) correct enabled with this system, else the tool may fail to run.\n";
    struct utsname sys_info;
    if (!uname(&sys_info))
    {
        std::string krel_str;
        uint32 krel_major_ver=0, krel_minor_ver=0;
        krel_str = sys_info.release;
        std::vector<std::string> krel_info = split(krel_str, '.');
        std::istringstream iss_krel_major(krel_info[0]);
        std::istringstream iss_krel_minor(krel_info[1]);
        iss_krel_major >> std::setbase(0) >> krel_major_ver;
        iss_krel_minor >> std::setbase(0) >> krel_minor_ver;

        switch (accel)
        {
            case ACCEL_IAA:
            case ACCEL_DSA:
                if ((krel_major_ver < 5) || (krel_major_ver == 5 && krel_minor_ver < 11))
                {
                    std::cout<< "Warning: IDX - current linux kernel version(" << krel_str << ") is too old, please upgrade it to the latest due to required idxd driver integrated to kernel since 5.11.\n";
                }
                break;
            default:
                break;
        }
    }
#endif

    try
    {
        m = PCM::getInstance();
    }
    catch (std::exception & e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        exit(EXIT_FAILURE);
    }

    if (m->supportIDXAccelDev() == false)
    {
        std::cerr << "Error: IDX accelerator is NOT supported with this platform! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    accs_->setEvents(m,accel,specify_evtfile,evtfile);

    std::ostream* output = &std::cout;
    std::fstream file_stream;
    if (!output_file.empty()) 
    {
        file_stream.open(output_file.c_str(), std::ios_base::out);
        output = &file_stream;
    }    
    accs_->programAccelCounters();
    std::vector<accel_counter> CTRS= accs_->getCounters();
    mainLoop([&]()
    {
        
        collect_data(m, delay, accel, CTRS);
        std::vector<std::string> display_buffer = csv ?
                    build_csv( accel, CTRS, human_readable, csv_delimiter, accel_results, loc_map) :
                    build_display( accel, CTRS, accel_results, loc_map);
        display(display_buffer, *output);
        return true;
    });

    file_stream.close();
    exit(EXIT_SUCCESS);
}
