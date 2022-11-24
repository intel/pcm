// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
/*
** Written by Otto Bruggeman, Roman Dementiev
*/

#pragma once

#pragma unmanaged
#include "pcm-lib.h"
#include "windriver.h"
#include <stdexcept>

#ifndef UNICODE
#include <locale>
#include <codecvt>
#endif
#pragma managed

using namespace pcm;
using namespace System;
using namespace System::Collections;
using namespace System::ServiceProcess;
using namespace System::ComponentModel;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Runtime::InteropServices;

namespace PCMServiceNS {
    ref struct Globals
    {
        static initonly String^ ServiceName = gcnew String(L"PCMService");
    };

    ref struct CollectionInformation {
        CollectionInformation()
        {
            core = true;
            socket = true;
            qpi = true;
        }

        CollectionInformation(const CollectionInformation^ &copyable)
        {
            core = copyable->core;
            socket = copyable->socket;
            qpi = copyable->qpi;
        }

        bool core;
        bool socket;
        bool qpi;
    };

    ref class MeasureThread
    {
    public:
        MeasureThread(System::Diagnostics::EventLog^ log, int sampleRate, CollectionInformation^ collectionInformation) : log_(log), sampleRate_(sampleRate), collectionInformation_(collectionInformation)
        {
            // Get a Monitor instance which sets up the PMU, it also figures out the number of cores and sockets which we need later on to create the performance counters
            m_ = PCM::getInstance();
            if ( !m_->good() )
            {
                log_->WriteEntry(Globals::ServiceName, "Monitor Instance could not be created.", EventLogEntryType::Error);
                m_->cleanup();
                String^ s = gcnew String(m_->getErrorMessage().c_str());
                throw gcnew Exception(s);
            }
            log_->WriteEntry(Globals::ServiceName, "PCM: Number of cores detected: " + UInt32(m_->getNumCores()).ToString());

            m_->program();

            log_->WriteEntry(Globals::ServiceName, "PMU Programmed.");

            CountersQpi = gcnew String(L"PCM " + gcnew System::String(m_->xPI()) + L" Counters");
            MetricQpiBand = gcnew String(gcnew System::String(m_->xPI()) + L" Link Bandwidth");

            // This here will only create the necessary registry entries, the actual counters are created later.
            // New unified category
            if (PerformanceCounterCategory::Exists(CountersCore))
            {
                PerformanceCounterCategory::Delete(CountersCore);
            }
            if (PerformanceCounterCategory::Exists(CountersSocket))
            {
                PerformanceCounterCategory::Delete(CountersSocket);
            }
            if (PerformanceCounterCategory::Exists(CountersQpi))
            {
                PerformanceCounterCategory::Delete(CountersQpi);
            }
            log_->WriteEntry(Globals::ServiceName, "Old categories deleted.");

            // First create the collection, then add counters to it so we add them all at once
            CounterCreationDataCollection^ counterCollection = gcnew CounterCreationDataCollection;

            // Here we add the counters one by one, need list of counters currently collected.
            // This is a stub: copy and paste when new counters are added to ipcustat "library".
            // CounterCreationData^ counter = gcnew CounterCreationData( "counter", "helptext for counter", PerformanceCounterType::NumberOfItems64 );
            // counterCollection->Add( counter );
            CounterCreationData^ counter;

            if (collectionInformation_->core)
            {
                counter = gcnew CounterCreationData(MetricCoreClocktick, "Displays the number of clockticks elapsed since previous measurement.", PerformanceCounterType::CounterDelta64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreRetired, "Displays the number of instructions retired since previous measurement.", PerformanceCounterType::CounterDelta64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreMissL2, "Displays the L2 Cache Misses caused by this core.", PerformanceCounterType::CounterDelta64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreMissL3, "Displays the L3 Cache Misses caused by this core.", PerformanceCounterType::CounterDelta64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreIpc, "Displays the instructions per clocktick executed for this core.", PerformanceCounterType::AverageCount64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreBaseIpc, "Not visible", PerformanceCounterType::AverageBase);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreFreqRel, "Displays the current frequency of the core to its rated frequency in percent.", PerformanceCounterType::SampleFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreFreqNom, "Not visible", PerformanceCounterType::SampleBase);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreHeadroom, "Displays temperature reading in 1 degree Celsius relative to the TjMax temperature. 0 corresponds to the max temperature.", PerformanceCounterType::NumberOfItems64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC0, "Displays the residency of core or socket in core C0-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC0Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC1, "Displays the residency of core or socket in core C1-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC1Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricCoreResC3, "Displays the residency of core or socket in core C3-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC3Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricCoreResC6, "Displays the residency of core or socket in core C6-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC6Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricCoreResC7, "Displays the residency of core or socket in core C7-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricCoreResC7Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                PerformanceCounterCategory::Create(CountersCore, "Intel(r) Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection);
            }

