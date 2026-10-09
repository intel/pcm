// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation

#include "event-resolver.h"
#include "utils.h"
#include "debug.h"

#include <fstream>
#include <iostream>
#include <regex>
#include <algorithm>
#include <cassert>

namespace pcm {

std::string PerfmonEventResolver::findPerfmonPath(const std::string& programPath)
{
    const std::string marker = "mapfile.csv";

    // 1. Next to the binary (build output: bin/perfmon/)
    size_t lastSlash = programPath.find_last_of("/\\");
    std::string binDir = (lastSlash != std::string::npos) ? programPath.substr(0, lastSlash) : ".";
    std::string candidate = binDir + "/perfmon";
    if (std::ifstream(candidate + "/" + marker).good()) return candidate;

    // 2. Install path
    candidate = getInstallPathPrefix() + "perfmon";
    if (std::ifstream(candidate + "/" + marker).good()) return candidate;

    return ".";
}

const std::map<std::string, std::string> PerfmonEventResolver::s_pmuNameMap = {
    {"cbo",    "cha"},
    {"b2cmi",  "m2m"},
    {"upi",    "xpi"},
    {"upi ll", "xpi"},
    {"b2upi",  "m3upi"},
    {"qpi",    "xpi"},
    {"qpi ll", "xpi"}
};

void PerfmonEventResolver::addLocalEvents(const std::vector<std::pair<std::string, LocalEvent>>& events)
{
    for (const auto& [name, fields] : events)
        m_localEvents[name] = fields;
}

#ifdef PCM_SIMDJSON_AVAILABLE

static void lowerCase(std::string& str)
{
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c)
    {
#ifdef _MSC_VER
        return std::tolower(c, std::locale());
#else
        return std::tolower(c);
#endif
    });
}

bool PerfmonEventResolver::init(const std::string& cpuFamilyModel, const std::string& eventFilePrefix)
{
    // getCPUFamilyModelString() includes stepping (e.g. "GenuineIntel-6-6A-0").
    auto lastDash = cpuFamilyModel.rfind('-');
    const std::string cpuBase = (lastDash != std::string::npos) ? cpuFamilyModel.substr(0, lastDash) : cpuFamilyModel;
    const int stepping = (lastDash != std::string::npos) ? std::stoi(cpuFamilyModel.substr(lastDash + 1)) : 0;

    if (!loadPerfmonEvents(cpuFamilyModel, eventFilePrefix)) return false;

    if (!loadPMUDeclarations(cpuBase, stepping, eventFilePrefix)) return false;

    m_initialized = true;
    return true;
}

bool PerfmonEventResolver::parseTSV(const std::string& path)
{
    std::ifstream inFile(path);
    if (!inFile.is_open()) return false;

    std::string line;
    bool colNamesParsed = false;
    int eventNamePos = -1;
    std::unordered_map<std::string, std::vector<std::string>> eventMap;

    while (std::getline(inFile, line))
    {
        if (line.size() == 1 && line[0] == '\n') continue;

        // Trim whitespace
        auto wsLeft = line.find_first_not_of(' ');
        auto wsRight = line.find_last_not_of(' ');
        if (wsLeft == std::string::npos) continue;
        line = line.substr(wsLeft, wsRight - wsLeft + 1);

        if (line[0] == '#') continue;

        if (!colNamesParsed)
        {
            std::vector<std::string> colNames = split(line, '\t');
            eventMap["COL_NAMES"] = colNames;
            auto it = std::find(colNames.begin(), colNames.end(), "EventName");
            if (it == colNames.end())
            {
                std::cerr << "ERROR: First row does not contain EventName\n";
                return false;
            }
            eventNamePos = static_cast<int>(it - colNames.begin());
            colNamesParsed = true;
            continue;
        }
        std::vector<std::string> entry = split(line, '\t');
        if (eventNamePos < static_cast<int>(entry.size()))
            eventMap[entry[eventNamePos]] = entry;
    }
    m_eventMapsTSV.push_back(std::move(eventMap));
    return true;
}

