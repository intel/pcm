// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2023, Intel Corporation
// written by White.Hu, Pavithran P

#pragma once
#include "cpucounters.h"
#ifdef __linux__
#include <sys/utsname.h>
#endif
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

idx_ccr* idx_get_ccr(uint64_t& ccr);

typedef enum
{
    ACCEL_IAA,
    ACCEL_DSA,
    ACCEL_QAT,
    ACCEL_MAX,
    ACCEL_NOCONFIG,
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
    std::vector<struct accel_counter> ctrs;
} accel_evt_parse_context;

typedef int (*pfn_evt_handler)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64);

int idx_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue);
void readAccelCounters(SystemCounterState &sycs_);

class AcceleratorCounterState {

    private:
        AcceleratorCounterState(){};     // forbidden to call directly because it is a singleton
        AcceleratorCounterState & operator = (const AcceleratorCounterState &) = delete;
        static AcceleratorCounterState * instance;
        accel_evt_parse_context evt_ctx = { {}, {}, {}, {} };
    public:
        AcceleratorCounterState(const AcceleratorCounterState& obj) = delete;
        // std::mutex instanceCreationMutex;
        static AcceleratorCounterState * getInstance();    
        std::map<std::string, uint32_t> opcodeFieldMap;
        std::string ev_file_name;
        pfn_evt_handler p_evt_handler = NULL;

        void setEvents(PCM * m,ACCEL_IP accel,std::string specify_evtfile,bool evtfile);
        uint32_t getNumOfAccelDevs();
        uint32_t getAccel();
        uint32_t getMaxNumOfAccelCtrs();
        std::vector<struct accel_counter>& getCounters();
        int32_t programAccelCounters();
        SimpleCounterState getAccelCounterState(uint32 dev, uint32 ctr_index);
        bool isAccelCounterAvailable();
        std::string getAccelCounterName();
        void setDSA();
        bool getAccelDevLocation( uint32_t dev, const ACCEL_DEV_LOC_MAPPING loc_map, uint32_t &location);
        // void readAccelCounters(SystemCounterState sycs_);
        int getNumberOfCounters();
        std::string getAccelIndexCounterName(int ctr_index);    
        std::string remove_string_inside_use(std::string text);
        uint64 getAccelIndexCounter(uint32 dev, const SystemCounterState & before,const SystemCounterState & after,int ctr_index);
    
};