            if (collectionInformation_->socket)
            {
                counterCollection->Clear();
                counter = gcnew CounterCreationData(MetricSocketBandRead, "Displays the memory read bandwidth in bytes/s of this socket.", PerformanceCounterType::NumberOfItems64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketBandWrite, "Displays the memory write bandwidth in bytes/s of this socket.", PerformanceCounterType::NumberOfItems64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketEnergyPack, "Displays the energy in Joules consumed by this socket.", PerformanceCounterType::NumberOfItems64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketEnergyDram, "Displays the energy in Joules consumed by DRAM memory attached to the memory controller of this socket.", PerformanceCounterType::NumberOfItems64);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC0, "Displays the residency of socket in package C0-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC0Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC2, "Displays the residency of socket in package C2-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC2Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC3, "Displays the residency of socket in package C3-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC3Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC6, "Displays the residency of socket in package C6-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC6Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC7, "Displays the residency of socket in package C7-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC7Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC8, "Displays the residency of socket in package C8-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC8Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC9, "Displays the residency of socket in package C9-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC9Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                counter = gcnew CounterCreationData(MetricSocketResC10, "Displays the residency of socket in package C10-state in percent.", PerformanceCounterType::RawFraction);
                counterCollection->Add( counter );
                counter = gcnew CounterCreationData(MetricSocketResC10Base, "", PerformanceCounterType::RawBase);
                counterCollection->Add(counter);
                PerformanceCounterCategory::Create(CountersSocket, "Intel(r) Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection);
            }

            if (collectionInformation_->qpi)
            {
                counterCollection->Clear();
                counter = gcnew CounterCreationData(MetricQpiBand, L"Displays the incoming bandwidth in bytes/s of this " + gcnew System::String(m_->xPI()) + L" link", PerformanceCounterType::CounterDelta64);
                counterCollection->Add( counter );
                PerformanceCounterCategory::Create(CountersQpi, "Intel(r) Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection);
            }

            log_->WriteEntry(Globals::ServiceName, "New categories added.");

            // Registry entries created, now we need to create the programmatic counters. For some things you may want one instance for every core/thread/socket/qpilink so create in a loop.
            // PerformanceCounter^ pc1 = gcnew PerformanceCounter( "SomeCounterName", "nameOfCounterAsEnteredInTheRegistry", "instanceNameOfCounterAsANumber" );

            // Create #processors instances of the core specific performance counters
            String^ s; // Used for creating the instance name and the string to search for in the hashtable
            PerformanceCounter^ pc;
            for ( unsigned int i = 0; i < m_->getNumCores(); ++i )
            {
                s = UInt32(i).ToString(); // For core counters we use just the number of the core
                if (collectionInformation_->core)
                {
                    ticksHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreClocktick, s, false));
                    instRetHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreRetired, s, false));
                    l2CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL2, s, false));
                    l3CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL3, s, false));
                    ipcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreIpc, s, false));
                    baseTicksForIpcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreBaseIpc, s, false));
                    relFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqRel, s, false));
                    baseTicksForRelFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqNom, s, false));
                    thermalHeadroomHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreHeadroom, s, false));
                    CoreC0StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC0, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC0Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC1StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC1, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC1Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC3StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC3, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC3Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC6StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC6, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC6Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC7StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC7, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC7Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                }
            }

            // Create socket instances of the common core counters, names are Socket+number
            for ( unsigned int i=0; i<m_->getNumSockets(); ++i )
            {
                s = "Socket"+UInt32(i).ToString();
                if (collectionInformation_->core)
                {
                    ticksHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreClocktick, s, false));
                    instRetHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreRetired, s, false));
                    l2CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL2, s, false));
                    l3CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL3, s, false));
                    ipcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreIpc, s, false));
                    baseTicksForIpcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreBaseIpc, s, false));
                    relFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqRel, s, false));
                    baseTicksForRelFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqNom, s, false));
                    thermalHeadroomHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreHeadroom, s, false));
                    CoreC0StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC0, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC0Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC1StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC1, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC1Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC3StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC3, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC3Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC6StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC6, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC6Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    CoreC7StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC7, s, false));
                    pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC7Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                }

                if (collectionInformation_->socket)
                {
                    mrbHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketBandRead, s, false));
                    mwbHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketBandWrite, s, false));
                    packageEnergyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketEnergyPack, s, false));
                    DRAMEnergyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketEnergyDram, s, false));
                    PackageC0StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC0, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC0Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC2StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC2, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC2Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC3StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC3, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC3Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC6StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC6, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC6Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC7StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC7, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC7Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC8StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC8, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC8Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC9StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC9, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC9Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                    PackageC10StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC10, s, false));
                    pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC10Base, s, false);
                    pc->RawValue = 1000;
                    baseArrayList_.Add(pc);
                }

                if (collectionInformation_->qpi)
                {
                    qpiHash_.Add(s, gcnew PerformanceCounter(CountersQpi, MetricQpiBand, s, false)); // Socket aggregate
                    String^ t;
                    for ( unsigned int j=0; j<m_->getQPILinksPerSocket(); ++j )
                    {
                        t = s + "_Link" + UInt32(j).ToString();
                        qpiHash_.Add(t, gcnew PerformanceCounter(CountersQpi, MetricQpiBand, t, false));
                    }
                }
            }

            // Create #system instances of the system specific performance counters, just kidding, there is only one system so 1 instance
            s = "Total_";
            if (collectionInformation_->core)
            {
                ticksHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreClocktick, s, false));
                instRetHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreRetired, s, false));
                l2CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL2, s, false));
                l3CacheMissHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreMissL3, s, false));
                ipcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreIpc, s, false));
                baseTicksForIpcHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreBaseIpc, s, false));
                relFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqRel, s, false));
                baseTicksForRelFreqHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreFreqNom, s, false));
                thermalHeadroomHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreHeadroom, s, false));
                CoreC0StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC0, s, false));
                pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC0Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                CoreC1StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC1, s, false));
                pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC1Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                CoreC3StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC3, s, false));
                pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC3Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                CoreC6StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC6, s, false));
                pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC6Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                CoreC7StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersCore, MetricCoreResC7, s, false));
                pc = gcnew PerformanceCounter(CountersCore, MetricCoreResC7Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
            }

            if (collectionInformation_->socket)
            {
                mrbHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketBandRead, s, false));
                mwbHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketBandWrite, s, false));
                packageEnergyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketEnergyPack, s, false));
                DRAMEnergyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketEnergyDram, s, false));
                PackageC0StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC0, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC0Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC2StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC2, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC2Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC3StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC3, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC3Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC6StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC6, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC6Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC7StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC7, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC7Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC8StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC8, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC8Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC9StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC9, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC9Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
                PackageC10StateResidencyHash_.Add(s, gcnew PerformanceCounter(CountersSocket, MetricSocketResC10, s, false));
                pc = gcnew PerformanceCounter(CountersSocket, MetricSocketResC10Base, s, false);
                pc->RawValue = 1000;
                baseArrayList_.Add(pc);
            }

            if (collectionInformation_->qpi)
            {
                qpiHash_.Add(s, gcnew PerformanceCounter(CountersQpi, MetricQpiBand, s, false));
            }

            log_->WriteEntry(Globals::ServiceName, "All instances of the performance counter categories have been created.");
        }

        void doMeasurements( void )
        {
            // FIXME: Do we care about hot swappability of CPUs?
            const size_t numSockets  = m_->getNumSockets();
            const size_t numCores    = m_->getNumCores();
            const size_t numQpiLinks = (size_t) m_->getQPILinksPerSocket();

            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

            // The structures
            SystemCounterState  oldSystemState;
            std::vector<SocketCounterState> oldSocketStates;
            std::vector<CoreCounterState>   oldCoreStates;
            SystemCounterState  systemState;
            std::vector<SocketCounterState> socketStates;
            std::vector<CoreCounterState>   coreStates;
            ULONGLONG BeforeTime = 0, AfterTime = 0;

            m_->getAllCounterStates(oldSystemState, oldSocketStates, oldCoreStates);
            BeforeTime = GetTickCount64();

 // labmda functions are not allowed in managed code, using a macro
#define  toBW(val) (val * 1000ULL / (AfterTime - BeforeTime))

            try {
                while (true)
                {
                    Thread::Sleep(sampleRate_);

                    // Fetch counter data here and store in the PerformanceCounter instances
                    m_->getAllCounterStates(systemState, socketStates, coreStates);
                    AfterTime = GetTickCount64();

                    // Set system performance counters
                    String^ s = "Total_";
                    if (collectionInformation_->core)
                    {
                        __int64 totalTicks    = getCycles(systemState);
                        __int64 totalRefTicks = m_->getNominalFrequency() * numCores;
                        __int64 totalInstr    = getInstructionsRetired(systemState);
                        ((PerformanceCounter^)ticksHash_[s])->RawValue = totalTicks;
                        ((PerformanceCounter^)instRetHash_[s])->RawValue = totalInstr;
                        ((PerformanceCounter^)l2CacheMissHash_[s])->IncrementBy(getL2CacheMisses(oldSystemState, systemState));
                        ((PerformanceCounter^)l3CacheMissHash_[s])->IncrementBy(getL3CacheMisses(oldSystemState, systemState));
                        ((PerformanceCounter^)ipcHash_[s])->RawValue = totalInstr >> 17;
                        ((PerformanceCounter^)baseTicksForIpcHash_[s])->RawValue = totalTicks >> 17;
                        ((PerformanceCounter^)relFreqHash_[s])->RawValue = totalTicks >> 17;
                        ((PerformanceCounter^)baseTicksForRelFreqHash_[s])->IncrementBy(totalRefTicks >> 17);
                        ((PerformanceCounter^)thermalHeadroomHash_[s])->RawValue = systemState.getThermalHeadroom();
                        ((PerformanceCounter^)CoreC0StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(0,oldSystemState, systemState));
                        ((PerformanceCounter^)CoreC1StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(1,oldSystemState, systemState));
                        ((PerformanceCounter^)CoreC3StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(3,oldSystemState, systemState));
                        ((PerformanceCounter^)CoreC6StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(6,oldSystemState, systemState));
                        ((PerformanceCounter^)CoreC7StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(7,oldSystemState, systemState));
                        //log_->WriteEntry(Globals::ServiceName, "Std: " + UInt64(totalTicks).ToString());
                        //log_->WriteEntry(Globals::ServiceName, "Ref: " + UInt64(totalRefTicks).ToString());
                    }

                    if (collectionInformation_->socket)
                    {
                        ((PerformanceCounter^)mrbHash_[s])->RawValue = toBW(getBytesReadFromMC(oldSystemState, systemState));
                        ((PerformanceCounter^)mwbHash_[s])->RawValue = toBW(getBytesWrittenToMC(oldSystemState, systemState));
                        ((PerformanceCounter^)packageEnergyHash_[s])->RawValue = (__int64)getConsumedJoules(oldSystemState, systemState);
                        ((PerformanceCounter^)DRAMEnergyHash_[s])->RawValue = (__int64)getDRAMConsumedJoules(oldSystemState, systemState);
                        ((PerformanceCounter^)PackageC0StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 0, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC2StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 2, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC3StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 3, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC6StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 6, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC7StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 7, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC8StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 8, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC9StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 9, oldSystemState, systemState));
                        ((PerformanceCounter^)PackageC10StateResidencyHash_[s])->RawValue = __int64(1000. * getPackageCStateResidency(10, oldSystemState, systemState));
                    }

                    if (collectionInformation_->qpi)
                    {
                        ((PerformanceCounter^)qpiHash_[s])->RawValue = getAllIncomingQPILinkBytes(systemState);
                    }

                    // Set socket performance counters
                    for ( unsigned int i = 0; i < numSockets; ++i )
                    {
                        s = "Socket"+UInt32(i).ToString();
                        const SocketCounterState & socketState = socketStates[i];
                        if (collectionInformation_->core)
                        {
                            __int64 socketTicks    = getCycles(socketState);
                            __int64 socketRefTicks = m_->getNominalFrequency()* numCores / numSockets;
                            __int64 socketInstr    = getInstructionsRetired(socketState);
                            ((PerformanceCounter^)instRetHash_[s])->RawValue = socketInstr;
                            ((PerformanceCounter^)ipcHash_[s])->RawValue = socketInstr >> 17;
                            ((PerformanceCounter^)l2CacheMissHash_[s])->IncrementBy(getL2CacheMisses(oldSocketStates[i], socketState));
                            ((PerformanceCounter^)l3CacheMissHash_[s])->IncrementBy(getL3CacheMisses(oldSocketStates[i], socketState));
                            ((PerformanceCounter^)ticksHash_[s])->RawValue = socketTicks;
                            ((PerformanceCounter^)baseTicksForIpcHash_[s])->RawValue = socketTicks >> 17;
                            ((PerformanceCounter^)relFreqHash_[s])->RawValue = socketTicks >> 17;
                            ((PerformanceCounter^)baseTicksForRelFreqHash_[s])->IncrementBy(socketRefTicks >> 17);
                            ((PerformanceCounter^)thermalHeadroomHash_[s])->RawValue = socketState.getThermalHeadroom();
                            ((PerformanceCounter^)CoreC0StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(0, oldSocketStates[i], socketState));
                            ((PerformanceCounter^)CoreC1StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(1, oldSocketStates[i], socketState));
                            ((PerformanceCounter^)CoreC3StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(3, oldSocketStates[i], socketState));
                            ((PerformanceCounter^)CoreC6StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(6, oldSocketStates[i], socketState));
                            ((PerformanceCounter^)CoreC7StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(7, oldSocketStates[i], socketState));
                        }

                        if (collectionInformation_->socket)
                        {
                            ((PerformanceCounter^)mrbHash_[s])->RawValue = toBW(getBytesReadFromMC(oldSocketStates[i], socketState));
                            ((PerformanceCounter^)mwbHash_[s])->RawValue = toBW(getBytesWrittenToMC(oldSocketStates[i], socketState));
                            ((PerformanceCounter^)packageEnergyHash_[s])->RawValue = (__int64)getConsumedJoules(oldSocketStates[i], socketState);
                            ((PerformanceCounter^)DRAMEnergyHash_[s])->RawValue = (__int64)getDRAMConsumedJoules(oldSocketStates[i], socketState);
                            ((PerformanceCounter^)PackageC0StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 0,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC2StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 2,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC3StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 3,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC6StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 6,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC7StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 7,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC8StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 8,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC9StateResidencyHash_[s])->RawValue  = __int64(1000. * getPackageCStateResidency( 9,oldSocketStates[i], socketState));
                            ((PerformanceCounter^)PackageC10StateResidencyHash_[s])->RawValue = __int64(1000. * getPackageCStateResidency(10,oldSocketStates[i], socketState));
                        }

                        if (collectionInformation_->qpi)
                        {
                            ((PerformanceCounter^)qpiHash_[s])->RawValue = toBW(getSocketIncomingQPILinkBytes(i, systemState));
                            String^ t;
                            // and qpi link counters per socket
                            for ( unsigned int j=0; j<numQpiLinks; ++j )
                            {
                                t = s + "_Link" + UInt32(j).ToString();
                                ((PerformanceCounter^)qpiHash_[t])->RawValue = toBW(getIncomingQPILinkBytes(i, j, systemState));
                            }
                        }
                    }

                    // Set core performance counters
                    for ( unsigned int i = 0; i < numCores; ++i )
                    {
                        s = UInt32(i).ToString();
                        const CoreCounterState & coreState = coreStates[i];
                        if (collectionInformation_->core)
                        {
                            __int64 ticks    = getCycles(coreState);
                            __int64 refTicks = m_->getNominalFrequency();
                            __int64 instr    = getInstructionsRetired(coreState);
                            ((PerformanceCounter^)instRetHash_[s])->RawValue = instr;
                            ((PerformanceCounter^)ipcHash_[s])->RawValue = instr >> 17;
                            ((PerformanceCounter^)l2CacheMissHash_[s])->IncrementBy(getL2CacheMisses(oldCoreStates[i], coreState));
                            ((PerformanceCounter^)l3CacheMissHash_[s])->IncrementBy(getL3CacheMisses(oldCoreStates[i], coreState));
                            ((PerformanceCounter^)ticksHash_[s])->RawValue = ticks;
                            ((PerformanceCounter^)baseTicksForIpcHash_[s])->RawValue = ticks >> 17;
                            ((PerformanceCounter^)relFreqHash_[s])->RawValue = ticks >> 17;
                            ((PerformanceCounter^)baseTicksForRelFreqHash_[s])->IncrementBy(refTicks >> 17);
                            ((PerformanceCounter^)thermalHeadroomHash_[s])->RawValue = coreState.getThermalHeadroom();
                            ((PerformanceCounter^)CoreC0StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(0,oldCoreStates[i], coreState));
                            ((PerformanceCounter^)CoreC1StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(1,oldCoreStates[i], coreState));
                            ((PerformanceCounter^)CoreC3StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(3,oldCoreStates[i], coreState));
                            ((PerformanceCounter^)CoreC6StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(6,oldCoreStates[i], coreState));
                            ((PerformanceCounter^)CoreC7StateResidencyHash_[s])->RawValue = __int64(1000. * getCoreCStateResidency(7,oldCoreStates[i], coreState));
                        }
                    }

                    std::swap(oldSystemState, systemState);
                    std::swap(oldSocketStates, socketStates);
                    std::swap(oldCoreStates, coreStates);
                    std::swap(BeforeTime, AfterTime);
                }
            }
            catch( ThreadAbortException^ )
            {
                // We get here when for instance the service gets stopped or something bad happens.
                // In order to do our cleanup, like unprogram the MSRs, close the driver and such, in case we get stopped we want to execute normally after this
                // so this resets the abort and allows a normal exit
                Thread::ResetAbort();
            }
            // Here we now have the chance to do cleanup after catching the ThreadAbortException because of the ResetAbort
            m_->cleanup();
        }

    private:
        // Core counter hashtables
        System::Collections::Hashtable ticksHash_;
        System::Collections::Hashtable instRetHash_;
        System::Collections::Hashtable ipcHash_;
        System::Collections::Hashtable baseTicksForIpcHash_;
        System::Collections::Hashtable relFreqHash_;
        System::Collections::Hashtable baseTicksForRelFreqHash_;
        System::Collections::Hashtable l3CacheMissHash_;
        System::Collections::Hashtable l2CacheMissHash_;
        // Socket counter hashtables
        System::Collections::Hashtable mrbHash_;
        System::Collections::Hashtable mwbHash_;
        // QPI counter hashtables
        System::Collections::Hashtable qpiHash_;
        // Energy counters
        System::Collections::Hashtable packageEnergyHash_;
        System::Collections::Hashtable DRAMEnergyHash_;
        // Thermal headroom
        System::Collections::Hashtable thermalHeadroomHash_;
        // C-state Residencies
        System::Collections::Hashtable CoreC0StateResidencyHash_;		
        System::Collections::Hashtable CoreC1StateResidencyHash_;
        System::Collections::Hashtable CoreC3StateResidencyHash_;
        System::Collections::Hashtable CoreC6StateResidencyHash_;
        System::Collections::Hashtable CoreC7StateResidencyHash_;
        System::Collections::Hashtable PackageC0StateResidencyHash_;
        System::Collections::Hashtable PackageC2StateResidencyHash_;
        System::Collections::Hashtable PackageC3StateResidencyHash_;
        System::Collections::Hashtable PackageC6StateResidencyHash_;
        System::Collections::Hashtable PackageC7StateResidencyHash_;
        System::Collections::Hashtable PackageC8StateResidencyHash_;
        System::Collections::Hashtable PackageC9StateResidencyHash_;
        System::Collections::Hashtable PackageC10StateResidencyHash_;
        System::Collections::ArrayList baseArrayList_;

        System::Diagnostics::EventLog^ log_;

        PCM* m_;

        // Counter variable names
        initonly String^ CountersCore = gcnew String(L"PCM Core Counters");
        initonly String^ CountersSocket = gcnew String(L"PCM Socket Counters");
        initonly String^ CountersQpi;

        initonly String^ MetricCoreClocktick = gcnew String(L"Clockticks");
        initonly String^ MetricCoreRetired = gcnew String(L"Instructions Retired");
        initonly String^ MetricCoreMissL2 = gcnew String(L"L2 Cache Misses");
        initonly String^ MetricCoreMissL3 = gcnew String(L"L3 Cache Misses");
        initonly String^ MetricCoreIpc = gcnew String(L"Instructions Per Clocktick (IPC)");
        initonly String^ MetricCoreBaseIpc = gcnew String(L"Base ticks IPC");
        initonly String^ MetricCoreFreqRel = gcnew String(L"Relative Frequency (%)");
        initonly String^ MetricCoreFreqNom = gcnew String(L"Nominal Frequency");
        initonly String^ MetricCoreHeadroom = gcnew String(L"Thermal Headroom below TjMax");
        initonly String^ MetricCoreResC0 = gcnew String(L"core C0-state residency (%)");
        initonly String^ MetricCoreResC1 = gcnew String(L"core C1-state residency (%)");
        initonly String^ MetricCoreResC3 = gcnew String(L"core C3-state residency (%)");
        initonly String^ MetricCoreResC6 = gcnew String(L"core C6-state residency (%)");
        initonly String^ MetricCoreResC7 = gcnew String(L"core C7-state residency (%)");
        initonly String^ MetricCoreResC0Base = gcnew String(L"core C0-state base");
        initonly String^ MetricCoreResC1Base = gcnew String(L"core C1-state base");
        initonly String^ MetricCoreResC3Base = gcnew String(L"core C3-state base");
        initonly String^ MetricCoreResC6Base = gcnew String(L"core C6-state base");
        initonly String^ MetricCoreResC7Base = gcnew String(L"core C7-state base");

        initonly String^ MetricSocketBandRead  = gcnew String(L"Memory Read Bandwidth");
        initonly String^ MetricSocketBandWrite = gcnew String(L"Memory Write Bandwidth");
        initonly String^ MetricSocketEnergyPack = gcnew String(L"Package/Socket Consumed Energy");
        initonly String^ MetricSocketEnergyDram = gcnew String(L"DRAM/Memory Consumed Energy");
        initonly String^ MetricSocketResC0  = gcnew String(L"package C0-state residency (%)");
        initonly String^ MetricSocketResC2  = gcnew String(L"package C2-state residency (%)");
        initonly String^ MetricSocketResC3  = gcnew String(L"package C3-state residency (%)");
        initonly String^ MetricSocketResC6  = gcnew String(L"package C6-state residency (%)");
        initonly String^ MetricSocketResC7  = gcnew String(L"package C7-state residency (%)");
        initonly String^ MetricSocketResC8  = gcnew String(L"package C8-state residency (%)");
        initonly String^ MetricSocketResC9  = gcnew String(L"package C9-state residency (%)");
        initonly String^ MetricSocketResC10 = gcnew String(L"package C10-state residency (%)");
        initonly String^ MetricSocketResC0Base  = gcnew String(L"package C0-state base");
        initonly String^ MetricSocketResC2Base  = gcnew String(L"package C2-state base");
        initonly String^ MetricSocketResC3Base  = gcnew String(L"package C3-state base");
        initonly String^ MetricSocketResC6Base  = gcnew String(L"package C6-state base");
        initonly String^ MetricSocketResC7Base  = gcnew String(L"package C7-state base");
        initonly String^ MetricSocketResC8Base  = gcnew String(L"package C8-state base");
        initonly String^ MetricSocketResC9Base  = gcnew String(L"package C9-state base");
        initonly String^ MetricSocketResC10Base = gcnew String(L"package C10-state base");

        initonly String^ MetricQpiBand;

        // Configuration values
        const int sampleRate_;
        const CollectionInformation^ collectionInformation_;
    };

    /// <summary>
    /// Summary for PMCService
    /// </summary>
    ///
    /// WARNING: If you change the name of this class, you will need to change the
    ///          'Resource File Name' property for the managed resource compiler tool
    ///          associated with all .resx files this class depends on.  Otherwise,
    ///          the designers will not be able to interact properly with localized
    ///          resources associated with this form.
    public ref class PCMService : public System::ServiceProcess::ServiceBase
    {
    [DllImport ("advapi32.dll")]
    static bool SetServiceStatus (IntPtr hServiceStatus, LPSERVICE_STATUS lpServiceStatus);

    private:
        void SetServiceFail (int ErrorCode) 
        { 
            SERVICE_STATUS ServiceStatus_;
            ServiceStatus_.dwCurrentState = (int)SERVICE_STOPPED;
            ServiceStatus_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
            ServiceStatus_.dwWaitHint = 0;
            ServiceStatus_.dwWin32ExitCode = ErrorCode;
            ServiceStatus_.dwServiceSpecificExitCode = 0;
            ServiceStatus_.dwCheckPoint = 0;
            ServiceStatus_.dwControlsAccepted = 0 |
                (this->CanStop ? (int) SERVICE_ACCEPT_STOP : 0) |
                (this->CanShutdown ? (int) SERVICE_ACCEPT_SHUTDOWN : 0) |
                (this->CanPauseAndContinue ? (int) SERVICE_ACCEPT_PAUSE_CONTINUE : 0) |
                (this->CanHandleSessionChangeEvent ? (int) SERVICE_ACCEPT_SESSIONCHANGE : 0) |
                (this->CanHandlePowerEvent ? (int) SERVICE_ACCEPT_POWEREVENT : 0);
            SetServiceStatus (this->ServiceHandle, &ServiceStatus_);
        }


    public:
        PCMService()
        {
            InitializeComponent();
            //
            //TODO: Add the constructor code here
            //
        }
    protected:
        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        ~PCMService()
        {
            if (components)
            {
                delete components;
            }
        }

        /// <summary>
        /// Set things in motion so your service can do its work.
        /// </summary>
        virtual void OnStart(array<String^>^ args) override
        {
            PCM* m_ = PCM::getInstance();
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            // Default values for configuration
            int sampleRate = 1000;
            CollectionInformation^ collectionInformation = gcnew CollectionInformation();

            // Read configuration values from registry
            HKEY hkey;
            if (ERROR_SUCCESS == RegOpenKeyEx(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\pcm\\service"), NULL, KEY_READ, &hkey))
            {
                DWORD regDWORD = static_cast<DWORD>(REG_DWORD);
                DWORD lenDWORD = 32;

                DWORD sampleRateRead(0);
                if (ERROR_SUCCESS == RegQueryValueEx(hkey, TEXT("SampleRate"), NULL, NULL, reinterpret_cast<LPBYTE>(&sampleRateRead), &lenDWORD))
                {
                    sampleRate = (int)sampleRateRead;
                }

                DWORD collectCoreRead(0);
                if (ERROR_SUCCESS == RegQueryValueEx(hkey, TEXT("CollectCore"), NULL, NULL, reinterpret_cast<LPBYTE>(&collectCoreRead), &lenDWORD)) {
                    collectionInformation->core = (int)collectCoreRead > 0;
                }

                DWORD collectSocketRead(0);
                if (ERROR_SUCCESS == RegQueryValueEx(hkey, TEXT("CollectSocket"), NULL, NULL, reinterpret_cast<LPBYTE>(&collectSocketRead), &lenDWORD)) {
                    collectionInformation->socket = (int)collectSocketRead > 0;
                }

                DWORD collectQpiRead(0);
                if (ERROR_SUCCESS == RegQueryValueEx(hkey, TEXT("CollectQpi"), NULL, NULL, reinterpret_cast<LPBYTE>(&collectQpiRead), &lenDWORD)) {
                    collectionInformation->qpi = (int)collectQpiRead > 0;
                }

                RegCloseKey(hkey);
            }

            this->RequestAdditionalTime(4000);
            // We should open the driver here
            EventLog->WriteEntry(Globals::ServiceName, "Trying to start the driver...", EventLogEntryType::Information);
            drv_ = new Driver;
            if (!drv_->start())
            {
#ifdef UNICODE
                const auto& driverPath = drv_->driverPath();
#else
                std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> char_to_wide;
                std::wstring driverPath = char_to_wide.from_bytes(drv_->driverPath().c_str());
#endif
                String^ s = gcnew String((L"Cannot open the driver.\nYou must have a signed driver at " + driverPath + L" and have administrator rights to run this program.\n\n").c_str());
                EventLog->WriteEntry(Globals::ServiceName, s, EventLogEntryType::Error);
                SetServiceFail(ERROR_FILE_NOT_FOUND);
                throw gcnew Exception(s);
            }

            // TODO: Add code here to start your service.
            MeasureThread^ mt;
            EventLog->WriteEntry(Globals::ServiceName, "Trying to create the measure thread...", EventLogEntryType::Information);
            try
            {
                mt = gcnew MeasureThread(EventLog, sampleRate, collectionInformation);
            }
            catch (Exception^ e)
            {
                EventLog->WriteEntry(Globals::ServiceName, "Could not create MeasureThread, aborting", EventLogEntryType::Error);
                EventLog->WriteEntry(Globals::ServiceName, e->Message, EventLogEntryType::Error);
                SetServiceFail(0x80886);
                throw e;
            }

            // Create thread, pretty obvious comment here
            workerThread_ = gcnew Thread( gcnew ThreadStart( mt, &MeasureThread::doMeasurements ) );
            // Start timer/thread to read out registers and fill performance counter structures
            workerThread_->Start();
//            EventLog->WriteEntry("PCMService", System::DateTime::Now.ToLongTimeString() + " Monitor could not initialize PMU, aborting.", EventLogEntryType::Error);

        }

        /// <summary>
        /// Stop this service.
        /// </summary>
        virtual void OnStop() override
        {
            // TODO: Add code here to perform any tear-down necessary to stop your service.
            this->RequestAdditionalTime(4000);
            // Stop timer/thread
            // doMeasurements will do cleanup itself, might have to do some sanity checks here
            workerThread_->Abort();
            drv_->stop();
        }

    private:
        /// <summary>
        /// Required designer variable.
        /// </summary>
        System::ComponentModel::Container ^components;
        System::Threading::Thread^ workerThread_;
        Driver* drv_;

#pragma region Windows Form Designer generated code
        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        void InitializeComponent(void)
        {
            // 
            // PCMService
            // 
            this->CanPauseAndContinue = true;
            this->ServiceName = Globals::ServiceName;

        }
#pragma endregion
    };
}