bool PerfmonEventResolver::parseMapfile(const std::string& cpuFamilyModel, const std::string& prefix,
                                        std::multimap<std::string, std::string>& eventFiles)
{
    const std::string mapfilePath = prefix + "/mapfile.csv";

    std::ifstream in(mapfilePath);
    if (!in.is_open())
    {
        std::cerr << "ERROR: File " << mapfilePath << " can't be opened.\n";
        std::cerr << "       use -ep <perfmon_directory> option to specify the perfmon directory,\n";
        std::cerr << "       or run 'git clone https://github.com/intel/perfmon' to download the perfmon event repository\n";
        return false;
    }

    std::string line;
    int32 fmsPos = -1, filenamePos = -1, eventTypePos = -1;

    if (std::getline(in, line))
    {
        auto header = split(line, ',');
        for (int32 i = 0; i < static_cast<int32>(header.size()); ++i)
        {
            if (header[i] == "Family-model") fmsPos = i;
            else if (header[i] == "Filename") filenamePos = i;
            else if (header[i] == "EventType") eventTypePos = i;
        }
    }
    else
    {
        std::cerr << "ERROR: Can't read first line from mapfile.csv\n";
        return false;
    }

    if (fmsPos < 0 || filenamePos < 0 || eventTypePos < 0)
    {
        std::cerr << "ERROR: mapfile.csv header missing required columns\n";
        return false;
    }

    bool headerPrinted = false;
    while (std::getline(in, line))
    {
        auto tokens = split(line, ',');
        const int32 tokenCount = static_cast<int32>(tokens.size());
        if (fmsPos >= tokenCount || filenamePos >= tokenCount || eventTypePos >= tokenCount)
        {
            std::cerr << "WARNING: skipping malformed mapfile.csv line: " << line << "\n";
            continue;
        }

        std::regex fmsRegex(tokens[fmsPos]);
        std::cmatch fmsMatch;
        if (std::regex_search(cpuFamilyModel.c_str(), fmsMatch, fmsRegex))
        {
            if (!headerPrinted)
            {
                std::cerr << "Matched event files:\n";
                headerPrinted = true;
            }
            std::cerr << tokens[fmsPos] << " " << tokens[eventTypePos] << " " << tokens[filenamePos] << "\n";
            eventFiles.insert(std::make_pair(tokens[eventTypePos], tokens[filenamePos]));
        }
    }

    if (eventFiles.empty())
    {
        std::cerr << "ERROR: CPU " << cpuFamilyModel << " not found in mapfile.csv\n";
        return false;
    }
    return true;
}

