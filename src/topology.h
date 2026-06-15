// SPDX-License-Identifier: BSD-3-Clause
// 2016-2020, Intel Corporation

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
    Visitor() {
        // Set the floatingpoint format to fixed. Setting the number of decimal digits to 3.
        ss << std::fixed << std::setprecision(3);
    }

    Visitor(const Visitor &) = delete;
    Visitor & operator = (const Visitor &) = delete;

public:
    virtual void dispatch( SystemRoot const & ) = 0;
    virtual void dispatch( Socket* )        = 0;
    virtual void dispatch( Core* )          = 0;
    virtual void dispatch( HyperThread* )   = 0;
    virtual void dispatch( ServerUncore* )  = 0;
    virtual void dispatch( ClientUncore* )  = 0;

    virtual ~Visitor() {};

protected:
    std::stringstream ss{};
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
    HyperThread( PCM* m, int32 osID, TopologyEntry te, enum Status status ) : pcm_(m), osID_(osID), te_(te), status_(status) {}
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

    std::string topologyDataString() const {
        std::stringstream ss;
        ss << osID_ << "\t" << te_.socket_id << "\t" << te_.die_grp_id << "\t" << te_.die_id << "\t" << te_.tile_id << "\t" << te_.core_id << "\t" << te_.thread_id << "\t";
        return ss.str();
    }

    TopologyEntry topologyEntry() const {
        return te_;
    }

    void addMSRHandle( std::shared_ptr<SafeMsrHandle> handle ) {
        msrHandle_ = handle;
    }

    int32 osID() const {
        return osID_;
    }

    int32 threadID() const {
        return te_.thread_id;
    }

    int32 coreID() const {
        return te_.core_id;
    }

    int32 moduleID() const {
        return te_.module_id;
    }

    int32 tileID() const {
        return te_.tile_id;
    }

    int32 dieID() const {
        return te_.die_id;
    }

    int32 dieGroupID() const {
        return te_.die_grp_id;
    }

    int32 socketID() const {
        return te_.socket_id;
    }

    int32 socketUniqueCoreID() const {
        return te_.socket_unique_core_id;
    }

    // We simply pass by value, this way the refcounting works best and as expected
    std::shared_ptr<SafeMsrHandle> msrHandle() const {
        return msrHandle_;
    }

    bool isOnline() const {
        return (status_ == Status::Online);
    }

private:
    PCM*                           pcm_;
    std::shared_ptr<SafeMsrHandle> msrHandle_;
    // osID is the expected osID, offlined cores have te.os_id == -1
    int32                          osID_;
    TopologyEntry                  te_;
    enum Status                    status_;
};

class Core : public SystemObject
{
public:
    Core( PCM* m ) : pcm_(m) {
        // PCM* m is not 0, we're being called from the PCM constructor
        // Just before this Core object is constructed, the value for
        // threads_per_core is determined
        MAX_THREADS_PER_CORE = pcm_->getThreadsPerCore();
    }
    virtual ~Core() {
        pcm_ = nullptr;
        for ( auto& thread : threads_ )
            deleteAndNullify(thread);
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

    void addHyperThreadInfo( int32 osID, TopologyEntry te ) {
        if ( te.thread_id >= MAX_THREADS_PER_CORE ) {
            std::stringstream ss;
            ss << "ERROR: Core: thread_id " << te.thread_id << " cannot be larger than " << MAX_THREADS_PER_CORE << ".\n";
            throw std::runtime_error( ss.str() );
        }
        if ( threads_.size() == 0 ||
           std::find_if( threads_.begin(), threads_.end(),
                [osID]( HyperThread const * ht ) -> bool {
                    return ht->osID() == osID;
                }
           ) == threads_.end() )
        {
            // std::cerr << "Core::addHyperThreadInfo: " << te.thread_id << ", " << te.os_id << "\n";
            threads_.push_back( new HyperThread( pcm_, osID, te, Status::Online ) );
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
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a coreID!");
        return threads_.front()->coreID();
    }

    int32 moduleID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a moduleID!");
        return threads_.front()->moduleID();
    }

    int32 tileID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a tileID!");
        return threads_.front()->tileID();
    }

    int32 dieID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a tileID!");
        return threads_.front()->dieID();
    }

    int32 dieGroupID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a tileID!");
        return threads_.front()->dieGroupID();
    }

    int32 socketID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a socketID!");
        return threads_.front()->socketID();
    }

    int32 socketUniqueCoreID() const {
        if ( 0 == threads_.size() )
            throw std::runtime_error("BUG: No threads yet but asking for a socketID!");
        return threads_.front()->socketUniqueCoreID();
    }

    bool isOnline() const {
        for( auto& thread : threads_ )
            if ( thread->isOnline() )
                return true;
        return false;
    }

