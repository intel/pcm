/*
   Copyright (c) 2009-2020, Intel Corporation
   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /*!     \file pcm-raw.cpp
         \brief Example of using CPU counters: implements a performance counter monitoring utility with raw events interface
   */
#include <iostream>
#ifdef _MSC_VER
#define strtok_r strtok_s
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#include <signal.h>
#include <sys/time.h> // for gettimeofday()
#endif
#include <math.h>
#include <iomanip>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <assert.h>
#include <bitset>
#include <regex>
#include <unordered_map>
#include "cpucounters.h"
#include "utils.h"
#include "gccversion.h"
#ifndef PCM_GCC_6_OR_BELOW
#pragma warning(push, 0)
#include "simdjson/simdjson.h"
#pragma warning(pop)
#endif

#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include <vector>
#define PCM_DELAY_DEFAULT 1.0 // in seconds
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define MAX_CORES 4096

using namespace std;
using namespace pcm;

void print_usage(const string progname)
{
    cerr << "\n Usage: \n " << progname
        << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cerr << "   <delay>                               => time interval to sample performance counters.\n";
    cerr << "                                            If not specified, or 0, with external program given\n";
    cerr << "                                            will read counters only after external program finishes\n";
    cerr << " Supported <options> are: \n";
    cerr << "  -h    | --help      | /h               => print this help and exit\n";
    cerr << "  -csv[=file.csv]     | /csv[=file.csv]  => output compact CSV format to screen or\n"
        << "                                            to a file, in case filename is provided\n";
    cerr << "  [-e event1] [-e event2] [-e event3] .. => list of custom events to monitor\n";
    cerr << "  event description example: -e core/config=0x30203,name=LD_BLOCKS.STORE_FORWARD/ -e core/fixed,config=0x333/ \n";
    cerr << "                             -e cha/config=0,name=UNC_CHA_CLOCKTICKS/ -e imc/fixed,name=DRAM_CLOCKS/\n";
#ifndef PCM_GCC_6_OR_BELOW
    cerr << "                             -e NAME where the NAME is an event from https://download.01.org/perfmon/ event lists\n";
#endif
    cerr << "  -yc   | --yescores  | /yc              => enable specific cores to output\n";
    cerr << "  -f    | /f                             => enforce flushing each line for interactive output\n";
    cerr << "  -i[=number] | /i[=number]              => allow to determine number of iterations\n";
    cerr << "  -tr | /tr                              => transpose output (print single event data in a row)\n";
    cerr << "  -el event_list.txt | /tr event_list.txt  => read event list from event_list.txt file, \n";
    cerr << "                                              each line represents an event,\n";
    cerr << "                                              event groups are separated by a semicolon\n";
    print_help_force_rtm_abort_mode(41);
    cerr << " Examples:\n";
    cerr << "  " << progname << " 1                   => print counters every second without core and socket output\n";
    cerr << "  " << progname << " 0.5 -csv=test.log   => twice a second save counter values to test.log in CSV format\n";
    cerr << "  " << progname << " /csv 5 2>/dev/null  => one sampe every 5 seconds, and discard all diagnostic output\n";
    cerr << "\n";
}

#ifndef PCM_GCC_6_OR_BELOW
using namespace simdjson;

std::vector<std::shared_ptr<simdjson::dom::parser> > JSONparsers;
std::unordered_map<std::string, simdjson::dom::object> PMUEventMap;
std::shared_ptr<simdjson::dom::element> PMURegisterDeclarations;

