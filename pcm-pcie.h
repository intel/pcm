#pragma once
/*

   Copyright (c) 2020, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
//written by Roman Sudarikov

#include <iostream>
#include "cpucounters.h"
#include "utils.h"
#include <vector>
#include <array>
#include <string>
#include <initializer_list>
#include <algorithm>

using namespace std;
const uint32 max_sockets = 4;
#define NUM_SAMPLES (1)

static void print(const vector<string> &listNames, bool csv)
{
    for(auto& name : listNames)
        if (csv)
            cout << "," << name;
        else
            cout << "|  " << name << "  ";
}

class IPlatform
{
public:
    IPlatform(PCM *m, bool csv, bool bandwidth, bool verbose);
    virtual void getEvents() = 0;
    virtual void printHeader() = 0;
    virtual void printEvents() = 0;
    virtual void printAggrEventData() = 0;
    virtual void cleanup() = 0;
    static IPlatform *getPlatform(PCM* m, bool csv, bool bandwidth,
                                        bool verbose, uint32 delay);
    virtual ~IPlatform() { }

protected:
    PCM *m_pcm;
    bool m_csv;
    bool m_bandwidth;
    bool m_verbose;
    uint m_eventsCount;
    uint m_socketCount;

    enum eventFilter {
        TOTAL,
        MISS,
        HIT,
        fltLast
    };

    vector<string> filterNames;
    vector<string> bwNames;

private:
    void init();
};

void IPlatform::init()
{
    m_pcm->disableJKTWorkaround();
    PCM::ErrorCode status = m_pcm->program();
    switch(status)
    {
        case PCM::Success:
            break;
        case PCM::MSRAccessDenied:
            cerr << "Access to Processor Counter Monitor has denied (no MSR or PCI CFG space access).\n";
            exit(EXIT_FAILURE);
        case PCM::PMUBusy:
            cerr << "Access to Processor Counter Monitor has denied (Performance Monitoring Unit is occupied by other application). Try to stop the application that uses PMU.\n";
            cerr << "Alternatively you can try to reset PMU configuration at your own risk. Try to reset? (y/n)\n";
            char yn;
            cin >> yn;
            if ('y' == yn)
            {
                m_pcm->resetPMU();
                cerr << "PMU configuration has been reset. Try to rerun the program again.\n";
            }
            exit(EXIT_FAILURE);
        default:
            cerr << "Access to Processor Counter Monitor has denied (Unknown error).\n";
            exit(EXIT_FAILURE);
    }

    print_cpu_details();

    if (m_socketCount > max_sockets)
    {
        cerr << "Only systems with up to "<<max_sockets<<" sockets are supported! Program aborted\n";
        exit(EXIT_FAILURE);
    }

    if (m_pcm->isSomeCoreOfflined())
    {
        cerr << "Core offlining is not supported. Program aborted\n";
        exit(EXIT_FAILURE);
    }
}

IPlatform::IPlatform(PCM *m, bool csv, bool bandwidth, bool verbose) :
        filterNames {"(Total)", "(Miss)", "(Hit)"},
        bwNames {"PCIe Rd (B)", "PCIe Wr (B)"}
{
    m_pcm = m;
    m_csv = csv;
    m_bandwidth = bandwidth;
    m_verbose = verbose;
    m_socketCount = m_pcm->getNumSockets();

    init();
}

/*
 * JKT through CPX use common API to program and access required Uncore counters.
 * The only difference is event opcodes and the way how bandwidth is calculated.
 */
class LegacyPlatform: public IPlatform
{
public:
    virtual void getEvents();
    virtual void printHeader();
    virtual void printEvents();
    virtual void printAggrEventData();
    virtual void cleanup();

    vector<string> eventNames;
    vector<CboEventCfg_t> EventCodes;

    LegacyPlatform(initializer_list<string> events, initializer_list<CboEventCfg_t> eventCodes,
        PCM *m, bool csv, bool bandwidth, bool verbose, uint32 delay) :
        IPlatform(m, csv, bandwidth, verbose),
            eventNames(events), EventCodes(eventCodes),
            eventSample {0}, aggregate_sample {0}
    {
        m_eventsCount = eventNames.size();

        m_delay = uint32(delay * 1000 / m_eventsCount / NUM_SAMPLES);
        if (m_delay * m_eventsCount * NUM_SAMPLES < delay * 1000) ++m_delay;

        aggregate_sample.resize(m_eventsCount);
        eventSample.resize(m_socketCount);
        for( auto& filter : eventSample)
            for(auto& event : filter)
                event.resize(m_eventsCount);

        cleanup();

        before.resize(HIT);
        for(auto& ctr : before)
            ctr.resize(m_socketCount);
        after.resize(HIT);
        for(auto& ctr : after)
            ctr.resize(m_socketCount);
    };

private:
    typedef vector <PCIeCounterState> ctr;
    vector <ctr> before, after;

