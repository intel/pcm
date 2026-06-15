// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2017-2025, Intel Corporation

// written by Patrick Lu,
//            Aaron Cruz
//            Alexander Antonov
//            and others
#pragma once

#ifdef _MSC_VER
    #include <windows.h>
    #include "windows/windriver.h"
#else
    #include <unistd.h>
#endif

#include <memory>
#include <cstdint>
#include <algorithm>

#ifdef _MSC_VER
    #include "freegetopt/getopt.h"
#endif

#include "lspci.h"
#include "utils.h"
#include "cpucounters.h"

using namespace std;
using namespace pcm;

#define PCM_DELAY_DEFAULT 3.0 // in seconds

struct iio_counter : public counter {
  std::vector<result_content> data;
};

typedef struct
{
    uint32 cpu_family_model;
    iio_counter ctr;
    vector<struct iio_counter> ctrs;
} iio_evt_parse_context;

vector<string> combine_stack_name_and_counter_names(string stack_name, const PCIeEventNameMap& nameMap);

string build_pci_header(const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part = -1, uint32_t level = 0);

void build_pci_tree(vector<string> &buffer, const PCIDB & pciDB, uint32_t column_width, const struct pci &p, int part, uint32_t level = 0);

std::string get_root_port_dev(const bool show_root_port, int part_id,  const pcm::iio_stack *stack);

struct pcm_iio_display_config {
    bool csv            = false;
    bool human_readable = false;
    bool show_root_port = false;
    bool list           = false;
    std::string csv_delimiter = ",";
    std::string output_file   = "";
};

struct pcm_iio_pmu_config {
    double delay = PCM_DELAY_DEFAULT;
    // Map with metrics names.
    PCIeEventNameMap pcieEventNameMap;
    vector<struct iio_stacks_on_socket> iios;
    iio_evt_parse_context evt_ctx;
};

struct pcm_iio_config {
    struct pcm_iio_display_config display;
    struct pcm_iio_pmu_config pmu_config;
    PCIDB pciDB;
};

class PcmIioOutputBuilder {
public:
    PcmIioOutputBuilder(struct pcm_iio_config& config) : m_config(config) {}

    virtual ~PcmIioOutputBuilder() = default;

    virtual vector<string> buildDisplayBuffer() = 0;
protected:
    struct pcm_iio_config& m_config;
};

std::unique_ptr<PcmIioOutputBuilder> getDisplayBuilder(struct pcm_iio_config& config);

int iio_evt_parse_handler(evt_cb_type cb_type, void *cb_ctx, counter &base_ctr, std::map<std::string, uint32_t> &ofm, std::string key, uint64 numValue);

class CounterHandlerStrategy;

class PcmIioDataCollector {
public:
    PcmIioDataCollector(struct pcm_iio_pmu_config& config);
    ~PcmIioDataCollector() = default;

    void collectData();
private:
    struct pcm_iio_pmu_config& m_config;
    PCM *m_pcm;
    uint32_t m_delay_ms;
    uint32_t m_stacks_count;
    double m_time_scaling_factor;
    std::unique_ptr<SimpleCounterState[]> m_before;
    std::unique_ptr<SimpleCounterState[]> m_after;
    result_content m_results;
    std::vector<std::shared_ptr<CounterHandlerStrategy>> m_strategies;

    result_content getSample(struct iio_counter & ctr);
    void initializeCounterHandlers();

    uint32_t getStackIndex(uint32_t socket_id, uint32_t io_unit_id) const { return m_stacks_count * socket_id + io_unit_id; }

    static constexpr int COUNTERS_NUMBER = 4;
};

void fillOpcodeFieldMapForPCIeEvents(map<string,uint32_t>& opcodeFieldMap);

bool initializePCIeBWCounters(struct pcm_iio_pmu_config& pmu_config);

