// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2023, Intel Corporation

 /*!     \file pcm-raw.cpp
         \brief Example of using CPU counters: implements a performance counter monitoring utility with raw events interface
   */
#include <iostream>
#ifdef _MSC_VER
#define strtok_r strtok_s
#include <windows.h>
#include "windows/windriver.h"
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

#if PCM_SIMDJSON_AVAILABLE
#include "simdjson.h"
#endif

#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include <vector>
#define PCM_DELAY_MIN 0.015 // 15 milliseconds is practical on most modern CPUs
#define MAX_CORES 4096

using namespace std;
using namespace pcm;

void print_usage(const string & progname)
{
    cout << "\n Usage: \n " << progname
        << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cout << "   <delay>                               => time interval to sample performance counters.\n";
    cout << "                                            If not specified, or 0, with external program given\n";
    cout << "                                            will read counters only after external program finishes\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help      | /h               => print this help and exit\n";
    cout << "  -silent                                => silence information output and print only measurements\n";
    cout << "  --version                              => print application version\n";
    cout << "  -e event1 [-e event2] [-e event3] ..   => list of custom events to monitor\n";
    cout << "  -pid PID | /pid PID                    => collect core metrics only for specified process ID\n";
    cout << "  -r    | --reset     | /reset           => reset PMU configuration (at your own risk)\n";
    cout << "  -csv[=file.csv]     | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                            to a file, in case filename is provided\n";
    cout << "  -json[=file.json]   | /json[=file.json]  => output json format to screen or\n"
         << "                                              to a file, in case filename is provided\n";
    cout << "  -out filename       | /out filename    => write all output (stdout and stderr) to specified file\n";
    cout << "  event description example: -e core/config=0x30203,name=LD_BLOCKS.STORE_FORWARD/ -e core/fixed,config=0x333/ \n";
    cout << "                             -e cha/config=0,name=UNC_CHA_CLOCKTICKS/ -e imc/fixed,name=DRAM_CLOCKS/\n";
#ifdef PCM_SIMDJSON_AVAILABLE
    cout << "                             -e NAME where the NAME is an event from https://github.com/intel/perfmon event lists\n";
    cout << "  -ep path | /ep path                    => path to event list directory (default is the current directory)\n";
#endif
    cout << "  -yc   | --yescores  | /yc              => enable specific cores to output\n";
    cout << "  -f    | /f                             => enforce flushing each line for interactive output\n";
    cout << "  -i[=number] | /i[=number]              => allow to determine number of iterations\n";
    cout << "  -tr | /tr                              => transpose output (print single event data in a row)\n";
    cout << "  -ext | /ext                            => add headers to transposed output and extend printout to match it\n";
    cout << "  -single-header | /single-header        => headers for transposed output are merged into single header\n";
    cout << "  -s  | /s                               => print a sample separator line between samples in transposed output\n";
    cout << "  -v  | /v                               => verbose mode (print additional diagnostic messages)\n";
    cout << "  -l                                     => use locale for printing values, calls -tab for readability\n";
    cout << "  -tab                                   => replace default comma separator with tab\n";
    cout << "  -el event_list.txt | /el event_list.txt  => read event list from event_list.txt file, \n";
    cout << "                                              each line represents an event,\n";
    cout << "                                              event groups are separated by a semicolon\n";
    cout << "  -edp | /edp                            => 'edp' output mode\n";
    print_help_force_rtm_abort_mode(41);
    cout << " Examples:\n";
    cout << "  " << progname << " 1                   => print counters every second without core and socket output\n";
    cout << "  " << progname << " 0.5 -csv=test.log   => twice a second save counter values to test.log in CSV format\n";
    cout << "  " << progname << " /csv 5 2>/dev/null  => one sample every 5 seconds, and discard all diagnostic output\n";
    cout << "\n";
}

bool verbose = false;
double defaultDelay = 1.0; // in seconds

PCM::RawEventConfig initCoreConfig()
{
    return PCM::RawEventConfig{ {0,0,0,
        PCM::ExtendedCustomCoreEventDescription::invalidMsrValue(),PCM::ExtendedCustomCoreEventDescription::invalidMsrValue(), 0
        },
        "" };
}

void printEvent(const std::string & pmuName, const bool fixed, const PCM::RawEventConfig & config)
{
    cerr << "parsed " << (fixed ? "fixed " : "") << " " << pmuName << " event: \"" << hex << config.second << "\" : {" << hex <<
        "0x" << config.first[0] <<
        ", 0x" << config.first[1] <<
        ", 0x" << config.first[2] <<
        ", 0x" << config.first[3] <<
        ", 0x" << config.first[4] <<
        ", 0x" << config.first[5] <<
        "}\n" << dec;
}

void lowerCase(std::string & str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
#ifdef _MSC_VER
        return std::tolower(c, std::locale());
#else
        return std::tolower(c); // std::locale has some bad_cast issues in g++
#endif
    });
}

enum AddEventStatus
{
    OK,
    Failed,
    FailedTooManyEvents
};

bool tooManyEvents(const std::string & pmuName, const int event_pos, const std::string& fullEventStr)
{
    if (isRegisterEvent(pmuName))
    {
        return false;
    }
    PCM* m = PCM::getInstance();
    assert(m);
    const int maxCounters = (pmuName == "core" || pmuName == "atom") ? m->getMaxCustomCoreEvents() : ServerUncoreCounterState::maxCounters;
    if (event_pos >= maxCounters)
    {
        std::cerr << "ERROR: trying to add event " << fullEventStr << " at position " << event_pos << " of an event group, which exceeds the max num possible (" << maxCounters << ").\n";
        return true;
    }
    return false;
}

#ifdef PCM_SIMDJSON_AVAILABLE
using namespace simdjson;

std::vector<std::shared_ptr<simdjson::dom::parser> > JSONparsers;
std::unordered_map<std::string, simdjson::dom::object> PMUEventMapJSON;
std::vector<std::unordered_map<std::string, std::vector<std::string>>> PMUEventMapsTSV;
std::shared_ptr<simdjson::dom::element> PMURegisterDeclarations;
std::string eventFileLocationPrefix = ".";

bool parse_tsv(const string &path) {
    bool col_names_parsed = false;
    int event_name_pos = -1;
    ifstream inFile;
    string line;
    inFile.open(path);
    std::unordered_map<std::string, std::vector<std::string>> PMUEventMap;

    while (getline(inFile, line)) {
        if (line.size() == 1 && line[0] == '\n')
            continue;
        // Trim whitespaces left/right // MOVE to utils
        auto ws_left_count = 0;
        for (size_t i = 0 ; i < line.size() ; i++) {
            if (line[i] == ' ') ws_left_count++;
            else break;
        }
        auto ws_right_count = 0;
        for (size_t i = line.size() - 1 ; i > 0 ; i--) {
            if (line[i] == ' ') ws_right_count++;
            else break;
        }
        line.erase(0, ws_left_count);
        line.erase(line.size() - ws_right_count, ws_right_count);
        if (line[0] == '#')
            continue;
        if (!col_names_parsed) {
            // Consider first row as Column name row
            std::vector<std::string> col_names = split(line, '\t');
            PMUEventMap["COL_NAMES"] = col_names;
            const auto event_name_it = std::find(col_names.begin(), col_names.end(), "EventName");
            if (event_name_it == col_names.end()) {
                cerr << "ERROR: First row does not contain EventName\n";
                inFile.close();
                return false;
            }
            event_name_pos = (int)(event_name_it - col_names.begin());
            col_names_parsed = true;
            continue;
        }
        std::vector<std::string> entry = split(line, '\t');
        std::string event_name = entry[event_name_pos];
        PMUEventMap[event_name] = entry;
    }
    inFile.close();
    PMUEventMapsTSV.push_back(PMUEventMap);
    return true;
}

bool initPMUEventMap()
{
    static bool inited = false;

    if (inited == true)
    {
        return true;
    }
    inited = true;
    const auto mapfile = "mapfile.csv";
    const auto mapfilePath = eventFileLocationPrefix + "/"  + mapfile;
    std::ifstream in(mapfilePath);
    std::string line, item;

    if (!in.is_open())
    {
        cerr << "ERROR: File " << mapfilePath << " can't be open. \n";
        cerr << "       Use -ep <pcm_source_directory>/perfmon option if you cloned PCM source repository recursively with submodules,\n";
        cerr << "       or run 'git clone https://github.com/intel/perfmon' to download the perfmon event repository and use -ep <perfmon_directory> option\n";
        cerr << "       or download the file from https://raw.githubusercontent.com/intel/perfmon/main/" << mapfile << " \n";
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
    assert(FMSPos >= 0);
    assert(FilenamePos >= 0);
    assert(EventTypetPos >= 0);
    const std::string ourFMS = PCM::getInstance()->getCPUFamilyModelString();
    // cout << "Our FMS: " << ourFMS << "\n";
    std::multimap<std::string, std::string> eventFiles;
    cerr << "Matched event files:\n";
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
            cerr << tokens[FMSPos] << " " << tokens[EventTypetPos] << " " << tokens[FilenamePos] << "\n";
            eventFiles.insert(std::make_pair(tokens[EventTypetPos], tokens[FilenamePos]));
        }
    }
    in.close();

    if (eventFiles.empty())
    {
        cerr << "ERROR: CPU " << ourFMS << " not found in " << mapfile << "\n";
        return false;
    }

    for (const auto& evfile : eventFiles)
    {
        std::string path;
        auto printError = [&evfile]()
        {
            cerr << "Make sure you have downloaded " << evfile.second << " from https://raw.githubusercontent.com/intel/perfmon/main/" + evfile.second + " \n";
        };
        try {

            cerr << evfile.first << " " << evfile.second << "\n";

            if (evfile.first == "core" || evfile.first == "uncore" || evfile.first == "uncore experimental")
            {
                const std::string path1 = eventFileLocationPrefix + evfile.second;
                const std::string path2 = eventFileLocationPrefix + evfile.second.substr(evfile.second.rfind('/'));

                if (std::ifstream(path1).good())
                {
                    path = path1;
                }
                else if (std::ifstream(path2).good())
                {
                    path = path2;
                }
                else
                {
                    std::cerr << "ERROR: Can't open event file at location " << path1 << " or " << path2 << "\n";
                    printError();
                    return false;
                }

                if (path.find(".json") != std::string::npos) {
                    JSONparsers.push_back(std::make_shared<simdjson::dom::parser>());
                    auto JSONObjects = JSONparsers.back()->load(path);
                    if (JSONObjects["Header"].error() != NO_SUCH_FIELD)
                    {
                        JSONObjects = JSONObjects["Events"];
                    }
                    for (simdjson::dom::object eventObj : JSONObjects) {
                        // cout << "Event ----------------\n";
                        const std::string EventName{eventObj["EventName"].get_c_str()};
                        if (EventName.empty())
                        {
                            cerr << "Did not find EventName in JSON object:\n";
                            for (const auto& keyValue : eventObj)
                            {
                                cout << "key: " << keyValue.key << " value: " << keyValue.value << "\n";
                            }
                        }
                        else
                        {
                            PMUEventMapJSON[EventName] = eventObj;
                        }
                    }
                } else if (path.find(".tsv") != std::string::npos) {
                    if (!parse_tsv(path))
                        return false;
                } else {
                    cerr << "ERROR: Could not determine Event file type (JSON/TSV)\n";
                    return false;
                }
            }
        }
        catch (std::exception& e)
        {
            cerr << "Error while opening and/or parsing " << path << " : " << e.what() << "\n";
           printError();
            return false;
        }
    }
    if (PMUEventMapJSON.empty() && PMUEventMapsTSV.empty())
    {
        return false;
    }

    return true;
}

