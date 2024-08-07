// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2016-2022, Intel Corporation

#include "topology.h"
#include "pcm-accel-common.h"

namespace pcm {

UncoreCounterState ServerUncore::uncoreCounterState( void ) const
{
    UncoreCounterState ucs;
    // Fill the ucs
    PCM* pcm = PCM::getInstance();
    pcm->readAndAggregateUncoreMCCounters( socketID(), ucs );
    pcm->readAndAggregateEnergyCounters( socketID(), ucs );
    pcm->readAndAggregatePackageCStateResidencies( refCore()->msrHandle(), ucs );

    return ucs;
}

UncoreCounterState ClientUncore::uncoreCounterState( void ) const
{
    UncoreCounterState ucs;
    // Fill the ucs
    PCM* pcm = PCM::getInstance();
    pcm->readAndAggregateUncoreMCCounters( socketID(), ucs );
    pcm->readAndAggregateEnergyCounters( socketID(), ucs );
    pcm->readAndAggregatePackageCStateResidencies( refCore()->msrHandle(), ucs );

    return ucs;
}

Socket::Socket( PCM* m, int32 apicID, int32 logicalID )
    : pcm_(m), refCore_(nullptr), apicID_(apicID), logicalID_(logicalID)
{
    if ( pcm_->isServerCPU() )
        uncore_ = new ServerUncore( pcm_, logicalID );
    else if ( pcm_->isClientCPU() )
        uncore_ = new ClientUncore( pcm_, logicalID );
    else
        throw std::runtime_error( "ERROR: Neither a client nor a server part, please fix the code!" );
}

SocketCounterState Socket::socketCounterState( void ) const {
    SocketCounterState scs;
    // Fill the scs
    // by iterating the cores
    for( auto& core : cores_ ) {
        scs.BasicCounterState::operator += ( core->coreCounterState() );
    }
    // and the uncore
    scs.UncoreCounterState::operator += ( uncore_->uncoreCounterState() );
    PCM::getInstance()->readPackageThermalHeadroom( socketID(), scs );
    return scs;
}

void Aggregator::dispatch( SystemRoot const& syp ) {
    // std::cerr << "Aggregator::dispatch( SystemRoot )\n";
    dispatchedAt_ = std::chrono::steady_clock::now();
    // CoreCounterStates are fetched asynchronously here
    for ( auto* socket : syp.sockets() )
        socket->accept( *this );
    // Dispatching offlined cores
    for ( auto* htp : syp.offlinedThreadsAtStart() )
        htp->accept( *this );

    auto ccsFuturesIter = ccsFutures_.begin();
    auto ccsIter = ccsVector_.begin();
    // int i;
    // i = 0;
    for ( ; ccsFuturesIter != ccsFutures_.end() && ccsIter != ccsVector_.end(); ++ccsFuturesIter, ++ccsIter ) {
        // std::cerr << "Works ccsFuture: " << ++i << "\n";
        (*ccsIter) = (*ccsFuturesIter).get();
    }

    // Aggregate BasicCounterStates
    for ( auto* socket : syp.sockets() ) {
        for ( auto* core : socket->cores() )
            for ( auto* thread : core->threads() )
                socsVector_[ socket->socketID() ] += ( ccsVector_[ thread->osID() ] );
        // UncoreCounterStates have not been filled here so it is ok to add
        // the entire SocketCounterState here
        sycs_ += socsVector_[ socket->socketID() ];
    }

    // Fetch and aggregate UncoreCounterStates
    auto ucsFuturesIter = ucsFutures_.begin();
    auto socsIter = socsVector_.begin();
    // i = 0;
    for ( ; ucsFuturesIter != ucsFutures_.end() && socsIter != socsVector_.end(); ++ucsFuturesIter, ++socsIter ) {
        // std::cerr << "Works ucsFuture: " << ++i << "\n";
        // Because we already aggregated the Basic/CoreCounterStates above, sycs_
        // only needs the ucs added here. If we would add socs to sycs we would
        // count all Basic/CoreCounterState counters double
        UncoreCounterState ucs = (*ucsFuturesIter).get();
        sycs_ += ucs;
        (*socsIter) = std::move( ucs );
    }
    PCM* pcm = PCM::getInstance();
    pcm->readQPICounters( sycs_ );
    pcm->readAndAggregateCXLCMCounters( sycs_ );
    readAccelCounters(sycs_);
}

}// namespace pcm
