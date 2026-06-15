// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation

#include "threadpool.h"
#include "utils.h"

namespace pcm {

void ThreadPool::execute( ThreadPool* tp ) {
    while( 1 ) {
        Work* w = tp->retrieveWork();
        if ( w == nullptr ) break;
        w->execute();
        // There can never be a double delete here, once taken from the tp it is owned by this thread
        // but in order to silence cppcheck w is set explicitly to null
        deleteAndNullify( w );
        DBG( 5, "Work deleted, waiting for more work..." );
    }
    DBG( 4, "Thread is explicitly dying now..." );
}

} // namespace pcm