class EventMap {
public:
    static bool isEvent(const std::string &eventStr) {
        if (PMUEventMapJSON.find(eventStr) != PMUEventMapJSON.end())
            return true;
        for (const auto &EventMapTSV : PMUEventMapsTSV) {
            if (EventMapTSV.find(eventStr) != EventMapTSV.end())
                return true;
        }
        return false;
    }

    static bool isField(const std::string &eventStr, const std::string event) {
        if (PMUEventMapJSON.find(eventStr) != PMUEventMapJSON.end()) {
            const auto eventObj = PMUEventMapJSON[eventStr];
            const auto unitObj = eventObj[event];
            return unitObj.error() != NO_SUCH_FIELD;
        }

        for (auto &EventMapTSV : PMUEventMapsTSV) {
            if (EventMapTSV.find(eventStr) != EventMapTSV.end()) {
                const auto &col_names = EventMapTSV["COL_NAMES"];
                const auto event_name_it = std::find(col_names.begin(), col_names.end(), event);
                if (event_name_it != col_names.end()) {
                    const size_t event_name_pos = event_name_it - col_names.begin();
                    return event_name_pos < EventMapTSV[eventStr].size();
                }
            }
        }

        return false;
    }

    static std::string getField(const std::string &eventStr, const std::string &event) {
        std::string res;

        if (PMUEventMapJSON.find(eventStr) != PMUEventMapJSON.end()) {
            const auto eventObj = PMUEventMapJSON[eventStr];
            const auto unitObj = eventObj[event];
            return std::string(unitObj.get_c_str());
        }

        for (auto &EventMapTSV : PMUEventMapsTSV) {
            if (EventMapTSV.find(eventStr) != EventMapTSV.end()) {
                const auto col_names = EventMapTSV["COL_NAMES"];
                const auto event_name_it = std::find(col_names.begin(), col_names.end(), event);
                if (event_name_it != col_names.end()) {
                    const auto event_name_pos = event_name_it - col_names.begin();
                    res = EventMapTSV[eventStr][event_name_pos];
                }
            }
        }
        return res;
    }

    static void print_event(const std::string &eventStr) {
        if (PMUEventMapJSON.find(eventStr) != PMUEventMapJSON.end()) {
            const auto eventObj = PMUEventMapJSON[eventStr];
            for (const auto & keyValue : eventObj)
                std::cout << keyValue.key << " : " << keyValue.value << "\n";
            return;
        }

        for (auto &EventMapTSV : PMUEventMapsTSV) {
            if (EventMapTSV.find(eventStr) != EventMapTSV.end()) {
                const auto &col_names = EventMapTSV["COL_NAMES"];
                const auto event = EventMapTSV[eventStr];
                if (EventMapTSV.find(eventStr) != EventMapTSV.end()) {
                    for (size_t i = 0 ; i < col_names.size() ; i++)
                        std::cout << col_names[i] << " : " << event[i] << "\n";
                    return;
                }
            }
        }

    }
};