    void printBandwidth(uint socket, uint filter);
    void printBandwidth();
    uint getIdent (const string &s);
    void printSocketScopeEvent(uint socket, uint filter, uint event);
    void printSocketScopeEvents(uint socket, uint filter);
    uint64 getEventCount (PCIeCounterState &before, PCIeCounterState &after);
    void getEvent(uint event);

protected:
    uint32 m_delay;
    typedef vector<uint64> events;
    typedef array<events, fltLast> filters;
    vector<filters> eventSample;
    vector<uint64> aggregate_sample;

    virtual uint64 getReadBw(uint socket, uint filter) = 0;
    virtual uint64 getWriteBw(uint socket, uint filter) = 0;
    virtual uint64 getReadBw() = 0;
    virtual uint64 getWriteBw() = 0;
};

uint LegacyPlatform::getIdent (const string &s)
{
    /*
     * We are adding "|  " before and "  " after the event name hence +5 to
     * strlen(eventNames). Rest of the logic is to center the event name.
     */
    uint ident = 5 + s.size();
    return (3 + ident / 2);
}

void LegacyPlatform::cleanup()
{
    for( auto& filter : eventSample)
        for(auto& event : filter)
            fill(event.begin(), event.end(), 0);

    fill(aggregate_sample.begin(), aggregate_sample.end(), 0);
}

inline uint64 LegacyPlatform::getEventCount (PCIeCounterState &before, PCIeCounterState &after)
{
    return m_eventsCount * getNumberOfEvents(before, after);
}

void LegacyPlatform::getEvent(uint idx)
{
    for (uint filter = TOTAL; filter < HIT; filter++)
    {
        m_pcm->programPCIeCounters(EventCodes[idx], filter);
        for(uint socket = 0; socket < m_socketCount; ++socket)
            before[filter][socket] = m_pcm->getPCIeCounterState(socket);
        MySleepMs(m_delay);
        for(uint socket = 0; socket < m_socketCount; ++socket)
            after[filter][socket] = m_pcm->getPCIeCounterState(socket);
    }

    for(uint socket = 0; socket < m_socketCount; socket++)
    {
        eventSample[socket][TOTAL][idx] += getEventCount(before[TOTAL][socket], after[TOTAL][socket]);
        eventSample[socket][MISS][idx] += getEventCount(before[MISS][socket], after[MISS][socket]);
        if (eventSample[socket][TOTAL][idx] > eventSample[socket][MISS][idx])
            eventSample[socket][HIT][idx] = eventSample[socket][TOTAL][idx] - eventSample[socket][MISS][idx];
            else eventSample[socket][HIT][idx] = 0;

        aggregate_sample[idx] += eventSample[socket][TOTAL][idx];
    }
}

void LegacyPlatform::getEvents()
{
    for(uint idx = 0; idx < EventCodes.size(); idx++)
            getEvent(idx);
}

void LegacyPlatform::printHeader()
{
    cout << "Skt";
    if (!m_csv)
        cout << ' ';

    print(eventNames, m_csv);
    if (m_bandwidth)
        print(bwNames, m_csv);

    cout << "\n";
}

void LegacyPlatform::printBandwidth(uint socket, uint filter)
{
    typedef uint64 (LegacyPlatform::*bwFunc_t)(uint, uint);
    vector<bwFunc_t> bwFunc = {
        &LegacyPlatform::getReadBw,
        &LegacyPlatform::getWriteBw,
    };

    if (!m_csv)
        for(auto& bw_f : bwFunc) {
            int ident = getIdent(bwNames[&bw_f - bwFunc.data()]);
            cout << setw(ident)
                 << unit_format((this->*bw_f)(socket,filter))
                 << setw(5 + bwNames[&bw_f - bwFunc.data()].size() - ident)
                 << ' ';
        }
    else
        for(auto& bw_f : bwFunc)
            cout << ',' << (this->*bw_f)(socket,filter);
}

void LegacyPlatform::printSocketScopeEvent(uint socket, uint filter, uint idx)
{
    uint64 event = eventSample[socket][filter][idx];

    if (m_csv)
        cout << ',' << event;
    else
    {
        int ident = getIdent(eventNames[idx]);
        cout << setw(ident)
         << unit_format(event)
              << setw(5 + eventNames[idx].size() - ident) << ' ';
    }
}

