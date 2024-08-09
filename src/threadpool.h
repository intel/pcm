// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation

#pragma once

#include "debug.h"

#include <thread>
#include <future>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>

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
    ThreadPool( const int n ) {
        for ( int i = 0; i < n; ++i )
            addThread();
    }

    ThreadPool( ThreadPool const& ) = delete;
    ThreadPool & operator = ( ThreadPool const& ) = delete;

public:
    void emptyThreadPool( void ) {
        try {
            for (size_t i = 0; i < threads_.size(); ++i)
                addWork(nullptr);
            for (size_t i = 0; i < threads_.size(); ++i)
                threads_[i].join();
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
        static ThreadPool tp_(64);
        return tp_;
    }

    void addWork( Work* w ) {
        DBG( 5, "WQ: Adding work" );
        std::lock_guard<std::mutex> lg( qMutex_ );
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
        threads_.push_back( std::thread( std::bind( &this->execute, this ) ) );
    }

    // Executes work items from a std::thread, do not call manually
    static void execute( ThreadPool* );

private:
    std::vector<std::thread> threads_;
    std::queue<Work*> workQ_;
    std::mutex qMutex_;
    std::condition_variable queueCV_;
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
