// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020, Intel Corporation

#include "threadpool.h"

namespace pcm {

void ThreadPool::execute( ThreadPool* tp ) {
    while( 1 ) {
        Work* w = tp->retrieveWork();
        if ( w == nullptr ) break;
        w->execute();
        delete w;
    }
}

} // namespace pcm