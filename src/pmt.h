// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2024, Intel Corporation

#pragma once

#include "types.h"
#include <memory>

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
    virtual ~TelemetryArray() override;
    size_t size() override; // in bytes
    void load() override;
    uint64 get(size_t qWordOffset, size_t lsb, size_t msb) override;
};

} // namespace pcm