// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022, Intel Corporation
// written by White.Hu

#include "cpucounters.h"
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
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
#ifdef __linux__
#include <sys/utsname.h>
#endif
#include "lspci.h"
#include "utils.h"
using namespace std;
using namespace pcm;

#define PCM_DELAY_DEFAULT 3.0 // in seconds

class idx_ccr {
    public:
        virtual uint64_t get_event_select() const = 0;
        virtual void set_event_select(uint64_t value) = 0;
        virtual uint64_t get_event_category() const  = 0;
        virtual void set_event_category(uint64_t value) = 0;
        virtual uint64_t get_enable() const  = 0;
        virtual void set_enable(uint64_t value) = 0;
        virtual uint64_t get_ccr_value() const  = 0;
        virtual void set_ccr_value(uint64_t value) = 0;
        virtual ~idx_ccr() {};
};

class spr_idx_ccr: public idx_ccr {
    public:
        spr_idx_ccr(uint64_t &v){
             ccr_value = &v;
        }
        virtual uint64_t get_event_select() const  { //EVENT bit, bit 32
             return ((*ccr_value >> 32) & 0xFFFFFFF);
        }
        virtual void set_event_select(uint64_t value) {
             *ccr_value |= (value << 32);
        }
        virtual uint64_t get_event_category() const  { //EVENT Categorg, bit 8
             return ((*ccr_value >> 8) & 0xF);
        }
        virtual void set_event_category(uint64_t value) {
             *ccr_value |= (value << 8);
        }
        virtual uint64_t get_enable() const  { //Enable counter, bit 0
             return ((*ccr_value >> 0 ) & 0x01);
        }
        virtual void set_enable(uint64_t value) {
             *ccr_value |= (value << 0);
        }
        virtual uint64_t get_ccr_value() const {
             return *ccr_value;
        }
        virtual void set_ccr_value(uint64_t value) {
             *ccr_value = value;
        }

    private:
        uint64_t* ccr_value = NULL;
};

idx_ccr* idx_get_ccr(uint64_t& ccr)
{
    return new spr_idx_ccr(ccr);
}

typedef enum
{
    ACCEL_IAA,
    ACCEL_DSA,
    ACCEL_QAT,
    ACCEL_MAX,
} ACCEL_IP;

enum IDXPerfmonField
{
    DPF_BASE = 0x100, //start from 0x100 to different with PerfmonField in cpucounter.h
    EVENT_CATEGORY,
    FILTER_WQ,
    FILTER_ENG,
    FILTER_TC,
    FILTER_PGSZ,
    FILTER_XFERSZ
};

typedef enum
{
    SOCKET_MAP,
    NUMA_MAP,
} ACCEL_DEV_LOC_MAPPING;

const std::vector<uint32_t> idx_accel_mapping = 
{
    PCM::IDX_IAA,
    PCM::IDX_DSA,
    PCM::IDX_QAT
};

#define ACCEL_IP_DEV_COUNT_MAX (16)

typedef uint32_t h_id;
typedef uint32_t v_id;
typedef std::map<std::pair<h_id,v_id>,uint64_t> ctr_data;
typedef std::vector<ctr_data> dev_content;
typedef std::vector<dev_content> accel_content;

accel_content accel_results(ACCEL_MAX, dev_content(ACCEL_IP_DEV_COUNT_MAX, ctr_data()));

struct accel_counter : public counter {
    //filter config for IDX Accelerator.
    uint32_t cfr_wq = 0;
    uint32_t cfr_eng = 0;
    uint32_t cfr_tc = 0;
    uint32_t cfr_pgsz = 0;
    uint32_t cfr_xfersz = 0;
};

typedef struct
{
    PCM *m;
    ACCEL_IP accel;
    accel_counter ctr;
    vector<struct accel_counter> ctrs;
} accel_evt_parse_context;

uint32_t getNumOfAccelDevs(PCM *m, ACCEL_IP accel)
{
    uint32_t dev_count = 0;

    if (accel >= ACCEL_MAX || m == NULL)
        return 0;

    switch (accel)
    {
        case ACCEL_IAA:
            dev_count = m->getNumOfIDXAccelDevs(PCM::IDX_IAA);
            break;
        case ACCEL_DSA:
            dev_count = m->getNumOfIDXAccelDevs(PCM::IDX_DSA);
            break;
        case ACCEL_QAT:
            dev_count = m->getNumOfIDXAccelDevs(PCM::IDX_QAT);
            break;
        default:
            dev_count = 0;
            break;
    }

    return dev_count;
}

