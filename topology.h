/*
BSD 3-Clause License

Copyright (c) 2016-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <future>

#include "types.h"
#include "cpucounters.h"
#include "threadpool.h"

namespace pcm {

// all can be done with forwards, anything hat actually uses PCM should be put in the topology.cpp file
class PCM;

class SystemRoot;
class Socket;
class Core;
class HyperThread;
class ServerUncore;
class ClientUncore;

class Visitor {
public:
    virtual void dispatch( SystemRoot const & ) = 0;
    virtual void dispatch( Socket* )        = 0;
    virtual void dispatch( Core* )          = 0;
    virtual void dispatch( HyperThread* )   = 0;
    virtual void dispatch( ServerUncore* )  = 0;
    virtual void dispatch( ClientUncore* )  = 0;

    virtual ~Visitor() {};
};

class SystemObject
{
public:
    virtual void accept( Visitor & v ) = 0;
    virtual ~SystemObject() {};
};

enum Status {
    Offline = 0,
    Online = 1
};

class HyperThread : public SystemObject
{
public:
    HyperThread( PCM* m, int32 threadID, int32 osID, enum Status status ) : pcm_(m), threadID_(threadID), osID_(osID), status_(status) {}
    virtual ~HyperThread() { pcm_ = nullptr; }

    virtual void accept( Visitor& v ) override {
        v.dispatch( this );
    }

    CoreCounterState coreCounterState() const {
        CoreCounterState ccs;
        // fill ccs
        ccs.BasicCounterState::readAndAggregate( msrHandle_ );
        return ccs;
    }

    void addMSRHandle( std::shared_ptr<SafeMsrHandle> handle ) {
        msrHandle_ = handle;
    }

    int32 threadID() const {
        return threadID_;
    }

    int32 osID() const {
        return osID_;
    }

    // We simply pass by value, this way the refcounting works best and as expected
    std::shared_ptr<SafeMsrHandle> msrHandle() const {
        return msrHandle_;
    }

    bool isOnline() const {
        return (status_ == Status::Online);
    }

private:
    PCM*   pcm_;
    std::shared_ptr<SafeMsrHandle> msrHandle_;
    int32 threadID_;
    int32 osID_;
    enum Status status_;
};

class Core : public SystemObject
{
    constexpr static int32 MAX_THREADS_PER_CORE = 4;

public:
    Core( PCM* m, int32 coreID, int32 tileID, int32 socketID ) {
        pcm_      = m;
        coreID_   = coreID;
        tileID_   = tileID;
        socketID_ = socketID;
    }
    virtual ~Core() {
        pcm_ = nullptr;
        for ( auto thread : threads_ )
            delete thread;
    }

    virtual void accept( Visitor& v ) override {
        v.dispatch( this );
    }

    CoreCounterState coreCounterState() const {
        CoreCounterState ccs;
        // Fill bcs
        for ( HyperThread* thread : threads_ ) {
            ccs += thread->coreCounterState();
        }
        return ccs;
    }

    void addHyperThreadInfo( int32 threadID, int32 osID ) {
        if ( threadID >= MAX_THREADS_PER_CORE ) {
            std::stringstream ss;
            ss << "ERROR: Core: threadID cannot be larger than " << MAX_THREADS_PER_CORE << ".\n";
            throw std::runtime_error( ss.str() );
        }
        if ( threads_.size() == 0 ||
           std::find_if( threads_.begin(), threads_.end(),
                [osID]( HyperThread const * ht ) -> bool {
                    return ht->osID() == osID;
                }
           ) == threads_.end() )
        {
            // std::cerr << "Core::addHyperThreadInfo: " << threadID << ", " << osID << "\n";
            threads_.push_back( new HyperThread( pcm_, threadID, osID, Status::Online ) );
        }
    }

    HyperThread* hyperThread( size_t threadNo ) const {
        if ( threadNo >= threads_.size() )
            throw std::runtime_error( "ERROR: hyperThread: threadNo larger than vector." );
        return threads_[ threadNo ];
    }

    HyperThread* findThreadByOSID( int32 osID ) {
        for ( HyperThread* thread : threads_ ) {
            if ( thread->osID() == osID )
                return thread;
        }
        return nullptr;
    }

    std::vector<HyperThread*> threads() const {
        return threads_;
    }

    std::shared_ptr<SafeMsrHandle> msrHandle() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a msrHandle!");
        return threads_.front()->msrHandle();
    }

    int32 coreID() const {
        return coreID_;
    }

    int32 tileID() const {
        return tileID_;
    }

    int32 socketID() const {
        return socketID_;
    }

    bool isOnline() const {
        for( auto thread : threads_ )
            if ( thread->isOnline() )
                return true;
        return false;
    }

private:
    PCM*                      pcm_;
    std::vector<HyperThread*> threads_;
    int32                     coreID_;
    int32                     tileID_;
    int32                     socketID_;
};

class Uncore : public SystemObject
{
public:
    Uncore( PCM* m, int32 socketID ) : pcm_( m ), refCore_( nullptr ), socketID_( socketID ) {}
    virtual ~Uncore() {
        pcm_ = nullptr;
        refCore_ = nullptr;
    }

    virtual void accept( Visitor& v ) = 0;

    virtual UncoreCounterState uncoreCounterState( void ) const = 0;

    Core* refCore() const {
        if ( refCore_ == nullptr )
            throw std::runtime_error( "BUG: Uncore: refCore was never set!" );
        return refCore_;
    }

    int32 socketID() const {
        return socketID_;
    }

    void setRefCore( Core* refCore ) {
        refCore_ = refCore;
    }

private:
    PCM*  pcm_;
    Core* refCore_;
    int32 socketID_;
};

class ServerUncore : public Uncore
{
public:
    ServerUncore( PCM* m, int32 socketID ) : Uncore( m, socketID ) {}
    virtual ~ServerUncore() {}

    virtual void accept( Visitor& v ) override {
        v.dispatch( this );
    }

    virtual UncoreCounterState uncoreCounterState( void ) const override;
};

class ClientUncore : public Uncore
{
public:
    ClientUncore( PCM* m, int32 socketID ) : Uncore( m, socketID ) {}
    virtual ~ClientUncore() {}

    virtual void accept( Visitor& v ) override {
        v.dispatch( this );
    }

    virtual UncoreCounterState uncoreCounterState( void ) const override {
        UncoreCounterState ucs;
        // TODO: Fill the ucs
        return ucs;
    }
};

class Socket : public SystemObject {
public:
    Socket( PCM* m, int32 apicID, int32 logicalID );
    virtual ~Socket() {
        pcm_ = nullptr;
        for ( auto core : cores_ )
            delete core;
        refCore_ = nullptr; // cores_ is owner so it is already deleted by here
        delete uncore_;
    }

    virtual void accept( Visitor& v ) override {
        v.dispatch( this );
    }

    void addCore( Core* c ) {
        cores_.push_back( c );
    }

    HyperThread* findThreadByOSID( int32 osID ) {
        HyperThread* thread;
        for ( Core* core : cores_ ) {
            thread = core->findThreadByOSID(osID);
            if ( nullptr != thread )
                return thread;
        }
        return nullptr;
    }

    void setRefCore() {
        if ( cores_.size() == 0 )
            throw std::runtime_error("No cores added to the socket so cannot set reference core");
        refCore_ = cores_.front();
        // uncore_ cannot be null, it is set in the constructor
        uncore_->setRefCore( refCore_ );
    }

    SocketCounterState socketCounterState( void ) const;

    Core* findCoreByTileID( int32 tileID ) {
        for ( auto core : cores_ ) {
            if ( core->tileID() == tileID )
                return core;
        }
        return nullptr;
    }

    std::vector<Core*> const & cores( void ) const {
        return cores_;
    }

    Uncore* uncore( void ) const {
        return uncore_;
    }

    int32 apicId() const {
        return apicID_;
    }

    int32 socketID() const {
        return logicalID_;
    }

    bool isOnline() const {
        return refCore_->isOnline();
    }

private:
    std::vector<Core*> cores_;
    PCM*    pcm_;
    Core*   refCore_;
    Uncore* uncore_;
    int32   apicID_;
    int32   logicalID_;
};

class SystemRoot : public SystemObject {
public:
    SystemRoot(PCM * p) : pcm_(p) {}

    SystemRoot( SystemRoot const & ) = delete; // do not try to copy this please

    virtual ~SystemRoot() {
        pcm_ = nullptr;
        for ( auto socket : sockets_ )
            delete socket;
        for ( auto thread : offlinedThreadsAtStart_ )
            delete thread;
    }

    virtual void accept( Visitor& v ) override {
        v.dispatch( *this );
    }

    void addSocket( int32 apic_id, int32 logical_id ) {
        Socket* s = new Socket( pcm_, apic_id, logical_id );
        sockets_.push_back( s );
    }

    // osID is the expected os_id, this is used in case te.os_id = -1 (offlined core)
    void addThread( int32 osID, TopologyEntry& te ) {
        // quick check during development to see if expected osId == te.os_id for onlined cores
        // assert( te.os_id != -1 && osID == te.os_id );
        bool entryAdded = false;
        for ( auto socket : sockets_ ) {
            if ( socket->apicId() == te.socket ) {
                Core* core = nullptr;
                if ( (core = socket->findCoreByTileID( te.tile_id )) == nullptr ) {
                    // std::cerr << "SystemRoot::addThread: " << te.tile_id << ", " << osID << "\n";
                    core = new Core( pcm_, te.core_id, te.tile_id, te.socket );
                    // std::cerr << "new Core ThreadID: " << te.thread_id << "\n";
                    core->addHyperThreadInfo( te.thread_id, osID );
                    socket->addCore( core );
                    // std::cerr << "Added core " << te.core_id << " with os_id " << osID << ", threadid " << te.thread_id << " and tileid " << te.tile_id << " to socket " << te.socket << ".\n";
                } else {
                    // std::cerr << "existing Core ThreadID: " << te.thread_id << "\n";
                    core->addHyperThreadInfo( te.thread_id, osID );
                    // std::cerr << "Augmented core " << te.core_id << " with osID " << osID << " and threadid " << te.thread_id << " for the hyperthread to socket " << te.socket << ".\n";
                }
                entryAdded = true;
                break;
            }
        }
        if ( !entryAdded ) {
            // if ( te.os_id == -1 )
            //     std::cerr << "TE not added because os_id == -1, core is offline\n";
            offlinedThreadsAtStart_.push_back( new HyperThread( pcm_, -1, osID, Status::Offline ) );
        }
    }

    HyperThread* findThreadByOSID( int32 osID ) {
        HyperThread* thread;
        for ( Socket* socket : sockets_ ) {
            thread = socket->findThreadByOSID( osID );
            if ( nullptr != thread )
                return thread;
        }
        for ( HyperThread* ht: offlinedThreadsAtStart_ )
            if ( ht->osID() == osID )
                return ht;
        return nullptr;
    }

    void addMSRHandleToOSThread( std::shared_ptr<SafeMsrHandle> handle, uint32 osID )
    {
        // std::cerr << "addMSRHandleToOSThread: osID: " << osID << "\n";
        HyperThread* thread = findThreadByOSID( osID );
        if ( nullptr == thread )
            throw std::runtime_error( "SystemRoot::addMSRHandleToOSThread osID not found" );
        thread->addMSRHandle( handle );
    }

    SystemCounterState systemCounterState() const {
        SystemCounterState scs;
        // Fill scs
        // by iterating the sockets
        for ( auto socket : sockets_ ) {
            scs += ( socket->socketCounterState() );
        }
        return scs;
    }

    std::vector<Socket*> const & sockets( void ) const {
        return sockets_;
    }

    std::vector<HyperThread*> const & offlinedThreadsAtStart( void ) const {
        return offlinedThreadsAtStart_;
    }

private:
    std::vector<Socket*>      sockets_;
    std::vector<HyperThread*> offlinedThreadsAtStart_;
    PCM*                      pcm_;
};


/* Method used here: while walking the tree and iterating the vector
 * elements, collect the counters. Once all elements have been walked
 * the vectors are filled with the aggregates.
 */
