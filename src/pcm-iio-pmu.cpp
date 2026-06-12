// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2017-2025, Intel Corporation

// written by Patrick Lu,
//            Aaron Cruz
//            Alexander Antonov
//            and others
#include <numeric>

#include "pcm-iio-pmu.h"
#include "pcm-iio-topology.h"

vector<string> combine_stack_name_and_counter_names(string stack_name, const PCIeEventNameMap& nameMap)
{
    vector<string> v;
    vector<string> tmp(nameMap.size());
    v.push_back(stack_name);
    for (auto iunit = nameMap.begin(); iunit != nameMap.end(); ++iunit) {
        string h_name = iunit->first;
        int h_id = (iunit->second).first;
        tmp[h_id] = h_name;
        //cout << "h_id:" << h_id << " name:" << h_name << "\n";
    }
    //XXX: How to simplify and just combine tmp & v?
    for (uint32_t i = 0; i < nameMap.size(); i++) {
        v.push_back(tmp[i]);
    }
    return v;
}

string build_pci_header(const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part, uint32_t level)
{
    string s = "|";
    char bdf_buf[32];
    char speed_buf[10];
    char vid_did_buf[10];
    char device_name_buf[128];

    snprintf(bdf_buf, sizeof(bdf_buf), "%04X:%02X:%02X.%1d", p.bdf.domainno, p.bdf.busno, p.bdf.devno, p.bdf.funcno);
    snprintf(speed_buf, sizeof(speed_buf), "Gen%1d x%-2d", p.link_speed, p.link_width);
    snprintf(vid_did_buf, sizeof(vid_did_buf), "%04X:%04X", p.vendor_id, p.device_id);
    snprintf(device_name_buf, sizeof(device_name_buf), "%s %s",
            (pciDB.first.count(p.vendor_id) > 0)?pciDB.first.at(p.vendor_id).c_str():"unknown vendor",
            (pciDB.second.count(p.vendor_id) > 0 && pciDB.second.at(p.vendor_id).count(p.device_id) > 0)?pciDB.second.at(p.vendor_id).at(p.device_id).c_str():"unknown device"
        );
    s += bdf_buf;
    s += '|';
    s += speed_buf;
    s += '|';
    s += vid_did_buf;
    s += " ";
    s += device_name_buf;

    if (!p.parts_no.empty()) {
        s += "; Part: ";
        for (auto& part : p.parts_no) {
            s += std::to_string(part) + ", ";
        }
        s.erase(s.size() - 2);
    }

    /* row with data */
    if (part >= 0) {
        s.insert(1,"P" + std::to_string(part) + " ");
        s += std::string(column_width - (s.size()-1), ' ');
    } else { /* row without data, just child pci device */
        s.insert(0, std::string(4*level, ' '));
    }

    return s;
}

void build_pci_tree(vector<string> &buffer, const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part, uint32_t level)
{
    string row;
    for (const auto& child : p.child_pci_devs) {
        row = build_pci_header(pciDB, column_width, child, part, level);
        buffer.push_back(row);
        if (child.hasChildDevices())
            build_pci_tree(buffer, pciDB, column_width, child, part, level + 1);
    }
}

std::string get_root_port_dev(const bool show_root_port, int part_id,  const pcm::iio_stack *stack)
{
    char tmp[9] = "        ";
    std::string rp_pci;

    if (!show_root_port)
        return rp_pci;

    for (auto part = stack->parts.begin(); part != stack->parts.end(); part = std::next(part))
    {
        if (part->part_id == part_id)
        {
            std::snprintf(tmp, sizeof(tmp), "%02x:%02x.%x", part->root_pci_dev.bdf.busno,
                        part->root_pci_dev.bdf.devno, part->root_pci_dev.bdf.funcno);
            break;
        }
    }

    rp_pci.append(tmp);
    return rp_pci;

}

class PcmIioCsvBuilder : public PcmIioOutputBuilder {
public:
    PcmIioCsvBuilder(struct pcm_iio_config& config) : PcmIioOutputBuilder(config) {}

    ~PcmIioCsvBuilder() = default;

    vector<string> buildDisplayBuffer() override;
private:
    void insertTimeStamp(vector<string> & out, CsvOutputType type);
};

