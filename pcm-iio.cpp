/*
Copyright (c) 2017-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// written by Patrick Lu,
//            Aaron Cruz
#include "cpucounters.h"
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#endif
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
using namespace std;
using namespace pcm;

#define PCM_DELAY_DEFAULT 3.0 // in seconds

const uint8_t max_sockets = 4;
static const std::string iio_stack_names[6] = {
    "IIO Stack 0 - CBDMA/DMI      ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - PCIe1          ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - MCP0           ",
    "IIO Stack 5 - MCP1           "
};

map<string,PCM::PerfmonField> opcodeFieldMap;
//TODO: add description for this nameMap
map<string,std::pair<h_id,std::map<string,v_id>>> nameMap;
result_content results(max_sockets, stack_content(6, ctr_data()));

struct data{
    uint32_t width;
    uint64_t value;
};

/**
 * For debug only
 */
void print_nameMap() {
    for (std::map<string,std::pair<h_id,std::map<string,v_id>>>::const_iterator iunit = nameMap.begin(); iunit != nameMap.end(); ++iunit)
    {
        string h_name = iunit->first;
        std::pair<h_id,std::map<string,v_id>> value = iunit->second;
        uint32_t hid = value.first;
        std::map<string,v_id> vMap = value.second;
        cout << "H name: " << h_name << " id =" << hid << " vMap size:" << vMap.size() << "\n";
        for (std::map<string,v_id>::const_iterator junit = vMap.begin(); junit != vMap.end(); ++junit)
        {
            string v_name = junit->first;
            uint32_t vid = junit->second;
            cout << "V name: " << v_name << " id =" << vid << "\n";

        }
    }
}


string a_title (const string &init, const string &name) {
    char begin = init[0];
    string row = init;
    row += name;
    return row + begin;
}

string a_data (string init, struct data d) {
    char begin = init[0];
    string row = init;
    string str_d = unit_format(d.value);
    row += str_d;
    if (str_d.size() > d.width)
        throw std::length_error("counter value > event_name length");
    row += string(d.width - str_d.size(), ' ');
    return row + begin;
}

string build_line(string init, string name, bool last_char = true, char this_char = '_')
{
    char begin = init[0];
    string row = init;
    row += string(name.size(), this_char);
    if (last_char == true)
        row += begin;
    return row;
}


string a_header_footer (string init, string name)
{
    return build_line(init, name);
}

vector<string> combine_stack_name_and_counter_names(string stack_name)
{

    vector<string> v;
    string *tmp = new string[nameMap.size()];
    v.push_back(stack_name);
    for (std::map<string,std::pair<h_id,std::map<string,v_id>>>::const_iterator iunit = nameMap.begin(); iunit != nameMap.end(); ++iunit) {
        string h_name = iunit->first;
        int h_id = (iunit->second).first;
        tmp[h_id] = h_name;
        //cout << "h_id:" << h_id << " name:" << h_name << "\n";
    }
    //XXX: How to simplify and just combine tmp & v?
    for (uint32_t i = 0; i < nameMap.size(); i++) {
        v.push_back(tmp[i]);
    }

	delete[] tmp;
    return v;
}

vector<struct data> prepare_data(const vector<uint64_t> &values, const vector<string> &headers)
{
    vector<struct data> v;
    uint32_t idx = 0;
    for (std::vector<string>::const_iterator iunit = std::next(headers.begin()); iunit != headers.end() && idx < values.size(); ++iunit, idx++)
    {
        struct data d;
        d.width = (uint32_t)iunit->size();
        d.value = values[idx];
        v.push_back(d);
    }

    return v;
}

string build_pci_header(const PCIDB & pciDB, uint32_t column_width, struct pci p, int part = -1, uint32_t level = 0)
{
    string s = "|";
    char bdf_buf[10];
    char speed_buf[10];
    char vid_did_buf[10];
    char device_name_buf[128];

    snprintf(bdf_buf, sizeof(bdf_buf), "%02X:%02X.%1d", p.bdf.busno, p.bdf.devno, p.bdf.funcno);
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

    /* row with data */
    if (part >= 0) {
        s.insert(1,"P" + std::to_string(part) + " ");
        s += std::string(column_width - (s.size()-1), ' ');
    } else { /* row without data, just child pci device */
        s.insert(0, std::string(4*level, ' '));
    }


    return s;
}