uint32_t getMaxNumOfAccelCtrs(PCM *m, ACCEL_IP accel)
{
    uint32_t ctr_count = 0;

    if (accel >= ACCEL_MAX || m == NULL)
        return 0;

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            ctr_count = m->getMaxNumOfIDXAccelCtrs(accel);
            break;
        default:
            ctr_count = 0;
            break;
    }

    return ctr_count;
}

int32_t programAccelCounters(PCM *m, ACCEL_IP accel, std::vector<struct accel_counter>& ctrs)
{
    vector<uint64_t> rawEvents;
    vector<uint32_t> filters_wq, filters_tc, filters_pgsz, filters_xfersz, filters_eng;

    if (m == NULL || accel >= ACCEL_MAX || ctrs.size() == 0 || ctrs.size()  > getMaxNumOfAccelCtrs(m, accel))
        return -1;

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            for (auto pctr = ctrs.begin(); pctr != ctrs.end(); ++pctr)
            {
                rawEvents.push_back(pctr->ccr);
                filters_wq.push_back(pctr->cfr_wq);
                filters_tc.push_back(pctr->cfr_tc);
                filters_pgsz.push_back(pctr->cfr_pgsz);
                filters_xfersz.push_back(pctr->cfr_xfersz);
                filters_eng.push_back(pctr->cfr_eng);
                //std::cout<<"ctr idx=0x" << std::hex << pctr->idx << " hid=0x" << std::hex << pctr->h_id << " vid=0x" << std::hex << pctr->v_id <<" ccr=0x" << std::hex << pctr->ccr << "\n";
                //std::cout<<"mul=0x" << std::hex << pctr->multiplier << " div=0x" << std::hex << pctr->divider << "\n" << std::dec;
            }
            m->programIDXAccelCounters(idx_accel_mapping[accel], rawEvents, filters_wq, filters_eng, filters_tc, filters_pgsz, filters_xfersz);
            break;
        default:
            break;
    }

    return 0;
}

SimpleCounterState getAccelCounterState(PCM *m, ACCEL_IP accel, uint32 dev, uint32 ctr_index)
{
    SimpleCounterState result;
    
    if (m == NULL || accel >= ACCEL_MAX || dev >= getNumOfAccelDevs(m, accel) || ctr_index >= getMaxNumOfAccelCtrs(m, accel))
        return result;

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            result = m->getIDXAccelCounterState(accel, dev, ctr_index);
            break;
        default:
            break;
    }

    return result;
}

bool isAccelCounterAvailable(PCM *m, ACCEL_IP accel)
{
    bool ret = true;

    if (m == NULL || accel >= ACCEL_MAX)
           ret =false;

    if (getNumOfAccelDevs(m, accel) == 0)
        ret = false;

    return ret;
}

std::string getAccelCounterName(ACCEL_IP accel)
{
    std::string ret;
    
    switch (accel)
    {
        case ACCEL_IAA:
            ret = "iaa";
            break;
        case ACCEL_DSA:
            ret = "dsa";
            break;
        case ACCEL_QAT:
            ret = "qat";
            break;
        default:
            ret = "id=" + std::to_string(accel) + "(unknown)";
            break;
    }

    return ret;
}

bool getAccelDevLocation(PCM *m, const ACCEL_IP accel, uint32_t dev, const ACCEL_DEV_LOC_MAPPING loc_map, uint32_t &location)
{
    bool ret = true;
    
    switch (loc_map)
    {
        case SOCKET_MAP:
            location = m->getCPUSocketIdOfIDXAccelDev(accel, dev);
            break;
        case NUMA_MAP:
            location = m->getNumaNodeOfIDXAccelDev(accel, dev);
            break;
        default:
            ret = false;
            break;
    }
    
    return ret;
}

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