bool initPMUEventMap()
{
    static bool inited = false;

    if (inited == true)
    {
        return true;
    }
    inited = true;
    const auto mapfile = "mapfile.csv";
    std::ifstream in(mapfile);
    std::string line, item;

    if (!in.is_open())
    {
        cerr << "ERROR: File " << mapfile << " can't be open. \n";
        return false;
    }
    int32 FMSPos = -1;
    int32 FilenamePos = -1;
    int32 EventTypetPos = -1;
    if (std::getline(in, line))
    {
        auto header = split(line, ',');
        for (int32 i = 0; i < (int32)header.size(); ++i)
        {
            if (header[i] == "Family-model")
            {
                FMSPos = i;
            }
            else if (header[i] == "Filename")
            {
                FilenamePos = i;
            }
            else if (header[i] == "EventType")
            {
                EventTypetPos = i;
            }
        }
    }
    else
    {
        cerr << "Can't read first line from " << mapfile << " \n";
        return false;
    }
    // cout << FMSPos << " " << FilenamePos << " " << EventTypetPos << "\n";
    assert(FMSPos > 0);
    assert(FilenamePos > 0);
    assert(EventTypetPos > 0);
    const std::string ourFMS = PCM::getInstance()->getCPUFamilyModelString();
    // cout << "Our FMS: " << ourFMS << "\n";
    std::map<std::string, std::string> eventFiles;
    while (std::getline(in, line))
    {
        auto tokens = split(line, ',');
        assert(FMSPos < (int32)tokens.size());
        assert(FilenamePos < (int32)tokens.size());
        assert(EventTypetPos < (int32)tokens.size());
        std::regex FMSRegex(tokens[FMSPos]);
        std::cmatch FMSMatch;
        if (std::regex_search(ourFMS.c_str(), FMSMatch, FMSRegex))
        {
            cout << tokens[FMSPos] << " " << tokens[EventTypetPos] << " " << tokens[FilenamePos] << " matched\n";
            eventFiles[tokens[EventTypetPos]] = tokens[FilenamePos];
        }
    }
    in.close();

    if (eventFiles.empty())
    {
        cerr << "ERROR: CPU " << ourFMS << "not found in " << mapfile << "\n";
        return false;
    }

    for (const auto evfile : eventFiles)
    {
        std::string path = std::string(".") + evfile.second;
        try {

            cout << evfile.first << " " << evfile.second << "\n";

            if (evfile.first == "core" || evfile.first == "uncore")
            {
                JSONparsers.push_back(std::make_shared<simdjson::dom::parser>());
                for (simdjson::dom::object eventObj : JSONparsers.back()->load(path)) {
                    // cout << "Event ----------------\n";
                    const std::string EventName{eventObj["EventName"].get_c_str()};
                    if (EventName.empty())
                    {
                        cerr << "Did not find EventName in JSON object:\n";
                        for (const auto keyValue : eventObj)
                        {
                            cout << "key: " << keyValue.key << " value: " << keyValue.value << "\n";
                        }
                    }
                    else
                    {
                        PMUEventMap[EventName] = eventObj;
                    }
                }
            }
        }
        catch (std::exception& e)
        {
            cerr << "Error while opening and/or parsing " << path << " : " << e.what() << "\n";
        }
    }
    return true;
}