vector<string> build_display(vector<struct iio_skx> iio_skx_v, vector<struct counter> &ctrs, vector<int> skt_list, vector<int> stack_list, const PCIDB & pciDB)
{
    vector<string> buffer;
    vector<string> headers;
    vector<struct data> data;
    uint64_t header_width;
    string row;

    for (vector<int>::const_iterator skt_unit = skt_list.begin(); skt_unit != skt_list.end(); ++skt_unit) {
        buffer.push_back("Socket" + std::to_string(*skt_unit));
        struct iio_skx iio_skx = iio_skx_v[*skt_unit];
        for (vector<int>::const_iterator stack_unit = stack_list.begin(); stack_unit != stack_list.end(); ++stack_unit) {
            uint32_t s = *stack_unit;
            headers = combine_stack_name_and_counter_names(iio_skx.stacks[s].stack_name);
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
            std::map<uint32_t,map<uint32_t,struct counter*>> v_sort;
            //re-organize data collection to be row wise
            for (vector<struct counter>::iterator cunit = ctrs.begin(); cunit != ctrs.end(); ++cunit) {
                v_sort[cunit->v_id][cunit->h_id] = &(*cunit);
            }
            for (map<uint32_t,map<uint32_t,struct counter*>>::const_iterator vunit = v_sort.begin(); vunit != v_sort.end(); ++vunit) {
                map<uint32_t, struct counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                for (map<uint32_t,struct counter*>::const_iterator hunit = h_array.begin(); hunit != h_array.end(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][*skt_unit][s][std::pair<h_id,v_id>(hh_id,vv_id)];
                    h_data.push_back(raw_data);
                }
                data = prepare_data(h_data, headers);
                row = "| " + v_name;
                row += string(headers[0].size() - (row.size() - 1), ' ');
                row += std::accumulate(data.begin(), data.end(), string("|"), a_data);
                buffer.push_back(row);
            }

            //Print deliminator
            row = std::accumulate(headers.begin(), headers.end(), string("|"), a_header_footer);
            buffer.push_back(row);
            //Print pcie devices
            for (uint32_t p = 0; p < 4; p++) {
                vector<struct pci> pp = iio_skx.stacks[s].parts[p].child_pci_devs;
                uint8_t level = 1;
                for (std::vector<struct pci>::const_iterator iunit = pp.begin(); iunit != pp.end(); ++iunit)
                {
                    row = build_pci_header(pciDB, (uint32_t)header_width, *iunit, -1, level);
                    buffer.push_back(row);
                    if (iunit->header_type == 1)
                        level += 1;
                }
            }
            //Print footer
            row = std::accumulate(headers.begin(), headers.end(), string(" "), a_header_footer);
            buffer.push_back(row);
        }
    }
    return buffer;
}

void display(const vector<string> &buff)
{
    for (std::vector<string>::const_iterator iunit = buff.begin(); iunit != buff.end(); ++iunit)
        std::cout << *iunit << "\n";
    std::cout << std::flush;
}