void LegacyPlatform::printSocketScopeEvents(uint socket, uint filter)
{
    if (!m_csv) {
        int ident = strlen("Skt |") / 2;
        cout << setw(ident) << socket << setw(ident) << ' ';
    } else
        cout << socket;

    for(uint idx = 0; idx < EventCodes.size(); idx++)
        printSocketScopeEvent(socket, filter, idx);

    if (m_bandwidth)
        printBandwidth(socket, filter);

    if(m_verbose)
        cout << filterNames[filter];

    cout << "\n";
}

void LegacyPlatform::printEvents()
{
    for (uint socket = 0; socket < m_socketCount; socket++)
        if (!m_verbose)
            printSocketScopeEvents(socket, TOTAL);
        else
            for (uint filter = 0; filter < fltLast; filter++)
                printSocketScopeEvents(socket, filter);
}

void LegacyPlatform::printAggrEventData()
{
    if (!m_csv)
    {
        uint len = strlen("Skt ");

        for(auto& evt : eventNames)
            len += (5 + evt.size());

        if (m_bandwidth)
            for(auto& bw : bwNames)
                len += (5 + bw.size());

        while (len--)
            cout << '-';
        cout << "\n";

        int ident = strlen("Skt |") /2 ;
        cout << setw(ident) << "*" << setw(ident) << ' ';

        for (uint idx = 0; idx < eventNames.size(); idx++) {
            ident = getIdent(eventNames[idx]);
            cout << setw(ident)
                  << unit_format(aggregate_sample[idx])
                  << setw(5 + eventNames[idx].size() - ident) << ' ';
        }

        if (m_bandwidth)
        {
            typedef uint64 (LegacyPlatform::*bwFunc_t)();
            vector<bwFunc_t> bwFunc = {
                &LegacyPlatform::getReadBw,
                &LegacyPlatform::getWriteBw,
            };

            for(auto& bw_f : bwFunc) {
                ident = getIdent(bwNames[&bw_f - bwFunc.data()]);
                cout << setw(ident)
                     << unit_format((this->*bw_f)())
                     << setw(5 + bwNames[&bw_f - bwFunc.data()].size() - ident)
                     << ' ';
            }
        }

        if (m_verbose)
            cout << "(Aggregate)\n\n";
        else
            cout << "\n\n";
    }
}

//CPX, CLX, SKX
class PurleyPlatform: public LegacyPlatform
{
public:
    PurleyPlatform(PCM *m, bool csv, bool bandwidth, bool verbose, uint32 delay) :
    LegacyPlatform( {"PCIeRdCur", "RFO", "CRd", "DRd", "ItoM", "PRd", "WiL"},
                    {//  opCode q  tid  nc
                        {0x21E, 2, 0x00, 0}, //PCIeRdCur
                        {0x200, 2, 0x00, 0}, //RFO
                        {0x201, 2, 0x00, 0}, //CRd
                        {0x202, 2, 0x00, 0}, //DRd
                        {0x248, 2, 0x00, 0}, //ItoM
                        {0x207, 1, 0x00, 1}, //PRd
                        {0x20F, 1, 0x00, 1}, //WiL
                    },
                    m, csv, bandwidth, verbose, delay)
    {
    };

private:
    enum {
        PCIeRdCur,
        RFO,
        CRd,
        DRd,
        ItoM,
        PRd,
        WiL,
        eventLast
    };

    virtual uint64 getReadBw(uint socket, uint filter);
    virtual uint64 getWriteBw(uint socket, uint filter);
    virtual uint64 getReadBw();
    virtual uint64 getWriteBw();
};

uint64 PurleyPlatform::getReadBw(uint socket, uint filter)
{
    uint64 readBw = eventSample[socket][filter][PCIeRdCur] +
                    eventSample[socket][filter][RFO] +
                    eventSample[socket][filter][CRd] +
                    eventSample[socket][filter][DRd];
    return (readBw * 64ULL);
}

uint64 PurleyPlatform::getReadBw()
{
    uint64 readBw = aggregate_sample[PCIeRdCur] +
                    aggregate_sample[RFO] +
                    aggregate_sample[CRd] +
                    aggregate_sample[DRd];
    return (readBw * 64ULL);
}

uint64 PurleyPlatform::getWriteBw(uint socket, uint filter)
{
    uint64 writeBw = eventSample[socket][filter][RFO] +
                     eventSample[socket][filter][ItoM];
    return (writeBw * 64ULL);
}

uint64 PurleyPlatform::getWriteBw()
{
    uint64 writeBw = aggregate_sample[RFO] +
                     aggregate_sample[ItoM];
    return (writeBw * 64ULL);
}

