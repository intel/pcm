// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2023-2024, Intel Corporation
// written by Roman Dementiev
//

#pragma once

/*!     \file tpmi.h
        \brief Interface to access TPMI registers

*/

#include "mmio.h"
#include <vector>

namespace pcm {

class TPMIHandleInterface
{
public:
    virtual size_t getNumEntries() const = 0;
    virtual uint64 read64(size_t entryPos) = 0;
    virtual void write64(size_t entryPos, uint64 val) = 0;
    virtual ~TPMIHandleInterface() {}
};

class TPMIHandle : public TPMIHandleInterface
{
    TPMIHandle(const TPMIHandle&) = delete;
    TPMIHandle& operator = (const TPMIHandle&) = delete;
    std::shared_ptr<TPMIHandleInterface> impl;
public:
    static size_t getNumInstances();
    static void setVerbose(const bool);
    TPMIHandle(const size_t instance_, const size_t ID_, const size_t offset_, const bool readonly_ = true);
    size_t getNumEntries() const override;
    uint64 read64(size_t entryPos) override;
    void write64(size_t entryPos, uint64 val) override;
};

} // namespace pcm
