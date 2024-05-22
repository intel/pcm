// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation

#include "pmt.h"
#include "utils.h"
#include <assert.h>
#include <vector>
#include <unordered_map>
#include <iostream>

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

}; // namespace pcm