void PcmIioCsvBuilder::insertTimeStamp(vector<string> & out, CsvOutputType type)
{
    std::string dateTime;
    printDateForCSV(type, m_config.display.csv_delimiter, &dateTime);
    // remove last delimiter
    dateTime.pop_back();
    out.insert(out.begin(), dateTime);
}

vector<string> PcmIioCsvBuilder::buildDisplayBuffer()
{
    vector<string> result;
    vector<string> current_row;
    auto header = combine_stack_name_and_counter_names("Part", m_config.pmu_config.pcieEventNameMap);
    header.insert(header.begin(), "Name");
    if (m_config.display.show_root_port)
        header.insert(header.begin(), "Root Port");
    header.insert(header.begin(), "Socket");
    insertTimeStamp(header, CsvOutputType::Header2);
    result.push_back(build_csv_row(header, m_config.display.csv_delimiter));
    std::map<uint32_t,map<uint32_t,struct iio_counter*>> v_sort;
    //re-organize data collection to be row wise
    size_t max_name_width = 0;
    for (auto counter = m_config.pmu_config.evt_ctx.ctrs.begin(); counter != m_config.pmu_config.evt_ctx.ctrs.end(); ++counter) {
        v_sort[counter->v_id][counter->h_id] = &(*counter);
        max_name_width = (std::max)(max_name_width, counter->v_event_name.size());
    }

    for (auto socket = m_config.pmu_config.iios.cbegin(); socket != m_config.pmu_config.iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            const std::string socket_name = "Socket" + std::to_string(socket->socket_id);

            std::string stack_name = stack->stack_name;
            if (!m_config.display.human_readable) {
                stack_name.erase(stack_name.find_last_not_of(' ') + 1);
            }

            const uint32_t stack_id = stack->iio_unit_id;
            //Print data
            int part_id;
            std::map<uint32_t,map<uint32_t,struct iio_counter*>>::const_iterator vunit;
            for (vunit = v_sort.cbegin(), part_id = 0;
                    vunit != v_sort.cend(); ++vunit, ++part_id) {
                map<uint32_t, struct iio_counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                if (m_config.display.human_readable) {
                    v_name += string(max_name_width - (v_name.size()), ' ');
                }

                current_row.clear();
                current_row.push_back(socket_name);
                if (m_config.display.show_root_port) {
                    auto pci_dev = get_root_port_dev(m_config.display.show_root_port, part_id, &(*stack));
                    current_row.push_back(pci_dev);
                }
                current_row.push_back(stack_name);
                current_row.push_back(v_name);
                for (map<uint32_t,struct iio_counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
                    current_row.push_back(m_config.display.human_readable ? unit_format(raw_data) : std::to_string(raw_data));
                }
                insertTimeStamp(current_row, CsvOutputType::Data);
                result.push_back(build_csv_row(current_row, m_config.display.csv_delimiter));
            }
        }
    }
    return result;
}

class PcmIioDisplayBuilder : public PcmIioOutputBuilder {
public:
    PcmIioDisplayBuilder(struct pcm_iio_config& config) : PcmIioOutputBuilder(config) {}

    ~PcmIioDisplayBuilder() = default;

    vector<string> buildDisplayBuffer() override;
};

