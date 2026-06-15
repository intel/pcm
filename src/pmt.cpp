// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation

#include "pmt.h"
#include "utils.h"
#include <assert.h>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <algorithm>
#include <cctype>

#ifdef PCM_PUGIXML_AVAILABLE
#include "pugixml/src/pugixml.hpp"
#endif

#ifdef __linux__
#include <unistd.h>
#endif

namespace pcm {

#ifdef __linux__
class TelemetryArrayLinux : public TelemetryArrayInterface
{
    TelemetryArrayLinux() = delete;
    typedef std::vector<std::FILE *> FileVector;
    typedef std::unordered_map<uint64, FileVector> FileMap;
    static std::shared_ptr<FileMap> TelemetryFiles;
    static FileMap & getTelemetryFiles()
    {
        if (!TelemetryFiles.get())
        {
            std::shared_ptr<FileMap> TelemetryFilesTemp = std::make_shared<FileMap>();
            auto paths = findPathsFromPattern("/sys/class/intel_pmt/telem*");
            for (auto & path : paths)
            {
                const auto guid = read_number(readSysFS((path + "/guid").c_str()).c_str());
                #if 0
                auto size = read_number(readSysFS((path + "/size").c_str()).c_str());
                std::cout << "path: " << path << " guid: 0x" << std::hex << guid << " size: "<< std::dec << size <<  std::endl;
                #endif
                auto file = std::fopen((path + "/telem").c_str(), "rb");
                if (!file)
                {
                    std::cerr << "Error: failed to open " << path << "/telem" << std::endl;
                    continue;
                }
                TelemetryFilesTemp->operator[](guid).push_back(file);
            }
            
            // print the telemetry files
            for (auto & guid : *TelemetryFilesTemp)
            {
                auto & files = guid.second;
                for (auto & file : files)
                {
                    if (!file)
                    {
                        std::cerr << "Error: file is null" << std::endl;
                        continue;
                    }
                    // std::cout << "guid: 0x" << std::hex << guid.first << " file: " << file << std::endl;
                }
            }

            TelemetryFiles = TelemetryFilesTemp;
        }
        return *TelemetryFiles;
    }
    std::vector<unsigned char> data;
    size_t uid, instance;
public:
    TelemetryArrayLinux(const size_t uid_, const size_t instance_): uid(uid_), instance(instance_)
    {
        assert(instance < numInstances(uid));
        TelemetryArrayLinux::load();
    }
    static size_t numInstances(const size_t uid)
    {
        auto t = getTelemetryFiles();
        if (t.find(uid) == t.end())
        {
            return 0;
        }
        return t.at(uid).size();
    }
    static std::vector<size_t> getUIDs()
    {
        auto t = getTelemetryFiles();
        std::vector<size_t> result;
        for (auto & guid : t)
        {
            result.push_back(guid.first);
        }
        return result;
    }
    virtual ~TelemetryArrayLinux() override
    {
    }
    size_t size() override
    {
        return data.size();
    }
    void load() override
    {
        FILE * file = getTelemetryFiles().at(uid).at(instance);
        assert(file);
        // get the file size
        fseek(file, 0, SEEK_END);
        const auto pos = ftell(file);
        if (pos < 0)
        {
            std::cerr << "Error: failed to get file size" << std::endl;
            return;
        }
        const size_t fileSize = pos;
        fseek(file, 0, SEEK_SET);
        data.resize(fileSize);
        const size_t bytesRead = fread(data.data(), 1, fileSize, file);
        if (bytesRead != fileSize)
        {
            std::cerr << "Error: failed to read " << fileSize << " bytes from telemetry file" << std::endl;
        }
    }
    uint64 get(size_t qWordOffset, size_t lsb, size_t msb) override
    {
        assert(qWordOffset * sizeof(uint64) + sizeof(uint64) <= data.size());
        return extract_bits(*reinterpret_cast<uint64 *>(&data[qWordOffset * sizeof(uint64)]), lsb, msb);
    }
};

std::shared_ptr<TelemetryArrayLinux::FileMap> TelemetryArrayLinux::TelemetryFiles;

#else

class TelemetryArrayDummy : public TelemetryArrayInterface
{
    TelemetryArrayDummy() = delete;
public:
    TelemetryArrayDummy(const size_t /* uid */, const size_t /* instance */) {};
    static size_t numInstances(const size_t /* uid */) { return 0; };
    static std::vector<size_t> getUIDs() { return std::vector<size_t>(); };
    virtual ~TelemetryArrayDummy() override {};
    size_t size() override { return 0;}; // in bytes
    void load() override {};
    uint64 get(size_t , size_t , size_t ) override { return 0;} ;
};

#endif

TelemetryArray::TelemetryArray(const size_t uid, const size_t instance)
{
#ifdef __linux__
    impl = std::make_shared<TelemetryArrayLinux>(uid, instance);
#else
    impl = std::make_shared<TelemetryArrayDummy>(uid, instance);
#endif
}

size_t TelemetryArray::numInstances(const size_t uid)
{
#ifdef __linux__
    return TelemetryArrayLinux::numInstances(uid);
#else
    return TelemetryArrayDummy::numInstances(uid);
#endif
}

std::vector<size_t> TelemetryArray::getUIDs()
{
#ifdef __linux__
    return TelemetryArrayLinux::getUIDs();
#else
    return TelemetryArrayDummy::getUIDs();
#endif
}

TelemetryArray::~TelemetryArray() {}

size_t TelemetryArray::size()
{
    assert(impl.get());
    return impl->size();
}

void TelemetryArray::load()
{
    assert(impl.get());
    impl->load();
}

uint64 TelemetryArray::get(size_t qWordOffset, size_t lsb, size_t msb)
{
    assert(impl.get());
    return impl->get(qWordOffset, lsb, msb);
}


bool TelemetryDB::loadFromXML(const std::string& pmtXMLPath)
{
#ifdef PCM_PUGIXML_AVAILABLE
    pugi::xml_document doc;
    auto result = doc.load_file((pmtXMLPath + "/xml/pmt.xml").c_str());

    if (!result)
    {
        std::cerr << "Error: failed to load " << pmtXMLPath << "/xml/pmt.xml" << std::endl;
        return false;
    }

    constexpr bool debug = false;

    auto guids = TelemetryArray::getUIDs();
    for (pugi::xml_node mapping: doc.child("pmt").child("mappings").children("mapping"))
    {
        auto guid = read_number(mapping.attribute("guid").value());
        if (std::find(guids.begin(), guids.end(), guid) == guids.end())
        {
            // std::cerr << " guid " << std::hex << guid << " not found in telemetry files" << std::endl;
            continue;
        }
        if (debug) std::cout << "Found mapping with guid: " << mapping.attribute("guid").value() << std::endl;
        if (debug) std::cout << "  Description: " << mapping.child("description").text().as_string() << std::endl;
        const auto xmlset = mapping.child("xmlset");
        const auto basedir = xmlset.child("basedir").text().as_string();
        const auto aggregator = xmlset.child("aggregator").text().as_string();
        const auto aggregator_path = pmtXMLPath + "/xml/" + basedir + "/" + aggregator;
        if (debug) std::cout << "  Aggregator XML path: " << aggregator_path << std::endl;

        pugi::xml_document aggregatorDoc;
        auto aggregatorResult = aggregatorDoc.load_file(aggregator_path.c_str());
        if (!aggregatorResult)
        {
            std::cerr << "Error: failed to load " << aggregator_path << std::endl;
            return false;
        }

        auto aggregatorNode = aggregatorDoc.child("TELEM:Aggregator");
        const std::string aggregatorName = aggregatorNode.child("TELEM:name").text().as_string();
        if (debug) std::cout << "  Agregator name: " << aggregatorName << std::endl;
        PMTRecord record;
        record.uid = guid;
        for (pugi::xml_node sampleGroup: aggregatorNode.children("TELEM:SampleGroup"))
        {
            const auto sampleID = sampleGroup.attribute("sampleID").as_uint();
            if (debug) std::cout << "    SampleID: " << sampleID << std::endl;
            record.qWordOffset = sampleID;
            for (pugi::xml_node sample: sampleGroup.children("TELC:sample"))
            {
                const auto name = sample.attribute("name").as_string();
                const std::string sampleSubGroup = sample.child("TELC:sampleSubGroup").text().as_string();
                record.fullName = aggregatorName + "." + sampleSubGroup + "." + name;
                record.sampleType = sample.child("TELC:sampleType").text().as_string();
                record.lsb = sample.child("TELC:lsb").text().as_uint();
                record.msb = sample.child("TELC:msb").text().as_uint();
                record.description = sample.child("TELC:description").text().as_string();
                if (debug) std::cout << "      ";
                if (debug) record.print(std::cout);
                records.push_back(record);
            }
        }

        if (debug) std::cout << std::endl;
    }

    return true;
#else
    (void)pmtXMLPath; // suppress warning
    std::cerr << "INFO: pugixml library is not available" << std::endl;
    return false;
#endif
}

std::vector<TelemetryDB::PMTRecord> TelemetryDB::lookup(const std::string & name)
{
    std::vector<PMTRecord> result;
    for (auto & record : records)
    {
        if (record.fullName.find(name) != std::string::npos)
        {
            result.push_back(record);
        }
    }
    return result;
}

std::vector<TelemetryDB::PMTRecord> TelemetryDB::ilookup(const std::string & name)
{
    std::vector<PMTRecord> result;
    auto to_lower = [](const std::string & s) -> std::string
    {
        std::string result;
        for (auto c : s)
        {
            result.push_back(std::tolower(c));
        }
        return result;
    };
    for (auto & record : records)
    {
        if (to_lower(record.fullName).find(to_lower(name)) != std::string::npos)
        {
            result.push_back(record);
        }
    }
    return result;
}


}; // namespace pcm