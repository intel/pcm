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

using namespace std;
using namespace pcm;

class IPlatformMapping {
private:
    uint32_t m_sockets;
    uint32_t m_model;
protected:
    void probeDeviceRange(std::vector<struct pci> &child_pci_devs, int domain, int secondary, int subordinate);

    uint32_t socketsCount() const { return m_sockets; }
    uint32_t cpuId() const { return m_model; }
public:
    IPlatformMapping(int cpu_model, uint32_t sockets_count) : m_sockets(sockets_count), m_model(cpu_model) {}
    virtual ~IPlatformMapping() {};
    static std::unique_ptr<IPlatformMapping> getPlatformMapping(int cpu_model, uint32_t sockets_count);
    virtual bool pciTreeDiscover(std::vector<struct iio_stacks_on_socket>& iios);
};

void initializeIOStacksStructure( std::vector<struct iio_stacks_on_socket>& iios );