void discover_pci_tree(const vector<uint32_t> & busno, uint8_t socket_id, vector<struct iio_skx> &v_iio_skx)
{
    struct iio_skx iio_skx;
    uint32 cpubusno = 0;

    if (PciHandleType::exists(0, (uint32)busno[socket_id], 8, 2)) {
        iio_skx.socket_id = socket_id;
        PciHandleType h(0, busno[socket_id], 8, 2);
        h.read32(0xcc, &cpubusno); // CPUBUSNO register
        iio_skx.stacks[0].busno = cpubusno & 0xff;
        iio_skx.stacks[1].busno = (cpubusno >> 8) & 0xff;
        iio_skx.stacks[2].busno = (cpubusno >> 16) & 0xff;
        iio_skx.stacks[3].busno = (cpubusno >> 24) & 0xff;
        h.read32(0xd0, &cpubusno); // CPUBUSNO1 register
        iio_skx.stacks[4].busno = cpubusno & 0xff;
        iio_skx.stacks[5].busno = (cpubusno >> 8) & 0xff;

        for (uint8_t stack = 0; stack < 6; stack++) {
            uint8_t busno = iio_skx.stacks[stack].busno;
            iio_skx.stacks[stack].stack_name = iio_stack_names[stack];
            //std::cout << "stack" << unsigned(stack) << std::hex << ":0x" << unsigned(busno) << std::dec << ",(" << unsigned(busno) << ")\n";
            for (uint8_t part = 0; part < 4; part++) {
                struct pci *pci = &iio_skx.stacks[stack].parts[part].root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = busno;
                bdf->devno = part;
                bdf->funcno = 0;
                if (stack != 0 && busno == 0) /* This is a workaround to catch some IIO stack does not exist */
                    pci->exist = false;
                else
                    probe_pci(pci);
            }
        }
        for (uint8_t stack = 0; stack < 6; stack++) {
            for (uint8_t part = 0; part < 4; part++) {
                struct pci p = iio_skx.stacks[stack].parts[part].root_pci_dev;
                if (!p.exist)
                    continue;
                for (uint8_t b = p.secondary_bus_number; b <= p.subordinate_bus_number; b++) { /* FIXME: for 0:0.0, we may need to scan from secondary switch down */
                    for (uint8_t d = 0; d < 32; d++) {
                        for (uint8_t f = 0; f < 8; f++) {
                            struct pci pci;
                            pci.exist = false;
                            pci.bdf.busno = b;
                            pci.bdf.devno = d;
                            pci.bdf.funcno = f;
                            probe_pci(&pci);
                            if (pci.exist)
                                iio_skx.stacks[stack].parts[part].child_pci_devs.push_back(pci);
                        }
                    }
                }
            }
        }
        v_iio_skx.push_back(iio_skx);
    }
}

std::string dos2unix(std::string in)
{
    if (in.length() > 0 && int(in[in.length() - 1]) == 13)
    {
        in.erase(in.length() - 1);
    }
    return in;
}

vector<struct counter> load_events(const char* fn)
{
    vector<struct counter> v;
    struct counter ctr;
    ctr.Opcodes.value = 0;

    std::ifstream in(fn);
    std::string line, item;

    if (!in.is_open())
    {
        const auto alt_fn = std::string("/usr/share/pcm/") + fn;
        in.open(alt_fn);
        if (!in.is_open())
        {
            const auto err_msg = std::string("event file ") + fn + " or " + alt_fn + " is not avaiable. Copy it from PCM build directory.";
            throw std::invalid_argument(err_msg);
        }
    }

    while (std::getline(in, line)) {
        /* Ignore anyline with # */
        //TODO: substring until #, if len == 0, skip, else parse normally
        if (line.find("#") != std::string::npos)
            continue;
        /* If line does not have any deliminator, we ignore it as well */
        if (line.find("=") == std::string::npos)
            continue;
        std::istringstream iss(line);
        string h_name, v_name;
        while (std::getline(iss, item, ',')) {
            std::string key, value;
            uint64 numValue;
            /* assume the token has the format <key>=<value> */
            key = item.substr(0,item.find("="));
            value = item.substr(item.find("=")+1);
            istringstream iss2(value);
            iss2 >> setbase(0) >> numValue;

            //cout << "Key:" << key << " Value:" << value << " opcodeFieldMap[key]:" << opcodeFieldMap[key] << "\n";
            switch(opcodeFieldMap[key]) {
                case PCM::H_EVENT_NAME:
                    h_name = dos2unix(value);
                    ctr.h_event_name = h_name;
                    if (nameMap.find(h_name) == nameMap.end()) {
                        /* It's a new horizontal event name */
                        uint32_t next_h_id = (uint32_t)nameMap.size();
                        std::pair<h_id,std::map<string,v_id>> nameMap_value(next_h_id, std::map<string,v_id>());
                        nameMap[h_name] = nameMap_value;
                    }
                    ctr.h_id = (uint32_t)nameMap.size() - 1;
                    break;
                case PCM::V_EVENT_NAME:
                    {
                        v_name = dos2unix(value);
                        ctr.v_event_name = v_name;
                        //XXX: If h_name comes after v_name, we'll have a problem.
                        //XXX: It's very weird, I forgot to assign nameMap[h_name] = nameMap_value earlier (:298), but this part still works?
                        std::map<string,v_id> &v_nameMap = nameMap[h_name].second;
                        if (v_nameMap.find(v_name) == v_nameMap.end()) {
                            v_nameMap[v_name] = (unsigned int)v_nameMap.size() - 1;
                        } else {
                            cerr << "Detect duplicated v_name:" << v_name << "\n";
                            exit(EXIT_FAILURE);
                        }
                        ctr.v_id = (uint32_t)v_nameMap.size() - 1;
                        break;
                    }
                case PCM::COUNTER_INDEX:
                    ctr.idx = (int)numValue;
                    break;
                case PCM::OPCODE:
                    ctr.Opcodes.value = numValue;
                    break;
                case PCM::EVENT_SELECT:
                    ctr.Opcodes.fields.event_select = numValue;
                    break;
                case PCM::UMASK:
                    ctr.Opcodes.fields.umask = numValue;
                    break;
                case PCM::RESET:
                    ctr.Opcodes.fields.reset = numValue;
                    break;
                case PCM::EDGE_DET:
                    ctr.Opcodes.fields.edge_det = numValue;
                    break;
                case PCM::IGNORED:
                    ctr.Opcodes.fields.ignored = numValue;
                    break;
                case PCM::OVERFLOW_ENABLE:
                    ctr.Opcodes.fields.overflow_enable = numValue;
                    break;
                case PCM::ENABLE:
                    ctr.Opcodes.fields.enable = numValue;
                    break;
                case PCM::INVERT:
                    ctr.Opcodes.fields.invert = numValue;
                    break;
                case PCM::THRESH:
                    ctr.Opcodes.fields.thresh = numValue;
                    break;
                case PCM::CH_MASK:
                    ctr.Opcodes.fields.ch_mask = numValue;
                    break;
                case PCM::FC_MASK:
                    ctr.Opcodes.fields.fc_mask = numValue;
                    break;
                //TODO: double type for multipler. drop divider variable
                case PCM::MULTIPLIER:
                    ctr.multiplier = (int)numValue;
                    break;
                case PCM::DIVIDER:
                    ctr.divider = (int)numValue;
                    break;
                case PCM::INVALID:
                    cerr << "Field in -o file not recognized. The key is: " << key << "\n";
                    exit(EXIT_FAILURE);
                    break;
            }
        }
        v.push_back(ctr);
        //cout << "Finish parsing: " << line << " size:" << v.size() << "\n";
        cout << line << " " << std::hex << ctr.Opcodes.value << std::dec << "\n";
    }
    cout << std::flush;

    in.close();

    return v;
}

