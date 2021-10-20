#pragma once

#ifdef __GNUC__
    #if __GNUC__ < 7
        #define PCM_GCC_6_OR_BELOW
    #endif
#endif

#ifndef PCM_GCC_6_OR_BELOW
    #if defined __has_include
        #if __has_include ("simdjson/singleheader/simdjson.h")
            #pragma warning(push, 0)
            #include "simdjson/singleheader/simdjson.h"
            #pragma warning(pop)
            #define PCM_SIMDJSON_AVAILABLE
        #else
            #pragma message("parsing events from 01.org/perfmon won't be supported because simdjson library is not found in simdjson/singleheader/simdjson.h")
            #pragma message("run 'git clone https://github.com/simdjson/simdjson.git' to get simdjson library")
        #endif
    #endif
#endif