vector<string> PcmIioDisplayBuilder::buildDisplayBuffer()
{
    vector<string> buffer;
    vector<string> headers;
    vector<struct data> data;
    uint64_t header_width;
    string row;
    for (auto socket = m_config.pmu_config.iios.cbegin(); socket != m_config.pmu_config.iios.cend(); ++socket) {
        buffer.push_back("Socket" + std::to_string(socket->socket_id));
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto stack_id = stack->iio_unit_id;
            headers = combine_stack_name_and_counter_names(stack->stack_name, m_config.pmu_config.pcieEventNameMap);
            //Print first row
            row = std::accumulate(headers.begin(), headers.end(), string(" "), a_header_footer);
            header_width = row.size();
            buffer.push_back(row);
            //Print a_title
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_title);
            buffer.push_back(row);
            //Print deliminator
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_header_footer);
            buffer.push_back(row);
            //Print data
            std::map<uint32_t,map<uint32_t,struct iio_counter*>> v_sort;
            //re-organize data collection to be row wise
            for (std::vector<struct iio_counter>::iterator counter = m_config.pmu_config.evt_ctx.ctrs.begin(); counter != m_config.pmu_config.evt_ctx.ctrs.end(); ++counter) {
                v_sort[counter->v_id][counter->h_id] = &(*counter);
            }
            for (std::map<uint32_t,map<uint32_t,struct iio_counter*>>::const_iterator vunit = v_sort.cbegin(); vunit != v_sort.cend(); ++vunit) {
                map<uint32_t, struct iio_counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                for (map<uint32_t,struct iio_counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
                    h_data.push_back(raw_data);
                }
                data = prepare_data(h_data, headers);
                row = "| " + v_name;
                row += string(abs(int(headers[0].size() - (row.size() - 1))), ' ');
                row += std::accumulate(data.begin(), data.end(), string("|"), a_data);
                buffer.push_back(row);
            }
            //Print deliminator
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_header_footer);
            buffer.push_back(row);
            //Print pcie devices
            for (const auto& part : stack->parts) {
                uint8_t level = 1;
                for (const auto& pci_device : part.child_pci_devs) {
                    row = build_pci_header(m_config.pciDB, (uint32_t)header_width, pci_device, -1, level);
                    buffer.push_back(row);
                    if (pci_device.hasChildDevices()) {
                        build_pci_tree(buffer, m_config.pciDB, (uint32_t)header_width, pci_device, -1, level + 1);
                    } else if (pci_device.header_type == 1) {
                            level++;
                    }
                }
            }
            //Print footer
            row = std::accumulate(headers.begin(), headers.end(), string(" "), a_header_footer);
            buffer.push_back(row);
        }
    }
    return buffer;
}

std::unique_ptr<PcmIioOutputBuilder> getDisplayBuilder(struct pcm_iio_config& config)
{
    std::unique_ptr<PcmIioOutputBuilder> displayBuilder;
    if (config.display.csv) {
        displayBuilder = std::make_unique<PcmIioCsvBuilder>(config);
    } else {
        displayBuilder = std::make_unique<PcmIioDisplayBuilder>(config);
    }
    return displayBuilder;
}

ccr* get_ccr(uint32 cpu_family_model, uint64_t& ccr)
{
    switch (cpu_family_model)
    {
        case PCM::SKX:
            return new pcm::ccr(ccr, ccr::ccr_type::skx);
        case PCM::ICX:
        case PCM::SNOWRIDGE:
        case PCM::SPR:
        case PCM::EMR:
        case PCM::GRR:
        case PCM::SRF:
        case PCM::GNR:
        case PCM::GNR_D:
            return new pcm::ccr(ccr, ccr::ccr_type::icx);
        default:
            std::cerr << PCM::cpuFamilyModelToUArchCodename(cpu_family_model) << " is not supported! Program aborted" << std::endl;
            exit(EXIT_FAILURE);
    }
}

int iio_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue)
{
    iio_evt_parse_context *context = (iio_evt_parse_context *)cb_ctx;

    if (cb_type == EVT_LINE_START) //this event will be called per line(start)
    {
        context->ctr.ccr = 0;
    }
    else if (cb_type == EVT_LINE_FIELD) //this event will be called per field of line
    {
        std::unique_ptr<ccr> pccr(get_ccr(context->cpu_family_model, context->ctr.ccr));
        switch (ofm[key])
        {
            case PCM::OPCODE:
                break;
            case PCM::EVENT_SELECT:
                pccr->set_event_select(numValue);
                break;
            case PCM::UMASK:
                pccr->set_umask(numValue);
                break;
            case PCM::RESET:
                pccr->set_reset(numValue);
                break;
            case PCM::EDGE_DET:
                pccr->set_edge(numValue);
                break;
            case PCM::IGNORED:
                break;
            case PCM::OVERFLOW_ENABLE:
                pccr->set_ov_en(numValue);
                break;
            case PCM::ENABLE:
                pccr->set_enable(numValue);
                break;
            case PCM::INVERT:
                pccr->set_invert(numValue);
                break;
            case PCM::THRESH:
                pccr->set_thresh(numValue);
                break;
            case PCM::CH_MASK:
                pccr->set_ch_mask(numValue);
                break;
            case PCM::FC_MASK:
                pccr->set_fc_mask(numValue);
                break;
            case PCM::INVALID:
            default:
                std::cerr << "Field in -o file not recognized. The key is: " << key << "\n";
                return -1;
        }
    }
    else if (cb_type == EVT_LINE_COMPLETE) //this event will be called every line(end)
    {
        context->ctr.h_event_name = base_ctr.h_event_name;
        context->ctr.v_event_name = base_ctr.v_event_name;
        context->ctr.idx = base_ctr.idx;
        context->ctr.multiplier = base_ctr.multiplier;
        context->ctr.h_id = base_ctr.h_id;
        context->ctr.v_id = base_ctr.v_id;
        context->ctr.type = base_ctr.type;
        DBG(4, "line parse OK, ctrcfg=0x", std::hex, context->ctr.ccr, ", h_event_name=",  base_ctr.h_event_name, ", v_event_name=", base_ctr.v_event_name);
        DBG(4, ", h_id=0x", std::hex, base_ctr.h_id, ", v_id=0x", std::hex, base_ctr.v_id);
        DBG(4, ", idx=0x", std::hex, base_ctr.idx, ", multiplier=0x", std::hex, base_ctr.multiplier, std::dec, ", counter type = ", static_cast<int>(base_ctr.type), "\n");
        context->ctrs.push_back(context->ctr);
    }

    return 0;
}

