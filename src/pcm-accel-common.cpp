// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2023, Intel Corporation
// written by White.Hu, Pavithran P

#include "pcm-accel-common.h"
#include "cpucounters.h"
#include <mutex>

idx_ccr* idx_get_ccr(uint64_t& ccr)
{
    return new spr_idx_ccr(ccr);
}

uint32_t AcceleratorCounterState::getNumOfAccelDevs()
{
    uint32_t dev_count = 0;

    if (evt_ctx.accel >= ACCEL_MAX || evt_ctx.m == NULL)
        return 0;

    switch (evt_ctx.accel)
    {
        case ACCEL_IAA:
            dev_count = evt_ctx.m->getNumOfIDXAccelDevs(PCM::IDX_IAA);
            break;
        case ACCEL_DSA:
            dev_count = evt_ctx.m->getNumOfIDXAccelDevs(PCM::IDX_DSA);
            break;
        case ACCEL_QAT:
            dev_count = evt_ctx.m->getNumOfIDXAccelDevs(PCM::IDX_QAT);
            break;
        default:
            dev_count = 0;
            break;
    }

    return dev_count;
}

uint32_t AcceleratorCounterState::getMaxNumOfAccelCtrs()
{
    uint32_t ctr_count = 0;

    if (evt_ctx.accel >= ACCEL_MAX || evt_ctx.m == NULL)
        return 0;

    switch (evt_ctx.accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            ctr_count = evt_ctx.m->getMaxNumOfIDXAccelCtrs(evt_ctx.accel);
            break;
        default:
            ctr_count = 0;
            break;
    }

    return ctr_count;
}

int32_t AcceleratorCounterState::programAccelCounters()
{
    std::vector<uint64_t> rawEvents;
    std::vector<uint32_t> filters_wq, filters_tc, filters_pgsz, filters_xfersz, filters_eng;

    if (evt_ctx.m == NULL || evt_ctx.accel >= ACCEL_MAX || evt_ctx.ctrs.size() == 0 || evt_ctx.ctrs.size()  > getMaxNumOfAccelCtrs())
        return -1;

    switch (evt_ctx.accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            for (auto pctr = evt_ctx.ctrs.begin(); pctr != evt_ctx.ctrs.end(); ++pctr)
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
            evt_ctx.m->programIDXAccelCounters(idx_accel_mapping[evt_ctx.accel], rawEvents, filters_wq, filters_eng, filters_tc, filters_pgsz, filters_xfersz);
            break;
        default:
            break;
    }

    return 0;
}

SimpleCounterState AcceleratorCounterState::getAccelCounterState(uint32 dev, uint32 ctr_index)
{
    SimpleCounterState result;
    
    if (evt_ctx.m == NULL || evt_ctx.accel >= ACCEL_MAX || dev >= getNumOfAccelDevs() || ctr_index >= getMaxNumOfAccelCtrs())
        return result;

    switch (evt_ctx.accel)
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
        case ACCEL_QAT:
            result = evt_ctx.m->getIDXAccelCounterState(evt_ctx.accel, dev, ctr_index);
            break;
        case ACCEL_MAX:
        case ACCEL_NOCONFIG:
            break;
    }

    return result;
}

bool AcceleratorCounterState::isAccelCounterAvailable()
{
    bool ret = true;

    if (evt_ctx.m == NULL || evt_ctx.accel >= ACCEL_MAX)
        ret =false;

    if (getNumOfAccelDevs() == 0)
        ret = false;

    return ret;
}

std::string AcceleratorCounterState::getAccelCounterName()
{
    std::string ret;
    
    switch (evt_ctx.accel)
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
            ret = "id=" + std::to_string(evt_ctx.accel) + "(unknown)";
    }

    return ret;
}

bool AcceleratorCounterState::getAccelDevLocation( uint32_t dev, const ACCEL_DEV_LOC_MAPPING loc_map, uint32_t &location)
{
    bool ret = true;
    
    switch (loc_map)
    {
        case SOCKET_MAP:
            location = evt_ctx.m->getCPUSocketIdOfIDXAccelDev(evt_ctx.accel, dev);
            break;
        case NUMA_MAP:
            location = evt_ctx.m->getNumaNodeOfIDXAccelDev(evt_ctx.accel, dev);
            break;
        default:
            ret = false;
    }
    
    return ret;
}

/*! \brief Computes number of accelerator counters present in system

    \return Number of accel counters in system
*/
int AcceleratorCounterState::getNumberOfCounters(){
    
    return getCounters().size();
}

std::string AcceleratorCounterState::getAccelIndexCounterName(int ctr_index)
{
    accel_counter pctr = getCounters().at(ctr_index);                
    return pctr.v_event_name;
}

uint64 AcceleratorCounterState::getAccelIndexCounter(uint32 dev, const SystemCounterState & before,const SystemCounterState & after,int ctr_index)
{
    const uint32_t counter_nb = getCounters().size();
    accel_counter pctr = getCounters().at(ctr_index);
    uint64_t raw_result = getNumberOfEvents(before.accel_counters[dev*counter_nb + ctr_index], after.accel_counters[dev*counter_nb + ctr_index]);
    uint64_t trans_result = uint64_t (raw_result * pctr.multiplier / (double) pctr.divider );
    return trans_result;
}

