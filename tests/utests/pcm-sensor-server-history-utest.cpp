// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

// Regression tests for configurable collection interval:
// 1. History selection preserves elapsed-time semantics under fractional intervals
// 2. High-frequency retention bounds memory footprint
// 3. Shutdown synchronization properly wakes waiting requests and fetcher threads
// 4. Default 1.0 second behavior remains backwards-compatible

#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>
#include <string>
#include <stdexcept>

#define UNIT_TEST 1
#include "../../src/pcm-sensor-server.cpp"
#undef UNIT_TEST

#include <gtest/gtest.h>

namespace {

// Helper to create a dummy Aggregator for testing history selection
std::shared_ptr<Aggregator> makeDummyAggregator() {
    return std::make_shared<Aggregator>();
}

} // namespace

// TEST 1 — HISTORY / TIMESTAMP SEMANTICS
// Verify that changing the collection interval to a fractional value (e.g. 0.1s)
// selects historical samples based on elapsed timestamp, NOT raw sample index == seconds.
TEST(PcmSensorServerHistoryTest, FractionalIntervalPreservesElapsedSecondsSemantics) {
    const double interval = 0.1;
    HTTPServer server( interval, false );

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<Aggregator>> samples;
    // 21 samples from t0 to t0 + 2.0s in 0.1s steps
    for ( size_t i = 0; i <= 20; ++i ) {
        auto agp = makeDummyAggregator();
        samples.push_back( agp );
        server.addAggregator( agp, t0 + std::chrono::milliseconds( i * 100 ) );
    }

    // Request 1 second of history relative to the latest sample (index2 = 0)
    auto pair1 = server.getAggregators( 1, 0 );
    EXPECT_EQ( pair1.second, samples[20] ); // newest sample (t0 + 2.0s)
    // 1 second before (t0 + 2.0s) is (t0 + 1.0s), which is samples[10].
    // If the code treated seconds as sample index, it would have returned samples[19] (0.1s ago).
    EXPECT_EQ( pair1.first, samples[10] );
    EXPECT_NE( pair1.first, samples[19] );

    // Request 2 seconds of history relative to the latest sample
    auto pair2 = server.getAggregators( 2, 0 );
    EXPECT_EQ( pair2.second, samples[20] );
    // 2 seconds before (t0 + 2.0s) is t0, which is samples[0].
    EXPECT_EQ( pair2.first, samples[0] );
}

// TEST 2 — HIGH-FREQUENCY RETENTION BOUND
// Verify that at very small collection intervals (e.g. 0.001s / 1 ms),
// the retained history is strictly bounded and does NOT grow to ~29/interval (29,001) objects.
TEST(PcmSensorServerHistoryTest, HighFrequencyRetentionIsBounded) {
    const double interval = 0.001; // 1 ms
    HTTPServer server( interval, false );

    const auto t0 = std::chrono::steady_clock::now();
    // Simulate 5,000 high-frequency samples spanning 5.0 seconds
    for ( size_t i = 0; i < 5000; ++i ) {
        server.addAggregator( makeDummyAggregator(), t0 + std::chrono::milliseconds( i ) );
    }

    // Verify recent and coarse history sizes are strictly bounded
    EXPECT_LE( server.recentHistorySize(), HTTPServer::maxRecentSamples_ );
    EXPECT_LE( server.coarseHistorySize(), HTTPServer::maxCoarseSamples_ );
    EXPECT_LE( server.totalHistorySize(), HTTPServer::maxRecentSamples_ + HTTPServer::maxCoarseSamples_ );

    // Verify history can still answer a 3-second elapsed query correctly
    auto pair = server.getAggregators( 3, 0 );
    EXPECT_NE( pair.first, nullptr );
    EXPECT_NE( pair.second, nullptr );
    EXPECT_NE( pair.first, pair.second );
}

