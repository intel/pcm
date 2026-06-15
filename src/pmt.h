// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation

#pragma once

#include "types.h"
#include <memory>
#include <vector>

namespace pcm {

class TelemetryArrayInterface
{
public:
    virtual size_t size() = 0; // in bytes
    virtual size_t numQWords()
    {
        return size() / sizeof(uint64);
    }
    virtual void load() = 0;
    virtual uint64 get(size_t qWordOffset, size_t lsb, size_t msb) = 0;
    virtual ~TelemetryArrayInterface() {};
};

class TelemetryArray : public TelemetryArrayInterface
{
    TelemetryArray() = delete;
    std::shared_ptr<TelemetryArrayInterface> impl;
public:
    TelemetryArray(const size_t /* uid */, const size_t /* instance */);
    static size_t numInstances(const size_t /* uid */);
    static std::vector<size_t> getUIDs();
    virtual ~TelemetryArray() override;
    size_t size() override; // in bytes
    void load() override;
    uint64 get(size_t qWordOffset, size_t lsb, size_t msb) override;
};

class TelemetryDB
{
public:
    struct PMTRecord
    {
        size_t uid;
        std::string fullName;
        std::string sampleType;
        size_t qWordOffset;
        uint32 lsb;
        uint32 msb;
        std::string description;
        void print(std::ostream & os) const
        {
            os << "uid: " << uid << " fullName: " << fullName << " description: \"" << description <<
                "\" sampleType: " << sampleType << " qWordOffset: " << qWordOffset << " lsb: " << lsb << " msb: " << msb << std::endl;
        }
    };
    std::vector<PMTRecord> records;
    TelemetryDB() = default;
    bool loadFromXML(const std::string& pmtXMLPath);
    virtual ~TelemetryDB() = default;
    std::vector<PMTRecord> lookup(const std::string & name);
    std::vector<PMTRecord> ilookup(const std::string & name);
};

} // namespace pcm