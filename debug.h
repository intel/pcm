/*
BSD 3-Clause License

Copyright (c) 2020, Intel Corporation
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

#include <sstream>
#include <iomanip>
#include <iostream>

#ifdef _MSC_VER
#include <BaseTsd.h>
#define ssize_t SSIZE_T
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

namespace pcm {

namespace debug {
    extern int currentDebugLevel;

    template<typename T>
    void dyn_debug_output_helper( std::stringstream& out, T t ) {
        out << t << "\n";
    }

    template<typename T, typename... Args>
    void dyn_debug_output_helper( std::stringstream& out, T t, Args... args ) {
        out << t;
        dyn_debug_output_helper( out, args... );
    }

    template<typename LVL, typename PF, typename F, typename L, typename... Args>
    void dyn_debug_output( std::ostream& out, LVL level, PF pretty_function, F file, L line, Args... args ) {
        std::stringstream ss;
        ss << "DBG(" << std::dec << level << "): File '" << file << "', line '" << std::dec << line << "' :\n";
        ss << "DBG(" << std::dec << level << "): " << pretty_function << ":\n";
        ss << "DBG(" << std::dec << level << "): "; // Next code line will continue printing on this output line
        dyn_debug_output_helper( ss, args... );
        out << ss.str() << std::flush;
    }

    template<typename T>
    void dyn_hex_table_output( int debugLevel, std::ostream& out, ssize_t len, T* inputBuffer_ ) {
        std::stringstream ss;
        if ( debug::currentDebugLevel < debugLevel )
            return;
        for ( ssize_t i = 0; i < len; ++i ) {
            constexpr int DHTO_CHARS_PER_LINE = 16;
            ss << std::hex << std::internal << std::setfill('0') << std::setw(2) << std::abs(inputBuffer_[i]) << " ";
            if ( (i % DHTO_CHARS_PER_LINE) == (DHTO_CHARS_PER_LINE - 1) ) ss << "\n";
        }
        out << ss.str() << std::flush;
    }

    void dyn_debug_level( int debugLevel );
}

#define DBG( level, ... ) \
    if ( debug::currentDebugLevel >= level ) \
        debug::dyn_debug_output( std::cout, level, __PRETTY_FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

} // namespace pcm