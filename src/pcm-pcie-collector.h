// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation
#pragma once

#include "pcm-pcie.h"

#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <cstdint>
#include <iostream>

class PCIeCollector {
public:
    struct SocketBW {
        uint64_t readBytes  = 0;
        uint64_t writeBytes = 0;
    };

    static PCIeCollector* getInstance() {
        static PCIeCollector instance;
        return instance.supported_ ? &instance : nullptr;
    }

    PCIeCollector(PCIeCollector const &)            = delete;
    PCIeCollector& operator=(PCIeCollector const &) = delete;
    PCIeCollector(PCIeCollector &&)                 = delete;
    PCIeCollector& operator=(PCIeCollector &&)      = delete;

    static constexpr uint32_t kDefaultIntervalMs = 2000;

    void startBackground(uint32_t intervalMs = kDefaultIntervalMs) {
        bool expected = false;
        if (!bgRunning_.compare_exchange_strong(expected, true)) return;
        bgThread_ = std::thread([this, intervalMs]() {
            while (bgRunning_.load()) {
                collect();
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait_for(lk, std::chrono::milliseconds(intervalMs),
                             [this] { return !bgRunning_.load(); });
            }
        });
    }

    void stop() {
        bool expected = true;
        if (!bgRunning_.compare_exchange_strong(expected, false)) return;
        cv_.notify_one();
        if (bgThread_.joinable()) bgThread_.join();
    }

    ~PCIeCollector() { stop(); }

    uint32_t socketCount() const { return socketCount_; }

    SocketBW getSocket(uint32_t skt) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (skt < snapshot_.size()) return snapshot_[skt];
        return {};
    }

    SocketBW getAggregate() const {
        std::lock_guard<std::mutex> lk(mu_);
        return aggregate_;
    }

    std::vector<uint64_t> getRawValues(uint32_t skt) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (skt < rawValues_.size()) return rawValues_[skt];
        return {};
    }

    std::vector<uint64_t> getRawAggregate() const {
        std::lock_guard<std::mutex> lk(mu_);
        return rawAggValues_;
    }

    const std::vector<std::string>& eventNames() const { return eventNames_; }
    uint32_t numEvents() const { return static_cast<uint32_t>(eventNames_.size()); }
    bool isSupported() const { return supported_; }

private:
    PCIeCollector() {
        try {
            PCM* pcm = PCM::getInstance();
            static constexpr uint32_t kPmonMultiplier = 1000;
            platform_.reset(IPlatform::getPlatform(pcm, false, true, false, kPmonMultiplier));
            if (platform_) {
                supported_ = true;
                socketCount_ = pcm->getNumSockets();
                const auto& names = platform_->getEventNames();
                eventNames_.assign(names.begin(), names.end());
                snapshot_.resize(socketCount_);
                rawValues_.resize(socketCount_, std::vector<uint64_t>(eventNames_.size(), 0));
                rawAggValues_.resize(eventNames_.size(), 0);
                cumSnapshot_.resize(socketCount_);
                cumRawValues_.resize(socketCount_, std::vector<uint64_t>(eventNames_.size(), 0));
            }
        } catch (const std::exception& e) {
            std::cerr << "PCIeCollector: " << e.what() << " (PCIe metrics disabled)\n";
            supported_ = false;
        }
    }

    void collect() {
        if (!platform_ || !bgRunning_.load()) return;
        platform_->cleanup();
        platform_->getEvents();

        SocketBW aggDelta{0, 0};
        const uint32_t nEvt = numEvents();

        for (uint32_t s = 0; s < socketCount_; ++s) {
            uint64_t dr = platform_->getReadBw(s, IPlatform::TOTAL);
            uint64_t dw = platform_->getWriteBw(s, IPlatform::TOTAL);
            cumSnapshot_[s].readBytes  += dr;
            cumSnapshot_[s].writeBytes += dw;
            aggDelta.readBytes  += dr;
            aggDelta.writeBytes += dw;
            for (uint32_t i = 0; i < nEvt; ++i)
                cumRawValues_[s][i] += platform_->event(s, IPlatform::TOTAL, i);
        }
        cumAggregate_.readBytes  += aggDelta.readBytes;
        cumAggregate_.writeBytes += aggDelta.writeBytes;

        std::vector<uint64_t> rawAgg(nEvt, 0);
        for (uint32_t s = 0; s < socketCount_; ++s)
            for (uint32_t i = 0; i < nEvt; ++i)
                rawAgg[i] += cumRawValues_[s][i];

        std::lock_guard<std::mutex> lk(mu_);
        snapshot_ = cumSnapshot_;
        aggregate_ = cumAggregate_;
        rawValues_ = cumRawValues_;
        rawAggValues_ = rawAgg;
    }

    std::unique_ptr<IPlatform> platform_;
    bool supported_ = false;
    uint32_t socketCount_ = 0;
    std::vector<std::string> eventNames_;

    std::vector<SocketBW> snapshot_;
    SocketBW aggregate_;
    std::vector<std::vector<uint64_t>> rawValues_;
    std::vector<uint64_t> rawAggValues_;

    std::vector<SocketBW> cumSnapshot_;
    SocketBW cumAggregate_;
    std::vector<std::vector<uint64_t>> cumRawValues_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::thread bgThread_;
    std::atomic<bool> bgRunning_{false};
};