bool addEventFromDB(PCM::RawPMUConfigs& curPMUConfigs, const string & fullEventStr)
{
    if (initPMUEventMap() == false)
    {
        cerr << "ERROR: PMU Event map can not be initialized\n";
        return false;
    }
    // cerr << "Parsing event " << fullEventStr << "\n";
    // cerr << "size: " << fullEventStr.size() << "\n";
    const auto EventTokens = split(fullEventStr, ':');
    assert(!EventTokens.empty());

    const auto eventStr = EventTokens[0];

    // cerr << "size: " << eventStr.size() << "\n";

    if (eventStr == "MSR_EVENT")
    {
        std::cerr << fullEventStr << " event is not supported. Ignoring the event.\n";
        return true;
    }

    if (PMUEventMap.find(eventStr) == PMUEventMap.end())
    {
        cerr << "ERROR: event " << eventStr << " could not be found in event database. Ignoring the event.\n";
        return true;
    }

    auto mod = EventTokens.begin();
    ++mod;

    const auto eventObj = PMUEventMap[eventStr];
    std::string pmuName;
    PCM::RawEventConfig config = { {0,0,0}, "" };
    bool fixed = false;
    config.second = fullEventStr;

    const std::string path = std::string("PMURegisterDeclarations/") + PCM::getInstance()->getCPUFamilyModelString() + ".json";
    if (PMURegisterDeclarations.get() == nullptr)
    {
        // declaration not loaded yet
        try {

            JSONparsers.push_back(std::make_shared<simdjson::dom::parser>());
            PMURegisterDeclarations = std::make_shared<simdjson::dom::element>();
            *PMURegisterDeclarations = JSONparsers.back()->load(path);
        }
        catch (std::exception& e)
        {
            cerr << "Error while opening and/or parsing " << path << " : " << e.what() << "\n";
            return false;
        }
    }

    if (eventObj["Unit"].error() == NO_SUCH_FIELD)
    {
        pmuName = "core";
        std::string CounterStr{eventObj["Counter"].get_c_str()};
        // cout << "Counter: " << CounterStr << "\n";
        int fixedCounter = -1;
        fixed = (pcm_sscanf(CounterStr) >> s_expect("Fixed counter ") >> fixedCounter) ? true : false;

        try {
            simdjson::dom::object PMUDeclObj;
            if (fixed)
            {
                PMUDeclObj = (*PMURegisterDeclarations)[pmuName][std::string("fixed") + std::to_string(fixedCounter)].get_object();
            }
            else
            {
                PMUDeclObj = (*PMURegisterDeclarations)[pmuName]["programmable"].get_object();
            }
            for (const auto registerKeyValue : PMUDeclObj)
            {
                // cout << "Setting " << registerKeyValue.key << " : " << registerKeyValue.value << "\n";
                simdjson::dom::object fieldDescriptionObj = registerKeyValue.value;
                // cout << "   config: " << uint64_t(fieldDescriptionObj["Config"]) << "\n";
                // cout << "   Position: " << uint64_t(fieldDescriptionObj["Position"]) << "\n";
                std::string fieldNameStr{ registerKeyValue.key.begin(), registerKeyValue.key.end() };
                const int64_t position = int64_t(fieldDescriptionObj["Position"]);
                if (position == -1)
                {
                    continue; // field ignored
                }
                if (eventObj[fieldNameStr].error() == NO_SUCH_FIELD)
                {
                    // cerr << fieldNameStr << " not found\n";
                    if (fieldDescriptionObj["DefaultValue"].error() == NO_SUCH_FIELD)
                    {
                        cerr << "ERROR: DefaultValue not provided for field \"" << fieldNameStr << "\" in " << path << "\n";
                        return false;
                    }
                    else
                    {
                        config.first[uint64_t(fieldDescriptionObj["Config"])] |= uint64_t(fieldDescriptionObj["DefaultValue"]) << position;
                    }
                }
                else
                {
                    std::string fieldValueStr{ eventObj[fieldNameStr].get_c_str() };
                    fieldValueStr.erase(std::remove(fieldValueStr.begin(), fieldValueStr.end(), '\"'), fieldValueStr.end());
                    // cout << " field value is " << fieldValueStr << " " << read_number(fieldValueStr.c_str()) <<  "\n";
                    config.first[uint64_t(fieldDescriptionObj["Config"])] |= read_number(fieldValueStr.c_str()) << position;
                }
            }

            auto setField = [&PMUDeclObj, &config](const char* field, const uint64 value)
            {
                const auto pos = int64_t(PMUDeclObj[field]["Position"]);
                const auto cfg = uint64_t(PMUDeclObj[field]["Config"]);
                const auto width = uint64_t(PMUDeclObj[field]["Width"]);
                const uint64 mask = (1ULL << width) - 1ULL; // 1 -> 1b, 2 -> 11b, 3 -> 111b
                config.first[cfg] &= ~(mask << pos); // clear
                config.first[cfg] |= (value & mask) << pos; // set
            };

            std::regex CounterMaskRegex("c(0x[0-9a-fA-F]+|[[:digit:]]+)");
            std::regex EdgeDetectRegex("e(0x[0-9a-fA-F]+|[[:digit:]]+)");
            while (mod != EventTokens.end())
            {
                const auto assigment = split(*mod, '=');
                if (*mod == "SUP")
                {
                    setField("User", 0);
                    setField("OS", 1);
                }
                else if (*mod == "USER")
                {
                    setField("User", 1);
                    setField("OS", 0);
                }
                else if (*mod == "tx")
                {
                    setField("InTX", 1);
                }
                else if (*mod == "cp")
                {
                    setField("InTXCheckpointed", 1);
                }
                else if (std::regex_match(mod->c_str(), CounterMaskRegex))
                {
                    // Counter Mask modifier
                    const std::string CounterMaskStr{ mod->begin() + 1, mod->end() };
                    setField("CounterMask", read_number(CounterMaskStr.c_str()));
                }
                else if (std::regex_match(mod->c_str(), EdgeDetectRegex))
                {
                    // Edge Detect modifier
                    const std::string Str{ mod->begin() + 1, mod->end() };
                    setField("EdgeDetect", read_number(Str.c_str()));
                }
                else if (assigment.size() == 2 && assigment[0] == "request")
                {
                    cerr << "Unsupported event modifier: " << *mod << " in event " << fullEventStr << "\n";
                    return true;
                }
                else if (assigment.size() == 2 && assigment[0] == "response")
                {
                    cerr << "Unsupported event modifier: " << *mod << " in event " << fullEventStr << "\n";
                    return true;
                }
                else
                {
                    cerr << "Unsupported event modifier: " << *mod << " in event " << fullEventStr << "\n";
                    return false;
                }
                ++mod;
            }

            if (fixed)
            {
                curPMUConfigs[pmuName].fixed.push_back(config);
            }
            else
            {
                curPMUConfigs[pmuName].programmable.push_back(config);
            }
        }
        catch (std::exception& e)
        {
            cerr << "Error while setting a register field for event " << eventStr << " : " << e.what() << "\n";
            for (const auto keyValue : eventObj)
            {
                std::cout << keyValue.key << " : " << keyValue.value << "\n";
            }
            return false;
        }
    }
    else
    {
        const std::string unit{eventObj["Unit"].get_c_str()};
        std::cout << eventStr << " is uncore event for unit " << unit << "\n";
    }

    /*
    for (const auto keyValue : eventObj)
    {
        cout << keyValue.key << " : " << keyValue.value << "\n";
    }
    */

    cout << "parsed " << (fixed ? "fixed " : "") <<  pmuName << " event : \"" << hex << config.second << "\" : {0x" << hex << config.first[0] << ", 0x" << config.first[1] << ", 0x" << config.first[2] << "}\n" << dec;

    return true;
}