class CounterHandlerStrategy {
public:
    CounterHandlerStrategy(PCM* pcm) : m_pcm(pcm) {}
    virtual ~CounterHandlerStrategy() = default;

    virtual void programCounters(uint64 rawEvents[4]) = 0;

    virtual SimpleCounterState getCounterState(uint32_t socket_id, uint32_t unit_id, uint32_t counter_idx) = 0;

protected:
    PCM* m_pcm;
};

class IIOCounterStrategy : public CounterHandlerStrategy {
public:
    IIOCounterStrategy(PCM* pcm) : CounterHandlerStrategy(pcm) {}

    void programCounters(uint64 rawEvents[4]) override
    {
        m_pcm->programIIOCounters(rawEvents);
    }

    SimpleCounterState getCounterState(uint32_t socket_id, uint32_t unit_id, uint32_t counter_idx) override
    {
        return m_pcm->getIIOCounterState(socket_id, unit_id, counter_idx);
    }
};

std::shared_ptr<CounterHandlerStrategy> createCounterStrategy(PCM* pcm, CounterType type)
{
    switch (type)
    {
    case CounterType::iio:
        return std::make_shared<IIOCounterStrategy>(pcm);
    default:
        std::cerr << "Unsupported counter type: " << static_cast<int>(type) << std::endl;
        exit(EXIT_FAILURE);
    }
}

void PcmIioDataCollector::initializeCounterHandlers()
{
    for (const auto& counter : m_config.evt_ctx.ctrs) {
        if (!m_strategies[static_cast<size_t>(counter.type)]) {
            m_strategies[static_cast<size_t>(counter.type)] = createCounterStrategy(m_pcm, counter.type);
        }
    }
}

PcmIioDataCollector::PcmIioDataCollector(struct pcm_iio_pmu_config& config) :
    m_config(config), m_strategies(static_cast<size_t>(CounterType::COUNTER_TYPES_COUNT), nullptr)
{
    m_pcm = PCM::getInstance();
    m_delay_ms = static_cast<uint32_t>(m_config.delay * 1000 / m_config.evt_ctx.ctrs.size());
    m_stacks_count = m_pcm->getMaxNumOfIOStacks();
    m_time_scaling_factor = 1000.0 / m_delay_ms;

    m_before = std::make_unique<SimpleCounterState[]>(m_config.iios.size() * m_stacks_count);
    m_after = std::make_unique<SimpleCounterState[]>(m_config.iios.size() * m_stacks_count);

    m_results.resize(m_pcm->getNumSockets(), stack_content(m_stacks_count, ctr_data()));

    initializeCounterHandlers();
}

void PcmIioDataCollector::collectData()
{
    for (auto& counter : m_config.evt_ctx.ctrs) {
        counter.data.clear();
        result_content sample = getSample(counter);
        counter.data.push_back(sample);
    }
}

