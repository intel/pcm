// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2020-2022, Intel Corporation

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
    void dyn_debug_output_helper( std::stringstream& out, const T & t, Args... args ) {
        out << t;
        dyn_debug_output_helper( out, args... );
    }

    template<typename LVL, typename PF, typename F, typename L, typename... Args>
    void dyn_debug_output( std::ostream& out, LVL level, PF pretty_function, F file, L line, Args... args ) {
        std::stringstream ss;
        auto now = time(nullptr);
        ss << "DBG(" << std::dec << level << "): File '" << file << "', line '" << std::dec << line << "' :\n";
        ss << "DBG(" << std::dec << level << "): " << pretty_function << ":\n";
        ss << "DBG(" << std::dec << level << ") " << std::put_time( localtime(&now), "%F_%T: " ); // Next code line will continue printing on this output line
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
        debug::dyn_debug_output( std::cerr, level, __PRETTY_FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)

} // namespace pcm