//BDX, HSX
class GrantleyPlatform: public LegacyPlatform
{
public:
    GrantleyPlatform(PCM *m, bool csv, bool bandwidth, bool verbose, uint32 delay) :
    LegacyPlatform( {"PCIeRdCur", "RFO", "CRd", "DRd", "ItoM", "PRd", "WiL"},
                    {//  opCode q  tid  nc
                        {0x19E, 0, 0x00, 0}, //PCIeRdCur
                        {0x180, 0, 0x3e, 0}, //RFO
                        {0x181, 0, 0x00, 0}, //CRd
                        {0x182, 0, 0x00, 0}, //DRd
                        {0x1C8, 0, 0x3e, 0}, //ItoM
                        {0x187, 0, 0x00, 0}, //PRd
                        {0x18F, 0, 0x00, 0}, //WiL
                    },
                    m, csv, bandwidth, verbose, delay)
    {
    };

private:
    enum {
        PCIeRdCur,
        RFO,
        CRd,
        DRd,
        ItoM,
        PRd,
        WiL,
        eventLast
    };

    virtual uint64 getReadBw(uint socket, uint filter);
    virtual uint64 getWriteBw(uint socket, uint filter);
    virtual uint64 getReadBw();
    virtual uint64 getWriteBw();
};

uint64 GrantleyPlatform::getReadBw(uint socket, uint filter)
{
    uint64 readBw = eventSample[socket][filter][PCIeRdCur] +
                    eventSample[socket][filter][RFO] +
                    eventSample[socket][filter][CRd] +
                    eventSample[socket][filter][DRd];
    return (readBw * 64ULL);
}

uint64 GrantleyPlatform::getReadBw()
{
    uint64 readBw = aggregate_sample[PCIeRdCur] +
                    aggregate_sample[RFO] +
                    aggregate_sample[CRd] +
                    aggregate_sample[DRd];
    return (readBw * 64ULL);
}

uint64 GrantleyPlatform::getWriteBw(uint socket, uint filter)
{
    uint64 writeBw = eventSample[socket][filter][RFO] +
                     eventSample[socket][filter][ItoM];
    return (writeBw * 64ULL);
}

uint64 GrantleyPlatform::getWriteBw()
{
    uint64 writeBw = aggregate_sample[RFO] +
                     aggregate_sample[ItoM];
    return (writeBw * 64ULL);
}

//IVT, JKT
class BromolowPlatform: public LegacyPlatform
{
public:
    BromolowPlatform(PCM *m, bool csv, bool bandwidth, bool verbose, uint32 delay) :
    LegacyPlatform( {"PCIeRdCur", "PCIeNSRd", "PCIeWiLF", "PCIeItoM", "PCIeNSWr", "PCIeNSWrF"},
                    {//  opCode q  tid  nc
                        {0x19E, 0, 0x00, 0}, //PCIeRdCur
                        {0x1E4, 0, 0x00, 0}, //PCIeNSRd
                        {0x194, 0, 0x00, 0}, //PCIeWiLF
                        {0x19C, 0, 0x00, 0}, //PCIeItoM
                        {0x1E5, 0, 0x00, 0}, //PCIeNSWr
                        {0x1E6, 0, 0x00, 0}, //PCIeNSWrF
                    },
                    m, csv, bandwidth, verbose, delay)
    {
    };

private:
    enum {
        PCIeRdCur,
        PCIeNSRd,
        PCIeWiLF,
        PCIeItoM,
        PCIeNSWr,
        PCIeNSWrF,
        eventLast
    };

    virtual uint64 getReadBw(uint socket, uint filter);
    virtual uint64 getWriteBw(uint socket, uint filter);
    virtual uint64 getReadBw();
    virtual uint64 getWriteBw();
};

uint64 BromolowPlatform::getReadBw(uint socket, uint filter)
{
    uint64 readBw = eventSample[socket][filter][PCIeRdCur] +
                    eventSample[socket][filter][PCIeNSWr];
    return (readBw * 64ULL);
}

uint64 BromolowPlatform::getReadBw()
{
    uint64 readBw = aggregate_sample[PCIeRdCur] +
                    aggregate_sample[PCIeNSWr];
    return (readBw * 64ULL);
}

uint64 BromolowPlatform::getWriteBw(uint socket, uint filter)
{
    uint64 writeBw = eventSample[socket][filter][PCIeWiLF] +
                     eventSample[socket][filter][PCIeItoM] +
                     eventSample[socket][filter][PCIeNSWr] +
                     eventSample[socket][filter][PCIeNSWrF];
    return (writeBw * 64ULL);
}

uint64 BromolowPlatform::getWriteBw()
{
    uint64 writeBw = aggregate_sample[PCIeWiLF] +
                     aggregate_sample[PCIeItoM] +
                     aggregate_sample[PCIeNSWr] +
                     aggregate_sample[PCIeNSWrF];
    return (writeBw * 64ULL);
}