std::vector<std::string> build_csv(PCM *m, const ACCEL_IP accel, std::vector<struct accel_counter>& ctrs,
    const bool human_readable, const std::string& csv_delimiter, accel_content& sample_data, const ACCEL_DEV_LOC_MAPPING loc_map)
{
    std::vector<std::string> result;
    std::vector<std::string> current_row;
    auto header = build_counter_names("Accelerator", ctrs, loc_map);
    result.push_back(build_csv_row(header, csv_delimiter));
    std::map<uint32_t,std::map<uint32_t,struct accel_counter*>> v_sort;
    uint32_t dev_count = getNumOfAccelDevs(m, accel);

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
			
            if (getAccelDevLocation(m, accel, dev, loc_map, location) == true)
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

std::vector<std::string> build_display(PCM *m, const ACCEL_IP accel, std::vector<struct accel_counter>& ctrs, accel_content& sample_data, const ACCEL_DEV_LOC_MAPPING loc_map)
{
    std::vector<std::string> buffer;
    std::vector<std::string> headers;
    std::vector<struct data> data;
    std::string row;
    uint32_t dev_count = getNumOfAccelDevs(m, accel);

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
            
            if (getAccelDevLocation(m, accel, dev, loc_map, location) == true)
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
    const uint32_t dev_count = getNumOfAccelDevs(m, accel);
    const uint32_t counter_nb = ctrs.size();
    uint32_t ctr_index = 0;

    before = new SimpleCounterState[dev_count*counter_nb];
    after = new SimpleCounterState[dev_count*counter_nb];

    programAccelCounters(m, accel, ctrs);

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
                ctr_index = 0;
                for (auto pctr = ctrs.begin(); pctr != ctrs.end(); ++pctr)
                {
                    before[dev*counter_nb + ctr_index] = getAccelCounterState(m, accel, dev, ctr_index);
                    ctr_index++;
                }
            }
            MySleepMs(delay_ms);
            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
                ctr_index = 0;
                for (auto pctr = ctrs.begin();pctr != ctrs.end(); ++pctr)
                {
                    after[dev*counter_nb + ctr_index] = getAccelCounterState(m, accel, dev, ctr_index);
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
                   after[dev*counter_nb + ctr_index] = getAccelCounterState(m, accel, dev, ctr_index);

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

    delete[] before;
    delete[] after;
}

int idx_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue)
{
    accel_evt_parse_context *context = (accel_evt_parse_context *)cb_ctx;
    PCM *m = context->m;

    if (cb_type == EVT_LINE_START) //this event will be called per line(start)
    {
        context->ctr.cfr_wq = 0xFFFF;
        context->ctr.cfr_eng = 0xFFFF;
        context->ctr.cfr_tc = 0xFFFF;
        context->ctr.cfr_pgsz = 0xFFFF;
        context->ctr.cfr_xfersz = 0xFFFF;
        context->ctr.ccr = 0;
    }
    else if (cb_type == EVT_LINE_FIELD) //this event will be called per field of line
    {
        std::unique_ptr<idx_ccr> pccr(idx_get_ccr(context->ctr.ccr));

        //std::cout << "Key:" << key << " Value:" << value << " opcodeFieldMap[key]:" << ofm[key] << "\n";
        switch (ofm[key]) 
        { 
            case PCM::EVENT_SELECT:
                pccr->set_event_select(numValue);
                //std::cout << "pccr value:" << std::hex << pccr->get_ccr_value() <<"\n" << std::dec;
                break;
            case PCM::ENABLE:
                pccr->set_enable(numValue);
                //std::cout << "pccr value:" << std::hex << pccr->get_ccr_value() <<"\n" << std::dec;
                break;
            case EVENT_CATEGORY:
                pccr->set_event_category(numValue);
                //std::cout << "pccr value:" << std::hex << pccr->get_ccr_value() <<"\n" << std::dec;
                break;
            case FILTER_WQ:
                context->ctr.cfr_wq = (uint32_t)numValue;
                break;
            case FILTER_ENG:
                context->ctr.cfr_eng = (uint32_t)numValue;
                break;
            case FILTER_TC:
                context->ctr.cfr_tc = (uint32_t)numValue;
                break;
            case FILTER_PGSZ:
                context->ctr.cfr_pgsz = (uint32_t)numValue;
                break;
            case FILTER_XFERSZ:
                context->ctr.cfr_xfersz = (uint32_t)numValue;
                break;
            case PCM::INVALID:
            default:
                std::cerr << "Field in -o file not recognized. The key is: " << key << "\n";
                return -1;
        }
    }
    else if(cb_type == EVT_LINE_COMPLETE) //this event will be called every line(end)
    {
        if (context->accel == ACCEL_IAA && base_ctr.h_event_name != "IAA")
        {
            return 0; //skip non-IAA cfg line
        }
        else if(context->accel == ACCEL_DSA && base_ctr.h_event_name != "DSA")
        {
            return 0; //skip non-DSA cfg line
        }
        else if(context->accel == ACCEL_QAT && base_ctr.h_event_name != "QAT")
        {
            return 0; //skip non-QAT cfg line
        }

        //Validate the total number of counter exceed the maximum or not.
        if ((uint32)base_ctr.idx >= getMaxNumOfAccelCtrs(m, context->accel))
        {
            std::cerr << "line parse KO due to invalid value!" << std::dec << "\n";
            return 0; //skip the invalid cfg line
        }

        context->ctr.h_event_name = base_ctr.h_event_name;
        context->ctr.v_event_name = base_ctr.v_event_name;
        context->ctr.idx = base_ctr.idx;
        context->ctr.multiplier = base_ctr.multiplier;
        context->ctr.divider = base_ctr.divider;
        context->ctr.h_id = base_ctr.h_id;
        context->ctr.v_id = base_ctr.v_id;
        //std::cout << "line parse OK, ctrcfg=0x" << std::hex << context->ctr.ccr << ", h_event_name=" <<  base_ctr.h_event_name << ", v_event_name=" << base_ctr.v_event_name;
        //std::cout << ", h_id=0x" << std::hex << base_ctr.h_id << ", v_id=0x" << std::hex << base_ctr.v_id;
        //std::cout << ", idx=0x"<< std::hex << base_ctr.idx << ", multiplier=0x" << std::hex << base_ctr.multiplier << ", divider=0x" << std::hex << base_ctr.divider << std::dec << "\n";
        context->ctrs.push_back(context->ctr);
    }
    
    return 0;
}