#endif

bool addEvent(PCM::RawPMUConfigs & curPMUConfigs, string eventStr)
{
    if (eventStr.empty())
    {
        return true;
    }
#ifndef PCM_GCC_6_OR_BELOW
    if (eventStr.find('/') == string::npos)
    {
        return addEventFromDB(curPMUConfigs, eventStr);
    }
#endif
    PCM::RawEventConfig config = { {0,0,0}, "" };
    const auto typeConfig = split(eventStr, '/');
    if (typeConfig.size() < 2)
    {
        cerr << "ERROR: wrong syntax in event description \"" << eventStr << "\"\n";
        return false;
    }
    auto pmuName = typeConfig[0];
    if (pmuName.empty())
    {
        pmuName = "core";
    }
    const auto configStr = typeConfig[1];
    if (configStr.empty())
    {
        cerr << "ERROR: empty config description in event description \"" << eventStr << "\"\n";
        return false;
    }
    const auto configArray = split(configStr, ',');
    bool fixed = false;
    for (auto item : configArray)
    {
        if (match(item, "config=", &config.first[0])) {}
        else if (match(item, "config1=", &config.first[1])) {}
        else if (match(item, "config2=", &config.first[2])) {}
        else if (pcm_sscanf(item) >> s_expect("name=") >> setw(255) >> config.second) {}
        else if (item == "fixed")
        {
            fixed = true;
        }
        else
        {
            cerr << "ERROR: unknown token " << item << " in event description \"" << eventStr << "\"\n";
            return false;
        }
    }
    cout << "parsed "<< (fixed?"fixed ":"")<< " " << pmuName << " event: \"" << hex << config.second << "\" : {0x" << hex << config.first[0] << ", 0x" << config.first[1] << ", 0x" << config.first[2] << "}\n" << dec;
    if (fixed)
        curPMUConfigs[pmuName].fixed.push_back(config);
    else
        curPMUConfigs[pmuName].programmable.push_back(config);
    return true;
}

bool addEvents(std::vector<PCM::RawPMUConfigs>& PMUConfigs, string fn)
{
    std::ifstream in(fn);
    std::string line, item;

    if (!in.is_open())
    {
        cerr << "ERROR: File " << fn << " can't be open. \n";
        return false;
    }
    PCM::RawPMUConfigs curConfig;
    auto doFinishGroup = [&curConfig, &PMUConfigs]()
    {
        if (!curConfig.empty())
        {
            cout << "Adding new group \n";
            PMUConfigs.push_back(curConfig);
            curConfig.clear();
        }
    };
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        const auto last = line[line.size() - 1];
        bool finishGroup = false;
        if (last == ',')
        {
            line.resize(line.size() - 1);
        }
        else if (last == ';')
        {
            line.resize(line.size() - 1);
            finishGroup = true;
        }
        if (addEvent(curConfig, line) == false)
        {
            return false;
        }
        if (finishGroup)
        {
            doFinishGroup();
        }
    }
    in.close();
    doFinishGroup();
    return true;
}

