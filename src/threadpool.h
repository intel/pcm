// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation

#pragma once

#include "debug.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>
#include <future>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>

namespace pcm {

class Work {
public:
    Work() {}
    virtual ~Work() {}
    virtual void execute() = 0;
};

template<class ReturnType>
class LambdaJob : public Work {
public:
    template<class F, class ... Args>
    LambdaJob( F&& f, Args&& ... args )
        //: task_( std::forward<F>(f)(std::forward<Args>( args )... ) ) {
        : task_(std::bind( f, args... ) ) {
    }

    virtual void execute() override {
        task_();
    }

    std::future<ReturnType> getFuture() {
        return task_.get_future();
    }

private:
    std::packaged_task<ReturnType()> task_;
};

class WorkQueue;

class ThreadPool {
private:
    ThreadPool( const int n ) : numThreads_( n ) {
        for ( int i = 0; i < n; ++i )
            addThread();
    }

    ThreadPool( ThreadPool const& ) = delete;
    ThreadPool & operator = ( ThreadPool const& ) = delete;

public:
    static bool isCurrentThreadWorker();

    void emptyThreadPool( void ) {
        if ( isCurrentThreadWorker() ) {
            return;
        }
        std::lock_guard<std::mutex> elg( emptyMutex_ );
        try {
            for (size_t i = 0; i < threads_.size(); ++i)
                addWork(nullptr);
            for (size_t i = 0; i < threads_.size(); ++i) {
                if (threads_[i].joinable()) {
                    threads_[i].join();
                }
            }
            threads_.clear();
        }
        catch (const std::exception& e)
        {
            std::cerr << "PCM Error. Exception in ThreadPool::~ThreadPool: " << e.what() << "\n";
        }
    }

    ~ThreadPool() {
        DBG( 5, "Threadpool is being deleted..." );
        emptyThreadPool();
    }

public:
    static ThreadPool& getInstance() {
        // Scale the worker pool with available hardware concurrency rather than
        // hard-coding a small fixed size. The fixed 64-thread pool combined with
        // line-oriented blocking parsing made it cheap for a remote attacker to
        // saturate all workers (CWE-400/CWE-770). The per-request wall-clock
        // deadline added in the request reader is the primary defense, but
        // sizing the pool generously (and at minimum 64) raises the bar for
        // any future similar resource-exhaustion attempts and lets larger
        // hosts actually use their cores.
        //
        // An upper bound (kMaxThreads) prevents the pool from growing without
        // limit on hosts (or container runtimes) that report very large
        // hardware_concurrency() values, which could itself exhaust memory or
        // scheduler resources. The pool size is also overridable at startup
        // via the PCM_SENSOR_SERVER_POOL_SIZE environment variable so
        // deployments can tune it without rebuilding.
        static const unsigned int kMinThreads = 64;
        static const unsigned int kMaxThreads = 256;
        static const unsigned int n = []() {
            if ( const char* env = std::getenv( "PCM_SENSOR_SERVER_POOL_SIZE" ) ) {
                try {
                    const std::string envStr( env );
                    std::size_t pos = 0;
                    unsigned long v = std::stoul( envStr, &pos );
                    if ( envStr.find_first_not_of( " \t\n\r\f\v", pos ) == std::string::npos &&
                         v >= kMinThreads && v <= kMaxThreads )
                        return static_cast<unsigned int>( v );
                } catch ( const std::invalid_argument& ) {
                    // fall through to default sizing on unparseable value
                } catch ( const std::out_of_range& ) {
                    // fall through to default sizing on out-of-range value
                }
            }
            const unsigned int hw = std::thread::hardware_concurrency();
            // Compute hw*2 in a wider type to avoid overflow before clamping
            // on platforms / container runtimes that report a very large
            // hardware_concurrency() value.
            // min/max are wrapped in extra parentheses to defeat the macro
            // definitions of min/max that <windows.h> introduces on MSVC
            // (see AGENTS.md for the project-wide convention).
            const std::uint64_t scaled = static_cast<std::uint64_t>( hw ) * 2u;
            const std::uint64_t clamped = (std::min<std::uint64_t>)(
                kMaxThreads,
                (std::max<std::uint64_t>)( kMinThreads, scaled ) );
            return static_cast<unsigned int>( clamped );
        }();
        static ThreadPool tp_( static_cast<int>( n ) );
        return tp_;
    }

    void addWork( Work* w ) {
        DBG( 5, "WQ: Adding work" );
        std::lock_guard<std::mutex> lg( qMutex_ );
        if ( threads_.empty() && w != nullptr ) {
            for ( int i = 0; i < numThreads_; ++i ) {
                addThread();
            }
        }
        workQ_.push( w );
        queueCV_.notify_one();
        DBG( 5, "WQ: Work available" );
    }

    Work* retrieveWork() {
        DBG( 5, "WQ: Retrieving work" );
        std::unique_lock<std::mutex> lock( qMutex_ );
        queueCV_.wait( lock, [this]{ return !workQ_.empty(); } );
        Work* w = workQ_.front();
        workQ_.pop();
        lock.unlock();
        DBG( 5, "WQ: Work retrieved" );

        return w;
    }

private:
    void addThread() {
        try {
            threads_.push_back( std::thread( std::bind( &this->execute, this ) ) );
        } catch (const std::exception& e) {
            std::cerr << "PCM Error. Exception in ThreadPool::addThread: " << e.what()
                      << ". Possible causes: insufficient system resources, thread limit reached, or invalid thread function."
                      << " Suggested actions: check system resource availability, verify thread pool configuration, and ensure the thread function is valid.\n";
            throw;
        }
    }

    // Executes work items from a std::thread, do not call manually
    static void execute( ThreadPool* );

private:
    std::vector<std::thread> threads_;
    std::queue<Work*> workQ_;
    std::mutex qMutex_;
    std::mutex emptyMutex_;
    std::condition_variable queueCV_;
    int numThreads_{ 0 };
};

class WorkQueue {
private:
    WorkQueue( size_t init ) : tp_( ThreadPool::getInstance() ), workProcessed_( init ) {
        DBG( 5, "Constructing WorkQueue..." );
    }
    WorkQueue( WorkQueue const& ) = delete;
    WorkQueue & operator = ( WorkQueue const& ) = delete;

public:
    ~WorkQueue() {
        DBG( 5, "Destructing WorkQueue..." );
    }

public:
    static WorkQueue* getInstance() {
        static WorkQueue wq_( 0 );
        return &wq_;
    }
    // Just forwarding to the threadpool
    void addWork( Work* w ) {
        ++workProcessed_;
        tp_.addWork( w );
    }

private:
    ThreadPool& tp_;
    size_t workProcessed_;
};

} // namespace pcm
