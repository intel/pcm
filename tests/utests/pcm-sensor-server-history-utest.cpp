// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

// Regression tests for configurable collection interval:
// 1. History selection preserves elapsed-time semantics under fractional intervals
// 2. High-frequency retention bounds memory footprint
// 3. Shutdown synchronization properly wakes waiting requests and fetcher threads
// 4. Default 1.0 second behavior remains backwards-compatible
// 5. Coarse queue retention spans the 29-second history window for subsecond intervals
// 6. Socket-free construction isolates tests and avoids binding privileged/fixed ports

#include <chrono>
#include <thread>
#include <memory>
#include <vector>
#include <atomic>
#include <string>
#include <stdexcept>
#include <cmath>

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

// TEST 1 - HISTORY / TIMESTAMP SEMANTICS
// Verify that changing the collection interval to a fractional value (e.g. 0.1s)
// selects historical samples based on elapsed timestamp, NOT raw sample index == seconds.
TEST(PcmSensorServerHistoryTest, FractionalIntervalPreservesElapsedSecondsSemantics) {
    const double interval = 0.1;
    HTTPServer server( interval, false, false );

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

// TEST 2 - HIGH-FREQUENCY RETENTION BOUND
// Verify that at very small collection intervals (e.g. 0.001s / 1 ms),
// the retained history is strictly bounded and does NOT grow to ~29/interval (29,001) objects.
TEST(PcmSensorServerHistoryTest, HighFrequencyRetentionIsBounded) {
    const double interval = 0.001; // 1 ms
    HTTPServer server( interval, false, false );

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

// TEST 3 - SHUTDOWN WAKE-UP
// Part A: Verify that stopping the server immediately unblocks an outstanding getAggregators() call.
TEST(PcmSensorServerHistoryTest, StoppingServerWakesBlockedGetAggregatorsWait) {
    HTTPServer server( 1.0, false, false );

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

// TEST 3 Part B: Verify that stopping PeriodicCounterFetcher coordinates lifecycle safely
TEST(PcmSensorServerHistoryTest, StoppingFetcherWakesLongWaitUntil) {
    HTTPServer server( 3600.0, false, false ); // 1-hour interval
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

// TEST 4 - DEFAULT REGRESSION
// Verify that the default 1.0 second interval retains the expected 30-sample history
// and correctly resolves 1..29 elapsed seconds.
TEST(PcmSensorServerHistoryTest, DefaultIntervalPreservesExistingContract) {
    HTTPServer server( 1.0, false, false );

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

// TEST 5 - COARSE QUEUE 29-SECOND COVERAGE FOR SUBSECOND INTERVALS
// Verify that coarseQueue_ with capacity 60 retains enough history to satisfy
// the 29-second window for intervals such as 0.75s and 0.4s.
TEST(PcmSensorServerHistoryTest, SubsecondIntervalsCover29SecondWindow) {
    // Case A: interval = 0.75s
    {
        const double interval = 0.75;
        HTTPServer server( interval, false, false );
        const auto t0 = std::chrono::steady_clock::now();
        // 45 samples at 0.75s = 33.0 seconds of history
        for ( size_t i = 0; i < 45; ++i ) {
            server.addAggregator( makeDummyAggregator(), t0 + std::chrono::milliseconds( static_cast<long long>( i * 750 ) ) );
        }
        EXPECT_LE( server.coarseHistorySize(), HTTPServer::maxCoarseSamples_ );
        EXPECT_LE( server.recentHistorySize(), HTTPServer::maxRecentSamples_ );

        auto pair = server.getAggregators( 29, 0 );
        EXPECT_NE( pair.first, nullptr );
        EXPECT_NE( pair.second, nullptr );
    }

    // Case B: interval = 0.4s
    {
        const double interval = 0.4;
        HTTPServer server( interval, false, false );
        const auto t0 = std::chrono::steady_clock::now();
        // 85 samples at 0.4s = 33.6 seconds of history
        for ( size_t i = 0; i < 85; ++i ) {
            server.addAggregator( makeDummyAggregator(), t0 + std::chrono::milliseconds( static_cast<long long>( i * 400 ) ) );
        }
        EXPECT_LE( server.coarseHistorySize(), HTTPServer::maxCoarseSamples_ );
        EXPECT_LE( server.recentHistorySize(), HTTPServer::maxRecentSamples_ );

        auto pair = server.getAggregators( 29, 0 );
        EXPECT_NE( pair.first, nullptr );
        EXPECT_NE( pair.second, nullptr );
    }
}

// TEST 6 - NEAREST SAMPLE SELECTION ACCURACY
// Verify that getAggregators selects the historical sample closest in time to the target timestamp.
TEST(PcmSensorServerHistoryTest, NearestSampleSelectionAccuracy) {
    const double interval = 0.1;
    HTTPServer server( interval, false, false );
    const auto t0 = std::chrono::steady_clock::now();

    auto agpA = makeDummyAggregator();
    auto agpB = makeDummyAggregator();
    auto agpC = makeDummyAggregator();

    // Sample A at t0 (0.0s)
    server.addAggregator( agpA, t0 );
    // Sample B at t0 + 0.95s (closer to 1.0s target from t0 + 2.0s)
    server.addAggregator( agpB, t0 + std::chrono::milliseconds( 950 ) );
    // Sample C at t0 + 2.0s (reference sample, index2 = 0)
    server.addAggregator( agpC, t0 + std::chrono::milliseconds( 2000 ) );

    // Target is 1 second before C (t0 + 1.0s). B (0.95s) is closer (diff 0.05s) than A (0.0s, diff 1.0s).
    auto pair = server.getAggregators( 1, 0 );
    EXPECT_EQ( pair.second, agpC );
    EXPECT_EQ( pair.first, agpB );
}


// TEST 7 - INCOMPATIBLE INTERVAL REJECTION
// Verify that requests for durations shorter than the configured collection interval
// are rejected with std::invalid_argument instead of returning a misleading duration.
TEST(PcmSensorServerHistoryTest, IncompatibleIntervalRejectsSubIntervalDuration) {
    // Case A: 3600-second interval rejects 1-second and 10-second requests
    {
        HTTPServer server( 3600.0, false, false );
        EXPECT_THROW( server.getAggregators( 1, 0 ), std::invalid_argument );
        EXPECT_THROW( server.getAggregators( 10, 0 ), std::invalid_argument );
    }

    // Case B: 2.0-second interval rejects 1-second request, but accepts 2-second request
    {
        HTTPServer server( 2.0, false, false );
        EXPECT_THROW( server.getAggregators( 1, 0 ), std::invalid_argument );

        const auto t0 = std::chrono::steady_clock::now();
        auto agp1 = makeDummyAggregator();
        auto agp2 = makeDummyAggregator();
        server.addAggregator( agp1, t0 );
        server.addAggregator( agp2, t0 + std::chrono::seconds( 2 ) );

        auto pair = server.getAggregators( 2, 0 );
        EXPECT_EQ( pair.second, agp2 );
        EXPECT_EQ( pair.first, agp1 );
    }
}

// TEST 8 - MISMATCHED HISTORY ERROR BOUND
// Verify that if history has a huge gap and no sample is within acceptable tolerance,
// getAggregators throws std::runtime_error rather than returning a grossly mismatched snapshot.
TEST(PcmSensorServerHistoryTest, MismatchedHistoryThrowsWhenNoSampleMatchesDuration) {
    const double interval = 1.0;
    HTTPServer server( interval, false, false );
    const auto t0 = std::chrono::steady_clock::now();

    auto agpA = makeDummyAggregator();
    auto agpB = makeDummyAggregator();

    // Two samples 20 seconds apart
    server.addAggregator( agpA, t0 );
    server.addAggregator( agpB, t0 + std::chrono::seconds( 20 ) );

    // Requesting 5 seconds: neither agpA (20s ago, diff 15s) nor agpB (0s ago, diff 5s)
    // is within the acceptable tolerance for a 5-second window.
    EXPECT_THROW( server.getAggregators( 5, 0 ), std::runtime_error );
}

// TEST 9 - THREAD SAFETY OF ATOMIC STOPPED
// Verify that stopped_ flag is atomic, isStopped() reflects stop(), and concurrent reads are race-free.
TEST(PcmSensorServerHistoryTest, AtomicStoppedThreadSafety) {
    HTTPServer server( 1.0, false, false );
    EXPECT_FALSE( server.isStopped() );

    std::atomic<bool> startReading( false );
    std::atomic<bool> stopReading( false );
    std::vector<std::thread> readers;

    for ( int i = 0; i < 4; ++i ) {
        readers.emplace_back( [&]() {
            while ( !startReading.load() ) {
                std::this_thread::yield();
            }
            while ( !stopReading.load() ) {
                (void)server.isStopped();
            }
        } );
    }

    startReading = true;
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
    server.stop();
    EXPECT_TRUE( server.isStopped() );

    stopReading = true;
    for ( auto& t : readers ) {
        t.join();
    }
}


// TEST 10 - CONCURRENT STOP IDEMPOTENCY
// Verify that calling stop() concurrently from multiple threads is serialized,
// idempotent, and free from double-close or null-dereference races.
TEST(PcmSensorServerHistoryTest, ConcurrentStopIsIdempotentAndThreadSafe) {
    HTTPServer server( 1.0, false, false );
    EXPECT_FALSE( server.isStopped() );

    std::atomic<bool> startStopping( false );
    std::vector<std::thread> threads;

    for ( int i = 0; i < 8; ++i ) {
        threads.emplace_back( [&]() {
            while ( !startStopping.load() ) {
                std::this_thread::yield();
            }
            server.stop();
            EXPECT_TRUE( server.isStopped() );
        } );
    }

    startStopping = true;
    for ( auto& t : threads ) {
        t.join();
    }

    EXPECT_TRUE( server.isStopped() );
    // Sequential call after concurrent stops should also be a safe no-op
    server.stop();
    EXPECT_TRUE( server.isStopped() );
}


// TEST 11 - WORKER THREAD STOP DEADLOCK PREVENTION
// Verify that invoking stop() from inside a ThreadPool worker thread
// (such as an HTTP callback or Work item) does not self-join deadlock or throw std::system_error.
TEST(PcmSensorServerHistoryTest, StopFromWorkerThreadDoesNotSelfJoinDeadlock) {
    HTTPServer server( 1.0, false, false );
    EXPECT_FALSE( server.isStopped() );

    class WorkerStopJob : public pcm::Work {
    public:
        WorkerStopJob( HTTPServer* s, std::promise<bool>& p ) : server_( s ), promise_( p ) {}
        virtual void execute() override {
            try {
                server_->stop();
                promise_.set_value( true );
            } catch ( ... ) {
                promise_.set_exception( std::current_exception() );
            }
        }
    private:
        HTTPServer* server_;
        std::promise<bool>& promise_;
    };

    std::promise<bool> promise;
    auto future = promise.get_future();

    pcm::WorkQueue::getInstance()->addWork( new WorkerStopJob( &server, promise ) );

    // Must resolve cleanly without deadlock
    auto status = future.wait_for( std::chrono::seconds( 5 ) );
    ASSERT_EQ( status, std::future_status::ready );
    EXPECT_TRUE( future.get() );
    EXPECT_TRUE( server.isStopped() );

    // Sequential stop call from non-worker control thread is safe and idempotent
    EXPECT_NO_THROW( server.stop() );
    EXPECT_TRUE( server.isStopped() );
}


// TEST 12 - CONCURRENT WORKER THREADS STOP DEADLOCK PREVENTION
// Verify that concurrent stop() calls from multiple ThreadPool worker threads
// complete without deadlock, cross-worker join cycles, or std::system_error,
// and that a subsequent sequential stop() call is safe.
TEST(PcmSensorServerHistoryTest, ConcurrentWorkersStopDoesNotDeadlock) {
    HTTPServer server( 1.0, false, false );
    EXPECT_FALSE( server.isStopped() );

    constexpr int kNumWorkers = 4;
    std::atomic<int> readyCount( 0 );
    std::atomic<bool> startGate( false );

    class ConcurrentWorkerStopJob : public pcm::Work {
    public:
        ConcurrentWorkerStopJob( HTTPServer* s,
                                 std::atomic<int>& ready,
                                 std::atomic<bool>& gate,
                                 std::promise<bool>& p )
            : server_( s ), ready_( ready ), gate_( gate ), promise_( p ) {}

        virtual void execute() override {
            try {
                ready_.fetch_add( 1 );
                while ( !gate_.load() ) {
                    std::this_thread::yield();
                }
                server_->stop();
                promise_.set_value( true );
            } catch ( ... ) {
                promise_.set_exception( std::current_exception() );
            }
        }

    private:
        HTTPServer* server_;
        std::atomic<int>& ready_;
        std::atomic<bool>& gate_;
        std::promise<bool>& promise_;
    };

    std::promise<bool> promises[kNumWorkers];
    std::future<bool> futures[kNumWorkers];
    for ( int i = 0; i < kNumWorkers; ++i ) {
        futures[i] = promises[i].get_future();
        pcm::WorkQueue::getInstance()->addWork(
            new ConcurrentWorkerStopJob( &server, readyCount, startGate, promises[i] ) );
    }

    // Wait until all workers are running their jobs inside the thread pool
    while ( readyCount.load() < kNumWorkers ) {
        std::this_thread::yield();
    }

    // Release all workers simultaneously to call stop() concurrently
    startGate.store( true );

    // Verify all workers complete within 5 seconds without deadlock or std::system_error
    for ( int i = 0; i < kNumWorkers; ++i ) {
        auto status = futures[i].wait_for( std::chrono::seconds( 5 ) );
        ASSERT_EQ( status, std::future_status::ready );
        EXPECT_TRUE( futures[i].get() );
    }

    EXPECT_TRUE( server.isStopped() );

    // A later sequential stop() call must be safe and idempotent
    EXPECT_NO_THROW( server.stop() );
    EXPECT_TRUE( server.isStopped() );
}