bool PerfmonEventResolver::loadEventFile(const std::string& eventType, const std::string& filename, const std::string& prefix)
{
    if (eventType != "core" && eventType != "uncore" && eventType != "uncore experimental")
        return true;

    const std::string path1 = prefix + filename;
    const std::string path2 = prefix + filename.substr(filename.rfind('/'));
    const std::string path3 = getInstallPathPrefix() + "perfmon" + filename;

    std::string path;
    if (std::ifstream(path1).good())
        path = path1;
    else if (std::ifstream(path2).good())
        path = path2;
    else if (std::ifstream(path3).good())
        path = path3;
    else
    {
        std::cerr << "ERROR: Can't open event file at " << path1 << " or " << path2 << " or " << path3 << "\n";
        std::cerr << "Make sure you have downloaded " << filename
                  << " from https://raw.githubusercontent.com/intel/perfmon/main"
                  << filename << "\n";
        return false;
    }

    try
    {
        if (path.find(".json") != std::string::npos)
        {
            m_jsonParsers.push_back(std::make_shared<simdjson::dom::parser>());
            auto jsonObjects = m_jsonParsers.back()->load(path);
            if (jsonObjects["Header"].error() != simdjson::NO_SUCH_FIELD) jsonObjects = jsonObjects["Events"];

            for (simdjson::dom::object eventObj : jsonObjects)
            {
                const std::string eventName{eventObj["EventName"].get_c_str()};
                if (!eventName.empty()) m_eventMapJSON[eventName] = eventObj;
            }
        }
        else if (path.find(".tsv") != std::string::npos)
        {
            if (!parseTSV(path)) return false;
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Error while parsing " << path << ": " << e.what() << "\n";
        return false;
    }
    return true;
}

bool PerfmonEventResolver::loadPerfmonEvents(const std::string& cpuFamilyModel, const std::string& prefix)
{
    std::multimap<std::string, std::string> eventFiles;
    if (!parseMapfile(cpuFamilyModel, prefix, eventFiles)) return false;

    for (const auto& [eventType, filename] : eventFiles)
    {
        if (!loadEventFile(eventType, filename, prefix)) return false;
    }

    return !m_eventMapJSON.empty() || !m_eventMapsTSV.empty();
}

bool PerfmonEventResolver::loadPMUDeclarations(const std::string& cpuFamilyModel, int stepping, const std::string& prefix)
{
    // Extract family and model from the family-model string for stepping iteration
    // Format: "GenuineIntel-6-6A" -> we append "-<stepping>"
    std::string path;
    std::string errMsg;

    // Normalize: strip trailing slashes so find_last_of correctly finds the parent separator
    std::string normalizedPrefix = prefix;
    while (!normalizedPrefix.empty() && normalizedPrefix.back() == '/')
        normalizedPrefix.pop_back();

    size_t lastSlash = normalizedPrefix.find_last_of('/');
    std::string baseDir = (lastSlash != std::string::npos) ? normalizedPrefix.substr(0, lastSlash) : ".";

    for (int s = stepping; s >= 0; --s)
    {
        const std::string relPath = "PMURegisterDeclarations/" + cpuFamilyModel + "-" + std::to_string(s) + ".json";

        const std::string candidates[] = {
            baseDir + "/" + relPath,              // sibling of perfmon dir (build output / install)
            normalizedPrefix + "/" + relPath,     // inside the provided prefix
            relPath,                              // relative to CWD
            getInstallPathPrefix() + relPath,     // system install path
        };

        for (const auto& declPath : candidates)
        {
            std::ifstream in(declPath);
            if (in.is_open())
            {
                path = declPath;
                in.close();
                break;
            }
        }
        if (!path.empty()) break;

        errMsg = "PMURegisterDeclarations file not found for " + cpuFamilyModel + " stepping " + std::to_string(s);
    }

    if (path.empty())
    {
        std::cerr << "ERROR: " << errMsg << "\n";
        return false;
    }

    try
    {
        m_jsonParsers.push_back(std::make_shared<simdjson::dom::parser>());
        m_pmuDeclarations = std::make_shared<simdjson::dom::element>();
        *m_pmuDeclarations = m_jsonParsers.back()->load(path);
        m_pmuDeclPath = path;
    }
    catch (std::exception& e)
    {
        std::cerr << "Error while parsing " << path << ": " << e.what() << "\n";
        return false;
    }
    return true;
}

bool PerfmonEventResolver::isEvent(const std::string& eventName) const
{
    if (m_localEvents.find(eventName) != m_localEvents.end()) return true;

    if (m_eventMapJSON.find(eventName) != m_eventMapJSON.end()) return true;

    for (const auto& tsvMap : m_eventMapsTSV)
    {
        if (tsvMap.find(eventName) != tsvMap.end()) return true;
    }
    return false;
}

bool PerfmonEventResolver::isField(const std::string& eventName,
                                    const std::string& fieldName) const
{
    auto localIt = m_localEvents.find(eventName);
    if (localIt != m_localEvents.end())
        return localIt->second.find(fieldName) != localIt->second.end();

    auto jsonIt = m_eventMapJSON.find(eventName);
    if (jsonIt != m_eventMapJSON.end())
    {
        auto fieldResult = jsonIt->second[fieldName];
        return fieldResult.error() != simdjson::NO_SUCH_FIELD;
    }

    for (const auto& tsvMap : m_eventMapsTSV)
    {
        auto eventIt = tsvMap.find(eventName);
        if (eventIt != tsvMap.end())
        {
            auto colIt = tsvMap.find("COL_NAMES");
            if (colIt == tsvMap.end()) continue;
            const auto& colNames = colIt->second;
            auto nameIt = std::find(colNames.begin(), colNames.end(), fieldName);
            if (nameIt != colNames.end())
            {
                size_t pos = nameIt - colNames.begin();
                return pos < eventIt->second.size();
            }
        }
    }
    return false;
}

std::string PerfmonEventResolver::getField(const std::string& eventName, const std::string& fieldName) const
{
    auto localIt = m_localEvents.find(eventName);
    if (localIt != m_localEvents.end())
    {
        auto fieldIt = localIt->second.find(fieldName);
        return (fieldIt != localIt->second.end()) ? fieldIt->second : "";
    }

    auto jsonIt = m_eventMapJSON.find(eventName);
    if (jsonIt != m_eventMapJSON.end())
    {
        auto fieldResult = jsonIt->second[fieldName];
        if (fieldResult.error() == simdjson::NO_SUCH_FIELD) return "";
        return std::string(fieldResult.get_c_str());
    }

    for (const auto& tsvMap : m_eventMapsTSV)
    {
        auto eventIt = tsvMap.find(eventName);
        if (eventIt != tsvMap.end())
        {
            auto colIt = tsvMap.find("COL_NAMES");
            if (colIt == tsvMap.end()) continue;
            const auto& colNames = colIt->second;
            auto nameIt = std::find(colNames.begin(), colNames.end(), fieldName);
            if (nameIt != colNames.end())
            {
                size_t pos = nameIt - colNames.begin();
                if (pos < eventIt->second.size()) return eventIt->second[pos];
            }
        }
    }
    return "";
}

std::vector<std::string> PerfmonEventResolver::getEventNames() const
{
    std::vector<std::string> names;
    names.reserve(m_eventMapJSON.size());
    for (const auto& [event, _] : m_eventMapJSON)
    {
        PCM_UNUSED(_);
        names.push_back(event);
    }

    for (const auto& tsvMap : m_eventMapsTSV)
    {
        for (const auto& [event, _] : tsvMap)
        {
            PCM_UNUSED(_);
            if (event != "COL_NAMES") names.push_back(event);
        }
    }
    return names;
}

std::vector<std::pair<std::string, std::string>> PerfmonEventResolver::getEventFields(const std::string& eventName) const
{
    std::vector<std::pair<std::string, std::string>> fields;
    auto jsonIt = m_eventMapJSON.find(eventName);
    if (jsonIt != m_eventMapJSON.end())
    {
        for (const auto& kv : jsonIt->second)
        {
            std::string key{kv.key.begin(), kv.key.end()};
            std::string_view val;
            if (!kv.value.get(val))
                fields.push_back({key, std::string(val)});
            else
                fields.push_back({key, ""});
        }
        return fields;
    }

    for (const auto& tsvMap : m_eventMapsTSV)
    {
        auto eventIt = tsvMap.find(eventName);
        if (eventIt != tsvMap.end())
        {
            auto colIt = tsvMap.find("COL_NAMES");
            if (colIt == tsvMap.end()) continue;
            const auto& colNames = colIt->second;
            for (size_t i = 0; i < colNames.size() && i < eventIt->second.size(); ++i)
                fields.push_back({colNames[i], eventIt->second[i]});
            return fields;
        }
    }
    return fields;
}

std::string PerfmonEventResolver::mapPMUName(const std::string& unit) const
{
    std::string lower = unit;
    lowerCase(lower);
    auto it = s_pmuNameMap.find(lower);
    return (it != s_pmuNameMap.end()) ? it->second : lower;
}

bool PerfmonEventResolver::resolveEvent(const std::string& eventName, std::string& pmuName, PCM::RawEventConfig& config) const
{
    if (!m_initialized || !isEvent(eventName)) return false;

    config = PCM::RawEventConfig{{0, 0, 0, 0, 0}, eventName};

    // Determine PMU name from Unit field
    pmuName = !isField(eventName, "Unit") ? "core" : mapPMUName(getField(eventName, "Unit"));

    // Look up PMU register declarations
    auto pmuObj = (*m_pmuDeclarations)[pmuName];
    if (pmuObj.error() == simdjson::NO_SUCH_FIELD)
    {
        std::cerr << "ERROR: PMU \"" << pmuName << "\" not found in PMURegisterDeclarations for event " << eventName << "\n";
        return false;
    }

    simdjson::dom::object pmuDeclObj;
    try
    {
        pmuDeclObj = (*m_pmuDeclarations)[pmuName]["programmable"].get_object();
    }
    catch (const std::exception& e)
    {
        std::cerr << "ERROR: No programmable section for PMU \"" << pmuName << "\": " << e.what() << "\n";
        return false;
    }

    auto setConfig = [](PCM::RawEventConfig& cfg, const simdjson::dom::object& fieldDesc, uint64 value, int64_t position)
    {
        const auto cfgIdx = uint64_t(fieldDesc["Config"]);
        if (cfgIdx >= cfg.first.size())
            throw std::runtime_error("Config field value is out of bounds");
        const auto width = uint64_t(fieldDesc["Width"]);
        cfg.first[cfgIdx] = insertBits(cfg.first[cfgIdx], value, position, width);
    };

    for (const auto& registerKeyValue : pmuDeclObj)
    {
        simdjson::dom::object fieldDesc = registerKeyValue.value;
        const std::string fieldName{registerKeyValue.key.begin(), registerKeyValue.key.end()};

        if (fieldName == "MSRIndex")
        {
            std::string msrIndexStr = getField(eventName, fieldName);
            if (msrIndexStr.empty()) continue;
            lowerCase(msrIndexStr);
            if (msrIndexStr == "0" || msrIndexStr == "0x00") continue;

            // Use first MSR index if comma-separated
            auto msrIndexes = split(msrIndexStr, ',');
            if (msrIndexes.empty()) continue;
            std::string selectedMsr = msrIndexes[0];

            try
            {
                simdjson::dom::object msrObject = registerKeyValue.value[selectedMsr];
                std::string msrValueStr = getField(eventName, "MSRValue");
                if (!msrValueStr.empty())
                {
                    const auto value = read_number(msrValueStr.c_str());
                    const auto position = int64_t(msrObject["Position"]);
                    setConfig(config, msrObject, value, position);
                }
            }
            catch (std::exception&)
            {
                // MSR sub-key not found in declarations, skip
            }
            continue;
        }

        const int64_t position = int64_t(fieldDesc["Position"]);
        if (position == -1) continue; // field ignored per declarations

        if (!isField(eventName, fieldName))
        {
            // Use DefaultValue if available
            if (fieldDesc["DefaultValue"].error() == simdjson::NO_SUCH_FIELD)
            {
                std::cerr << "ERROR: DefaultValue not provided for field \"" << fieldName << "\" in PMURegisterDeclarations\n";
                return false;
            }
            const auto cfgIdx = uint64_t(fieldDesc["Config"]);
            if (cfgIdx >= config.first.size())
                throw std::runtime_error("Config field value is out of bounds");
            config.first[cfgIdx] |= uint64_t(fieldDesc["DefaultValue"]) << position;
        }
        else
        {
            std::string fieldValueStr = getField(eventName, fieldName);
            // Remove double quotes and use first value if comma-separated
            fieldValueStr.erase(
                std::remove(fieldValueStr.begin(), fieldValueStr.end(), '\"'),
                fieldValueStr.end());
            auto fieldValues = split(fieldValueStr, ',');
            if (fieldValues.empty()) continue;
            setConfig(config, fieldDesc, read_number(fieldValues[0].c_str()), position);
        }
    }
    return true;
}

#else // !PCM_SIMDJSON_AVAILABLE

bool PerfmonEventResolver::init(const std::string&, const std::string&)
{
    return false;
}

bool PerfmonEventResolver::isEvent(const std::string& eventName) const
{
    return m_localEvents.find(eventName) != m_localEvents.end();
}

bool PerfmonEventResolver::isField(const std::string& eventName, const std::string& fieldName) const
{
    auto localIt = m_localEvents.find(eventName);
    if (localIt != m_localEvents.end())
        return localIt->second.find(fieldName) != localIt->second.end();
    return false;
}

std::string PerfmonEventResolver::getField(const std::string& eventName, const std::string& fieldName) const
{
    auto localIt = m_localEvents.find(eventName);
    if (localIt != m_localEvents.end())
    {
        auto fieldIt = localIt->second.find(fieldName);
        return (fieldIt != localIt->second.end()) ? fieldIt->second : "";
    }
    return "";
}

std::string PerfmonEventResolver::mapPMUName(const std::string& unit) const { return unit; }
std::vector<std::string> PerfmonEventResolver::getEventNames() const { return {}; }
std::vector<std::pair<std::string, std::string>> PerfmonEventResolver::getEventFields(const std::string&) const { return {}; }

bool PerfmonEventResolver::resolveEvent(const std::string&, std::string&, PCM::RawEventConfig&) const
{
    return false;
}

#endif // PCM_SIMDJSON_AVAILABLE

} // namespace pcm