AddEventStatus addEventFromDB(PCM::RawPMUConfigs& curPMUConfigs, string fullEventStr)
{
    if (initPMUEventMap() == false)
    {
        cerr << "ERROR: PMU Event map can not be initialized\n";
        return AddEventStatus::Failed;
    }
    // cerr << "Parsing event " << fullEventStr << "\n";
    // cerr << "size: " << fullEventStr.size() << "\n";
    while (fullEventStr.empty() == false && fullEventStr.back() == ' ')
    {
        fullEventStr.resize(fullEventStr.size() - 1); // remove trailing spaces
    }
    while (fullEventStr.empty() == false && fullEventStr.front() == ' ')
    {
        fullEventStr = fullEventStr.substr(1); // remove leading spaces
    }
    if (fullEventStr.empty())
    {
        return AddEventStatus::OK;
    }
    const auto EventTokens = split(fullEventStr, ':');
    assert(!EventTokens.empty());
    auto mod = EventTokens.begin();
    ++mod;

    const auto eventStr = EventTokens[0];

    // cerr << "size: " << eventStr.size() << "\n";
    PCM::RawEventConfig config = { {0,0,0,0,0}, "" };
    std::string pmuName;

    auto unsupported = [&]()
    {
        cerr << "Unsupported event modifier: " << *mod << " in event " << fullEventStr << "\n";
    };

    if (eventStr == "MSR_EVENT")
    {
        while (mod != EventTokens.end())
        {
            auto assignment = split(*mod, '=');
            for (auto& s : assignment)
            {
                lowerCase(s);
            }
            if (assignment.size() == 2 && assignment[0] == "msr")
            {
                config.first[PCM::MSREventPosition::index] = read_number(assignment[1].c_str());
            }
            else if (assignment.size() == 2 && assignment[0] == "type")
            {
                if (assignment[1] == "static")
                {
                    config.first[PCM::MSREventPosition::type] = PCM::MSRType::Static;
                } else if (assignment[1] == "freerun")
                {
                    config.first[PCM::MSREventPosition::type] = PCM::MSRType::Freerun;
                }
                else
                {
                    unsupported();
                    return AddEventStatus::Failed;
                }
            }
            else if (assignment.size() == 2 && assignment[0] == "scope")
            {
                if (assignment[1] == "package")
                {
                    pmuName = "package_msr";
                }
                else if (assignment[1] == "thread")
                {
                    pmuName = "thread_msr";
                }
                else
                {
                    unsupported();
                    return AddEventStatus::Failed;
                }
            }
            else
            {
                unsupported();
                return AddEventStatus::Failed;
            }
            ++mod;
        }
        if (pmuName.empty())
        {
            cerr << "ERROR: scope is not defined in event " << fullEventStr << ". Possible values: package, thread\n";
            return AddEventStatus::Failed;
        }

        config.second = fullEventStr;
        curPMUConfigs[pmuName].fixed.push_back(config);
        return AddEventStatus::OK;
    }

    if (!EventMap::isEvent(eventStr))
    {
        cerr << "ERROR: event " << eventStr << " could not be found in event database. Ignoring the event.\n";
        return AddEventStatus::OK;
    }

    bool fixed = false;
    static size_t offcoreEventIndex = 0;
    auto * pcm = PCM::getInstance();
    assert(pcm);

    int stepping = pcm->getCPUStepping();
    assert(stepping >= 0);
    std::string path, err_msg;

    for (; stepping >= 0; --stepping)
    {
        try
        {
            path = std::string("PMURegisterDeclarations/") + pcm->getCPUFamilyModelString(pcm->getCPUFamily(), pcm->getInternalCPUModel(), (uint32)stepping) + ".json";

            std::ifstream in(path);
            if (!in.is_open())
            {
                const auto alt_path = getInstallPathPrefix() + path;
                in.open(alt_path);
                if (!in.is_open())
                {
                    err_msg = std::string("event file ") + path + " or " + alt_path + " is not available.";
                    throw std::invalid_argument(err_msg);
                }
                path = alt_path;
            }
            in.close();
            break;
        }
        catch (std::invalid_argument & e)
        {
            std::cerr << "INFO: " << e.what() << "\n";
            path.clear();
        }
    }

    if (path.empty())
    {
        throw std::invalid_argument(err_msg);
    }

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
            return AddEventStatus::Failed;
        }
    }

    static std::map<std::string, std::string> pmuNameMap = {
        {std::string("cbo"), std::string("cha")},
        {std::string("b2cmi"), std::string("m2m")},
        {std::string("upi"), std::string("xpi")},
        {std::string("upi ll"), std::string("xpi")},
        {std::string("b2upi"), std::string("m3upi")},
        {std::string("qpi"), std::string("xpi")},
        {std::string("qpi ll"), std::string("xpi")}
    };

    if (!EventMap::isField(eventStr, "Unit"))
    {
        pmuName = "core";
        config = initCoreConfig();
    }
    else
    {
        std::string unit = EventMap::getField(eventStr, "Unit");
        lowerCase(unit);
        // std::cout << eventStr << " is uncore event for unit " << unit << "\n";
        pmuName = (pmuNameMap.find(unit) == pmuNameMap.end()) ? unit : pmuNameMap[unit];
    }

    config.second = fullEventStr;

    if (1)
    {
        // cerr << "pmuName: " << pmuName << " full event "<< fullEventStr << " \n";
        std::string CounterStr = EventMap::getField(eventStr, "Counter");
        // cout << "Counter: " << CounterStr << "\n";
        int fixedCounter = -1;
        fixed = (pcm_sscanf(CounterStr) >> s_expect("Fixed counter ") >> fixedCounter) ? true : false;
        if (!fixed){
            bool counter_match=false;
            std::stringstream ss(CounterStr);
            // to get current event position
            int event_pos = curPMUConfigs[pmuName].programmable.size();
            // loop through counter string and check if event pos matches any counter values
            for (int i = 0; ss >> i;) {
                if(event_pos == i)
                    counter_match = true;  
                if (ss.peek() == ',')
                    ss.ignore();
            }
            if(!counter_match)
            {
                std::cerr << "ERROR: position of " << fullEventStr << " event in the command is " << event_pos<<" but the supported counters are "<<CounterStr<<"\n";
                return AddEventStatus::Failed;
            }
            if (tooManyEvents(pmuName, event_pos, fullEventStr))
            {
                return AddEventStatus::FailedTooManyEvents;
            }
        }
        bool offcore = false;
        if (EventMap::isField(eventStr, "Offcore"))
        {
            const std::string offcoreStr = EventMap::getField(eventStr, "Offcore");
            offcore = (offcoreStr == "1");
        }
        if (pmuName == "core" && curPMUConfigs[pmuName].programmable.empty() && fixed == false)
        {
            // on first programmable core PMU event
            offcoreEventIndex = 0; // reset offcore event index
        }

        try {
            auto setConfig = [](PCM::RawEventConfig & config, const simdjson::dom::object& fieldDescriptionObj, const uint64 value, const int64_t position)
            {
                const auto cfg = uint64_t(fieldDescriptionObj["Config"]);
                if (cfg >= config.first.size()) throw std::runtime_error("Config field value is out of bounds");
                const auto width = uint64_t(fieldDescriptionObj["Width"]);
                assert(width <= 64);
                config.first[cfg] = insertBits(config.first[cfg], value, position, width);
            };
            auto PMUObj = (*PMURegisterDeclarations)[pmuName];
            if (PMUObj.error() == NO_SUCH_FIELD)
            {
                cerr << "ERROR: PMU \"" << pmuName << "\" not found for event " << fullEventStr << " in " << path << ", ignoring the event.\n";
                return AddEventStatus::OK;
            }
            simdjson::dom::object PMUDeclObj;
            if (fixed)
            {
                PMUDeclObj = (*PMURegisterDeclarations)[pmuName][std::string("fixed") + std::to_string(fixedCounter)].get_object();
            }
            else
            {
                PMUDeclObj = (*PMURegisterDeclarations)[pmuName]["programmable"].get_object();
            }
            auto& myPMUConfigs = fixed ? curPMUConfigs[pmuName].fixed : curPMUConfigs[pmuName].programmable;
            simdjson::dom::object MSRObject;
            auto setMSRValue = [&setConfig,&MSRObject,&config,&myPMUConfigs](const string & valueStr)
            {
                const auto value = read_number(valueStr.c_str());
                const auto position = int64_t(MSRObject["Position"]);
                // update the first event
                setConfig(myPMUConfigs.empty() ? config : myPMUConfigs.front(), MSRObject, value, position);
                // update the current as well for display
                setConfig(config, MSRObject, value, position);
            };
            for (const auto & registerKeyValue : PMUDeclObj)
            {
                // cout << "Setting " << registerKeyValue.key << " : " << registerKeyValue.value << "\n";
                simdjson::dom::object fieldDescriptionObj = registerKeyValue.value;
                // cout << "   config: " << uint64_t(fieldDescriptionObj["Config"]) << "\n";
                // cout << "   Position: " << uint64_t(fieldDescriptionObj["Position"]) << "\n";
                const std::string fieldNameStr{ registerKeyValue.key.begin(), registerKeyValue.key.end() };
                if (fieldNameStr == "MSRIndex")
                {
                    string fieldValueStr = EventMap::getField(eventStr, fieldNameStr);
                    // cout << "MSR field " << fieldNameStr << " value is " << fieldValueStr << " (" << read_number(fieldValueStr.c_str()) << ") offcore=" << offcore << "\n";
                    lowerCase(fieldValueStr);
                    if (fieldValueStr == "0" || fieldValueStr == "0x00")
                    {
                        continue;
                    }
                    auto MSRIndexStr = fieldValueStr;
                    if (offcore)
                    {
                        const auto MSRIndexes = split(MSRIndexStr, ',');
                        if (offcoreEventIndex >= MSRIndexes.size())
                        {
                            std::cerr << "ERROR: too many offcore events specified (max is " << MSRIndexes.size() << "). Ignoring " << fullEventStr << " event\n";
                            return AddEventStatus::OK;
                        }
                        MSRIndexStr = MSRIndexes[offcoreEventIndex];
                    }
                    // cout << " MSR field " << fieldNameStr << " value is " << MSRIndexStr << " (" << read_number(MSRIndexStr.c_str()) << ") offcore=" << offcore << "\n";
                    MSRObject = registerKeyValue.value[MSRIndexStr];
                    const string msrValueStr = EventMap::getField(eventStr, "MSRValue");
                    setMSRValue(msrValueStr);
                    continue;
                }
                const int64_t position = int64_t(fieldDescriptionObj["Position"]);
                if (position == -1)
                {
                    continue; // field ignored
                }
                if (!EventMap::isField(eventStr, fieldNameStr))
                {
                    // cerr << fieldNameStr << " not found\n";
                    if (fieldDescriptionObj["DefaultValue"].error() == NO_SUCH_FIELD)
                    {
                        cerr << "ERROR: DefaultValue not provided for field \"" << fieldNameStr << "\" in " << path << "\n";
                        return AddEventStatus::Failed;
                    }
                    else
                    {
                        const auto cfg = uint64_t(fieldDescriptionObj["Config"]);
                        if (cfg >= config.first.size()) throw std::runtime_error("Config field value is out of bounds");
                        config.first[cfg] |= uint64_t(fieldDescriptionObj["DefaultValue"]) << position;
                    }
                }
                else
                {
                    std::string fieldValueStr = EventMap::getField(eventStr, fieldNameStr);

                    fieldValueStr.erase(std::remove(fieldValueStr.begin(), fieldValueStr.end(), '\"'), fieldValueStr.end());
                    if (offcore && fieldNameStr == "EventCode")
                    {
                        const auto offcoreCodes = split(fieldValueStr,',');
                        if (offcoreEventIndex >= offcoreCodes.size())
                        {
                            std::cerr << "ERROR: too many offcore events specified (max is " << offcoreCodes.size() << "). Ignoring " << fullEventStr << " event\n";
                            return AddEventStatus::OK;
                        }
                        fieldValueStr = offcoreCodes[offcoreEventIndex];
                    }
                    // cout << " field " << fieldNameStr << " value is " << fieldValueStr << " (" << read_number(fieldValueStr.c_str()) << ") offcore=" << offcore << "\n";
                    setConfig(config, fieldDescriptionObj, read_number(fieldValueStr.c_str()), position);
                }
            }

            auto setField = [&PMUDeclObj, &config, &setConfig](const char* field, const uint64 value)
            {
                const auto pos = int64_t(PMUDeclObj[field]["Position"]);
                setConfig(config, PMUDeclObj[field], value, pos);
            };

            std::regex CounterMaskRegex("c(0x[0-9a-fA-F]+|[[:digit:]]+)");
            std::regex UmaskRegex("u(0x[0-9a-fA-F]+|[[:digit:]]+)");
            std::regex EdgeDetectRegex("e(0x[0-9a-fA-F]+|[[:digit:]]+)");
            std::regex AnyThreadRegex("amt(0x[0-9a-fA-F]+|[[:digit:]]+)");
            std::regex InvertRegex("i(0x[0-9a-fA-F]+|[[:digit:]]+)");
            while (mod != EventTokens.end())
            {
                const auto assignment = split(*mod, '=');
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
                else if (*mod == "percore")
                {
                    unsupported();
                    return AddEventStatus::OK;
                }
                else if (*mod == "perf_metrics")
                {
                    setField("PerfMetrics", 1);
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
                else if (std::regex_match(mod->c_str(), AnyThreadRegex))
                {
                    // AnyThread modifier
                    const std::string Str{ mod->begin() + 1, mod->end() };
                    setField("AnyThread", read_number(Str.c_str()));
                }
                else if (std::regex_match(mod->c_str(), InvertRegex))
                {
                    // Invert modifier
                    const std::string Str{ mod->begin() + 1, mod->end() };
                    setField("Invert", read_number(Str.c_str()));
                }
                else if (std::regex_match(mod->c_str(), UmaskRegex))
                {
                    // UMask modifier
                    const std::string Str{ mod->begin() + 1, mod->end() };
                    setField("UMask", read_number(Str.c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "request")
                {
                    unsupported();
                    return AddEventStatus::OK;
                }
                else if (assignment.size() == 2 && assignment[0] == "response")
                {
                    unsupported();
                    return AddEventStatus::OK;
                }
                else if (assignment.size() == 2 && assignment[0] == "filter0")
                {
                    setField("Filter0", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "filter1")
                {
                    setField("Filter1", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "opc")
                {
                    setField("OPC", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "nc")
                {
                    setField("NC", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "isoc")
                {
                    setField("ISOC", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "state")
                {
                    setField("State", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "t")
                {
                    setField("Threshold", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "tid")
                {
                    setField("TIDEnable", 1);
                    setField("TID", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "umask_ext")
                {
                    setField("UMaskExt", read_number(assignment[1].c_str()));
                }
                else if (assignment.size() == 2 && assignment[0] == "ocr_msr_val")
                {
                    setMSRValue(assignment[1]);
                }
                else
                {
                    unsupported();
                    return AddEventStatus::Failed;
                }
                ++mod;
            }
            if (offcore)
            {
                ++offcoreEventIndex;
            }
            myPMUConfigs.push_back(config);
        }
        catch (std::exception& e)
        {
            cerr << "Error while setting a register field for event " << fullEventStr << " : " << e.what() << "\n";
            EventMap::print_event(eventStr);
            return AddEventStatus::Failed;
        }
    }

    /*
    for (const auto& keyValue : eventObj)
    {
        cout << keyValue.key << " : " << keyValue.value << "\n";
    }
    */

    printEvent(pmuName, fixed, config);

    return AddEventStatus::OK;
}

#endif

AddEventStatus addEvent(PCM::RawPMUConfigs & curPMUConfigs, string eventStr)
{
    if (eventStr.empty())
    {
        return AddEventStatus::OK;
    }
#ifdef PCM_SIMDJSON_AVAILABLE
    if (eventStr.find('/') == string::npos)
    {
        return addEventFromDB(curPMUConfigs, eventStr);
    }
#endif
    PCM::RawEventConfig config = { {0,0,0,0,0}, "" };
    const auto typeConfig = split(eventStr, '/');
    if (typeConfig.size() < 2)
    {
#ifndef PCM_SIMDJSON_AVAILABLE
        cerr << "WARNING: pcm-raw is compiled without simdjson library (check cmake output). Collecting events by names from json event lists is not supported.\n";
#endif
        cerr << "ERROR: wrong syntax in event description \"" << eventStr << "\"\n";
        return AddEventStatus::Failed;
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
        return AddEventStatus::Failed;
    }
    if (pmuName == "core" || pmuName == "atom")
    {
        config = initCoreConfig();
    }
    const auto configArray = split(configStr, ',');
    bool fixed = false;
    for (const auto & item : configArray)
    {
        if (match(item, "config=", &config.first[0]))
        {
            // matched and initialized config 0
        }
        else if (match(item, "config1=", &config.first[1]))
        {
            // matched and initialized config 1
        }
        else if (match(item, "config2=", &config.first[2]))
        {
            // matched and initialized config 2
        }
        else if (match(item, "config3=", &config.first[3]))
        {
            // matched and initialized config 3
        }
        else if (match(item, "config4=", &config.first[4]))
        {
            // matched and initialized config 4
        }
        else if (match(item, "config5=", &config.first[5]))
        {
            // matched and initialized config 5
        }
        else if (match(item, "width=", &config.first[PCM::PCICFGEventPosition::width]))
        {
            // matched and initialized config 5 (width)
        }
        else if (pcm_sscanf(item) >> s_expect("name=") >> setw(255) >> config.second)
        {
            // matched and initialized name
            if (check_for_injections(config.second))
                return AddEventStatus::Failed;
        }
        else if (item == "fixed")
        {
            fixed = true;
        }
        else
        {
            cerr << "ERROR: unknown token " << item << " in event description \"" << eventStr << "\"\n";
            return AddEventStatus::Failed;
        }
    }
    printEvent(pmuName, fixed, config);
    if (fixed == false && tooManyEvents(pmuName, curPMUConfigs[pmuName].programmable.size(), eventStr))
    {
        return AddEventStatus::FailedTooManyEvents;
    }
    if (fixed)
        curPMUConfigs[pmuName].fixed.push_back(config);
    else
        curPMUConfigs[pmuName].programmable.push_back(config);
    return AddEventStatus::OK;
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
            cerr << "Adding new group \n";
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
        const auto status = addEvent(curConfig, line);
        switch (status)
        {
        case AddEventStatus::Failed:
            return false;
        case AddEventStatus::FailedTooManyEvents:
            cerr << "Failed to add event due to a too large group. Trying to split the event group.\n";
            doFinishGroup();
            if (addEvent(curConfig, line) != AddEventStatus::OK)
            {
                return false;
            }
            break;
        case AddEventStatus::OK:
            // all is fine
            break;
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
bool extendPrintout = false;
bool singleHeader = false;
std::string separator = ",";
const std::string jsonSeparator = "\":";
bool sampleSeparator = false;
bool outputToJson = false;

struct PrintOffset {
    const std::string entry;
    int start;
    int end;
};

std::vector<PrintOffset> printOffsets;
std::vector<std::string> printedBlocks;

int getPrintOffsetIdx(const std::string &value) {
    for (size_t i = 0 ; i < printOffsets.size() ; i++) {
        if (printOffsets[i].entry == value)
            return (int)i;
    }
    return -1;
}

void printNewLine(const CsvOutputType outputType) {
    if (outputType == Data)
        cout << "\n";
    else if (outputType == Json)
        cout << "}\n";
}

void printRowBeginCSV(const std::string & EventName, const CoreCounterState & BeforeState, const CoreCounterState & AfterState, PCM* m)
{
    printDateForCSV(CsvOutputType::Data, separator);
    cout << EventName << separator << (1000ULL * getInvariantTSC(BeforeState, AfterState)) / m->getNominalFrequency() << separator << getInvariantTSC(BeforeState, AfterState);
}

void printRowBeginJson(const std::string & EventName, const CoreCounterState & BeforeState, const CoreCounterState & AfterState, PCM* m)
{
    cout << "{\"";
    printDateForJson(separator, jsonSeparator);
    cout << "Event" << jsonSeparator << "\"" << EventName << "\"" << separator << "ms" << jsonSeparator << (1000ULL * getInvariantTSC(BeforeState, AfterState)) / m->getNominalFrequency()
        << separator << "InvariantTSC" << jsonSeparator << getInvariantTSC(BeforeState, AfterState);
}

void printRowBegin(const std::string & EventName, const CoreCounterState & BeforeState, const CoreCounterState & AfterState, PCM* m, const CsvOutputType outputType, PrintOffset& printOffset) {
    if (outputType == Data) {
        printRowBeginCSV(EventName, BeforeState, AfterState, m);
        for (int i = 0 ; i < printOffset.start ; i++)
            std::cout << separator;
    } else if (outputType == Json) {
        printRowBeginJson(EventName, BeforeState, AfterState, m);
    }
}

template <class MetricFunc>
void printRow(const std::string & EventName, MetricFunc metricFunc, const std::vector<CoreCounterState>& BeforeState, const std::vector<CoreCounterState>& AfterState, PCM* m,
    const CsvOutputType outputType, PrintOffset& printOffset, const pcm::TopologyEntry::CoreType & coreType, const std::string & pmuType)
{
    printRowBegin(EventName, BeforeState[0], AfterState[0], m, outputType, printOffset);

    for (uint32 core = 0; core < m->getNumCores(); ++core)
    {
        if (extendPrintout && m->isHybrid() && m->getCoreType(core) != coreType)
        {
            continue;
        }
        if (!(show_partial_core_output && ycores.test(core) == false))
        {
            if (outputType == Header1) {
                cout << separator << "SKT" << m->getSocketId(core) << "CORE" << core;
                printOffset.end++;
            }
            else if (outputType == Header2)
                cout << separator << pmuType;
            else if (outputType == Data) {
                cout << separator;
                if (m->isHybrid() == false || m->getCoreType(core) == coreType) {
                    cout << metricFunc(BeforeState[core], AfterState[core]);
                }
            }
            else if (outputType == Header21) {
                cout << separator << pmuType << "_SKT" << m->getSocketId(core) << "_CORE" << core;
                printOffset.end++;
            }
            else if (outputType == Json) {
                cout << separator << pmuType << "_SKT" << m->getSocketId(core) << "_CORE" << core <<
                    jsonSeparator;
                if (m->isHybrid() == false || m->getCoreType(core) == coreType) {
                    cout << metricFunc(BeforeState[core], AfterState[core]);
                }
            } else
                assert(!"unknown output type");
        }
    }

    printNewLine(outputType);
};

typedef uint64 (*UncoreMetricFunc)(const uint32 u, const uint32 i,  const ServerUncoreCounterState& before, const ServerUncoreCounterState& after);
typedef uint64(*UncoreFixedMetricFunc)(const uint32 u, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after);

uint64 nullFixedMetricFunc(const uint32, const ServerUncoreCounterState&, const ServerUncoreCounterState&)
{
    return ~0ULL;
}

const char* fixedCoreEventNames[] = { "InstructionsRetired" , "Cycles", "RefCycles", "TopDownSlots" };
const char* topdownEventNames[] = { "PERF_METRICS.FRONTEND_BOUND" , "PERF_METRICS.BAD_SPECULATION", "PERF_METRICS.BACKEND_BOUND", "PERF_METRICS.RETIRING",
                                    "PERF_METRICS.HEAVY_OPERATIONS", "PERF_METRICS.BRANCH_MISPREDICTS", "PERF_METRICS.FETCH_LATENCY", "PERF_METRICS.MEMORY_BOUND"};
constexpr uint32 PerfMetricsConfig = 2;
constexpr uint64 PerfMetricsMask = 1ULL;
constexpr uint64 maxPerfMetricsValue = 255ULL;

const char * getTypeString(uint64 typeID)
{
    switch (typeID)
    {
    case PCM::MSRType::Freerun:
        return "freerun";
    case PCM::MSRType::Static:
        return "static";
    }
    return "unknownType";
}

std::string getMSREventString(const uint64 & index, const std::string & type, const PCM::MSRType & msrType)
{
    std::stringstream c;
    c << type << "/MSR 0x" << std::hex << index << "/" << getTypeString(msrType);
    return c.str();
}

std::string getPCICFGEventString(const PCM::RawEventEncoding & eventEnc, const std::string& type)
{
    std::stringstream c;
    c << type << "/deviceID 0x" << std::hex << eventEnc[PCM::PCICFGEventPosition::deviceID]
              << "/offset 0x" << eventEnc[PCM::PCICFGEventPosition::offset]
              << "/width 0x" << eventEnc[PCM::PCICFGEventPosition::width] << "/"
        << getTypeString(eventEnc[PCM::PCICFGEventPosition::type]);
    return c.str();
}

std::string getMMIOEventString(const PCM::RawEventEncoding& eventEnc, const std::string& type)
{
    std::stringstream c;
    c << type << "/deviceID 0x" << std::hex <<
                          eventEnc[PCM::MMIOEventPosition::deviceID] <<
                 "/offset 0x" << eventEnc[PCM::MMIOEventPosition::offset] <<
                 "/membar_bits1 0x" << eventEnc[PCM::MMIOEventPosition::membar_bits1] <<
                 "/membar_bits2 0x" << eventEnc[PCM::MMIOEventPosition::membar_bits2] <<
                 "/width 0x" << eventEnc[PCM::MMIOEventPosition::width] <<
                 "/" << getTypeString(eventEnc[PCM::MMIOEventPosition::type]);
    return c.str();
}

std::string getPMTEventString(const PCM::RawEventEncoding& eventEnc, const std::string& type)
{
    std::stringstream c;
    c << type << "/UID 0x" << std::hex <<
                          eventEnc[PCM::PMTEventPosition::UID] <<
                 "/offset 0x" << eventEnc[PCM::PMTEventPosition::offset] <<
                 "/lsb 0x" << eventEnc[PCM::PMTEventPosition::lsb] <<
                 "/msb 0x" << eventEnc[PCM::PMTEventPosition::msb] <<
                 "/" << getTypeString(eventEnc[PCM::PMTEventPosition::type]);
    return c.str();
}

typedef std::string(*getEventStringFunc)(const PCM::RawEventEncoding& eventEnc, const std::string& type);
typedef std::vector<uint64>(getEventFunc)(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after);

enum MSRScope
{
    Thread,
    Package
};

uint32 numTMAEvents(PCM* m)
{
    return (m->isHWTMAL2Supported() ? 8 : 4);
}

uint32 pmu_type = PCM::INVALID_PMU_ID;

void printTransposed(const PCM::RawPMUConfigs& curPMUConfigs,
    PCM* m,
    SystemCounterState& SysBeforeState, SystemCounterState& SysAfterState,
    vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState,
    vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState,
    vector<SocketCounterState>& BeforeSocketState, vector<SocketCounterState>& AfterSocketState,
    const CsvOutputType outputType,
    const bool& isLastGroup)
{
        const bool is_header = (outputType == Header1 || outputType == Header2 || outputType == Header21);
        for (const auto & typeEvents : curPMUConfigs)
        {
            bool is_header_printed = false;
            const auto& type = typeEvents.first;
            const auto& events = typeEvents.second.programmable;
            const auto& fixedEvents = typeEvents.second.fixed;

            PrintOffset printOffset{type, 0, 0};
            const auto print_idx = getPrintOffsetIdx(type);

            if (outputType == Header1 || outputType == Header21) {
                if (print_idx != -1)
                    continue; // header already printed
                else {
                    printOffset.start = (printOffsets.empty()) ? 0 : printOffsets.back().end;
                    printOffset.end = printOffset.start;
                }
            } else if (outputType == Header2) {
                if (std::find(printedBlocks.begin(), printedBlocks.end(), type) != printedBlocks.end())
                    continue;
                printedBlocks.push_back(type);
            } else if (outputType == Data) {
                assert(printOffsets.empty() || print_idx >= 0);
                printOffset.start = (printOffsets.empty()) ? 0 : printOffsets[print_idx].start;
                printOffset.end = (printOffsets.empty()) ? 0 : printOffsets[print_idx].end;
            }

            auto printUncoreRows = [&](UncoreMetricFunc metricFunc, const uint32 maxUnit, const std::string &miscName = std::string("<invalid-fixed-event-name>"), UncoreFixedMetricFunc fixedMetricFunc = nullFixedMetricFunc)
            {
                if (fixedEvents.size())
                {
                    printRowBegin(miscName, BeforeState[0], AfterState[0], m, outputType, printOffset);

                    for (uint32 s = 0; s < m->getNumSockets(); ++s)
                    {
                        for (uint32 u = 0; u < maxUnit; ++u)
                        {
                            if (outputType == Header1)
                            {
                                cout << separator << "SKT" << s << miscName << u;
                                printOffset.end++;
                            }
                            else if (outputType == Header2)
                                cout << separator << miscName ;
                            else if (outputType == Data)
                                cout << separator << fixedMetricFunc(u, BeforeUncoreState[s], AfterUncoreState[s]);
                            else if (outputType == Header21) {
                                cout << separator << type << "_SKT" << s << "_" << miscName << u;
                                printOffset.end++;
                            } else if (outputType == Json) {
                                cout << separator << type << "_SKT" << s << "_" << miscName << u
                                    << jsonSeparator << fixedMetricFunc(u, BeforeUncoreState[s], AfterUncoreState[s]);
                            } else
                                assert(!"unknown output type");
                        }
                    }

                    if (is_header)
                        is_header_printed = true;

                    printNewLine(outputType);
                }
                uint32 i = 0;
                for (auto& event : events)
                {
                    const std::string name = (event.second.empty()) ? (type + "Event" + std::to_string(i)) : event.second;

                    if (is_header && is_header_printed)
                        break;

                    printRowBegin(name, BeforeState[0], AfterState[0], m, outputType, printOffset);

                    for (uint32 s = 0; s < m->getNumSockets(); ++s)
                    {
                        for (uint32 u = 0; u < maxUnit; ++u)
                        {
                            if (outputType == Header1)
                            {
                                cout  << separator << "SKT_" << s << miscName << u;
                                printOffset.end++;
                            }
                            else if (outputType == Header2)
                            {
                                cout << separator << miscName ;
                            }
                            else if (outputType == Data)
                            {
                                assert(metricFunc);
                                cout << separator << metricFunc(u, i, BeforeUncoreState[s], AfterUncoreState[s]);
                            }
                            else if (outputType == Header21)
                            {
                                cout << separator << type << "_SKT" << s << "_" << miscName << u;
                                printOffset.end++;
                            }
                            else if (outputType == Json)
                            {
                                assert(metricFunc);
                                cout << separator << type << "_SKT" << s << "_" << miscName << u
                                    << jsonSeparator << metricFunc(u, i, BeforeUncoreState[s], AfterUncoreState[s]);
                            }
                            else
                            {
                                assert(!"unknown output type");
                            }
                        }
                    }

                    if (is_header)
                        is_header_printed = true;

                    ++i;
                    printNewLine(outputType);
                }
            };
            auto printMSRRows = [&](const MSRScope& scope)
            {
                auto printMSR = [&](const PCM::RawEventConfig& event) -> bool
                {
                    const auto index = event.first[PCM::MSREventPosition::index];
                    const auto msrType = (PCM::MSRType)event.first[PCM::MSREventPosition::type];
                    const std::string name = (event.second.empty()) ? getMSREventString(index, type, msrType) : event.second;

                    if (is_header && is_header_printed)
                        return false;

                    printRowBegin(name, BeforeState[0], AfterState[0], m, outputType, printOffset);

                    switch (scope)
                    {
                    case MSRScope::Package:
                        for (uint32 s = 0; s < m->getNumSockets(); ++s)
                        {
                            if (outputType == Header1)
                            {
                                cout << separator << "SKT" << s ;
                                printOffset.end++;
                            }
                            else if (outputType == Header2)
                            {
                                cout << separator << type ;
                            }
                            else if (outputType == Data)
                            {
                                cout << separator << getMSREvent(index, msrType, BeforeSocketState[s], AfterSocketState[s]);
                            }
                            else if (outputType == Header21)
                            {
                                cout << separator << type << "_SKT" << s ;
                                printOffset.end++;
                            }
                            else if (outputType == Json) {
                                cout << separator << type << "_SKT" << s
                                    << jsonSeparator << getMSREvent(index, msrType, BeforeSocketState[s], AfterSocketState[s]);
                            }
                            else
                            {
                                assert(!"unknown output type");
                            }
                        }
                        break;
                    case MSRScope::Thread:
                        for (uint32 core = 0; core < m->getNumCores(); ++core)
                        {
                            if (outputType == Header1)
                            {
                                cout << separator << "SKT" << m->getSocketId(core) << "CORE" << core;
                                printOffset.end++;
                            }
                            else if (outputType == Header2)
                            {
                                cout << separator << type ;
                            }
                            else if (outputType == Data)
                            {
                                cout << separator << getMSREvent(index, msrType, BeforeState[core], AfterState[core]);
                            }
                            else if (outputType == Header21)
                            {
                                cout << separator << type << "_SKT" << m->getSocketId(core) << "_CORE" << core;
                                printOffset.end++;
                            }
                            else if (outputType == Json) {
                                cout << separator << type << "_SKT" << m->getSocketId(core) << "_CORE" << core
                                    << jsonSeparator << getMSREvent(index, msrType, BeforeState[core], AfterState[core]);
                            }
                            else
                            {
                                assert(!"unknown output type");
                            }
                        }
                        break;
                    }

                    if (is_header)
                        is_header_printed = true;

                    printNewLine(outputType);

                    return true;
                };
                for (const auto& event : events)
                {
                    if (!printMSR(event))
                    {
                        break;
                    }
                }
                for (const auto& event : fixedEvents)
                {
                    if (!printMSR(event))
                    {
                        break;
                    }
                }
            };
            auto printCores = [&](const pcm::TopologyEntry::CoreType & coreType)
            {
                typedef uint64(*FuncType) (const CoreCounterState& before, const CoreCounterState& after);
                static FuncType funcFixed[] = { [](const CoreCounterState& before, const CoreCounterState& after) { return getInstructionsRetired(before, after); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return getCycles(before, after); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return getRefCycles(before, after); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return getAllSlotsRaw(before, after); }
                };
                static FuncType funcTopDown[] = { [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getFrontendBound(before, after) * maxPerfMetricsValue); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getBadSpeculation(before, after) * maxPerfMetricsValue); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getBackendBound(before, after) * maxPerfMetricsValue); },
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getRetiring(before, after) * maxPerfMetricsValue); },
                              // "PERF_METRICS.HEAVY_OPERATIONS" :
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getHeavyOperationsBound(before, after) * maxPerfMetricsValue); },
                              // "PERF_METRICS.BRANCH_MISPREDICTS" :
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getBranchMispredictionBound(before, after) * maxPerfMetricsValue); },
                              // "PERF_METRICS.FETCH_LATENCY" :
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getFetchLatencyBound(before, after) * maxPerfMetricsValue); },
                              // "PERF_METRICS.MEMORY_BOUND" :
                              [](const CoreCounterState& before, const CoreCounterState& after) { return uint64(getMemoryBound(before, after) * maxPerfMetricsValue); }
                };
                for (const auto& event : fixedEvents)
                {
                    for (uint32 cnt = 0; cnt < 4; ++cnt)
                    {
                        if (extract_bits(event.first[0], 4U * cnt, 1U + 4U * cnt))
                        {
                            if (is_header && is_header_printed)
                                break;

                            printRow(event.second.empty() ? fixedCoreEventNames[cnt] : event.second, funcFixed[cnt], BeforeState, AfterState, m, outputType, printOffset, coreType, type);

                            if (is_header)
                                is_header_printed = true;

                            if (cnt == 3 && (event.first[PerfMetricsConfig] & PerfMetricsMask))
                            {
                                for (uint32 t = 0; t < numTMAEvents(m); ++t)
                                {
                                    printRow(topdownEventNames[t], funcTopDown[t], BeforeState, AfterState, m, outputType, printOffset, coreType, type);
                                }
                            }
                        }
                    }
                }
                uint32 i = 0;
                for (const auto& event : events)
                {
                    if (is_header && is_header_printed)
                        break;

                    const std::string name = (event.second.empty()) ? (type + "Event" + std::to_string(i)) : event.second;
                    printRow(name, [&i](const CoreCounterState& before, const CoreCounterState& after) { return getNumberOfCustomEvents(i, before, after); }, BeforeState, AfterState, m, outputType, printOffset, coreType, type);
                    ++i;

                    if (is_header)
                        is_header_printed = true;
                }
            };
            auto printRegisterRows = [&](getEventStringFunc getEventString, getEventFunc getEvent)
            {
                auto printRegister = [&](const PCM::RawEventConfig& event) -> bool
                {
                    const std::string name = (event.second.empty()) ? getEventString(event.first, type) : event.second;
                    const auto values = getEvent(event.first, SysBeforeState, SysAfterState);

                    if (is_header && is_header_printed)
                        return false;

                    printRowBegin(name, BeforeState[0], AfterState[0], m, outputType, printOffset);

                    for (size_t r = 0; r < values.size(); ++r)
                    {
                        if (outputType == Header1)
                        {
                            cout << separator << "SYSTEM_" << r;
                            printOffset.end++;
                        }
                        else if (outputType == Header2)
                        {
                            cout << separator << type;
                        }
                        else if (outputType == Data)
                        {
                            cout << separator << values[r];
                        }
                        else if (outputType == Header21)
                        {
                            cout << separator << type << "_SYSTEM_" << r;
                            printOffset.end++;
                        }
                        else if (outputType == Json) {
                            cout << separator << type << "_SYSTEM_" << r
                                << jsonSeparator << values[r];
                        }
                        else
                        {
                            assert(!"unknown output type");
                        }
                    }

                    if (is_header)
                        is_header_printed = true;

                    printNewLine(outputType);

                    return true;
                };
                for (const auto& event : events)
                {
                    if (!printRegister(event))
                    {
                        break;
                    }
                }
                for (const auto& event : fixedEvents)
                {
                    if (!printRegister(event))
                    {
                        break;
                    }
                }
            };
            if (type == "core")
            {
                printCores(pcm::TopologyEntry::Core);
            }
            else if (type == "atom")
            {
                printCores(pcm::TopologyEntry::Atom);
            }
            else if (type == "thread_msr")
            {
                printMSRRows(MSRScope::Thread);
            }
            else if (type == "package_msr")
            {
                printMSRRows(MSRScope::Package);
            }
            else if (type == "pcicfg")
            {
                printRegisterRows(getPCICFGEventString, getPCICFGEvent);
            }
            else if (type == "mmio")
            {
                printRegisterRows(getMMIOEventString, getMMIOEvent);
            }
            else if (type == "pmt")
            {
                printRegisterRows(getPMTEventString, getPMTEvent);
            }
            else if (type == "m3upi")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getQPILinksPerSocket(), "LINK"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getQPILinksPerSocket(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getM3UPICounter(u, i, before, after); }, (uint32) m->getQPILinksPerSocket(), "LINK");
                    });
            }
            else if (type == "xpi" || type == "upi" || type == "qpi")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getQPILinksPerSocket(), "LINK"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getQPILinksPerSocket(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getXPICounter(u, i, before, after); }, (uint32) m->getQPILinksPerSocket(), "LINK");
                    });
            }
            else if (type == "imc")
            {
                const std::string fixedEventName = (fixedEvents.empty() == false && fixedEvents[0].second.empty() == false) ? fixedEvents[0].second : "DRAMClocks";
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCChannelsPerSocket(), "CHAN"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCChannelsPerSocket(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getMCCounter(u, i, before, after); }, (uint32)m->getMCChannelsPerSocket(),
                        fixedEventName, [](const uint32 u, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getDRAMClocks(u, before, after); });
                    });
            }
            else if (type == "m2m")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCPerSocket(), "MC"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCPerSocket(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getM2MCounter(u, i, before, after); }, (uint32)m->getMCPerSocket(), "MC");
                    });
            }
            else if (type == "ha")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCPerSocket(), "HA"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMCPerSocket(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getHACounter(u, i, before, after); }, (uint32)m->getMCPerSocket(), "HA");
                    });
            }
            else if (type == "pcu")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::PCU_PMU_ID), "P"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::PCU_PMU_ID), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreCounter(PCM::PCU_PMU_ID, u, i, before, after); }, 1U, "");
                    });
            }
            else if (type == "ubox")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, 1U, ""); },
                    [&]() { printUncoreRows(nullptr, 1U, type); },
                    [&]() { printUncoreRows([](const uint32, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreCounter(PCM::UBOX_PMU_ID, 0, i, before, after); }, 1U,
                            "UncoreClocks", [](const uint32, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreClocks(before, after); });
                    });
            }
            else if (type == "cbo" || type == "cha")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::CBO_PMU_ID), "C"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::CBO_PMU_ID), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreCounter(PCM::CBO_PMU_ID, u, i, before, after); }, (uint32)m->getMaxNumOfUncorePMUs(PCM::CBO_PMU_ID), "C");
                    });
            }
            else if (type == "mdf")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::MDF_PMU_ID), "MDF"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(PCM::MDF_PMU_ID), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreCounter(PCM::MDF_PMU_ID, u, i, before, after); }, (uint32)m->getMaxNumOfUncorePMUs(PCM::MDF_PMU_ID), "MDF");
                    });
            }
            else if (type == "irp")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfIIOStacks(), "IRP"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfIIOStacks(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getIRPCounter(u, i, before, after); }, (uint32)m->getMaxNumOfIIOStacks(), "IRP");
                    });
            }
            else if (type == "iio")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfIIOStacks(), "IIO"); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfIIOStacks(), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getIIOCounter(u, i, before, after); }, (uint32)m->getMaxNumOfIIOStacks(), "IIO");
                    });
            }
            else if (type == "cxlcm")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) ServerUncoreCounterState::maxCXLPorts, "CXLCM"); },
                    [&]() { printUncoreRows(nullptr, (uint32) ServerUncoreCounterState::maxCXLPorts, type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getCXLCMCounter(u, i, before, after); }, ServerUncoreCounterState::maxCXLPorts, "CXLCM");
                    });
            }
            else if (type == "cxldp")
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) ServerUncoreCounterState::maxCXLPorts, "CXLDP"); },
                    [&]() { printUncoreRows(nullptr, (uint32) ServerUncoreCounterState::maxCXLPorts, type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getCXLDPCounter(u, i, before, after); }, ServerUncoreCounterState::maxCXLPorts, "CXLDP");
                    });
            }
            else if ((pmu_type = m->strToUncorePMUID(type)) != PCM::INVALID_PMU_ID)
            {
                choose(outputType,
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(pmu_type), type); },
                    [&]() { printUncoreRows(nullptr, (uint32) m->getMaxNumOfUncorePMUs(pmu_type), type); },
                    [&]() { printUncoreRows([](const uint32 u, const uint32 i, const ServerUncoreCounterState& before, const ServerUncoreCounterState& after) { return getUncoreCounter(pmu_type, u, i, before, after); }, (uint32)m->getMaxNumOfUncorePMUs(pmu_type), type);
                    });
            }
            else
            {
                std::cerr << "ERROR: unrecognized PMU type \"" << type << "\"\n";
            }

            if (outputType == Header1 || outputType == Header21)
                printOffsets.push_back(printOffset);
        }
        if (sampleSeparator)
        {
            cout << (isLastGroup? "==========\n" : "----------\n");
        }
        if (flushLine)
        {
            cout.flush();
        }
}