private:
    PCM*                      pcm_;
    std::vector<HyperThread*> threads_;
    int32                     MAX_THREADS_PER_CORE;
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

    virtual UncoreCounterState uncoreCounterState( void ) const override;
};

class Socket : public SystemObject {
    Socket(const Socket &) = delete;
    Socket & operator = (const Socket &) = delete;
public:
    Socket( PCM* m, int32 logicalID );
    virtual ~Socket() {
        pcm_ = nullptr;
        refCore_ = nullptr; // cores_ is owner, set it to null before deleting it one below
        for ( auto& core : cores_ )
            deleteAndNullify(core);
        deleteAndNullify(uncore_);
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

    Core* findCoreByTopologyEntry( TopologyEntry te ) {
        for ( auto& core : cores_ )
            if ( core->hyperThread( 0 )->topologyEntry().isSameCore( te ) )
                return core;
        return nullptr;
    }

    std::vector<Core*> const & cores( void ) const {
        return cores_;
    }

    Uncore* uncore( void ) const {
        return uncore_;
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
    int32   logicalID_;
};

class SystemRoot : public SystemObject {
public:
    SystemRoot(PCM * p) : pcm_(p) {}

    SystemRoot( SystemRoot const & ) = delete; // do not try to copy this please
    SystemRoot & operator = ( SystemRoot const & ) = delete; // do not try to copy this please

    virtual ~SystemRoot() {
        pcm_ = nullptr;
        for ( auto& socket : sockets_ )
            deleteAndNullify(socket);
        for ( auto& thread : offlinedThreadsAtStart_ )
            deleteAndNullify(thread);
    }

    virtual void accept( Visitor& v ) override {
        v.dispatch( *this );
    }

    void addSocket( int32 logical_id ) {
        Socket* s = new Socket( pcm_, logical_id );
        sockets_.push_back( s );
    }

    // osID is the expected os_id, this is used in case te.os_id = -1 (offlined core)
    void addThread( int32 osID, TopologyEntry& te ) {
        // std::cerr << "SystemRoot::addThread: coreid: " << te.core_id <<  ", module_id: " << te.module_id << ", tile_id: " << te.tile_id << ", die_id: " << te.die_id << ", die_grp_id: " << te.die_grp_id << ", socket_id: " << te.socket_id << ", os_id: " << osID << "\n";
        // quick check during development to see if expected osId == te.os_id for onlined cores
        // assert( te.os_id != -1 && osID == te.os_id );
        bool entryAdded = false;
        for ( auto& socket : sockets_ ) {
            if ( socket->socketID() == te.socket_id ) {
                Core* core = nullptr;
                if ( (core = socket->findCoreByTopologyEntry( te )) == nullptr ) {
                    core = new Core( pcm_ );
                    // std::cerr << "new Core ThreadID: " << te.thread_id << "\n";
                    core->addHyperThreadInfo( osID, te );
                    socket->addCore( core );
                    // std::cerr << "Added core " << te.core_id << " with os_id " << osID << ", threadid " << te.thread_id << " and tileid " << te.tile_id << " to socket " << te.socket_id << ".\n";
                } else {
                    // std::cerr << "existing Core ThreadID: " << te.thread_id << "\n";
                    core->addHyperThreadInfo( osID, te );
                    // std::cerr << "Augmented core " << te.core_id << " with osID " << osID << " and threadid " << te.thread_id << " for the hyperthread to socket " << te.socket_id << ".\n";
                }
                entryAdded = true;
                break;
            }
        }
        if ( !entryAdded ) {
            // if ( te.os_id == -1 )
            //     std::cerr << "TE not added because os_id == -1, core is offline\n";
            offlinedThreadsAtStart_.push_back( new HyperThread( pcm_, osID, te, Status::Offline ) );
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
        for ( auto& socket : sockets_ ) {
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
 * the vectors contain the aggregates.
 */
class Aggregator : Visitor
{
public:
    Aggregator() : wq_( WorkQueue::getInstance() )
    {
        PCM* const pcm = PCM::getInstance();
        // Resize user provided vectors to the right size
        ccsVector_.resize( pcm->getNumCores() );
        socsVector_.resize( pcm->getNumSockets() );
        // Internal use only, need to be the same size as the user provided vectors
        ccsFutures_.resize( pcm->getNumCores() );
        ucsFutures_.resize( pcm->getNumSockets() );
    }

    virtual ~Aggregator() {
        wq_ = nullptr;
    }

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
                DBG( 5, "Lambda fetching UncoreCounterState async" );
                UncoreCounterState ucs;
                if ( !s->isOnline() )
                    return ucs;
                return s->uncore()->uncoreCounterState();
            }, sop
        );
        ucsFutures_[ sop->socketID() ] = job->getFuture();
        wq_->addWork( job );
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
                DBG( 5, "Lambda fetching CoreCounterState async" );
                CoreCounterState ccs;
                if ( !h->isOnline() )
                    return ccs;
                return h->coreCounterState();
            }, htp
        );
        ccsFutures_[ htp->osID() ] = job->getFuture();
        wq_->addWork( job );
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
    WorkQueue* wq_;
    std::vector<CoreCounterState> ccsVector_;
    std::vector<SocketCounterState> socsVector_;
    SystemCounterState sycs_;
    std::vector<std::future<CoreCounterState>> ccsFutures_;
    std::vector<std::future<UncoreCounterState>> ucsFutures_;
    std::chrono::steady_clock::time_point dispatchedAt_{};
};

/* Method used here: while walking the cores in the tree and iterating the
 * vector elements, print the core related ids into a large string. Once all
 * cores have been walked the vector of strings contains all ids.
 */
class TopologyPrinter : Visitor
{
public:
    TopologyPrinter() : wq_( WorkQueue::getInstance() )
    {
        PCM* const pcm = PCM::getInstance();
        // Resize user provided vectors to the right size
        threadIDsVector_.resize( pcm->getNumCores() );
        // Internal use only, need to be the same size as the user provided vectors
        threadIDsFutures_.resize( pcm->getNumCores() );
    }

    virtual ~TopologyPrinter() {
        wq_ = nullptr;
    }

public:
    virtual void dispatch( SystemRoot const& syp ) override {
        // std::cerr << "TopologyPrinter::dispatch( SystemRoot )\n";
        for ( auto* socket : syp.sockets() )
            socket->accept( *this );

        auto tidFuturesIter = threadIDsFutures_.begin();
        auto tidIter = threadIDsVector_.begin();
        // int i;
        // i = 0;
        for ( ; tidFuturesIter != threadIDsFutures_.end() && tidIter != threadIDsVector_.end(); ++tidFuturesIter, ++tidIter ) {
            // std::cerr << "Works tidFuture: " << ++i << "\n";
            (*tidIter) = (*tidFuturesIter).get();
        }
    }

    virtual void dispatch( Socket* sop ) override {
        // std::cerr << "TopologyPrinter::dispatch( Socket )\n";
        // Fetch Topology Data
        for ( auto* core : sop->cores() )
            core->accept( *this );
    }

    virtual void dispatch( Core* cop ) override {
        // std::cerr << "TopologyPrinter::dispatch( Core )\n";
        // Loop each HyperThread
        for ( auto* thread : cop->threads() ) {
            // Fetch the Topology Data
            thread->accept( *this );
        }
    }

    virtual void dispatch( HyperThread* htp ) override {
        // std::cerr << "TopologyPrinter::dispatch( HyperThread )\n";
        // std::cerr << "Dispatch htp with osID=" << htp->osID() << "\n";
        auto job = new LambdaJob<std::string>(
            []( HyperThread* h ) -> std::string {
                DBG( 5, "Lambda fetching Topology Data async" );
                std::string s;
                if ( !h->isOnline() )
                    return s;
                return h->topologyDataString();
            }, htp
        );
        threadIDsFutures_[ htp->osID() ] = job->getFuture();
        wq_->addWork( job );
    }

    virtual void dispatch( ServerUncore* /*sup*/ ) override {
        // std::cerr << "TopologyPrinter::dispatch( ServerUncore )\n";
    }

    virtual void dispatch( ClientUncore* /*cup*/ ) override {
        // std::cerr << "TopologyPrinter::dispatch( ClientUncore )\n";
    }

    std::vector<std::string> & topologyDataStrings( void ) {
        return threadIDsVector_;
    }

    std::chrono::steady_clock::time_point dispatchedAt( void ) const {
        return dispatchedAt_;
    }

private:
    WorkQueue* wq_;
    std::vector<std::string> threadIDsVector_;
    std::vector<std::future<std::string>> threadIDsFutures_;
    std::chrono::steady_clock::time_point dispatchedAt_{};
};

bool TopologyStringCompare( const std::string& topology1, const std::string& topology2 );

} // namespace pcm
