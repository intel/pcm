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
using namespace std;
using namespace pcm;

#define PCM_DELAY_DEFAULT 3.0 // in seconds

#define QAT_DID 0x18DA
#define NIS_DID 0x18D1
#define HQM_DID 0x270B

#define ROOT_BUSES_OFFSET   0xCC
#define ROOT_BUSES_OFFSET_2 0xD0

#define SKX_SOCKETID_UBOX_DID 0x2014
#define SKX_UBOX_DEVICE_NUM   0x08
#define SKX_UBOX_FUNCTION_NUM 0x02
#define SKX_BUS_NUM_STRIDE    8
//the below LNID and GID applies to Skylake Server
#define SKX_UNC_SOCKETID_UBOX_LNID_OFFSET 0xC0
#define SKX_UNC_SOCKETID_UBOX_GID_OFFSET  0xD4

const uint8_t max_sockets = 4;
static const std::string iio_stack_names[6] = {
    "IIO Stack 0 - CBDMA/DMI      ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - PCIe1          ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - MCP0           ",
    "IIO Stack 5 - MCP1           "
};

static const std::string skx_iio_stack_names[6] = {
    "IIO Stack 0 - CBDMA/DMI      ",
    "IIO Stack 1 - PCIe0          ",
    "IIO Stack 2 - PCIe1          ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - MCP0           ",
    "IIO Stack 5 - MCP1           "
};

static const std::string icx_iio_stack_names[6] = {
    "IIO Stack 0 - PCIe0          ",
    "IIO Stack 1 - PCIe1          ",
    "IIO Stack 2 - MCP            ",
    "IIO Stack 3 - PCIe2          ",
    "IIO Stack 4 - PCIe3          ",
    "IIO Stack 5 - CBDMA/DMI      "
};

static const std::string snr_iio_stack_names[5] = {
    "IIO Stack 0 - QAT            ",
    "IIO Stack 1 - CBDMA/DMI      ",
    "IIO Stack 2 - NIS            ",
    "IIO Stack 3 - HQM            ",
    "IIO Stack 4 - PCIe           "
};

#define ICX_CBDMA_DMI_SAD_ID 0
#define ICX_MCP_SAD_ID       3

#define ICX_PCH_PART_ID   0
#define ICX_CBDMA_PART_ID 3

#define SNR_ICX_SAD_CONTROL_CFG_OFFSET 0x3F4
#define SNR_ICX_MESH2IIO_MMAP_DID      0x09A2

#define ICX_VMD_PCI_DEVNO   0x00
#define ICX_VMD_PCI_FUNCNO  0x05

static const std::map<int, int> icx_sad_to_pmu_id_mapping = {
    { ICX_CBDMA_DMI_SAD_ID, 5 },
    { 1,                    0 },
    { 2,                    1 },
    { ICX_MCP_SAD_ID,       2 },
    { 4,                    3 },
    { 5,                    4 }
};

#define SNR_ACCELERATOR_PART_ID 4

#define SNR_ROOT_PORT_A_DID 0x334A

#define SNR_CBDMA_DMI_SAD_ID 0
#define SNR_PCIE_GEN3_SAD_ID 1
#define SNR_HQM_SAD_ID       2
#define SNR_NIS_SAD_ID       3
#define SNR_QAT_SAD_ID       4

static const std::map<int, int> snr_sad_to_pmu_id_mapping = {
    { SNR_CBDMA_DMI_SAD_ID, 1 },
    { SNR_PCIE_GEN3_SAD_ID, 4 },
    { SNR_HQM_SAD_ID      , 3 },
    { SNR_NIS_SAD_ID      , 2 },
    { SNR_QAT_SAD_ID      , 0 }
};

map<string,PCM::PerfmonField> opcodeFieldMap;
//TODO: add description for this nameMap
map<string,std::pair<h_id,std::map<string,v_id>>> nameMap;
//TODO: remove binding to stacks amount
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