void print(const PCM::RawPMUConfigs& curPMUConfigs,
            PCM* m,
            SystemCounterState& SysBeforeState, SystemCounterState& SysAfterState,
            vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState,
            vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState,
            vector<SocketCounterState>& BeforeSocketState, vector<SocketCounterState>& AfterSocketState,
            const CsvOutputType outputType)
{
    printDateForCSV(outputType, separator);
    if (BeforeState.size() > 0 && AfterState.size() > 0)
    {
        choose(outputType,
            []() { cout << separator; },
            []() { cout << "ms" << separator; },
            [&]() { cout << (1000ULL * getInvariantTSC(BeforeState[0], AfterState[0])) / m->getNominalFrequency() << separator; });
    }
    for (auto& typeEvents : curPMUConfigs)
    {
        const auto & type = typeEvents.first;
        const auto & events = typeEvents.second.programmable;
        const auto & fixedEvents = typeEvents.second.fixed;
        auto printCores = [&m, &BeforeState, &AfterState, &type, &fixedEvents, &events, &outputType](const pcm::TopologyEntry::CoreType & coreType)
        {
            for (uint32 core = 0; core < m->getNumCores(); ++core)
            {
                if (show_partial_core_output && ycores.test(core) == false)
                    continue;

                if (m->isHybrid() && m->getCoreType(core) != coreType)
                {
                    continue;
                }

                const uint64 fixedCtrValues[] = {
                    getInstructionsRetired(BeforeState[core], AfterState[core]),
                    getCycles(BeforeState[core], AfterState[core]),
                    getRefCycles(BeforeState[core], AfterState[core]),
                    getAllSlotsRaw(BeforeState[core], AfterState[core])
                };
                const uint64 topdownCtrValues[] = {
                    uint64(getFrontendBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getBadSpeculation(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getBackendBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getRetiring(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getHeavyOperationsBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getBranchMispredictionBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getFetchLatencyBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue),
                    uint64(getMemoryBound(BeforeState[core], AfterState[core]) * maxPerfMetricsValue)
                };
                for (const auto& event : fixedEvents)
                {
                    auto print = [&](const std::string& metric, const uint64 value)
                    {
                        choose(outputType,
                            [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << separator; },
                            [&metric]() { cout << metric << separator; },
                            [&value]() { cout << value << separator; });
                    };
                    for (uint32 cnt = 0; cnt < 4; ++cnt)
                    {
                        if (extract_bits(event.first[0], 4U * cnt, 1U + 4U * cnt))
                        {
                            print(event.second.empty() ? fixedCoreEventNames[cnt] : event.second, fixedCtrValues[cnt]);
                            if (cnt == 3 && (event.first[PerfMetricsConfig] & PerfMetricsMask))
                            {
                                for (uint32 t = 0; t < numTMAEvents(m); ++t)
                                {
                                    print(topdownEventNames[t], topdownCtrValues[t]);
                                }
                            }
                        }
                    }
                }
                int i = 0;
                for (auto& event : events)
                {
                    choose(outputType,
                        [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << separator; },
                        [&event, &i, &type]() { if (event.second.empty()) cout << type << "Event" << i << separator;  else cout << event.second << separator; },
                        [&]() { cout << getNumberOfCustomEvents(i, BeforeState[core], AfterState[core]) << separator; });
                    ++i;
                }
            }
        };
        auto printRegisters = [&](getEventStringFunc getEventString, getEventFunc getEvent)
        {
            auto printOneRegister = [&](const PCM::RawEventConfig& event)
            {
                const auto values = getEvent(event.first, SysBeforeState, SysAfterState);
                for (size_t r = 0; r < values.size(); ++r)
                {
                    choose(outputType,
                        [&r]() { cout << "SYSTEM_" << r << separator; },
                        [&]() { if (event.second.empty()) cout << getEventString(event.first, type) << separator;  else cout << event.second << separator; },
                        [&]() { cout << values[r] << separator; });
                }
            };
            for (const auto& event : events)
            {
                printOneRegister(event);
            }
            for (const auto& event : fixedEvents)
            {
                printOneRegister(event);
            }
        };
        if (type == "core")
        {
            printCores(pcm::TopologyEntry::Core);
        }
        else if (type == "atom")
        {
            printCores(pcm::TopologyEntry::Atom);
        }
        else if (type == "m3upi")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 l = 0; l < m->getQPILinksPerSocket(); ++l)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "M3UPIEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getM3UPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
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
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, l]() { cout << "SKT" << s << "LINK" << l << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "XPIEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getXPICounter(l, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
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
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << separator; },
                            [&fixedEvents]() { cout << "DRAMClocks" << fixedEvents[0].second << separator; },
                            [&]() { cout << getDRAMClocks(ch, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                    }
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, ch]() { cout << "SKT" << s << "CHAN" << ch << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "IMCEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getMCCounter(ch, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
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
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, mc]() { cout << "SKT" << s << "MC" << mc << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "M2MEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getM2MCounter(mc, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "ha")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 mc = 0; mc < m->getMCPerSocket(); ++mc)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, mc]() { cout << "SKT" << s << "HA" << mc << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "HAEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getHACounter(mc, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "pcu")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 u = 0; u < m->getMaxNumOfUncorePMUs(PCM::PCU_PMU_ID); ++u)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, u]() { cout << "SKT" << s << "P" << u << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "PCUEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getUncoreCounter(PCM::PCU_PMU_ID, u, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "package_msr")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                auto printMSR = [&](const PCM::RawEventConfig & event)
                {
                    const auto index = event.first[PCM::MSREventPosition::index];
                    const auto msrType = (PCM::MSRType)event.first[PCM::MSREventPosition::type];
                    choose(outputType,
                        [s]() { cout << "SKT" << s << separator; },
                        [&]() { if (event.second.empty()) cout << getMSREventString(index, type, msrType) << separator;  else cout << event.second << separator; },
                        [&]() { cout << getMSREvent(index, msrType, BeforeSocketState[s], AfterSocketState[s]) << separator; });
                };
                for (const auto& event : events)
                {
                    printMSR(event);
                }
                for (const auto& event : fixedEvents)
                {
                    printMSR(event);
                }
            }
        }
        else if (type == "thread_msr")
        {
            for (uint32 core = 0; core < m->getNumCores(); ++core)
            {
                auto printMSR = [&](const PCM::RawEventConfig& event)
                {
                    const auto index = event.first[PCM::MSREventPosition::index];
                    const auto msrType = (PCM::MSRType)event.first[PCM::MSREventPosition::type];
                    choose(outputType,
                        [m, core]() { cout << "SKT" << m->getSocketId(core) << "CORE" << core << separator; },
                        [&]() { if (event.second.empty()) cout << getMSREventString(index, type, msrType) << separator;  else cout << event.second << separator; },
                        [&]() { cout << getMSREvent(index, msrType, BeforeState[core], AfterState[core]) << separator; });
                };
                for (const auto& event : events)
                {
                    printMSR(event);
                }
                for (const auto& event : fixedEvents)
                {
                    printMSR(event);
                }
            }
        }
        else if (type == "pcicfg")
        {
            printRegisters(getPCICFGEventString, getPCICFGEvent);
        }
        else if (type == "mmio")
        {
            printRegisters(getMMIOEventString, getMMIOEvent);
        }
        else if (type == "pmt")
        {
            printRegisters(getPMTEventString, getPMTEvent);
        }
        else if (type == "ubox")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                if (fixedEvents.size())
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << separator; },
                        [&fixedEvents]() { cout << "UncoreClocks" << fixedEvents[0].second << separator; },
                        [&]() { cout << getUncoreClocks(BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                }
                int i = 0;
                for (auto& event : events)
                {
                    choose(outputType,
                        [s]() { cout << "SKT" << s << separator; },
                        [&event, &i]() { if (event.second.empty()) cout << "UBOXEvent" << i << separator;  else cout << event.second << separator; },
                        [&]() { cout << getUncoreCounter(PCM::UBOX_PMU_ID, 0, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                    ++i;
                }
            }
        }
        else if (type == "cbo" || type == "cha")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 cbo = 0; cbo < m->getMaxNumOfUncorePMUs(PCM::CBO_PMU_ID); ++cbo)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, cbo]() { cout << "SKT" << s << "C" << cbo << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "CBOEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getUncoreCounter(PCM::CBO_PMU_ID, cbo, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "mdf")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 mdf = 0; mdf < m->getMaxNumOfUncorePMUs(PCM::MDF_PMU_ID); ++mdf)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, mdf]() { cout << "SKT" << s << "MDF" << mdf << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "MDFEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getUncoreCounter(PCM::MDF_PMU_ID, mdf, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "irp")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 stack = 0; stack < m->getMaxNumOfIIOStacks(); ++stack)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, stack]() { cout << "SKT" << s << "IRP" << stack << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "IRPEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getIRPCounter(stack, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
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
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, stack]() { cout << "SKT" << s << "IIO" << stack << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "IIOEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getIIOCounter(stack, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "cxlcm")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 p = 0; p < ServerUncoreCounterState::maxCXLPorts; ++p)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, p]() { cout << "SKT" << s << "CXLCM" << p << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "CXLCMEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getCXLCMCounter(p, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if (type == "cxldp")
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 p = 0; p < ServerUncoreCounterState::maxCXLPorts; ++p)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, p]() { cout << "SKT" << s << "CXLDP" << p << separator; },
                            [&event, &i]() { if (event.second.empty()) cout << "CXLDPEvent" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getCXLDPCounter(p, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
                        ++i;
                    }
                }
            }
        }
        else if ((pmu_type = m->strToUncorePMUID(type)) != PCM::INVALID_PMU_ID)
        {
            for (uint32 s = 0; s < m->getNumSockets(); ++s)
            {
                for (uint32 unit = 0; unit < m->getMaxNumOfUncorePMUs(pmu_type); ++unit)
                {
                    int i = 0;
                    for (auto& event : events)
                    {
                        choose(outputType,
                            [s, unit, &type]() { cout << "SKT" << s << type << unit << separator; },
                            [&event, &i, &type]() { if (event.second.empty()) cout << type << "Event" << i << separator;  else cout << event.second << separator; },
                            [&]() { cout << getUncoreCounter(pmu_type, unit, i, BeforeUncoreState[s], AfterUncoreState[s]) << separator; });
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

void printAll(const PCM::RawPMUConfigs& curPMUConfigs,
                PCM * m,
                SystemCounterState & SysBeforeState, SystemCounterState& SysAfterState,
                vector<CoreCounterState>& BeforeState, vector<CoreCounterState>& AfterState,
                vector<ServerUncoreCounterState>& BeforeUncoreState, vector<ServerUncoreCounterState>& AfterUncoreState,
                vector<SocketCounterState>& BeforeSocketState, vector<SocketCounterState>& AfterSocketState,
                std::vector<PCM::RawPMUConfigs>& PMUConfigs,
                const bool & isLastGroup)
{
    if (outputToJson) {
        printTransposed(curPMUConfigs, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Json, isLastGroup);
        return;
    }

    static bool displayHeader = true;

    if (!extendPrintout && transpose)
        displayHeader = false;

    if (transpose) {
        if (displayHeader) {
            // Need to go through all possible print on first run to form header.
            if (singleHeader) {
                // merge header 2 and 1, print and get all offsets
                cout << "Date" << separator << "Time" << separator << "Event" << separator;
                cout << "ms" << separator << "InvariantTSC";
                for (auto &config : PMUConfigs)
                    printTransposed(config, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Header21, isLastGroup);
            } else {
                // print 2 headers in 2 rows
                for (int i = 0 ; i < 4 ; i++)
                    cout << separator;

                // print header_1 and get all offsets
                for (auto &config : PMUConfigs)
                    printTransposed(config, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Header1, isLastGroup);

                cout << endl;

                // print header_2
                cout << "Date" << separator << "Time" << separator << "Event" << separator;
                cout << "ms" << separator << "InvariantTSC";
                for (auto &config : PMUConfigs)
                    printTransposed(config, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Header2, isLastGroup);
            }
            cout << endl;
        }
        printTransposed(curPMUConfigs, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Data, isLastGroup);
    } else {
        if (displayHeader) {
            print(curPMUConfigs, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Header1);
            print(curPMUConfigs, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Header2);
        }
        print(curPMUConfigs, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, Data);
    }

    displayHeader = false;
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        exit(EXIT_SUCCESS);

    parseParam(argc, argv, "out", [](const char* p) {
            const string filename{ p };
            if (!filename.empty()) {
                PCM::setOutput(filename, true);
            }
        });

    null_stream nullStream2;
#ifdef PCM_FORCE_SILENT
    null_stream nullStream1;
    std::cout.rdbuf(&nullStream1);
    std::cerr.rdbuf(&nullStream2);
#else
    check_and_set_silent(argc, argv, nullStream2);
#endif

    set_signal_handlers();
    set_real_time_priority(true);

    cerr << "\n";
    cerr << " Intel(r) Performance Counter Monitor: Raw Event Monitoring Utility \n";
    cerr << "\n";

    std::vector<PCM::RawPMUConfigs> PMUConfigs(1);
    double delay = -1.0;
    int pid{-1};
    char* sysCmd = NULL;
    char** sysArgv = NULL;
    MainLoop mainLoop;
    string program = string(argv[0]);
    bool forceRTMAbortMode = false;
    bool reset_pmu = false;
    PCM* m = PCM::getInstance();

    parsePID(argc, argv, pid);

#ifdef PCM_SIMDJSON_AVAILABLE
    parseParam(argc, argv, "ep", [](const char* p) { eventFileLocationPrefix = p;});
#endif

    if (argc > 1) do
    {
        argv++;
        argc--;
        string arg_value;

        if (*argv == nullptr)
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
        {
            print_usage(program);
            exit(EXIT_FAILURE);
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            // handled in check_and_set_silent
            continue;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
        {
            if (!arg_value.empty()) {
                m->setOutput(arg_value);
            }
        }
        else if (check_argument_equals(*argv, {"-json", "/json"}))
        {
            separator = ",\"";
            outputToJson = true;
        }
        else if (extract_argument_value(*argv, {"-json", "/json"}, arg_value))
        {
            separator = ",\"";
            outputToJson = true;
            if (!arg_value.empty()) {
                m->setOutput(arg_value);
            }
            continue;
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else if (isPIDOption(argv))
        {
            argv++;
            argc--;
            continue;
        }
        else if (check_argument_equals(*argv, {"-reset", "/reset", "-r"}))
        {
            reset_pmu = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-tr", "/tr"}))
        {
            transpose = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-ext", "/ext"}))
        {
            extendPrintout = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-single-header", "/single-header"}))
        {
            singleHeader = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-l"})) {
            std::cout.imbue(std::locale(""));
            separator = "\t";
            continue;
        }
        else if (check_argument_equals(*argv, {"-tab"})) {
            separator = "\t";
            continue;
        }
        else if (check_argument_equals(*argv, {"--yescores", "-yc", "/yc"}))
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
        else if (check_argument_equals(*argv, {"-out", "/out"}))
        {
            argv++;
            argc--;
            continue;
        }
        else if (check_argument_equals(*argv, {"-ep", "/ep"}))
        {
            argv++;
            argc--;
            continue;
        }
        else if (check_argument_equals(*argv, {"-edp", "/edp"}))
        {
            sampleSeparator = true;
            defaultDelay = 0.2;
            transpose = true;
            m->printDetailedSystemTopology();
            continue;
        }
        else if (check_argument_equals(*argv, {"-el", "/el"}))
        {
            argv++;
            argc--;
            const auto p = *argv;
            if (p == nullptr)
            {
                cerr << "ERROR: no parameter value provided for 'el' option\n";
                exit(EXIT_FAILURE);
            }
            else if (addEvents(PMUConfigs, p) == false)
            {
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (check_argument_equals(*argv, {"-e"}))
        {
            argv++;
            argc--;
            const auto p = *argv;
            if (p == nullptr)
            {
                cerr << "ERROR: no parameter value provided for 'e' option\n";
                exit(EXIT_FAILURE);
            } else if (addEvent(PMUConfigs[0], p) != AddEventStatus::OK)
            {
                exit(EXIT_FAILURE);
            }
            continue;
        }
        else if (CheckAndForceRTMAbortMode(*argv, m))
        {
            forceRTMAbortMode = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-f", "/f"}))
        {
            flushLine = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-s", "/s"}))
        {
            sampleSeparator = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-v", "/v"}))
        {
            verbose = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"--"}))
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            delay = parse_delay(*argv, program, (print_usage_func)print_usage);
            continue;
        }
    } while (argc > 1); // end of command line parsing loop

    if (reset_pmu)
    {
        cerr << "\n Resetting PMU configuration\n";
        m->resetPMU();
    }

    print_cpu_details();

    size_t nGroups = 0;
    for (const auto& group : PMUConfigs)
    {
        if (!group.empty()) ++nGroups;
    }
    for (size_t i = 0; i < PMUConfigs.size(); ++i)
    {
        if (PMUConfigs[i].empty())
        {   // erase empty groups
            PMUConfigs.erase(PMUConfigs.begin() + i);
            --i;
        }
    }
    assert(PMUConfigs.size() == nGroups);
    if (nGroups == 0)
    {
        cerr << "No events specified. Exiting.\n";
        exit(EXIT_FAILURE);
    }
    cerr << "Collecting " << nGroups << " event group(s)\n";

    if (nGroups > 1)
    {
        transpose = true;
        cerr << "Enforcing transposed event output because the number of event groups > 1\n";
    }

    print_pid_collection_message(pid);

    auto programPMUs = [&m, &pid](const PCM::RawPMUConfigs & config)
    {
        if (verbose)
        {
            for (const auto & pmuConfig: config)
            {
                for (const auto & e : pmuConfig.second.fixed)
                {
                    cerr << "Programming " << pmuConfig.first << " fixed event: " << e.second << "\n";
                }
                for (const auto & e : pmuConfig.second.programmable)
                {
                    cerr << "Programming " << pmuConfig.first << " programmable event: " << e.second << "\n";
                }
            }
        }
        PCM::ErrorCode status = m->program(config, !verbose, pid);
        m->checkError(status);
    };

    SystemCounterState SysBeforeState, SysAfterState;
    vector<CoreCounterState> BeforeState, AfterState;
    vector<SocketCounterState> BeforeSocketState, AfterSocketState;
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

    if (delay <= 0.0) delay = defaultDelay;

    cerr << "Update every " << delay << " seconds\n";

    std::cout.precision(2);
    std::cout << std::fixed;

    if (sysCmd != NULL) {
        MySystem(sysCmd, sysArgv);
    }

    auto programAndReadGroup = [&](const PCM::RawPMUConfigs & group)
    {
        if (forceRTMAbortMode)
        {
            m->enableForceRTMAbortMode(true);
        }
        programPMUs(group);
        m->globalFreezeUncoreCounters();
        m->getAllCounterStates(SysBeforeState, BeforeSocketState, BeforeState);
        for (uint32 s = 0; s < m->getNumSockets(); ++s)
        {
            BeforeUncoreState[s] = m->getServerUncoreCounterState(s);
        }
        m->globalUnfreezeUncoreCounters();
    };

    if (nGroups == 1)
    {
        programAndReadGroup(PMUConfigs[0]);
    }

    mainLoop([&]()
    {
         size_t groupNr = 0;
         for (const auto & group : PMUConfigs)
         {
                ++groupNr;

                if (nGroups > 1)
                {
                    programAndReadGroup(group);
                }

                calibratedSleep(delay, sysCmd, mainLoop, m);

                m->globalFreezeUncoreCounters();
                m->getAllCounterStates(SysAfterState, AfterSocketState, AfterState);
                for (uint32 s = 0; s < m->getNumSockets(); ++s)
                {
                    AfterUncoreState[s] = m->getServerUncoreCounterState(s);
                }
                m->globalUnfreezeUncoreCounters();

                //cout << "Time elapsed: " << dec << fixed << AfterTime - BeforeTime << " ms\n";
                //cout << "Called sleep function for " << dec << fixed << delay_ms << " ms\n";

                printAll(group, m, SysBeforeState, SysAfterState, BeforeState, AfterState, BeforeUncoreState, AfterUncoreState, BeforeSocketState, AfterSocketState, PMUConfigs, groupNr == nGroups);
                if (nGroups == 1)
                {
                    std::swap(BeforeState, AfterState);
                    std::swap(BeforeSocketState, AfterSocketState);
                    std::swap(BeforeUncoreState, AfterUncoreState);
                    std::swap(SysBeforeState, SysAfterState);
                }
         }
         if (m->isBlocked()) {
             // in case PCM was blocked after spawning child application: break monitoring loop here
             return false;
         }
         return true;
    });
    exit(EXIT_SUCCESS);
}