bool show_partial_core_output = false;
bitset<MAX_CORES> ycores;
bool flushLine = false;
bool transpose = false;

void printRowBegin(const std::string & EventName, const CoreCounterState & BeforeState, const CoreCounterState & AfterState, PCM* m)
{
    printDateForCSV(CsvOutputType::Data);
    cout << EventName << "," << (1000ULL * getInvariantTSC(BeforeState, AfterState)) / m->getNominalFrequency() << "," << getInvariantTSC(BeforeState, AfterState);
}


template <class MetricFunc>
void printRow(const std::string & EventName, MetricFunc metricFunc, const std::vector<CoreCounterState>& BeforeState, const std::vector<CoreCounterState>& AfterState, PCM* m)
{
    printRowBegin(EventName, BeforeState[0], AfterState[0], m);
    for (uint32 core = 0; core < m->getNumCores(); ++core)
    {
        if (!(m->isCoreOnline(core) == false || (show_partial_core_output && ycores.test(core) == false)))
        {
            cout << "," << metricFunc(BeforeState[core], AfterState[core]);
        }
    }
    cout << "\n";
};

typedef uint64 (*UncoreMetricFunc)(const uint32 u, const uint32 i,  const ServerUncoreCounterState& before, const ServerUncoreCounterState& after);
typedef uint64(*UncoreFixedMetricFunc)(const uint32 u, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after);

uint64 nullFixedMetricFunc(const uint32, const ServerUncoreCounterState&, const ServerUncoreCounterState&)
{
    return ~0ULL;
}

void printTransposed(const PCM::RawPMUConfigs& curPMUConfigs, PCM* m, vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState, vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState, const CsvOutputType outputType)
{
    if (outputType == CsvOutputType::Data)
    {
        for (auto typeEvents : curPMUConfigs)
        {
            const auto& type = typeEvents.first;
            const auto& events = typeEvents.second.programmable;
            const auto& fixedEvents = typeEvents.second.fixed;
            auto printUncoreRows = [&](UncoreMetricFunc metricFunc, const uint32 maxUnit, const std::string & fixedName = std::string("<invalid-fixed-event-name>"), UncoreFixedMetricFunc fixedMetricFunc = nullFixedMetricFunc)
            {
                if (fixedEvents.size())
                {
                    printRowBegin(fixedName, BeforeState[0], AfterState[0], m);
                    for (uint32 s = 0; s < m->getNumSockets(); ++s)
                    {
                        for (uint32 u = 0; u < maxUnit; ++u)
                        {
                            cout << "," << fixedMetricFunc(u, BeforeUncoreState[s], AfterUncoreState[s]);
                        }
                    }
                    cout << "\n";
                }
                uint32 i = 0;
                for (auto event : events)
                {
                    const std::string name = (event.second.empty()) ? (type + "Event" + std::to_string(i)) : event.second;
                    printRowBegin(name, BeforeState[0], AfterState[0], m);
                    for (uint32 s = 0; s < m->getNumSockets(); ++s)
                    {
                        for (uint32 u = 0; u < maxUnit; ++u)
                        {
                            cout << "," << metricFunc(u, i, BeforeUncoreState[s], AfterUncoreState[s]);
                        }
                    }
                    cout << "\n";
                    ++i;
                }
            };
            if (type == "core")
            {
                const char * names[] = {"InstructionsRetired" , "Cycles", "RefCycles"};
                typedef uint64 (*FuncType) (const CoreCounterState& before, const CoreCounterState& after);
                static FuncType func[] = { [](const CoreCounterState& before, const CoreCounterState& after) { return getInstructionsRetired(before, after); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return getCycles(before, after); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return getRefCycles(before, after); } };
                for (const auto event : fixedEvents)
                {
                    for (uint32 cnt = 0; cnt < 3; ++cnt)
                    {
                        if (extract_bits(event.first[0], 4U * cnt, 1U + 4U * cnt))
                        {
                            printRow(event.second.empty() ? names[cnt] : event.second, func[cnt], BeforeState, AfterState, m);
                        }
                    }
                }
                uint32 i = 0;
                for (const auto event : events)
                {
                    const std::string name = (event.second.empty()) ? (type + "Event" + std::to_string(i)) : event.second;
                    printRow(name, [&i](const CoreCounterState& before, const CoreCounterState& after) { return getNumberOfCustomEvents(i, before, after); }, BeforeState, AfterState, m);
                    ++i;
                }
            }
            else if (type == "m3upi")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getM3UPICounter(u, i, before, after); }, (uint32) m->getQPILinksPerSocket());
            }
            else if (type == "xpi" || type == "upi" || type == "qpi")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getXPICounter(u, i, before, after); }, (uint32) m->getQPILinksPerSocket());
            }
            else if (type == "imc")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getMCCounter(u, i, before, after); }, (uint32)m->getMCChannelsPerSocket(),
                                "DRAMClocks", [](const uint32 u, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getDRAMClocks(u, before, after); });
            }
            else if (type == "m2m")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getM2MCounter(u, i, before, after); }, (uint32)m->getMCPerSocket());
            }
            else if (type == "pcu")
            {
                printUncoreRows([](const uint32, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getPCUCounter(i, before, after); }, 1U);
            }
            else if (type == "ubox")
            {
                printUncoreRows([](const uint32, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUBOXCounter(i, before, after); }, 1U,
                    "UncoreClocks", [](const uint32, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreClocks(before, after); });
            }
            else if (type == "cbo" || type == "cha")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getCBOCounter(u, i, before, after); }, (uint32)m->getMaxNumOfCBoxes());
            }
            else if (type == "iio")
            {
                printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getIIOCounter(u, i, before, after); }, (uint32)m->getMaxNumOfIIOStacks());
            }
            else
            {
                std::cerr << "ERROR: unrecognized PMU type \"" << type << "\"\n";
            }
        }
        if (flushLine)
        {
            cout.flush();
        }
    }
}