vector<string> build_display(vector<struct iio_stacks_on_socket>& iios, vector<struct counter>& ctrs, const PCIDB& pciDB)
{
    vector<string> buffer;
    vector<string> headers;
    vector<struct data> data;
    uint64_t header_width;
    string row;
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        buffer.push_back("Socket" + std::to_string(socket->socket_id));
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto stack_id = stack->iio_unit_id;
            headers = combine_stack_name_and_counter_names(stack->stack_name);
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
            for (std::vector<struct counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
                v_sort[counter->v_id][counter->h_id] = &(*counter);
            }
            for (std::map<uint32_t,map<uint32_t,struct counter*>>::const_iterator vunit = v_sort.cbegin(); vunit != v_sort.cend(); ++vunit) {
                map<uint32_t, struct counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                for (map<uint32_t,struct counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
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
            for (const auto& part : stack->parts) {
                uint8_t level = 1;
                for (const auto& pci_device : part.child_pci_devs) {
                    row = build_pci_header(pciDB, (uint32_t)header_width, pci_device, -1, level);
                    buffer.push_back(row);
                    if (pci_device.header_type == 1)
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

std::string build_csv_row(const std::vector<std::string>& chunks, const std::string& delimiter)
{
    return std::accumulate(chunks.begin(), chunks.end(), std::string(""),
                           [delimiter](const string &left, const string &right){
                               return left.empty() ? right : left + delimiter + right;
                           });
}

vector<string> build_csv(vector<struct iio_stacks_on_socket>& iios, vector<struct counter>& ctrs,
    const bool human_readable, const std::string& csv_delimiter)
{
    vector<string> result;
    vector<string> current_row;
    auto header = combine_stack_name_and_counter_names("Name");
    header.insert(header.begin(), "Socket");
    result.push_back(build_csv_row(header, csv_delimiter));
    std::map<uint32_t,map<uint32_t,struct counter*>> v_sort;
    //re-organize data collection to be row wise
    size_t max_name_width = 0;
    for (std::vector<struct counter>::iterator counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
        v_sort[counter->v_id][counter->h_id] = &(*counter);
        max_name_width = (std::max)(max_name_width, counter->v_event_name.size());
    }

    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            const std::string socket_name = "Socket" + std::to_string(socket->socket_id);

            std::string stack_name = stack->stack_name;
            if (!human_readable) {
                stack_name.erase(stack_name.find_last_not_of(' ') + 1);
            }

            const uint32_t stack_id = stack->iio_unit_id;
            //Print data
            for (std::map<uint32_t,map<uint32_t,struct counter*>>::const_iterator vunit = v_sort.cbegin(); vunit != v_sort.cend(); ++vunit) {
                map<uint32_t, struct counter*> h_array = vunit->second;
                uint32_t vv_id = vunit->first;
                vector<uint64_t> h_data;
                string v_name = h_array[0]->v_event_name;
                if (human_readable) {
                    v_name += string(max_name_width - (v_name.size()), ' ');
                }

                current_row.clear();
                current_row.push_back(socket_name);
                current_row.push_back(stack_name);
                current_row.push_back(v_name);
                for (map<uint32_t,struct counter*>::const_iterator hunit = h_array.cbegin(); hunit != h_array.cend(); ++hunit) {
                    uint32_t hh_id = hunit->first;
                    uint64_t raw_data = hunit->second->data[0][socket->socket_id][stack_id][std::pair<h_id,v_id>(hh_id,vv_id)];
                    current_row.push_back(human_readable ? unit_format(raw_data) : std::to_string(raw_data));
                }
                result.push_back(build_csv_row(current_row, csv_delimiter));
            }
        }
    }
    return result;
}

void display(const vector<string> &buff, std::ostream& stream)
{
    for (std::vector<string>::const_iterator iunit = buff.begin(); iunit != buff.end(); ++iunit)
        stream << *iunit << "\n";
    stream << std::flush;
}

class IPlatformMapping {
private:
public:
    virtual ~IPlatformMapping() {};
    static IPlatformMapping* getPlatformMapping(int cpu_model);
    virtual bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count) = 0;
};

// Mapping for SkyLake Server.
class PurleyPlatformMapping: public IPlatformMapping {
private:
    void getUboxBusNumbers(std::vector<uint32_t>& ubox);
public:
    PurleyPlatformMapping() = default;
    ~PurleyPlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count) override;
};

void PurleyPlatformMapping::getUboxBusNumbers(std::vector<uint32_t>& ubox)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev)) {
                    if ((pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID) && (pci_dev.device_id == SKX_SOCKETID_UBOX_DID)) {
                        ubox.push_back(bus);
                    }
                }
            }
        }
    }
}

