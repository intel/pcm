/*
Copyright (c) 2009-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the dis
tribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNES
S FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDI
NG, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRI
CT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev


/*!     \file utils.h
        \brief Some common utility routines
  */

#ifndef PCM_UTILS_HEADER
#define PCM_UTILS_HEADER

#include <cstdio>
#include <cstring>
#include <fstream>
#include <time.h>
#include "types.h"

#ifndef _MSC_VER
#include <csignal>
#include <ctime>
#include <cmath>
#endif

void exit_cleanup(void);
void set_signal_handlers(void);
void restore_signal_handlers(void);
#ifndef _MSC_VER
void sigINT_handler(int signum);
void sigHUP_handler(int signum);
void sigUSR_handler(int signum);
void sigSTOP_handler(int signum);
void sigCONT_handler(int signum);
#endif

void set_post_cleanup_callback(void(*cb)(void));

#ifdef _MSC_VER
inline void win_usleep(int delay_us)
{
    uint64 t1 = 0, t2 = 0, freq = 0;
    uint64 wait_tick;
    QueryPerformanceFrequency((LARGE_INTEGER *)&freq);
    wait_tick = freq * delay_us / 1000000ULL;
    QueryPerformanceCounter((LARGE_INTEGER *)&t1);
    do {
        QueryPerformanceCounter((LARGE_INTEGER *)&t2);
        _mm_pause();
    } while ((t2 - t1) < wait_tick);
}
#endif

inline void MySleep(int delay)
{
#ifdef _MSC_VER
    if (delay) Sleep(delay * 1000);
#else
    ::sleep(delay);
#endif
}

inline void MySleepMs(int delay_ms)
{
#ifdef _MSC_VER
    if (delay_ms) Sleep((DWORD)delay_ms);
#else
    struct timespec sleep_intrval;
    double complete_seconds;
    sleep_intrval.tv_nsec = static_cast<long>(1000000000.0 * (::modf(delay_ms / 1000.0, &complete_seconds)));
    sleep_intrval.tv_sec = static_cast<time_t>(complete_seconds);
    ::nanosleep(&sleep_intrval, NULL);
#endif
}

inline void MySleepUs(int delay_us)
{
#ifdef _MSC_VER
    if (delay_us) win_usleep(delay_us);
#else
    ::usleep(delay_us);

#endif
}

void MySystem(char * sysCmd, char ** argc);

#ifdef _MSC_VER
#pragma warning (disable : 4068 ) // disable unknown pragma warning
#endif

#ifdef __GCC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Woverloaded-virtual"
#elif defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Woverloaded-virtual"
#endif
struct null_stream : public std::streambuf
{
    void overflow(char) { }
};
#ifdef __GCC__
#pragma GCC diagnostic pop
#elif defined __clang__
#pragma clang diagnostic pop
#endif

template <class IntType>
inline std::string unit_format(IntType n)
{
    char buffer[1024];
    if (n <= 9999ULL)
    {
        snprintf(buffer, 1024, "%4d  ", int32(n));
        return buffer;
    }
    if (n <= 9999999ULL)
    {
        snprintf(buffer, 1024, "%4d K", int32(n / 1000ULL));
        return buffer;
    }
    if (n <= 9999999999ULL)
    {
        snprintf(buffer, 1024, "%4d M", int32(n / 1000000ULL));
        return buffer;
    }
    if (n <= 9999999999999ULL)
    {
        snprintf(buffer, 1024, "%4d G", int32(n / 1000000000ULL));
        return buffer;
    }

    snprintf(buffer, 1024, "%4d T", int32(n / (1000000000ULL * 1000ULL)));
    return buffer;
}

void print_cpu_details();

#define PCM_UNUSED(x) (void)(x)

#define PCM_COMPILE_ASSERT(condition) \
    typedef char pcm_compile_assert_failed[(condition) ? 1 : -1]; \
    pcm_compile_assert_failed pcm_compile_assert_failed_; \
    PCM_UNUSED(pcm_compile_assert_failed_);

#ifdef _MSC_VER
class ThreadGroupTempAffinity
{
    GROUP_AFFINITY PreviousGroupAffinity;

    ThreadGroupTempAffinity();                                              // forbidden
    ThreadGroupTempAffinity(const ThreadGroupTempAffinity &);               // forbidden
    ThreadGroupTempAffinity & operator = (const ThreadGroupTempAffinity &); // forbidden

public:
    ThreadGroupTempAffinity(uint32 core_id);
    ~ThreadGroupTempAffinity();
};
#endif



// a secure (but partial) alternative for sscanf
// see example usage in pcm-core.cpp
typedef std::istringstream pcm_sscanf;

class s_expect : public std::string
{
public:
    explicit s_expect(const char * s) : std::string(s) {}
    explicit s_expect(const std::string & s) : std::string(s) {}
    friend std::istream & operator >> (std::istream & istr, s_expect && s);
    friend std::istream & operator >> (std::istream && istr, s_expect && s);
private:

    void match(std::istream & istr) const
    {
        istr >> std::noskipws;
        const auto len = length();
        char * buffer = new char[len + 2];
        buffer[0] = 0;
        istr.get(buffer, len+1);
        if (*this != std::string(buffer))
        {
            istr.setstate(std::ios_base::failbit);
        }
        delete [] buffer;
    }
};

inline std::istream & operator >> (std::istream & istr, s_expect && s)
{
    s.match(istr);
    return istr;
}

inline std::istream & operator >> (std::istream && istr, s_expect && s)
{
    s.match(istr);
    return istr;
}

inline tm pcm_localtime()
{
    time_t now = time(NULL);
    tm result;
#ifdef _MSC_VER
    localtime_s(&result, &now);
#else
    localtime_r(&now, &result);
#endif
    return result;
}

#endif