int idx_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue)
{
    accel_evt_parse_context *context = (accel_evt_parse_context *)cb_ctx;
    // PCM *m = context->m;
    AcceleratorCounterState *accs_;
    accs_ = AcceleratorCounterState::getInstance();

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
        if ((uint32)base_ctr.idx >= accs_->getMaxNumOfAccelCtrs())
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

std::vector<struct accel_counter>& AcceleratorCounterState::getCounters(){
    return evt_ctx.ctrs;
}

uint32_t AcceleratorCounterState::getAccel()
{
    return evt_ctx.accel;
}

void readAccelCounters(SystemCounterState& sycs_)
{
    AcceleratorCounterState *accs_ = AcceleratorCounterState::getInstance();
    PCM *pcm = PCM::getInstance();
    // const uint32_t delay_ms = uint32_t(delay * 1000);
    const uint32_t dev_count = accs_->getNumOfAccelDevs();
    const uint32_t counter_nb = accs_->getCounters().size();
    pcm->setNumberofAccelCounters(dev_count*counter_nb);
    uint32_t ctr_index = 0;
    // accel_content accel_results(ACCEL_MAX, dev_content(ACCEL_IP_DEV_COUNT_MAX, ctr_data()));
    sycs_.accel_counters.resize(size_t(dev_count) * size_t(counter_nb));
    SimpleCounterState *currState = new SimpleCounterState[dev_count*counter_nb];
    // programAccelCounters(m, accel, ctrs);

    switch (accs_->getAccel())
    {
        case ACCEL_IAA:
        case ACCEL_DSA:
            for (uint32_t dev = 0; dev != dev_count; ++dev)
            {
                ctr_index = 0;
                for (auto pctr = accs_->getCounters().begin(); pctr != accs_->getCounters().end(); ++pctr)
                {
                    sycs_.accel_counters[dev*counter_nb + ctr_index] = accs_->getAccelCounterState( dev, ctr_index);
                    ctr_index++;
                }
            }
            break;

        case ACCEL_QAT:
            // MySleepMs(delay_ms);

            for (uint32_t dev = 0; dev != dev_count; ++dev)
            { 
               pcm->controlQATTelemetry(dev, PCM::QAT_TLM_REFRESH);
               ctr_index = 0;
               for (auto pctr = accs_->getCounters().begin();pctr != accs_->getCounters().end(); ++pctr)
               {
                   sycs_.accel_counters[dev*counter_nb + ctr_index] = accs_->getAccelCounterState(dev, ctr_index);

                //    raw_result = currState[dev*counter_nb + ctr_index].getRawData();
                //    trans_result = uint64_t (raw_result * pctr->multiplier / (double) pctr->divider );

                //accel_result[evt_ctx.accel][dev][std::pair<h_id,v_id>(pctr->h_id,pctr->v_id)] = trans_result;
                //std::cout << "collect_data: accel=" << accel << " dev=" << dev << " h_id=" << pctr->h_id << " v_id=" << pctr->v_id << " data=" << std::hex << trans_result << "\n" << std::dec;
                   ctr_index++;
               }
            }
            break;
    }

    deleteAndNullifyArray(currState);
    
}

AcceleratorCounterState* AcceleratorCounterState::instance = NULL;

std::mutex instanceCreationMutexForAcceleratorCounterState{};

AcceleratorCounterState * AcceleratorCounterState::getInstance()
 {
    // lock-free read
    // cppcheck-suppress identicalConditionAfterEarlyExit
    if (instance) return instance;

    std::unique_lock<std::mutex> _(instanceCreationMutexForAcceleratorCounterState);
    // cppcheck-suppress identicalConditionAfterEarlyExit
    if (instance) return instance;

    return instance = new AcceleratorCounterState();
 }

std::string AcceleratorCounterState::remove_string_inside_use(std::string text) {
    std::string result = "";
    int open_use_count = 0;
    for (char c : text) {
        if (c == '(') {
            open_use_count += 1;
        } else if (c == ')' ) {
            open_use_count -= 1;
        } else if (open_use_count == 0) {
            result += c;
        }
    }
    return result;
}

void AcceleratorCounterState::setEvents(PCM *m,ACCEL_IP  accel, std::string specify_evtfile,bool evtfile)
{
    evt_ctx.m = m;
    evt_ctx.accel = accel;
    if (isAccelCounterAvailable() == true)
    {
        if (evtfile==false) //All platform use the spr config file by default.
        {
            ev_file_name = "opCode-6-143-accel.txt";
        }
        else
        {
            ev_file_name = specify_evtfile;
        }
        //std::cout << "load event config file from:" << ev_file_name << "\n";
    }
    else
    {
        std::cerr << "Error: " << getAccelCounterName() << " device is NOT available/ready with this platform! Program aborted\n";
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
            evt_ctx.ctrs.clear();//fill the ctrs by evt_handler callback func.
            break;
        default:
            std::cerr << "Error: Accel type=0x" << std::hex << accel << " is not supported! Program aborted\n" << std::dec;
            exit(EXIT_FAILURE);
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
    if (evt_ctx.ctrs.size() ==0 || evt_ctx.ctrs.size() > getMaxNumOfAccelCtrs())
    {
        std::cout<< evt_ctx.ctrs.size()<< " " << getMaxNumOfAccelCtrs();
        std::cerr << "Error: event counter size is 0 or exceed maximum, please check the event cfg file! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    if (accel == ACCEL_QAT)
    {
        const uint32_t dev_count = getNumOfAccelDevs();
        for (uint32_t dev = 0; dev != dev_count; ++dev)
        {
            m->controlQATTelemetry(dev, PCM::QAT_TLM_START); //start the QAT telemetry service
        }
    }
}