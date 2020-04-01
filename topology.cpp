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

#include "cpucounters.h"
#include "topology.h"

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

Aggregator::Aggregator( std::vector<CoreCounterState>& ccs, std::vector<SocketCounterState>& socs, SystemCounterState& sycs )
    : ccsVector_( ccs ), socsVector_( socs ), sycs_( sycs )
{
    PCM* const pcm = PCM::getInstance();
    // Resize user provided vectors to the right size
    ccsVector_.resize( pcm->getNumCores() );
    socsVector_.resize( pcm->getNumSockets() );
    // Internal use only, need to be the same size as the user provided vectors
    ccsFutures_.resize( pcm->getNumCores() );
    ucsFutures_.resize( pcm->getNumSockets() );
}
