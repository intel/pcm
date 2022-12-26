// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
//
// monitor CPU conters for ksysguard
//
// contact: Thomas Willhalm, Patrick Ungerer, Roman Dementiev
//
// This program is not a tutorial on how to write nice interpreters
// but a proof of concept on using ksysguard with performance counters
//

/*!     \file pcm-sensor.cpp
        \brief Example of using CPU counters: implements a graphical plugin for KDE ksysguard
*/
#include <iostream>
#include <string>
#include <sstream>
#include "cpuasynchcounter.h"
#include "utils.h"

using namespace std;
using namespace pcm;

PCM_MAIN_NOTHROW;

int mainThrows(int /* argc */, char * /*argv*/ [])
{
    set_signal_handlers();

    AsynchronCounterState counters;

    cout << "CPU counter sensor " << PCM_VERSION << "\n";
    cout << "ksysguardd 1.2.0\n";
    cout << "ksysguardd> ";

    while (1)
    {
        string s;
        cin >> s;

        const auto xpi = counters.getXpi();

        // list counters
        if (s == "monitors") {
            for (uint32 i = 0; i < counters.getNumCores(); ++i) {
                for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                    if (a == counters.getSocketId(i)) {
                        cout << "Socket" << a << "/CPU" << i << "/Frequency\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/IPC\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/L2CacheHitRatio\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/L3CacheHitRatio\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/L2CacheMisses\tinteger\n";
                        cout << "Socket" << a << "/CPU" << i << "/L3CacheMisses\tinteger\n";
                        cout << "Socket" << a << "/CPU" << i << "/L3Occupancy\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/LocalMemoryBandwidth\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/RemoteMemoryBandwidth\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/CoreC0StateResidency\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/CoreC3StateResidency\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/CoreC6StateResidency\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/CoreC7StateResidency\tfloat\n";
                        cout << "Socket" << a << "/CPU" << i << "/ThermalHeadroom\tinteger\n";
                    }
            }
            for (uint32 a = 0; a < counters.getNumSockets(); ++a) {
                cout << "Socket" << a << "/BytesReadFromMC\tfloat\n";
                cout << "Socket" << a << "/BytesWrittenToMC\tfloat\n";
                cout << "Socket" << a << "/BytesReadFromPMM\tfloat\n";
                cout << "Socket" << a << "/BytesWrittenToPMM\tfloat\n";
                cout << "Socket" << a << "/Frequency\tfloat\n";
                cout << "Socket" << a << "/IPC\tfloat\n";
                cout << "Socket" << a << "/L2CacheHitRatio\tfloat\n";
                cout << "Socket" << a << "/L3CacheHitRatio\tfloat\n";
                cout << "Socket" << a << "/L2CacheMisses\tinteger\n";
                cout << "Socket" << a << "/L3CacheMisses\tinteger\n";
                cout << "Socket" << a << "/L3Occupancy\tfloat\n";
                cout << "Socket" << a << "/LocalMemoryBandwidth\tfloat\n";
                cout << "Socket" << a << "/RemoteMemoryBandwidth\tfloat\n";
                cout << "Socket" << a << "/CoreC0StateResidency\tfloat\n";
                cout << "Socket" << a << "/CoreC3StateResidency\tfloat\n";
                cout << "Socket" << a << "/CoreC6StateResidency\tfloat\n";
                cout << "Socket" << a << "/CoreC7StateResidency\tfloat\n";
                cout << "Socket" << a << "/PackageC2StateResidency\tfloat\n";
                cout << "Socket" << a << "/PackageC3StateResidency\tfloat\n";
                cout << "Socket" << a << "/PackageC6StateResidency\tfloat\n";
                cout << "Socket" << a << "/PackageC7StateResidency\tfloat\n";
                cout << "Socket" << a << "/ThermalHeadroom\tinteger\n";
                cout << "Socket" << a << "/CPUEnergy\tfloat\n";
                cout << "Socket" << a << "/DRAMEnergy\tfloat\n";
            }
            for (uint32 a = 0; a < counters.getNumSockets(); ++a) {
                for (uint32 l = 0; l < counters.getQPILinksPerSocket(); ++l)
                    cout << "Socket" << a << "/BytesIncomingTo" << xpi << l << "\tfloat\n";
            }

            cout << xpi << "_Traffic\tfloat\n";
            cout << "Frequency\tfloat\n";
            cout << "IPC\tfloat\n";       //double check output
            cout << "L2CacheHitRatio\tfloat\n";
            cout << "L3CacheHitRatio\tfloat\n";
            cout << "L2CacheMisses\tinteger\n";
            cout << "L3CacheMisses\tinteger\n";
            cout << "CoreC0StateResidency\tfloat\n";
            cout << "CoreC3StateResidency\tfloat\n";
            cout << "CoreC6StateResidency\tfloat\n";
            cout << "CoreC7StateResidency\tfloat\n";
            cout << "PackageC2StateResidency\tfloat\n";
            cout << "PackageC3StateResidency\tfloat\n";
            cout << "PackageC6StateResidency\tfloat\n";
            cout << "PackageC7StateResidency\tfloat\n";
            cout << "CPUEnergy\tfloat\n";
            cout << "DRAMEnergy\tfloat\n";
        }

        // provide metadata

        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/Frequency?";
                        if (s == c.str()) {
                            cout << "FREQ. CPU" << i << "\t\t\tMHz\n";
                        }
                    }
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/ThermalHeadroom?";
                        if (s == c.str()) {
                            cout << "Temperature reading in 1 degree Celsius relative to the TjMax temperature (thermal headroom) for CPU" << i << "\t\t\t°C\n";
                        }
                    }
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/CoreC0StateResidency?";
                        if (s == c.str()) {
                            cout << "core C0-state residency for CPU" << i << "\t\t\t%\n";
                        }
                    }
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/CoreC3StateResidency?";
                        if (s == c.str()) {
                            cout << "core C3-state residency for CPU" << i << "\t\t\t%\n";
                        }
                    }
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/CoreC6StateResidency?";
                        if (s == c.str()) {
                            cout << "core C6-state residency for CPU" << i << "\t\t\t%\n";
                        }
                    }
                    {
                        stringstream c;
                        c << "Socket" << a << "/CPU" << i << "/CoreC7StateResidency?";
                        if (s == c.str()) {
                            cout << "core C7-state residency for CPU" << i << "\t\t\t%\n";
                        }
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/IPC?";
                    if (s == c.str()) {
                        cout << "IPC CPU" << i << "\t0\t\t\n";
                        //cout << "CPU" << i << "\tInstructions per Cycle\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/L2CacheHitRatio?";
                    if (s == c.str()) {
                        cout << "L2 Cache Hit Ratio CPU" << i << "\t0\t\t\n";
                        //   cout << "CPU" << i << "\tL2 Cache Hit Ratio\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/L3CacheHitRatio?";
                    if (s == c.str()) {
                        cout << "L3 Cache Hit Ratio CPU" << i << "\t0\t\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/L2CacheMisses?";
                    if (s == c.str()) {
                        cout << "L2 Cache Misses CPU" << i << "\t0\t\t \n";
                        //cout << "CPU" << i << "\tL2 Cache Misses\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/L3CacheMisses?";
                    if (s == c.str()) {
                        cout << "L3 Cache Misses CPU" << i << "\t0\t\t \n";
                        //cout << "CPU" << i << "\tL3 Cache Misses\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/L3Occupancy?";
                    if (s == c.str()) {
                        cout << "L3 Cache Occupancy CPU " << i << "\t0\t\t \n";
                        //cout << "CPU" << i << "\tL3 Cache Occupancy\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/LocalMemoryBandwidth?";
                    if (s == c.str()) {
                        cout << "Local Memory Bandwidth CPU " << i << "\t0\t\t \n";
                        //cout << "CPU" << i << "\tLocal Memory Bandwidth\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumCores(); ++i) {
            for (uint32 a = 0; a < counters.getNumSockets(); ++a)
                if (a == counters.getSocketId(i)) {
                    stringstream c;
                    c << "Socket" << a << "/CPU" << i << "/RemoteMemoryBandwidth?";
                    if (s == c.str()) {
                        cout << "Remote Memory Bandwidth CPU " << i << "\t0\t\t \n";
                        //cout << "CPU" << i << "\tRemote Memory Bandwidth\t0\t1\t \n";
                    }
                }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/BytesReadFromMC?";
            if (s == c.str()) {
                cout << "read from MC Socket" << i << "\t0\t\tGB\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/BytesReadFromPMM?";
            if (s == c.str()) {
                cout << "read from PMM memory on Socket" << i << "\t0\t\tGB\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/DRAMEnergy?";
            if (s == c.str()) {
                cout << "Energy consumed by DRAM on socket " << i << "\t0\t\tJoule\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/CPUEnergy?";
            if (s == c.str()) {
                cout << "Energy consumed by CPU package " << i << "\t0\t\tJoule\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/ThermalHeadroom?";
            if (s == c.str()) {
                cout << "Temperature reading in 1 degree Celsius relative to the TjMax temperature (thermal headroom) for CPU package " << i << "\t0\t\t°C\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/CoreC0StateResidency?";
            if (s == c.str()) {
                cout << "core C0-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/CoreC3StateResidency?";
            if (s == c.str()) {
                cout << "core C3-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/CoreC6StateResidency?";
            if (s == c.str()) {
                cout << "core C6-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/CoreC7StateResidency?";
            if (s == c.str()) {
                cout << "core C7-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/PackageC2StateResidency?";
            if (s == c.str()) {
                cout << "package C2-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/PackageC3StateResidency?";
            if (s == c.str()) {
                cout << "package C3-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/PackageC6StateResidency?";
            if (s == c.str()) {
                cout << "package C6-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/PackageC7StateResidency?";
            if (s == c.str()) {
                cout << "package C7-state residency for CPU package " << i << "\t0\t\t%\n";
            }
        }
        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/BytesWrittenToPMM?";
            if (s == c.str()) {
                cout << "written to PMM memory on Socket" << i << "\t0\t\tGB\n";
                //cout << "CPU" << i << "\tBytes written to memory channel\t0\t1\t GB\n";
            }
        }

        for (uint32 l = 0; l < counters.getQPILinksPerSocket(); ++l) {
            for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
                stringstream c;
                c << "Socket" << i << "/BytesIncomingTo" << xpi << l << "?";
                if (s == c.str()) {
                    //cout << "Socket" << i << "\tBytes incoming to QPI link\t" << l<< "\t\t GB\n";
                    cout << "incoming to Socket" << i << " " << xpi << " Link" << l << "\t0\t\tGB\n";
                }
            }
        }

        {
            stringstream c;
            c << xpi << "_Traffic?";
            if (s == c.str()) {
                cout << "Traffic on all " << xpi << " links\t0\t\tGB\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/Frequency?";
            if (s == c.str()) {
                cout << "Socket" << i << " Frequency\t0\t\tMHz\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/IPC?";
            if (s == c.str()) {
                cout << "Socket" << i << " IPC\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/L2CacheHitRatio?";
            if (s == c.str()) {
                cout << "Socket" << i << " L2 Cache Hit Ratio\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/L3CacheHitRatio?";
            if (s == c.str()) {
                cout << "Socket" << i << " L3 Cache Hit Ratio\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/L2CacheMisses?";
            if (s == c.str()) {
                cout << "Socket" << i << " L2 Cache Misses\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/L3CacheMisses?";
            if (s == c.str()) {
                cout << "Socket" << i << " L3 Cache Misses\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/L3Occupancy";
            if (s == c.str()) {
                cout << "Socket" << i << " L3 Cache Occupancy\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/LocalMemoryBandwidth";
            if (s == c.str()) {
                cout << "Socket" << i << " Local Memory Bandwidth\t0\t\t\n";
            }
        }

        for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
            stringstream c;
            c << "Socket" << i << "/RemoteMemoryBandwidth";
            if (s == c.str()) {
                cout << "Socket" << i << " Remote Memory Bandwidth\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "Frequency?";
            if (s == c.str()) {
                cout << "Frequency system wide\t0\t\tMhz\n";
            }
        }

        {
            stringstream c;
            c << "IPC?";
            if (s == c.str()) {
                cout << "IPC system wide\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "L2CacheHitRatio?";
            if (s == c.str()) {
                cout << "System wide L2 Cache Hit Ratio\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "L3CacheHitRatio?";
            if (s == c.str()) {
                cout << "System wide L3 Cache Hit Ratio\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "L2CacheMisses?";
            if (s == c.str()) {
                cout << "System wide L2 Cache Misses\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "L3CacheMisses?";
            if (s == c.str()) {
                cout << "System wide L3 Cache Misses\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "L3CacheMisses?";
            if (s == c.str()) {
                cout << "System wide L3 Cache Misses\t0\t\t\n";
            }
        }

        {
            stringstream c;
            c << "DRAMEnergy?";
            if (s == c.str()) {
                cout << "System wide energy consumed by DRAM \t0\t\tJoule\n";
            }
        }
        {
            stringstream c;
            c << "CPUEnergy?";
            if (s == c.str()) {
                cout << "System wide energy consumed by CPU packages \t0\t\tJoule\n";
            }
        }
        {
            stringstream c;
            c << "CoreC0StateResidency?";
            if (s == c.str()) {
                cout << "System wide core C0-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "CoreC3StateResidency?";
            if (s == c.str()) {
                cout << "System wide core C3-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "CoreC6StateResidency?";
            if (s == c.str()) {
                cout << "System wide core C6-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "CoreC7StateResidency?";
            if (s == c.str()) {
                cout << "System wide core C7-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "PackageC2StateResidency?";
            if (s == c.str()) {
                cout << "System wide package C2-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "PackageC3StateResidency?";
            if (s == c.str()) {
                cout << "System wide package C3-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "PackageC6StateResidency?";
            if (s == c.str()) {
                cout << "System wide package C6-state residency \t0\t\t%\n";
            }
        }
        {
            stringstream c;
            c << "PackageC7StateResidency?";
            if (s == c.str()) {
                cout << "System wide package C7-state residency \t0\t\t%\n";
            }
        }

        // sensors

#define OUTPUT_CORE_METRIC(name, function) \
    for (uint32 i = 0; i<counters.getNumCores(); ++i) { \
                             for (uint32 a = 0; a<counters.getNumSockets(); ++a) \
                                                  if (a == counters.getSocketId(i)) { \
                                                      stringstream c; \
                                                      c << "Socket" << a << "/CPU" << i << name; \
                                                      if (s == c.str()) { \
                                                          cout << function << "\n"; \
                                                      } \
                                                  } \
                                                  }

        OUTPUT_CORE_METRIC("/Frequency", (counters.get<double, ::getAverageFrequency>(i) / 1000000))
        OUTPUT_CORE_METRIC("/IPC", (counters.get<double, ::getIPC>(i)))
        OUTPUT_CORE_METRIC("/L2CacheHitRatio", (counters.get<double, ::getL2CacheHitRatio>(i)))
        OUTPUT_CORE_METRIC("/L3CacheHitRatio", (counters.get<double, ::getL3CacheHitRatio>(i)))
        OUTPUT_CORE_METRIC("/L2CacheMisses", (counters.get<uint64, ::getL2CacheMisses>(i)))
        OUTPUT_CORE_METRIC("/L3CacheMisses", (counters.get<uint64, ::getL3CacheMisses>(i)))
        OUTPUT_CORE_METRIC("/L3Occupancy", (counters.get<uint64, ::getL3CacheOccupancy>(i)))
        OUTPUT_CORE_METRIC("/LocalMemoryBandwidth", (counters.get<uint64, ::getLocalMemoryBW>(i)))
        OUTPUT_CORE_METRIC("/RemoteMemoryBandwidth", (counters.get<uint64, ::getRemoteMemoryBW>(i)))
        OUTPUT_CORE_METRIC("/CoreC0StateResidency", (counters.get<double, ::getCoreCStateResidency>(0, i) * 100.))
        OUTPUT_CORE_METRIC("/CoreC3StateResidency", (counters.get<double, ::getCoreCStateResidency>(3, i) * 100.))
        OUTPUT_CORE_METRIC("/CoreC6StateResidency", (counters.get<double, ::getCoreCStateResidency>(6, i) * 100.))
        OUTPUT_CORE_METRIC("/CoreC7StateResidency", (counters.get<double, ::getCoreCStateResidency>(7, i) * 100.))
        OUTPUT_CORE_METRIC("/ThermalHeadroom", (counters.get<int32, ::getThermalHeadroom>(i)))

        #define OUTPUT_SOCKET_METRIC(name, function) \
    for (uint32 i = 0; i<counters.getNumSockets(); ++i) { \
                             stringstream c; \
                             c << "Socket" << i << name; \
                             if (s == c.str()) { \
                                 cout << function << "\n"; \
                             } \
                         }

        OUTPUT_SOCKET_METRIC("/DRAMEnergy", (counters.getSocket<double, ::getDRAMConsumedJoules>(i)))
        OUTPUT_SOCKET_METRIC("/CPUEnergy", (counters.getSocket<double, ::getConsumedJoules>(i)))
        OUTPUT_SOCKET_METRIC("/CoreC0StateResidency", (counters.getSocket<double, ::getCoreCStateResidency>(0, i) * 100.))
        OUTPUT_SOCKET_METRIC("/CoreC3StateResidency", (counters.getSocket<double, ::getCoreCStateResidency>(3, i) * 100.))
        OUTPUT_SOCKET_METRIC("/CoreC6StateResidency", (counters.getSocket<double, ::getCoreCStateResidency>(6, i) * 100.))
        OUTPUT_SOCKET_METRIC("/CoreC7StateResidency", (counters.getSocket<double, ::getCoreCStateResidency>(7, i) * 100.))
        OUTPUT_SOCKET_METRIC("/PackageC2StateResidency", (counters.getSocket<double, ::getPackageCStateResidency>(2, i) * 100.))
        OUTPUT_SOCKET_METRIC("/PackageC3StateResidency", (counters.getSocket<double, ::getPackageCStateResidency>(3, i) * 100.))
        OUTPUT_SOCKET_METRIC("/PackageC6StateResidency", (counters.getSocket<double, ::getPackageCStateResidency>(6, i) * 100.))
        OUTPUT_SOCKET_METRIC("/PackageC7StateResidency", (counters.getSocket<double, ::getPackageCStateResidency>(7, i) * 100.))
        OUTPUT_SOCKET_METRIC("/ThermalHeadroom", (counters.getSocket<int32, ::getThermalHeadroom>(i)))
        OUTPUT_SOCKET_METRIC("/BytesReadFromMC", (double(counters.getSocket<uint64, ::getBytesReadFromMC>(i)) / 1024 / 1024 / 1024))
        OUTPUT_SOCKET_METRIC("/BytesWrittenToMC", (double(counters.getSocket<uint64, ::getBytesWrittenToMC>(i)) / 1024 / 1024 / 1024))
        OUTPUT_SOCKET_METRIC("/BytesReadFromPMM", (double(counters.getSocket<uint64, ::getBytesReadFromPMM>(i)) / 1024 / 1024 / 1024))
        OUTPUT_SOCKET_METRIC("/BytesWrittenToPMM", (double(counters.getSocket<uint64, ::getBytesWrittenToPMM>(i)) / 1024 / 1024 / 1024))
        OUTPUT_SOCKET_METRIC("/Frequency", (counters.getSocket<double, ::getAverageFrequency>(i) / 1000000))
        OUTPUT_SOCKET_METRIC("/IPC", (counters.getSocket<double, ::getIPC>(i)))
        OUTPUT_SOCKET_METRIC("/L2CacheHitRatio", (counters.getSocket<double, ::getL2CacheHitRatio>(i)))
        OUTPUT_SOCKET_METRIC("/L3CacheHitRatio", (counters.getSocket<double, ::getL3CacheHitRatio>(i)))
        OUTPUT_SOCKET_METRIC("/L2CacheMisses", (counters.getSocket<uint64, ::getL2CacheMisses>(i)))
        OUTPUT_SOCKET_METRIC("/L3CacheMisses", (counters.getSocket<uint64, ::getL3CacheMisses>(i)))
        OUTPUT_SOCKET_METRIC("/L3Occupancy", (counters.getSocket<uint64, ::getL3CacheOccupancy>(i)))
        OUTPUT_SOCKET_METRIC("/LocalMemoryBandwidth", (counters.getSocket<uint64, ::getLocalMemoryBW>(i)))
        OUTPUT_SOCKET_METRIC("/RemoteMemoryBandwidth", (counters.getSocket<uint64, ::getRemoteMemoryBW>(i)))

        for (uint32 l = 0; l < counters.getQPILinksPerSocket(); ++l) {
            for (uint32 i = 0; i < counters.getNumSockets(); ++i) {
                stringstream c;
                c << "Socket" << i << "/BytesIncomingTo" << xpi << l;
                if (s == c.str()) {
                    cout << double(counters.getSocket<uint64, ::getIncomingQPILinkBytes>(i, l)) / 1024 / 1024 / 1024 << "\n";
                }
            }
        }

    #define OUTPUT_SYSTEM_METRIC(name, function) \
    { \
        stringstream c; \
        c << name; \
        if (s == c.str()) { \
            cout << function << "\n"; \
        } \
    }

        OUTPUT_SYSTEM_METRIC("DRAMEnergy", (counters.getSystem<double, ::getDRAMConsumedJoules>()))
        OUTPUT_SYSTEM_METRIC("CPUEnergy", (counters.getSystem<double, ::getConsumedJoules>()))
        OUTPUT_SYSTEM_METRIC("CoreC0StateResidency", (counters.getSystem<double, ::getCoreCStateResidency>(0) * 100.))
        OUTPUT_SYSTEM_METRIC("CoreC3StateResidency", (counters.getSystem<double, ::getCoreCStateResidency>(3) * 100.))
        OUTPUT_SYSTEM_METRIC("CoreC6StateResidency", (counters.getSystem<double, ::getCoreCStateResidency>(6) * 100.))
        OUTPUT_SYSTEM_METRIC("CoreC7StateResidency", (counters.getSystem<double, ::getCoreCStateResidency>(7) * 100.))
        OUTPUT_SYSTEM_METRIC("PackageC2StateResidency", (counters.getSystem<double, ::getPackageCStateResidency>(2) * 100.))
        OUTPUT_SYSTEM_METRIC("PackageC3StateResidency", (counters.getSystem<double, ::getPackageCStateResidency>(3) * 100.))
        OUTPUT_SYSTEM_METRIC("PackageC6StateResidency", (counters.getSystem<double, ::getPackageCStateResidency>(6) * 100.))
        OUTPUT_SYSTEM_METRIC("PackageC7StateResidency", (counters.getSystem<double, ::getPackageCStateResidency>(7) * 100.))
        OUTPUT_SYSTEM_METRIC("Frequency", (double(counters.getSystem<double, ::getAverageFrequency>()) / 1000000))
        OUTPUT_SYSTEM_METRIC("IPC", (double(counters.getSystem<double, ::getIPC>())))
        OUTPUT_SYSTEM_METRIC("L2CacheHitRatio", (double(counters.getSystem<double, ::getL2CacheHitRatio>())))
        OUTPUT_SYSTEM_METRIC("L3CacheHitRatio", (double(counters.getSystem<double, ::getL3CacheHitRatio>())))
        OUTPUT_SYSTEM_METRIC("L2CacheMisses", (double(counters.getSystem<uint64, ::getL2CacheMisses>())))
        OUTPUT_SYSTEM_METRIC("L3CacheMisses", (double(counters.getSystem<uint64, ::getL3CacheMisses>())))
        OUTPUT_SYSTEM_METRIC(std::string(xpi + std::string("_Traffic")),
            (double(counters.getSystem<uint64, ::getAllIncomingQPILinkBytes>()) / 1024 / 1024 / 1024))

        // exit
        if (s == "quit" || s == "exit" || s == "") {
            break;
        }


        cout << "ksysguardd> ";
    }

    return 0;
}
