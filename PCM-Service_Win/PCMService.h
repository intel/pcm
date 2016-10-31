/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
** Written by Otto Bruggeman, Roman Dementiev
*/

#pragma once

#pragma unmanaged
#include "..\Intelpcm.dll\Intelpcm.h"
#include "..\PCM_Win\windriver.h"
#include <stdexcept>
#pragma managed

using namespace System;
using namespace System::Collections;
using namespace System::ServiceProcess;
using namespace System::ComponentModel;
using namespace System::Diagnostics;
using namespace System::Threading;
using namespace System::Runtime::InteropServices;

namespace PCMServiceNS {

    ref class MeasureThread
    {
    public:
        MeasureThread( System::Diagnostics::EventLog^ log ) : log_(log)
        {
            // Get a Monitor instance which sets up the PMU, it also figures out the number of cores and sockets which we need later on to create the performance counters
            m_ = PCM::getInstance();
            if ( !m_->good() )
            {
                log_->WriteEntry("PCMService", "Monitor Instance could not be created.", EventLogEntryType::Error);
                m_->cleanup();
                String^ s = gcnew String(m_->getErrorMessage().c_str());
                throw gcnew Exception(s);
            }
            log_->WriteEntry( "PCMService", "PCM: Number of cores detected: " + UInt32(m_->getNumCores()).ToString() );

            m_->program();

            log_->WriteEntry( "PCMService", "PMU Programmed." );

            // This here will only create the necessary registry entries, the actual counters are created later.
            // New unified category
            if ( PerformanceCounterCategory::Exists( "PCM Core Counters" ) )
            {
                PerformanceCounterCategory::Delete( "PCM Core Counters" );
            }
            if ( PerformanceCounterCategory::Exists( "PCM Socket Counters" ) )
            {
                PerformanceCounterCategory::Delete( "PCM Socket Counters" );
            }
            if ( PerformanceCounterCategory::Exists( "PCM QPI Counters" ) )
            {
                PerformanceCounterCategory::Delete( "PCM QPI Counters" );
            }
            log_->WriteEntry( "PCMService", "Old categories deleted." );
            // First create the collection, then add counters to it so we add them all at once
            CounterCreationDataCollection^ counterCollection = gcnew CounterCreationDataCollection;

            // Here we add the counters one by one, need list of counters currently collected.
            // This is a stub: copy and paste when new counters are added to ipcustat "library".
            // CounterCreationData^ counter = gcnew CounterCreationData( "counter", "helptext for counter", PerformanceCounterType::NumberOfItems64 );
            // counterCollection->Add( counter );
            CounterCreationData^ counter;
            counter = gcnew CounterCreationData( "Clockticks", "Displays the number of clockticks elapsed since previous measurement.", PerformanceCounterType::CounterDelta64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Instructions Retired", "Displays the number of instructions retired since previous measurement.", PerformanceCounterType::CounterDelta64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "L2 Cache Misses", "Displays the L2 Cache Misses caused by this core.", PerformanceCounterType::CounterDelta64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "L3 Cache Misses", "Displays the L3 Cache Misses caused by this core.", PerformanceCounterType::CounterDelta64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Instructions Per Clocktick (IPC)", "Displays the instructions per clocktick executed for this core.", PerformanceCounterType::AverageCount64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Base ticks IPC", "Not visible", PerformanceCounterType::AverageBase );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Relative Frequency (%)", "Displays the current frequency of the core to its rated frequency in percent.", PerformanceCounterType::SampleFraction );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Nominal Frequency", "Not visible", PerformanceCounterType::SampleBase );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "Thermal Headroom below TjMax", "Displays temperature reading in 1 degree Celsius relative to the TjMax temperature. 0 corresponds to the max temperature.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "core C0-state residency (%)", "Displays the residency of core or socket in core C0-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "core C3-state residency (%)", "Displays the residency of core or socket in core C3-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "core C6-state residency (%)", "Displays the residency of core or socket in core C6-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "core C7-state residency (%)", "Displays the residency of core or socket in core C7-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
            PerformanceCounterCategory::Create( "PCM Core Counters", "Intel Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection );

            counterCollection->Clear();
            counter = gcnew CounterCreationData( "Memory Read Bandwidth", "Displays the memory read bandwidth in bytes/s of this socket.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
            counter = gcnew CounterCreationData( "Memory Write Bandwidth", "Displays the memory write bandwidth in bytes/s of this socket.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "Package/Socket Consumed Energy", "Displays the energy in Joules consumed by this socket.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "DRAM/Memory Consumed Energy", "Displays the energy in Joules consumed by DRAM memory attached to the memory controller of this socket.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "package C2-state residency (%)", "Displays the residency of socket in package C2-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "package C3-state residency (%)", "Displays the residency of socket in package C3-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "package C6-state residency (%)", "Displays the residency of socket in package C6-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
			counter = gcnew CounterCreationData( "package C7-state residency (%)", "Displays the residency of socket in package C7-state in percent.", PerformanceCounterType::NumberOfItems64 );
            counterCollection->Add( counter );
            PerformanceCounterCategory::Create( "PCM Socket Counters", "Intel Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection );

            counterCollection->Clear();
            counter = gcnew CounterCreationData( "QPI Link Bandwidth", "Displays the incoming bandwidth in bytes/s of this QPI link.", PerformanceCounterType::CounterDelta64 );
            counterCollection->Add( counter );
            PerformanceCounterCategory::Create( "PCM QPI Counters", "Intel Performance Counter Monitor", PerformanceCounterCategoryType::MultiInstance, counterCollection );

            log_->WriteEntry( "PCMService", "New categories added." );

            // Registry entries created, now we need to create the programmatic counters. For some things you may want one instance for every core/thread/socket/qpilink so create in a loop.
            // PerformanceCounter^ pc1 = gcnew PerformanceCounter( "SomeCounterName", "nameOfCounterAsEnteredInTheRegistry", "instanceNameOfCounterAsANumber" );

            // Create #processors instances of the core specific performance counters
            String^ s; // Used for creating the instance name and the string to search for in the hashtable
            for ( unsigned int i = 0; i < m_->getNumCores(); ++i )
            {
                s = UInt32(i).ToString(); // For core counters we use just the number of the core
                ticksHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Clockticks", s, false ) );
                instRetHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Retired", s, false ) );
                ipcHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Per Clocktick (IPC)", s, false ) );
                baseTicksForIpcHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Base ticks IPC", s, false ) );
                relFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Relative Frequency (%)", s, false ) );
                baseTicksForRelFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Nominal Frequency", s, false ) );
                l2CacheMissHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "L2 Cache Misses", s, false ) );
                l3CacheMissHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "L3 Cache Misses", s, false ) );
				thermalHeadroomHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Thermal Headroom below TjMax", s, false ) );
				CoreC0StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C0-state residency (%)", s, false ) );
				CoreC3StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C3-state residency (%)", s, false ) );
				CoreC6StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C6-state residency (%)", s, false ) );
				CoreC7StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C7-state residency (%)", s, false ) );
            }

            // Create socket instances of the common core counters, names are Socket+number
            for ( unsigned int i=0; i<m_->getNumSockets(); ++i )
            {
                s = "Socket"+UInt32(i).ToString();
                ticksHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Clockticks", s, false ) );
                instRetHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Retired", s, false ) );
                ipcHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Per Clocktick (IPC)", s, false ) );
                baseTicksForIpcHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Base ticks IPC", s, false ) );
                relFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Relative Frequency (%)", s, false ) );
                baseTicksForRelFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Nominal Frequency", s, false ) );
                l2CacheMissHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "L2 Cache Misses", s, false ) );
                l3CacheMissHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "L3 Cache Misses", s, false ) );
				thermalHeadroomHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Thermal Headroom below TjMax", s, false ) );
				CoreC0StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C0-state residency (%)", s, false ) );
				CoreC3StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C3-state residency (%)", s, false ) );
				CoreC6StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C6-state residency (%)", s, false ) );
				CoreC7StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C7-state residency (%)", s, false ) );
                mrbHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "Memory Read Bandwidth", s, false ) );
                mwbHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "Memory Write Bandwidth", s, false ) );
                qpiHash_.Add( s, gcnew PerformanceCounter( "PCM QPI Counters", "QPI Link Bandwidth", s, false ) ); // Socket aggregate
				packageEnergyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "Package/Socket Consumed Energy", s, false ) );
				DRAMEnergyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "DRAM/Memory Consumed Energy", s, false ) );
				PackageC2StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C2-state residency (%)", s, false ) );
				PackageC3StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C3-state residency (%)", s, false ) );
				PackageC6StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C6-state residency (%)", s, false ) );
				PackageC7StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C7-state residency (%)", s, false ) );
                String^ t;
                for ( unsigned int j=0; j<m_->getQPILinksPerSocket(); ++j )
                {
                    t = s + "_Link" + UInt32(j).ToString();
                    qpiHash_.Add( t, gcnew PerformanceCounter( "PCM QPI Counters", "QPI Link Bandwidth", t, false ) );
                }
            }

            // Create #system instances of the system specific performance counters, just kidding, there is only one system so 1 instance
            s = "Total_";
            ticksHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Clockticks", s, false ) );
            instRetHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Retired", s, false ) );
            ipcHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Instructions Per Clocktick (IPC)", s, false ) );
            baseTicksForIpcHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "Base ticks IPC", s, false ) );
            relFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Relative Frequency (%)", s, false ) );
            baseTicksForRelFreqHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Nominal Frequency", s, false ) );
            l2CacheMissHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "L2 Cache Misses", s, false ) );
            l3CacheMissHash_.Add(s, gcnew PerformanceCounter( "PCM Core Counters", "L3 Cache Misses", s, false ) );
			thermalHeadroomHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "Thermal Headroom below TjMax", s, false ) );
			CoreC0StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C0-state residency (%)", s, false ) );
			CoreC3StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C3-state residency (%)", s, false ) );
			CoreC6StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C6-state residency (%)", s, false ) );
			CoreC7StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Core Counters", "core C7-state residency (%)", s, false ) );
            mrbHash_.Add(s, gcnew PerformanceCounter( "PCM Socket Counters", "Memory Read Bandwidth", s, false ) );
            mwbHash_.Add(s, gcnew PerformanceCounter( "PCM Socket Counters", "Memory Write Bandwidth", s, false ) );
			packageEnergyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "Package/Socket Consumed Energy", s, false ) );
			DRAMEnergyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "DRAM/Memory Consumed Energy", s, false ) );
			PackageC2StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C2-state residency (%)", s, false ) );
			PackageC3StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C3-state residency (%)", s, false ) );
			PackageC6StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C6-state residency (%)", s, false ) );
			PackageC7StateResidencyHash_.Add( s, gcnew PerformanceCounter( "PCM Socket Counters", "package C7-state residency (%)", s, false ) );
            qpiHash_.Add( s, gcnew PerformanceCounter( "PCM QPI Counters", "QPI Link Bandwidth", s, false ) );

            log_->WriteEntry( "PCMService", "All instances of the performance counter categories have been created." );
        }

        void doMeasurements( void )
        {
            // FIXME: Do we care about hot swappability of CPUs?
            const size_t numSockets  = m_->getNumSockets();
            const size_t numCores    = m_->getNumCores();
            const size_t numQpiLinks = (size_t) m_->getQPILinksPerSocket();

            // The structures
            SystemCounterState  oldSystemState;
            SocketCounterState* oldSocketStates = new SocketCounterState[numSockets];
            CoreCounterState*   oldCoreStates   = new CoreCounterState[numCores];

            try {
                while ( 1 )
                {
                    // Sampling roughly once per second is enough
                    Thread::Sleep(1000);

                    // Fetch counter data here and store in the PerformanceCounter instances
                    SystemCounterState systemState = getSystemCounterState();
                    // Set system performance counters
                    String^ s = "Total_";
                    __int64 totalTicks    = getCycles(systemState);
                    __int64 totalRefTicks = m_->getNominalFrequency() * numCores;
                    __int64 totalInstr    = getInstructionsRetired(systemState);
                    ((PerformanceCounter^)ticksHash_["Total_"])->RawValue = totalTicks;
                    ((PerformanceCounter^)instRetHash_["Total_"])->RawValue = totalInstr;
                    ((PerformanceCounter^)ipcHash_["Total_"])->RawValue = totalInstr >> 17;
                    ((PerformanceCounter^)baseTicksForIpcHash_["Total_"])->RawValue = totalTicks >> 17;
                    ((PerformanceCounter^)relFreqHash_["Total_"])->RawValue = totalTicks >> 17;
                    ((PerformanceCounter^)baseTicksForRelFreqHash_["Total_"])->IncrementBy(totalRefTicks >> 17);
					((PerformanceCounter^)CoreC0StateResidencyHash_["Total_"])->RawValue = __int64(100.*getCoreCStateResidency(0,oldSystemState, systemState));
					((PerformanceCounter^)CoreC3StateResidencyHash_["Total_"])->RawValue = __int64(100.*getCoreCStateResidency(3,oldSystemState, systemState));
					((PerformanceCounter^)CoreC6StateResidencyHash_["Total_"])->RawValue = __int64(100.*getCoreCStateResidency(6,oldSystemState, systemState));
					((PerformanceCounter^)CoreC7StateResidencyHash_["Total_"])->RawValue = __int64(100.*getCoreCStateResidency(7,oldSystemState, systemState));
					((PerformanceCounter^)PackageC2StateResidencyHash_["Total_"])->RawValue = __int64(100.*getPackageCStateResidency(2,oldSystemState, systemState));
					((PerformanceCounter^)PackageC3StateResidencyHash_["Total_"])->RawValue = __int64(100.*getPackageCStateResidency(3,oldSystemState, systemState));
					((PerformanceCounter^)PackageC6StateResidencyHash_["Total_"])->RawValue = __int64(100.*getPackageCStateResidency(6,oldSystemState, systemState));
					((PerformanceCounter^)PackageC7StateResidencyHash_["Total_"])->RawValue = __int64(100.*getPackageCStateResidency(7,oldSystemState, systemState));
                    //log_->WriteEntry("PCMService", "Std: " + UInt64(totalTicks).ToString());
                    //log_->WriteEntry("PCMService", "Ref: " + UInt64(totalRefTicks).ToString());
                    ((PerformanceCounter^)l2CacheMissHash_["Total_"])->IncrementBy(getL2CacheMisses(oldSystemState, systemState));
                    ((PerformanceCounter^)l3CacheMissHash_["Total_"])->IncrementBy(getL3CacheMisses(oldSystemState, systemState));
                    ((PerformanceCounter^)mrbHash_["Total_"])->RawValue = getBytesReadFromMC(oldSystemState, systemState);
                    ((PerformanceCounter^)mwbHash_["Total_"])->RawValue = getBytesWrittenToMC(oldSystemState, systemState);
                    ((PerformanceCounter^)qpiHash_["Total_"])->RawValue = getAllIncomingQPILinkBytes(systemState);
					((PerformanceCounter^)packageEnergyHash_["Total_"])->RawValue = (__int64)getConsumedJoules(oldSystemState, systemState);
					((PerformanceCounter^)DRAMEnergyHash_["Total_"])->RawValue = (__int64)getDRAMConsumedJoules(oldSystemState, systemState);
					((PerformanceCounter^)thermalHeadroomHash_["Total_"])->RawValue = systemState.getThermalHeadroom();

                    // Copy current state to old state
                    oldSystemState = systemState;

                    // Set socket performance counters
                    for ( unsigned int i = 0; i < numSockets; ++i )
                    {
                        s = "Socket"+UInt32(i).ToString();
                        SocketCounterState socketState = getSocketCounterState(i);
                        __int64 socketTicks    = getCycles(socketState);
                        __int64 socketRefTicks = m_->getNominalFrequency()* numCores / numSockets;
                        __int64 socketInstr    = getInstructionsRetired(socketState);
                        ((PerformanceCounter^)instRetHash_[s])->RawValue = socketInstr;
                        ((PerformanceCounter^)ipcHash_[s])->RawValue = socketInstr >> 17;
                        ((PerformanceCounter^)ticksHash_[s])->RawValue = socketTicks;
                        ((PerformanceCounter^)baseTicksForIpcHash_[s])->RawValue = socketTicks >> 17;
                        ((PerformanceCounter^)relFreqHash_[s])->RawValue = socketTicks >> 17;
                        ((PerformanceCounter^)baseTicksForRelFreqHash_[s])->IncrementBy(socketRefTicks >> 17);
                        ((PerformanceCounter^)l2CacheMissHash_[s])->IncrementBy(getL2CacheMisses(oldSocketStates[i], socketState));
                        ((PerformanceCounter^)l3CacheMissHash_[s])->IncrementBy(getL3CacheMisses(oldSocketStates[i], socketState));
                        ((PerformanceCounter^)mrbHash_[s])->RawValue = getBytesReadFromMC(oldSocketStates[i], socketState);
                        ((PerformanceCounter^)mwbHash_[s])->RawValue = getBytesWrittenToMC(oldSocketStates[i], socketState);
                        ((PerformanceCounter^)qpiHash_[s])->RawValue = getSocketIncomingQPILinkBytes(i, systemState);
						((PerformanceCounter^)packageEnergyHash_[s])->RawValue = (__int64)getConsumedJoules(oldSocketStates[i], socketState);
						((PerformanceCounter^)DRAMEnergyHash_[s])->RawValue = (__int64)getDRAMConsumedJoules(oldSocketStates[i], socketState);
						((PerformanceCounter^)thermalHeadroomHash_[s])->RawValue = socketState.getThermalHeadroom();
						((PerformanceCounter^)CoreC0StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(0,oldSocketStates[i], socketState));
						((PerformanceCounter^)CoreC3StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(3,oldSocketStates[i], socketState));
						((PerformanceCounter^)CoreC6StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(6,oldSocketStates[i], socketState));
						((PerformanceCounter^)CoreC7StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(7,oldSocketStates[i], socketState));
						((PerformanceCounter^)PackageC2StateResidencyHash_[s])->RawValue = __int64(100.*getPackageCStateResidency(2,oldSocketStates[i], socketState));
						((PerformanceCounter^)PackageC3StateResidencyHash_[s])->RawValue = __int64(100.*getPackageCStateResidency(3,oldSocketStates[i], socketState));
						((PerformanceCounter^)PackageC6StateResidencyHash_[s])->RawValue = __int64(100.*getPackageCStateResidency(6,oldSocketStates[i], socketState));
						((PerformanceCounter^)PackageC7StateResidencyHash_[s])->RawValue = __int64(100.*getPackageCStateResidency(7,oldSocketStates[i], socketState));
                        String^ t;
                        // and qpi link counters per socket
                        for ( unsigned int j=0; j<numQpiLinks; ++j )
                        {
                            t = s + "_Link" + UInt32(j).ToString();
                            ((PerformanceCounter^)qpiHash_[t])->RawValue = getIncomingQPILinkBytes(i, j, systemState);
                        }
                        oldSocketStates[i] = socketState;
                    }

                    // Set core performance counters
                    for ( unsigned int i = 0; i < numCores; ++i )
                    {
                        s = UInt32(i).ToString();
                        CoreCounterState coreState = getCoreCounterState(i);
                        __int64 ticks    = getCycles(coreState);
                        __int64 refTicks = m_->getNominalFrequency();
                        __int64 instr    = getInstructionsRetired(coreState);
                        ((PerformanceCounter^)instRetHash_[s])->RawValue = instr;
                        ((PerformanceCounter^)ipcHash_[s])->RawValue = instr >> 17;
                        ((PerformanceCounter^)ticksHash_[s])->RawValue = ticks;
                        ((PerformanceCounter^)baseTicksForIpcHash_[s])->RawValue = ticks >> 17;
                        ((PerformanceCounter^)relFreqHash_[s])->RawValue = ticks >> 17;
                        ((PerformanceCounter^)baseTicksForRelFreqHash_[s])->IncrementBy(refTicks >> 17);
                        ((PerformanceCounter^)l2CacheMissHash_[s])->IncrementBy(getL2CacheMisses(oldCoreStates[i], coreState));
                        ((PerformanceCounter^)l3CacheMissHash_[s])->IncrementBy(getL3CacheMisses(oldCoreStates[i], coreState));
						((PerformanceCounter^)thermalHeadroomHash_[s])->RawValue = coreState.getThermalHeadroom();
						((PerformanceCounter^)CoreC0StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(0,oldCoreStates[i], coreState));
						((PerformanceCounter^)CoreC3StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(3,oldCoreStates[i], coreState));
						((PerformanceCounter^)CoreC6StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(6,oldCoreStates[i], coreState));
						((PerformanceCounter^)CoreC7StateResidencyHash_[s])->RawValue = __int64(100.*getCoreCStateResidency(7,oldCoreStates[i], coreState));
                        oldCoreStates[i] = coreState;
                    }
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
		System::Collections::Hashtable CoreC3StateResidencyHash_;
		System::Collections::Hashtable CoreC6StateResidencyHash_;
		System::Collections::Hashtable CoreC7StateResidencyHash_;
		System::Collections::Hashtable PackageC2StateResidencyHash_;
		System::Collections::Hashtable PackageC3StateResidencyHash_;
		System::Collections::Hashtable PackageC6StateResidencyHash_;
		System::Collections::Hashtable PackageC7StateResidencyHash_;

        System::Diagnostics::EventLog^ log_;

        PCM* m_;
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
            this->RequestAdditionalTime(4000);
            // We should open the driver here
            TCHAR driverPath[] = L"c:\\windows\\system32\\msr.sys";

            EventLog->WriteEntry("PCMService", "Trying to start the driver...", EventLogEntryType::Information);
            drv_ = new Driver;
            try
            {
                drv_->start(driverPath);
            }
            catch (std::runtime_error* e)
            {
                String^ s = gcnew String("Cannot open the driver msr.sys.\nYou must have a signed msr.sys driver in c:\\windows\\system32\\ and have administrator rights to run this program.\n\n");
                String^ es = gcnew String(e->what());
                
                throw gcnew Exception(s + es);
            }

            // TODO: Add code here to start your service.
            MeasureThread^ mt;
            EventLog->WriteEntry("PCMService", "Trying to create the measure thread...", EventLogEntryType::Information);
            try
            {
                mt = gcnew MeasureThread(EventLog);
            }
            catch (Exception^ e)
            {
                EventLog->WriteEntry("PCMService", e->Message, EventLogEntryType::Error);
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
            this->ServiceName = L"PCMService";

        }
#pragma endregion
	};
}