result_content PcmIioDataCollector::getSample(struct iio_counter & ctr)
{
    uint64 rawEvents[COUNTERS_NUMBER] = {0};
    std::unique_ptr<ccr> pccr(get_ccr(m_pcm->getCPUFamilyModel(), ctr.ccr));
    rawEvents[ctr.idx] = pccr->get_ccr_value();

    auto strategy = m_strategies[static_cast<size_t>(ctr.type)];

    strategy->programCounters(rawEvents);
    for (const auto& socket : m_config.iios) {
        for (const auto& stack : socket.stacks) {
            auto iio_unit_id = stack.iio_unit_id;
            uint32_t idx = getStackIndex(socket.socket_id, iio_unit_id);
            m_before[idx] = strategy->getCounterState(socket.socket_id, iio_unit_id, ctr.idx);
        }
    }
    MySleepMs(m_delay_ms);
    for (const auto& socket : m_config.iios) {
        for (const auto& stack : socket.stacks) {
            auto iio_unit_id = stack.iio_unit_id;
            uint32_t idx = getStackIndex(socket.socket_id, iio_unit_id);
            m_after[idx] = strategy->getCounterState(socket.socket_id, iio_unit_id, ctr.idx);
            uint64_t raw_result = getNumberOfEvents(m_before[idx], m_after[idx]);
            uint64_t trans_result = static_cast<uint64_t>(raw_result * ctr.multiplier * m_time_scaling_factor);
            m_results[socket.socket_id][iio_unit_id][std::pair<h_id,v_id>(ctr.h_id, ctr.v_id)] = trans_result;
        }
    }
    return m_results;
}

void fillOpcodeFieldMapForPCIeEvents(map<string,uint32_t>& opcodeFieldMap)
{
    opcodeFieldMap["opcode"] = PCM::OPCODE;
    opcodeFieldMap["ev_sel"] = PCM::EVENT_SELECT;
    opcodeFieldMap["umask"] = PCM::UMASK;
    opcodeFieldMap["reset"] = PCM::RESET;
    opcodeFieldMap["edge_det"] = PCM::EDGE_DET;
    opcodeFieldMap["ignored"] = PCM::IGNORED;
    opcodeFieldMap["overflow_enable"] = PCM::OVERFLOW_ENABLE;
    opcodeFieldMap["en"] = PCM::ENABLE;
    opcodeFieldMap["invert"] = PCM::INVERT;
    opcodeFieldMap["thresh"] = PCM::THRESH;
    opcodeFieldMap["ch_mask"] = PCM::CH_MASK;
    opcodeFieldMap["fc_mask"] = PCM::FC_MASK;
    opcodeFieldMap["hname"] = PCM::H_EVENT_NAME;
    opcodeFieldMap["vname"] = PCM::V_EVENT_NAME;
    opcodeFieldMap["multiplier"] = PCM::MULTIPLIER;
    opcodeFieldMap["ctr"] = PCM::COUNTER_INDEX;
    opcodeFieldMap["unit"] = PCM::UNIT_TYPE;
}

bool setupPCIeEventContextAndNameMap( iio_evt_parse_context& evt_ctx, PCIeEventNameMap& nameMap)
{
    PCM * m = PCM::getInstance();

    string ev_file_name = "opCode-" + std::to_string(m->getCPUFamily()) + "-" + std::to_string(m->getInternalCPUModel()) + ".txt";

    map<string,uint32_t> opcodeFieldMap;
    fillOpcodeFieldMapForPCIeEvents( opcodeFieldMap );

    evt_ctx.cpu_family_model = m->getCPUFamilyModel();
    evt_ctx.ctrs.clear();//fill the ctrs by evt_handler call back func.

    try
    {
        load_events(ev_file_name, opcodeFieldMap, iio_evt_parse_handler, (void *)&evt_ctx, nameMap);
    }
    catch (const std::exception & e)
    {
        std::cerr << "Error info:" << e.what() << std::endl;
        std::cerr << "The event configuration file (" << ev_file_name << ") cannot be loaded. Please verify the file. Exiting." << std::endl;
        return false;
    }

    return true;
}

bool initializePCIeBWCounters(struct pcm_iio_pmu_config& pmu_config)
{
    PCM * m = PCM::getInstance();
    if (!m->IIOEventsAvailable())
    {
        cerr << "This CPU is not supported by PCM IIO tool! Program aborted\n";
        return false;
    }

    if (!IPlatformMapping::initializeIOStacksStructure(pmu_config.iios, m->getCPUFamilyModel(), m->getNumSockets(), m->getMaxNumOfIOStacks())) return false;

    return setupPCIeEventContextAndNameMap(pmu_config.evt_ctx, pmu_config.pcieEventNameMap);
}