bool PurleyPlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count)
{
    std::vector<uint32_t> ubox;
    getUboxBusNumbers(ubox);
    if (ubox.empty()) {
        cerr << "UBOXs were not found! Program aborted" << endl;
        return false;
    }

    for (uint32_t socket_id = 0; socket_id < sockets_count; socket_id++) {
        if (!PciHandleType::exists(0, ubox[socket_id], SKX_UBOX_DEVICE_NUM, SKX_UBOX_FUNCTION_NUM)) {
            cerr << "No access to PCICFG\n" << endl;
            return false;
        }
        uint64 cpubusno = 0;
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = socket_id;
        PciHandleType h(0, ubox[socket_id], SKX_UBOX_DEVICE_NUM, SKX_UBOX_FUNCTION_NUM);
        h.read64(ROOT_BUSES_OFFSET, &cpubusno);

        iio_on_socket.stacks.reserve(6);
        for (int stack_id = 0; stack_id < 6; stack_id++) {
            struct iio_stack stack;
            stack.iio_unit_id = stack_id;
            stack.busno = (uint8_t)(cpubusno >> (stack_id * SKX_BUS_NUM_STRIDE));
            stack.stack_name = skx_iio_stack_names[stack_id];
            for (uint8_t part_id = 0; part_id < 4; part_id++) {
                struct iio_bifurcated_part part;
                part.part_id = part_id;
                struct pci *pci = &part.root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = stack.busno;
                bdf->devno = part_id;
                bdf->funcno = 0;
                /* This is a workaround to catch some IIO stack does not exist */
                if (stack_id != 0 && stack.busno == 0) {
                    pci->exist = false;
                }
                else if (probe_pci(pci)) {
                    /* FIXME: for 0:0.0, we may need to scan from secondary switch down */
                    for (uint8_t bus = pci->secondary_bus_number; bus <= pci->subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                }
                stack.parts.push_back(part);
            }

            iio_on_socket.stacks.push_back(stack);
        }
        iios.push_back(iio_on_socket);
    }

    return true;
}

class IPlatformMapping10Nm: public IPlatformMapping {
private:
public:
    bool getSadIdRootBusMap(uint32_t socket_id, std::map<uint8_t, uint8_t>& sad_id_bus_map);
};

bool IPlatformMapping10Nm::getSadIdRootBusMap(uint32_t socket_id, std::map<uint8_t, uint8_t>& sad_id_bus_map)
{
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev) && (pci_dev.vendor_id == PCM_INTEL_PCI_VENDOR_ID)
                    && (pci_dev.device_id == SNR_ICX_MESH2IIO_MMAP_DID)) {

                    PciHandleType h(0, bus, device, function);
                    std::uint32_t sad_ctrl_cfg;
                    h.read32(SNR_ICX_SAD_CONTROL_CFG_OFFSET, &sad_ctrl_cfg);
                    if (sad_ctrl_cfg == (std::numeric_limits<uint32_t>::max)()) {
                        cerr << "Could not read SAD_CONTROL_CFG" << endl;
                        return false;
                    }

                    if ((sad_ctrl_cfg & 0xf) == socket_id) {
                        uint8_t sid = (sad_ctrl_cfg >> 4) & 0x7;
                        sad_id_bus_map.insert(std::pair<uint8_t, uint8_t>(sid, (uint8_t)bus));
                    }
                }
            }
        }
    }

    if (sad_id_bus_map.empty()) {
        cerr << "Could not find Root Port bus numbers" << endl;
        return false;
    }

    return true;
}

// Mapping for IceLake Server.
class WhitleyPlatformMapping: public IPlatformMapping10Nm {
private:
public:
    WhitleyPlatformMapping() = default;
    ~WhitleyPlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count) override;
};