class Aggregator : Visitor
{
public:
    Aggregator();
    virtual ~Aggregator() {}

public:
    virtual void dispatch( SystemRoot const& syp ) override;

    virtual void dispatch( Socket* sop ) override {
        // std::cerr << "Aggregator::dispatch( Socket )\n";
        // Fetch CoreCounterStates
        for ( auto* core : sop->cores() )
            core->accept( *this );
        // Fetch UncoreCounterState async result
        auto job = new LambdaJob<UncoreCounterState>(
            []( Socket* s ) -> UncoreCounterState {
                DBG( 3, "Lambda fetching UncoreCounterState async" );
                UncoreCounterState ucs;
                if ( !s->isOnline() )
                    return ucs;
                return s->uncore()->uncoreCounterState();
            }, sop
        );
        ucsFutures_[ sop->socketID() ] = job->getFuture();
        wq_.addWork( job );
        // For now execute directly to compile test
        //job->execute();
    }

    virtual void dispatch( Core* cop ) override {
        // std::cerr << "Aggregator::dispatch( Core )\n";
        // Loop each HyperThread
        for ( auto* thread : cop->threads() ) {
            // Fetch the CoreCounterState
            thread->accept( *this );
        }
    }

    virtual void dispatch( HyperThread* htp ) override {
        // std::cerr << "Aggregator::dispatch( HyperThread )\n";
        // std::cerr << "Dispatch htp with osID=" << htp->osID() << "\n";
        auto job = new LambdaJob<CoreCounterState>(
            []( HyperThread* h ) -> CoreCounterState {
                DBG( 3, "Lambda fetching CoreCounterState async" );
                CoreCounterState ccs;
                if ( !h->isOnline() )
                    return ccs;
                return h->coreCounterState();
            }, htp
        );
        ccsFutures_[ htp->osID() ] = job->getFuture();
        wq_.addWork( job );
    }

    virtual void dispatch( ServerUncore* /*sup*/ ) override {
        // std::cerr << "Aggregator::dispatch( ServerUncore )\n";
    }

    virtual void dispatch( ClientUncore* /*cup*/ ) override {
        // std::cerr << "Aggregator::dispatch( ClientUncore )\n";
    }

    std::vector<CoreCounterState>const & coreCounterStates( void ) const {
        return ccsVector_;
    }

    std::vector<SocketCounterState>const & socketCounterStates( void ) const {
        return socsVector_;
    }

    SystemCounterState const & systemCounterState( void ) const {
        return sycs_;
    }

    std::chrono::steady_clock::time_point dispatchedAt( void ) const {
        return dispatchedAt_;
    }

private:
    std::vector<CoreCounterState> ccsVector_;
    std::vector<SocketCounterState> socsVector_;
    SystemCounterState sycs_;
    std::vector<std::future<CoreCounterState>> ccsFutures_;
    std::vector<std::future<UncoreCounterState>> ucsFutures_;
    std::chrono::steady_clock::time_point dispatchedAt_;
    WorkQueue wq_;
};

} // namespace pcm