result_content get_IIO_Samples(PCM *m, vector<struct iio_skx> iio_skx_v, struct counter ctr, uint32_t delay_ms)
{
    IIOCounterState *before, *after;
    IIOPMUCNTCTLRegister rawEvents[4];
    std::vector<int32> IIO_units;
    IIO_units.push_back((int32)PCM::IIO_CBDMA);
    IIO_units.push_back((int32)PCM::IIO_PCIe0);
    IIO_units.push_back((int32)PCM::IIO_PCIe1);
    IIO_units.push_back((int32)PCM::IIO_PCIe2);
    IIO_units.push_back((int32)PCM::IIO_MCP0);
    IIO_units.push_back((int32)PCM::IIO_MCP1);
    rawEvents[ctr.idx] = ctr.Opcodes;
    before = new IIOCounterState[iio_skx_v.size() * IIO_units.size()];
    after = new IIOCounterState[iio_skx_v.size() * IIO_units.size()];

    m->programIIOCounters(rawEvents, -1);
    for (vector<struct iio_skx>::const_iterator socket = iio_skx_v.begin(); socket != iio_skx_v.end(); ++socket) {
        for (vector<int32>::const_iterator stack = IIO_units.begin(); stack != IIO_units.end(); ++stack) {
            uint32_t idx = (uint32_t)IIO_units.size()*socket->socket_id + *stack;
            before[idx] = m->getIIOCounterState(socket->socket_id, *stack, ctr.idx);
        }
    }
    MySleepMs(delay_ms);
    for (vector<struct iio_skx>::const_iterator socket = iio_skx_v.begin(); socket != iio_skx_v.end(); ++socket) {
        struct iio_skx iio_skx = *socket;
        //iio_skx.stacks[*stack].values.clear();
        for (vector<int32>::const_iterator stack = IIO_units.begin(); stack != IIO_units.end(); ++stack) {
            uint32_t idx = (uint32_t)IIO_units.size()*socket->socket_id + *stack;
            after[idx] = m->getIIOCounterState(socket->socket_id, *stack, ctr.idx);
            uint64_t raw_result = getNumberOfEvents(before[idx], after[idx]);
            uint64_t trans_result = uint64_t (raw_result * ctr.multiplier / (double) ctr.divider * (1000 / (double) delay_ms));
            results[iio_skx.socket_id][*stack][std::pair<h_id,v_id>(ctr.h_id,ctr.v_id)] = trans_result;
            //cout << "skt:" << iio_skx.socket_id << " stack:" << *stack << " h_id:" << ctr.h_id << " v_id:" << ctr.v_id << " res:" << raw_result << " trans:" << trans_result << "\n";
        }
    }
    delete[] before;
    delete[] after;
    return results;
}