bool WhitleyPlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count)
{
    for (uint32_t socket = 0; socket < sockets_count; socket++) {
        struct iio_stacks_on_socket iio_on_socket;
        iio_on_socket.socket_id = socket;
        std::map<uint8_t, uint8_t> sad_id_bus_map;
        if (!getSadIdRootBusMap(socket, sad_id_bus_map)) {
            return false;
        }

        {
            struct iio_stack stack;
            stack.iio_unit_id = icx_sad_to_pmu_id_mapping.at(ICX_MCP_SAD_ID);
            stack.stack_name = icx_iio_stack_names[stack.iio_unit_id];
            iio_on_socket.stacks.push_back(stack);
        }

        for (auto sad_id_bus_pair = sad_id_bus_map.cbegin(); sad_id_bus_pair != sad_id_bus_map.cend(); ++sad_id_bus_pair) {
            int sad_id = sad_id_bus_pair->first;
            if (icx_sad_to_pmu_id_mapping.find(sad_id) ==
                icx_sad_to_pmu_id_mapping.end()) {
                cerr << "Unknown SAD ID: " << sad_id << endl;
                return false;
            }

            if (sad_id == ICX_MCP_SAD_ID) {
                continue;
            }

            struct iio_stack stack;
            int root_bus = sad_id_bus_pair->second;
            if (sad_id == ICX_CBDMA_DMI_SAD_ID) {
                // There is one DMA Controller on each socket
                stack.iio_unit_id = icx_sad_to_pmu_id_mapping.at(sad_id);
                stack.busno = root_bus;
                stack.stack_name = icx_iio_stack_names[stack.iio_unit_id];

                // PCH is on socket 0 only
                if (socket == 0) {
                    struct iio_bifurcated_part pch_part;
                    struct pci *pci = &pch_part.root_pci_dev;
                    struct bdf *bdf = &pci->bdf;
                    pch_part.part_id = ICX_PCH_PART_ID;
                    bdf->busno = root_bus;
                    bdf->devno = 0x00;
                    bdf->funcno = 0x00;
                    probe_pci(pci);
                    // Probe child devices only under PCH part.
                    for (uint8_t bus = pci->secondary_bus_number; bus <= pci->subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    pch_part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                    stack.parts.push_back(pch_part);
                }

                struct iio_bifurcated_part part;
                part.part_id = ICX_CBDMA_PART_ID;
                struct pci *pci = &part.root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = root_bus;
                bdf->devno = 0x01;
                bdf->funcno = 0x00;
                probe_pci(pci);
                stack.parts.push_back(part);

                iio_on_socket.stacks.push_back(stack);
                continue;
            }
            stack.busno = root_bus;
            stack.iio_unit_id = icx_sad_to_pmu_id_mapping.at(sad_id);
            stack.stack_name = icx_iio_stack_names[stack.iio_unit_id];
            for (int slot = 2; slot < 6; slot++) {
                struct pci pci;
                pci.bdf.busno = root_bus;
                pci.bdf.devno = slot;
                pci.bdf.funcno = 0x00;
                if (!probe_pci(&pci)) {
                    continue;
                }
                struct iio_bifurcated_part part;
                part.part_id = slot - 2;
                part.root_pci_dev = pci;

                for (uint8_t bus = pci.secondary_bus_number; bus <= pci.subordinate_bus_number; bus++) {
                    for (uint8_t device = 0; device < 32; device++) {
                        for (uint8_t function = 0; function < 8; function++) {
                            struct pci child_pci_dev;
                            child_pci_dev.bdf.busno = bus;
                            child_pci_dev.bdf.devno = device;
                            child_pci_dev.bdf.funcno = function;
                            if (probe_pci(&child_pci_dev)) {
                                part.child_pci_devs.push_back(child_pci_dev);
                            }
                        }
                    }
                }
                stack.parts.push_back(part);
            }
            iio_on_socket.stacks.push_back(stack);
        }
        std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());
        iios.push_back(iio_on_socket);
    }
    return true;
}