typedef int (*pfn_evt_handler)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64);

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    null_stream nullStream;
    check_and_set_silent(argc, argv, nullStream);
    set_signal_handlers();

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION ;
    std::cout << "\n This utility measures Sapphire Rapids-SP accelerators information.\n";

    std::string program = string(argv[0]);
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
    accel_evt_parse_context evt_ctx;
    std::map<std::string, uint32_t> opcodeFieldMap;
    std::string ev_file_name;
    pfn_evt_handler p_evt_handler;

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

    if (isAccelCounterAvailable(m, accel) == true)
    {
        if (evtfile == false) //All platform use the spr config file by default.
        {
            ev_file_name = "opCode-143-accel.txt";
        }
        else
        {
            ev_file_name = specify_evtfile;
        }
        //std::cout << "load event config file from:" << ev_file_name << "\n";
    }
    else
    {
        std::cerr << "Error: " << getAccelCounterName(accel) << " device is NOT available/ready with this platform! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    switch (accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            opcodeFieldMap["hname"] = PCM::H_EVENT_NAME;
            opcodeFieldMap["vname"] = PCM::V_EVENT_NAME;
            opcodeFieldMap["multiplier"] = PCM::MULTIPLIER;
            opcodeFieldMap["divider"] = PCM::DIVIDER;
            opcodeFieldMap["ctr"] = PCM::COUNTER_INDEX;
            opcodeFieldMap["en"] = PCM::ENABLE;
            opcodeFieldMap["ev_sel"] = PCM::EVENT_SELECT;
            opcodeFieldMap["ev_cat"] = EVENT_CATEGORY;
            opcodeFieldMap["filter_wq"] = FILTER_WQ;
            opcodeFieldMap["filter_eng"] = FILTER_ENG;
            opcodeFieldMap["filter_tc"] = FILTER_TC;
            opcodeFieldMap["filter_pgsz"] = FILTER_PGSZ;
            opcodeFieldMap["filter_xfersz"] = FILTER_XFERSZ;

            p_evt_handler = idx_evt_parse_handler;
            evt_ctx.m = m;
            evt_ctx.accel = accel;
            evt_ctx.ctrs.clear();//fill the ctrs by evt_handler callback func.
            break;
        default:
            std::cerr << "Error: Accel type=0x" << std::hex << accel << " is not supported! Program aborted\n" << std::dec;
            exit(EXIT_FAILURE);
            break;
    }

    try
    {
        load_events(ev_file_name, opcodeFieldMap, p_evt_handler, (void *)&evt_ctx);
    }
    catch (std::exception & e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        std::cerr << "Error: event cfg file have the problem, please double check it! Program aborted\n";
        exit(EXIT_FAILURE);
    }
    
    if (evt_ctx.ctrs.size() ==0 || evt_ctx.ctrs.size() > getMaxNumOfAccelCtrs(m, evt_ctx.accel))
    {
        std::cerr << "Error: event counter size is 0 or exceed maximum, please check the event cfg file! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    std::ostream* output = &std::cout;
    std::fstream file_stream;
    if (!output_file.empty()) 
    {
        file_stream.open(output_file.c_str(), std::ios_base::out);
        output = &file_stream;
    }

    if (accel == ACCEL_QAT)
    {
        const uint32_t dev_count = getNumOfAccelDevs(m, accel);
        for (uint32_t dev = 0; dev != dev_count; ++dev)
        {
            m->controlQATTelemetry(dev, PCM::QAT_TLM_START); //start the QAT telemetry service
        }
    }

    mainLoop([&]()
    {
        collect_data(m, delay, accel, evt_ctx.ctrs);
        std::vector<std::string> display_buffer = csv ?
                    build_csv(m, accel, evt_ctx.ctrs, human_readable, csv_delimiter, accel_results, loc_map) :
                    build_display(m, accel, evt_ctx.ctrs, accel_results, loc_map);
        display(display_buffer, *output);
        return true;
    });

    file_stream.close();
    exit(EXIT_SUCCESS);
}