void collect_data(PCM *m, vector<struct iio_skx> iio_skx_v, vector<struct counter> &ctrs)
{
    result_content s;
    uint32_t delay_ms = (uint32_t)(PCM_DELAY_DEFAULT / ctrs.size() * 1000);
    //cout << "delay_ms:" << delay_ms << "\n";
    for (vector<struct counter>::iterator cunit = ctrs.begin(); cunit != ctrs.end(); ++cunit) {
        cunit->data.clear();
        s = get_IIO_Samples(m, iio_skx_v, *cunit, delay_ms);
        cunit->data.push_back(s);
    }
}

int main(int /*argc*/, char * /*argv*/[])
{
    set_signal_handlers();
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n This utility measures Skylake-SP IIO information\n\n";
#define TEST_VAR 1
#if TEST_VAR == 1
    string ev_file_name = "opCode.txt";
#endif
    vector<int> skt_list;
    vector<int> stack_list;
    vector<struct iio_skx> iio_skx_v;
    vector<struct counter> counters;
    vector<string> display_buffer;
    PCIDB pciDB;
    load_PCIDB(pciDB);

    PCM * m = PCM::getInstance();
    print_cpu_details();
    if(!(m->IIOEventsAvailable()))
    {
        cerr << "Skylake Server CPU is required for this tool! Program aborted\n";
        exit(EXIT_FAILURE);
    }
    if(m->getNumSockets() > max_sockets)
    {
        cerr << "Only systems with up to " << (int)max_sockets << " sockets are supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    opcodeFieldMap["opcode"] =PCM::OPCODE;
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
    opcodeFieldMap["hname"] =PCM::H_EVENT_NAME;
    opcodeFieldMap["vname"] =PCM::V_EVENT_NAME;
    opcodeFieldMap["multiplier"] = PCM::MULTIPLIER;
    opcodeFieldMap["divider"] = PCM::DIVIDER;
    opcodeFieldMap["ctr"] = PCM::COUNTER_INDEX;

    counters = load_events(ev_file_name.c_str());
    //print_nameMap();
    //TODO: Taking from cli

    vector<uint32_t> busno;

    switch(m->getNumSockets())
    {
        case 1:
        case 2:
            {   // TODO: do a proper bus scan
                vector<uint32_t> _{0x0, 0x80};
                busno = _;
            }
            break;
        case 4:
            {
                vector<uint32_t> _{0x0, 0x40, 0x80, 0xc0};
                busno = _;
            }
            break;
        default:
            cerr << "Only systems with " <<m->getNumSockets()<< " sockets are not supported! Program aborted\n";
            exit(EXIT_FAILURE);
    }
    for(uint32 s=0; s < m->getNumSockets();++s) {
        skt_list.push_back(s);
        discover_pci_tree(busno, s, iio_skx_v);
    }
    stack_list.push_back(PCM::IIO_CBDMA);
    stack_list.push_back(PCM::IIO_PCIe0);
    stack_list.push_back(PCM::IIO_PCIe1);
    stack_list.push_back(PCM::IIO_PCIe2);
    stack_list.push_back(PCM::IIO_MCP0);
    stack_list.push_back(PCM::IIO_MCP1);

    while (1) {
        collect_data(m, iio_skx_v, counters);
        display_buffer = build_display(iio_skx_v, counters, skt_list, stack_list, pciDB);
        display(display_buffer);
    };
}