// TEST 3 — SHUTDOWN WAKE-UP
// Part A: Verify that stopping the server immediately unblocks an outstanding getAggregators() call.
TEST(PcmSensorServerHistoryTest, StoppingServerWakesBlockedGetAggregatorsWait) {
    HTTPServer server( 1.0, false );

    std::atomic<bool> workerStarted( false );
    std::atomic<bool> threwExpectedException( false );

    std::thread worker( [&]() {
        workerStarted = true;
        try {
            // Request 10 seconds of history when no samples have been collected.
            // This will block inside agVectorCV_.wait() until satisfied or stopped.
            server.getAggregators( 10, 0 );
        } catch ( const std::runtime_error& e ) {
            std::string msg = e.what();
            if ( msg.find( "Server stopped" ) != std::string::npos ) {
                threwExpectedException = true;
            }
        }
    } );

    while ( !workerStarted.load() ) {
        std::this_thread::yield();
    }
    // Short sleep to ensure the worker thread is registered in condition_variable wait
    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );

    server.stop();
    worker.join();

    EXPECT_TRUE( threwExpectedException.load() );
}

// TEST 3 Part B: Verify that stopping PeriodicCounterFetcher wakes up a long wait_until
TEST(PcmSensorServerHistoryTest, StoppingFetcherWakesLongWaitUntil) {
    HTTPServer server( 3600.0, false ); // 1-hour interval
    PeriodicCounterFetcher pcf( &server, 3600.0 );

    const auto startTime = std::chrono::steady_clock::now();
    std::atomic<bool> pcfExited( false );

    std::thread fetcherThread( [&]() {
        pcf.execute();
        pcfExited = true;
    } );

    std::this_thread::sleep_for( std::chrono::milliseconds( 30 ) );
    pcf.stop();

    fetcherThread.join();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime ).count();

    EXPECT_TRUE( pcfExited.load() );
    // Must exit promptly (within 2 seconds), rather than waiting the 1-hour deadline
    EXPECT_LT( elapsed, 2000 );
}

// TEST 4 — DEFAULT REGRESSION
// Verify that the default 1.0 second interval retains the expected 30-sample history
// and correctly resolves 1..29 elapsed seconds.
TEST(PcmSensorServerHistoryTest, DefaultIntervalPreservesExistingContract) {
    HTTPServer server( 1.0, false );

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::shared_ptr<Aggregator>> samples;
    // Add 30 samples at 1.0 second intervals (0s to 29s)
    for ( size_t i = 0; i < 30; ++i ) {
        auto agp = makeDummyAggregator();
        samples.push_back( agp );
        server.addAggregator( agp, t0 + std::chrono::seconds( i ) );
    }

    EXPECT_EQ( server.recentHistorySize(), 30 );
    EXPECT_EQ( server.coarseHistorySize(), 0 ); // Coarse queue unused when interval >= 1.0s

    // Test /persecond/1 (1 second ago)
    auto pair1 = server.getAggregators( 1, 0 );
    EXPECT_EQ( pair1.second, samples[29] );
    EXPECT_EQ( pair1.first, samples[28] );

    // Test /persecond/10 (10 seconds ago)
    auto pair10 = server.getAggregators( 10, 0 );
    EXPECT_EQ( pair10.second, samples[29] );
    EXPECT_EQ( pair10.first, samples[19] );

    // Test /persecond/29 (maximum allowed seconds)
    auto pair29 = server.getAggregators( 29, 0 );
    EXPECT_EQ( pair29.second, samples[29] );
    EXPECT_EQ( pair29.first, samples[0] );

    // Boundary check: requesting 30 seconds throws (exceeds maxPerSecondSeconds_)
    EXPECT_THROW( server.getAggregators( 30, 0 ), std::runtime_error );
    // Boundary check: requesting 0 seconds throws
    EXPECT_THROW( server.getAggregators( 0, 0 ), std::runtime_error );

    // Adding 31st sample maintains max cap of 30
    auto extraAgp = makeDummyAggregator();
    server.addAggregator( extraAgp, t0 + std::chrono::seconds( 30 ) );
    EXPECT_EQ( server.recentHistorySize(), 30 );
}