// Mapping for Snowridge.
class JacobsvillePlatformMapping: public IPlatformMapping10Nm {
private:
public:
    JacobsvillePlatformMapping() = default;
    ~JacobsvillePlatformMapping() = default;
    bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count) override;
    bool JacobsvilleAccelerators(const std::pair<uint8_t, uint8_t>& sad_id_bus_pair, struct iio_stack& stack);
};

bool JacobsvillePlatformMapping::JacobsvilleAccelerators(const std::pair<uint8_t, uint8_t>& sad_id_bus_pair, struct iio_stack& stack)
{
    uint16_t expected_dev_id;
    auto sad_id = sad_id_bus_pair.first;
    switch (sad_id) {
    case SNR_HQM_SAD_ID:
        expected_dev_id = HQM_DID;
        break;
    case SNR_NIS_SAD_ID:
        expected_dev_id = NIS_DID;
        break;
    case SNR_QAT_SAD_ID:
        expected_dev_id = QAT_DID;
        break;
    default:
        return false;
    }
    stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
    stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
    for (uint16_t bus = sad_id_bus_pair.second; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                struct pci pci_dev;
                pci_dev.bdf.busno = bus;
                pci_dev.bdf.devno = device;
                pci_dev.bdf.funcno = function;
                if (probe_pci(&pci_dev)) {
                    if (expected_dev_id == pci_dev.device_id) {
                        struct iio_bifurcated_part part;
                        part.part_id = SNR_ACCELERATOR_PART_ID;
                        part.root_pci_dev = pci_dev;
                        stack.busno = bus;
                        stack.parts.push_back(part);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool JacobsvillePlatformMapping::pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios, uint32_t sockets_count)
{
    std::map<uint8_t, uint8_t> sad_id_bus_map;
    PCM_UNUSED(sockets_count);
    if (!getSadIdRootBusMap(0, sad_id_bus_map)) {
        return false;
    }
    struct iio_stacks_on_socket iio_on_socket;
    iio_on_socket.socket_id = 0;
    if (sad_id_bus_map.size() != snr_sad_to_pmu_id_mapping.size()) {
        cerr << "Found unexpected number of stacks: " << sad_id_bus_map.size() << ", expected: " << snr_sad_to_pmu_id_mapping.size() << endl;
        return false;
    }

    for (auto sad_id_bus_pair = sad_id_bus_map.cbegin(); sad_id_bus_pair != sad_id_bus_map.cend(); ++sad_id_bus_pair) {
        int sad_id = sad_id_bus_pair->first;
        struct iio_stack stack;
        switch (sad_id) {
        case SNR_CBDMA_DMI_SAD_ID:
            {
                int root_bus = sad_id_bus_pair->second;
                stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
                stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
                stack.busno = root_bus;
                // DMA Controller
                struct iio_bifurcated_part part;
                part.part_id = 0;
                struct pci pci_dev;
                pci_dev.bdf.busno = root_bus;
                pci_dev.bdf.devno = 0x01;
                pci_dev.bdf.funcno = 0x00;
                probe_pci(&pci_dev);
                part.root_pci_dev = pci_dev;
                stack.parts.push_back(part);

                part.part_id = 4;
                pci_dev.bdf.busno = root_bus;
                pci_dev.bdf.devno = 0x00;
                pci_dev.bdf.funcno = 0x00;
                probe_pci(&pci_dev);
                for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                    for (uint8_t device = 0; device < 32; device++) {
                        for (uint8_t function = 0; function < 8; function++) {
                            struct pci child_pci_dev;
                            child_pci_dev.bdf.busno = bus;
                            child_pci_dev.bdf.devno = device;
                            child_pci_dev.bdf.funcno = function;
                            if (probe_pci(&child_pci_dev)) {
                                part.child_pci_devs.push_back(child_pci_dev);
                            }
                        }
                    }
                }
                part.root_pci_dev = pci_dev;
                stack.parts.push_back(part);
            }
            break;
        case SNR_PCIE_GEN3_SAD_ID:
            {
                int root_bus = sad_id_bus_pair->second;
                stack.busno = root_bus;
                stack.iio_unit_id = snr_sad_to_pmu_id_mapping.at(sad_id);
                stack.stack_name = snr_iio_stack_names[stack.iio_unit_id];
                for (int slot = 4; slot < 8; slot++) {
                    struct pci pci_dev;
                    pci_dev.bdf.busno = root_bus;
                    pci_dev.bdf.devno = slot;
                    pci_dev.bdf.funcno = 0x00;
                    if (!probe_pci(&pci_dev)) {
                        continue;
                    }
                    int part_id = 4 + pci_dev.device_id - SNR_ROOT_PORT_A_DID;
                    if ((part_id < 0) || (part_id > 4)) {
                        cerr << "Invalid part ID " << part_id << endl;
                        return false;
                    }
                    struct iio_bifurcated_part part;
                    part.part_id = part_id;
                    part.root_pci_dev = pci_dev;
                    for (uint8_t bus = pci_dev.secondary_bus_number; bus <= pci_dev.subordinate_bus_number; bus++) {
                        for (uint8_t device = 0; device < 32; device++) {
                            for (uint8_t function = 0; function < 8; function++) {
                                struct pci child_pci_dev;
                                child_pci_dev.bdf.busno = bus;
                                child_pci_dev.bdf.devno = device;
                                child_pci_dev.bdf.funcno = function;
                                if (probe_pci(&child_pci_dev)) {
                                    part.child_pci_devs.push_back(child_pci_dev);
                                }
                            }
                        }
                    }
                    stack.parts.push_back(part);
                }
            }
            break;
        case SNR_HQM_SAD_ID:
        case SNR_NIS_SAD_ID:
        case SNR_QAT_SAD_ID:
            JacobsvilleAccelerators(*sad_id_bus_pair, stack);
            break;
        default:
            cerr << "Unknown SAD ID: " << sad_id << endl;
            return false;
        }
        iio_on_socket.stacks.push_back(stack);
    }

    std::sort(iio_on_socket.stacks.begin(), iio_on_socket.stacks.end());

    iios.push_back(iio_on_socket);

    return true;
}

IPlatformMapping* IPlatformMapping::getPlatformMapping(int cpu_model)
{
    switch (cpu_model) {
    case PCM::SKX:
        return new PurleyPlatformMapping();
    case PCM::ICX:
        return new WhitleyPlatformMapping();
    case PCM::SNOWRIDGE:
        return new JacobsvillePlatformMapping();
    default:
        return nullptr;
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

ccr* get_ccr(PCM* m, uint64_t& ccr)
{
    switch (m->getCPUModel())
    {
        case PCM::SKX:
            return new skx_ccr(ccr);
        case PCM::ICX:
        case PCM::SNOWRIDGE:
            return new icx_ccr(ccr);
        default:
            cerr << "Skylake Server CPU is required for this tool! Program aborted" << endl;
            exit(EXIT_FAILURE);
    }
}

vector<struct counter> load_events(PCM * m, const char* fn)
{
    vector<struct counter> v;
    struct counter ctr;
    std::unique_ptr<ccr> pccr(get_ccr(m, ctr.ccr));

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
        pccr->set_ccr_value(0);
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
        cout << line << " " << std::hex << ctr.ccr << std::dec << "\n";
    }
    cout << std::flush;

    in.close();

    return v;
}

result_content get_IIO_Samples(PCM *m, const std::vector<struct iio_stacks_on_socket>& iios, struct counter ctr, uint32_t delay_ms)
{
    IIOCounterState *before, *after;
    uint64 rawEvents[4] = {0};
    std::unique_ptr<ccr> pccr(get_ccr(m, ctr.ccr));
    rawEvents[ctr.idx] = pccr->get_ccr_value();
    int stacks_count = iios[0].stacks.size();
    before = new IIOCounterState[iios.size() * stacks_count];
    after = new IIOCounterState[iios.size() * stacks_count];

    m->programIIOCounters(rawEvents);
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto iio_unit_id = stack->iio_unit_id;
            uint32_t idx = (uint32_t)stacks_count * socket->socket_id + iio_unit_id;
            before[idx] = m->getIIOCounterState(socket->socket_id, iio_unit_id, ctr.idx);
        }
    }
    MySleepMs(delay_ms);
    for (auto socket = iios.cbegin(); socket != iios.cend(); ++socket) {
        for (auto stack = socket->stacks.cbegin(); stack != socket->stacks.cend(); ++stack) {
            auto iio_unit_id = stack->iio_unit_id;
            uint32_t idx = (uint32_t)stacks_count * socket->socket_id + iio_unit_id;
            after[idx] = m->getIIOCounterState(socket->socket_id, iio_unit_id, ctr.idx);
            uint64_t raw_result = getNumberOfEvents(before[idx], after[idx]);
            uint64_t trans_result = uint64_t (raw_result * ctr.multiplier / (double) ctr.divider * (1000 / (double) delay_ms));
            results[socket->socket_id][iio_unit_id][std::pair<h_id,v_id>(ctr.h_id,ctr.v_id)] = trans_result;
        }
    }
    delete[] before;
    delete[] after;
    return results;
}

void collect_data(PCM *m, const double delay, vector<struct iio_stacks_on_socket>& iios, vector<struct counter>& ctrs)
{
    const uint32_t delay_ms = uint32_t(delay * 1000 / ctrs.size());
    for (auto counter = ctrs.begin(); counter != ctrs.end(); ++counter) {
        counter->data.clear();
        result_content sample = get_IIO_Samples(m, iios, *counter, delay_ms);
        counter->data.push_back(sample);
    }
}

/**
 * For debug only
 */
void print_PCIeMapping(const std::vector<struct iio_stacks_on_socket>& iios, const PCIDB & pciDB)
{
    for (auto it = iios.begin(); it != iios.end(); ++it) {
        printf("Socket %d\n", (*it).socket_id);
        for (int stack = 0; stack < 6; stack++) {
            for (auto stack : it->stacks) {
                printf("\t%s root bus: 0x%x", stack.stack_name.c_str(), stack.busno);
                printf(stack.flipped ? "\tflipped: true\n" : "\tflipped: false\n");
                for (auto part : stack.parts) {
                    vector<struct pci> pp = part.child_pci_devs;
                    uint8_t level = 1;
                    for (std::vector<struct pci>::const_iterator iunit = pp.begin(); iunit != pp.end(); ++iunit)
                    {
                        uint64_t header_width = 100;
                        string row = build_pci_header(pciDB, (uint32_t)header_width, *iunit, -1, level);
                        printf("\t\t%s\n", row.c_str());
                        if (iunit->header_type == 1)
                            level += 1;
                    }
                }
            }
        }
    }
}

bool check_argument_equals(const char* arg, std::initializer_list<const char*> arg_names)
{
    const auto arg_len = strlen(arg);
    for (const auto arg_name: arg_names) {
        if (arg_len == strlen(arg_name) && strncmp(arg, arg_name, arg_len) == 0) {
            return true;
        }
    }

    return false;
}

bool extract_argument_value(const char* arg, std::initializer_list<const char*> arg_names, std::string& value)
{
    const auto arg_len = strlen(arg);
    for (const auto arg_name: arg_names) {
        const auto arg_name_len = strlen(arg_name);
        if (arg_len > arg_name_len && strncmp(arg, arg_name, arg_name_len) == 0 && arg[arg_name_len] == '=') {
            value = arg + arg_name_len + 1;
            const auto last_pos = value.find_last_not_of("\"");
            if (last_pos != string::npos) {
                value.erase(last_pos + 1);
            }
            const auto first_pos = value.find_first_not_of("\"");
            if (first_pos != string::npos) {
                value.erase(0, first_pos);
            }

            return true;
        }
    }

    return false;
}

void print_usage(const string& progname)
{
    cerr << "\n Usage: \n " << progname << " --help | [interval] [options] \n";
    cerr << "   <interval>                           => time interval in seconds (floating point number is accepted)\n";
    cerr << "                                        to sample performance counters.\n";
    cerr << "                                        If not specified - 3.0 is used\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help  | /h               => print this help and exit\n";
    cerr << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cerr << "  -csv-delimiter=<value>  | /csv-delimiter=<value>   => set custom csv delimiter\n";
    cerr << "  -human-readable | /human-readable  => use human readable format for output (for csv only)\n";
    cerr << " Examples:\n";
    cerr << "  " << progname << " 1.0                   => print counters every second\n";
    cerr << "  " << progname << " 0.5 -csv=test.log     => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << progname << " -csv -human-readable  => every 3 second print counters in human-readable CSV format\n";
    cerr << "\n";
}

int main(int argc, char * argv[])
{
    set_signal_handlers();
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";
    std::cout << "\n This utility measures Skylake-SP IIO information\n\n";

    string program = string(argv[0]);

    vector<struct counter> counters;
    PCIDB pciDB;
    load_PCIDB(pciDB);
    bool csv = false;
    bool human_readable = false;
    std::string csv_delimiter = ",";
    std::string output_file;
    double delay = PCM_DELAY_DEFAULT;
    PCM * m = PCM::getInstance();

    while (argc > 1) {
        argv++;
        argc--;
        std::string arg_value;
        if (check_argument_equals(*argv, {"--help", "-h", "/h"})) {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (extract_argument_value(*argv, {"-csv-delimiter", "/csv-delimiter"}, arg_value)) {
            csv_delimiter = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"})) {
            csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value)) {
            csv = true;
            output_file = std::move(arg_value);
        }
        else if (check_argument_equals(*argv, {"-human-readable", "/human-readable"})) {
            human_readable = true;
        }
        else {
            // any other options positional that is a floating point number is treated as <delay>,
            // while the other options are ignored with a warning issues to stderr
            double delay_input;
            istringstream is_str_stream(*argv);
            is_str_stream >> noskipws >> delay_input;
            if (is_str_stream.eof() && !is_str_stream.fail()) {
                if (delay_input < 0) {
                    cerr << "Unvalid delay specified: \"" << *argv << "\". Delay should be positive.\n";
                    print_usage(program);
                    exit(EXIT_FAILURE);
                }
                delay = delay_input;
            }
            else {
                cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                print_usage(program);
                exit(EXIT_FAILURE);
            }
        }
    }

    print_cpu_details();
    string ev_file_name;
    if (m->IIOEventsAvailable())
    {
        ev_file_name = "opCode-" + std::to_string(m->getCPUModel()) + ".txt";
    }
    else
    {
        cerr << "This CPU is not supported by PCM IIO tool! Program aborted\n";
        exit(EXIT_FAILURE);
    }

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
    opcodeFieldMap["hname"] =PCM::H_EVENT_NAME;
    opcodeFieldMap["vname"] =PCM::V_EVENT_NAME;
    opcodeFieldMap["multiplier"] = PCM::MULTIPLIER;
    opcodeFieldMap["divider"] = PCM::DIVIDER;
    opcodeFieldMap["ctr"] = PCM::COUNTER_INDEX;

    counters = load_events(m, ev_file_name.c_str());
    //print_nameMap();
    //TODO: Taking from cli

    //TODO: remove binding to max sockets count.
    if (m->getNumSockets() > max_sockets) {
        cerr << "Only systems with up to " << max_sockets << " sockets are supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    auto mapping = IPlatformMapping::getPlatformMapping(m->getCPUModel());
    if (!mapping) {
        cerr << "Failed to discover pci tree: unknown platform" << endl;
        exit(EXIT_FAILURE);
    }

    std::vector<struct iio_stacks_on_socket> iios;
    if (!mapping->pciTreeDiscover(iios, m->getNumSockets())) {
        exit(EXIT_FAILURE);
    }

    /* Debug only:
    print_PCIeMapping(iios, pciDB);
    return 0;
    */
    std::ostream* output = &std::cout;
    std::fstream file_stream;
    if (!output_file.empty()) {
        file_stream.open(output_file.c_str(), std::ios_base::out);
        output = &file_stream;
    }

    while (1) {
        collect_data(m, delay, iios, counters);
        vector<string> display_buffer = csv ?
            build_csv(iios, counters, human_readable, csv_delimiter) :
            build_display(iios, counters, pciDB);
        display(display_buffer, *output);
    };
}