void print(const PCM::RawPMUConfigs& curPMUConfigs, PCM* m, vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState, vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState, const CsvOutputType outputType)
{
    if (transpose)
    {
        printTransposed(curPMUConfigs, m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, outputType);
        return;
    }
    printDateForCSV(outputType);
    if (BeforeState.size() > 0 && AfterState.size() > 0)
    {
        choose(outputType,
            []() { cout << ","; },
            []() { cout << "ms,"; },
            [&]() { cout << (1000ULL * getInvariantTSC(BeforeState[0], AfterState[0])) / m->getNominalFrequency() << ","; });
    }
    for (auto typeEvents : curPMUConfigs)
    {
        const auto & type = typeEvents.first;
        const auto & events = typeEvents.second.programmable;
        const auto & fixedEvents = typeEvents.second.fixed;
        if (type == "core")
        {
            for (uint32 core = 0; core < m->getNumCores(); ++core)
            {
                if (m->isCoreOnline(core) == false || (show_partial_core_output && ycores.test(core) == false))
                    continue;

                const char* names[] = { "InstructionsRetired" , "Cycles", "RefCycles" };
                const uint64 values[] = { getInstructionsRetired(BeforeState[core], AfterState[core]), getCycles(BeforeState[core], AfterState[core]), getRefCycles(BeforeState[core], AfterState[core]) };
                for (const auto event : fixedEvents)
                {
                    auto print = [&](const std::string & metric, const uint64 value)
                    {
                        choose(outputType,
                            [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << ","; },
                            [&metric]() { cout << metric << ","; },
                            [&value]() { cout << value << ","; });
                    };
                    for (uint32 cnt = 0; cnt < 3; ++cnt)
                    {
                        if (extract_bits(event.first[0], 4U * cnt, 1U + 4U * cnt))
                        {
                            print(event.second.empty() ? names[cnt] : event.second, values[cnt]);
                        }
                    }
                }
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "COREEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getNumberOfCustomEvents(i, BeforeState[core], AfterState[core]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "m3upi")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 l = 0; l < m->getQPILinksPerSocket(); ++l)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "M3UPIEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getM3UPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "xpi" || type == "upi" || type == "qpi")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 l = 0; l < m->getQPILinksPerSocket(); ++l)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "XPIEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getXPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "imc")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 ch = 0; ch < m->getMCChannelsPerSocket(); ++ch)
                {
                    if (fixedEvents.size())
                    {
                        choose(outputType,
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << ","; },
                            [&fixedEvents]() { cout << "DRAMClocks" << fixedEvents[0].second << ","; },
                            [&]() { cout << getDRAMClocks(ch, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    }
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "IMCEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getMCCounter(ch, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "m2m")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 mc = 0; mc < m->getMCPerSocket(); ++mc)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, mc]() { cout << "SKT" << s << "MC" << mc << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "M2MEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getM2MCounter(mc, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "pcu")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "PCUEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getPCUCounter(i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "ubox")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                if (fixedEvents.size())
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&fixedEvents]() { cout << "UncoreClocks" << fixedEvents[0].second << ","; },
                        [&]() { cout << getUncoreClocks(BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                }
                int i = 0;
                for (auto event : events)
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << ","; },
                        [&event, &i]() { if (event.second.empty()) cout << "UBOXEvent" << i << ",";  else cout << event.second << ","; },
                        [&]() { cout << getUBOXCounter(i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                    ++i;
                }
            }
        }
        else if (type == "cbo" || type == "cha")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 cbo = 0; cbo < m->getMaxNumOfCBoxes(); ++cbo)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, cbo]() { cout << "SKT" << s << "C" << cbo << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "CBOEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getCBOCounter(cbo, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "iio")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 stack = 0; stack < m->getMaxNumOfIIOStacks(); ++stack)
                {
                    int i = 0;
                    for (auto event : events)
                    {
                        choose(outputType,
                            [s, stack]() { cout << "SKT" << s << "IIO" << stack << ","; },
                            [&event, &i]() { if (event.second.empty()) cout << "IIOEvent" << i << ",";  else cout << event.second << ","; },
                            [&]() { cout << getIIOCounter(stack, i, BeforeUncoreState[s], AfterUncoreState[s]) << ","; });
                        ++i;
                    }
                }
            }
        }
        else
        {
            std::cerr << "ERROR: unrecognized PMU type \"" << type << "\"\n";
        }
    }
    if (flushLine)
    {
        cout << endl;
    }
    else
    {
        cout << "\n";
    }
}

void printAll(const PCM::RawPMUConfigs& curPMUConfigs, PCM * m, vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState, vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState)
{
    static bool displayHeader = true;
    if (displayHeader)
    {
        print(curPMUConfigs, m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Header1);
        print(curPMUConfigs, m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Header2);
        displayHeader = false;
    }
    print(curPMUConfigs, m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, Data);
}

int main(int argc, char* argv[])
{
    set_signal_handlers();

#ifdef PCM_FORCE_SILENT
    null_stream nullStream1, nullStream2;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#endif

    cerr << "\n";
    cerr << " Processor Counter Monitor: Raw Event Monitoring Utility \n";
    cerr << "\n";

    std::vector<PCM::RawPMUConfigs> PMUConfigs(1);
    double delay = -1.0;
    char* sysCmd = NULL;
    char** sysArgv = NULL;
    MainLoop mainLoop;
    string program = string(argv[0]);

    PCM* m = PCM::getInstance();

    if (argc > 1) do
    {
        argv++;
        argc--;
        if (strncmp(*argv, "--help", 6) == 0 ||
            strncmp(*argv, "-h", 2) == 0 ||
            strncmp(*argv, "/h", 2) == 0)
        {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (strncmp(*argv, "-csv", 4) == 0 ||
            strncmp(*argv, "/csv", 4) == 0)
        {
            string cmd = string(*argv);
            size_t found = cmd.find('=', 4);
            if (found != string::npos) {
                string filename = cmd.substr(found + 1);
                if (!filename.empty()) {
                    m->setOutput(filename);
                }
            }
            continue;
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else if (
            strncmp(*argv, "-f", 2) == 0 ||
            strncmp(*argv, "/f", 2) == 0)
        {
            flushLine = true;
            continue;
        }
        else if (
            strncmp(*argv, "-tr", 3) == 0 ||
            strncmp(*argv, "/tr", 3) == 0)
        {
            transpose = true;
            continue;
        }
        else if (strncmp(*argv, "--yescores", 10) == 0 ||
            strncmp(*argv, "-yc", 3) == 0 ||
            strncmp(*argv, "/yc", 3) == 0)
        {
            argv++;
            argc--;
            show_partial_core_output = true;
            if (*argv == NULL)
            {
                cerr << "Error: --yescores requires additional argument.\n";
                exit(EXIT_FAILURE);
            }
            std::stringstream ss(*argv);
            while (ss.good())
            {
                string s;
                int core_id;
                std::getline(ss, s, ',');
                if (s.empty())
                    continue;
                core_id = atoi(s.c_str());
                if (core_id > MAX_CORES)
                {
                    cerr << "Core ID:" << core_id << " exceed maximum range " << MAX_CORES << ", program abort\n";
                    exit(EXIT_FAILURE);
                }

                ycores.set(atoi(s.c_str()), true);
            }
            if (m->getNumCores() > MAX_CORES)
            {
                cerr << "Error: --yescores option is enabled, but #define MAX_CORES " << MAX_CORES << " is less than  m->getNumCores() = " << m->getNumCores() << "\n";
                cerr << "There is a potential to crash the system. Please increase MAX_CORES to at least " << m->getNumCores() << " and re-enable this option.\n";
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (strncmp(*argv, "-el", 3) == 0 || strncmp(*argv, "/el", 3) == 0)
        {
            argv++;
            argc--;
            if (addEvents(PMUConfigs, *argv) == false)
            {
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (strncmp(*argv, "-e", 2) == 0)
        {
            argv++;
            argc--;
            if (addEvent(PMUConfigs[0], *argv) == false)
            {
                exit(EXIT_FAILURE);
            }

            continue;
        }
        else
            if (CheckAndForceRTMAbortMode(*argv, m))
            {
                continue;
            }
            else if (strncmp(*argv, "--", 2) == 0)
            {
                argv++;
                sysCmd = *argv;
                sysArgv = argv;
                break;
            }
            else
            {
                // any other options positional that is a floating point number is treated as <delay>,
                // while the other options are ignored with a warning issues to stderr
                double delay_input;
                std::istringstream is_str_stream(*argv);
                is_str_stream >> noskipws >> delay_input;
                if (is_str_stream.eof() && !is_str_stream.fail()) {
                    delay = delay_input;
                }
                else {
                    cerr << "WARNING: unknown command-line option: \"" << *argv << "\". Ignoring it.\n";
                    print_usage(program);
                    exit(EXIT_FAILURE);
                }
                continue;
            }
    } while (argc > 1); // end of command line parsing loop

    print_cpu_details();

    size_t nGroups = 0;
    for (const auto group : PMUConfigs)
    {
        if (!group.empty()) ++nGroups;
    }

    cout << "Collecting " << nGroups << " event groups\n";

    if (nGroups > 1)
    {
        transpose = true;
        cout << "Enforcing transposed event output because the number of event groups > 1\n";
    }

    auto programPMUs = [&m](const PCM::RawPMUConfigs & config)
    {
        PCM::ErrorCode status = m->program(config);
        switch (status)
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
            std::cin >> yn;
            if ('y' == yn)
            {
                m->resetPMU();
                cerr << "PMU configuration has been reset. Try to rerun the program again.\n";
            }
            exit(EXIT_FAILURE);
        default:
            cerr << "Access to Processor Counter Monitor has denied (Unknown error).\n";
            exit(EXIT_FAILURE);
        }
    };

    SystemCounterState SysBeforeState, SysAfterState;
    vector<CoreCounterState> BeforeState, AfterState;
    vector<SocketCounterState> DummySocketStates;
    vector<ServerUncoreCounterState> BeforeUncoreState, AfterUncoreState;
    BeforeUncoreState.resize(m->getNumSockets());
    AfterUncoreState.resize(m->getNumSockets());

    if ((sysCmd != NULL) && (delay <= 0.0)) {
        // in case external command is provided in command line, and
        // delay either not provided (-1) or is zero
        m->setBlocked(true);
    }
    else {
        m->setBlocked(false);
    }


    if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;

    cerr << "Update every " << delay << " seconds\n";

    std::cout.precision(2);
    std::cout << std::fixed;

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    mainLoop([&]()
    {
         for (const auto group : PMUConfigs)
         {
                if (group.empty()) continue;
                programPMUs(group);
                m->getAllCounterStates(SysBeforeState, DummySocketStates, BeforeState);
                for (uint32 s = 0; s < m->getNumSockets(); ++s)
                {
                    BeforeUncoreState[s] = m->getServerUncoreCounterState(s);
                }

                calibratedSleep(delay, sysCmd, mainLoop, m);

                m->getAllCounterStates(SysAfterState, DummySocketStates, AfterState);
                for (uint32 s = 0; s < m->getNumSockets(); ++s)
                {
                    AfterUncoreState[s] = m->getServerUncoreCounterState(s);
                }

                //cout << "Time elapsed: " << dec << fixed << AfterTime - BeforeTime << " ms\n";
                //cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";

                printAll(group, m, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState);
                m->cleanup(true);
         }
         if (m->isBlocked()) {
             // in case PCM was blocked after spawning child application: break monitoring loop here
             return false;
         }
         return true;
    });
    exit(EXIT_SUCCESS);
}
