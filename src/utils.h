// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2020, Intel Corporation
// written by Roman Dementiev


/*!     \file utils.h
        \brief Some common utility routines
  */

#pragma once

#include <cstdio>
#include <cstring>
#include <fstream>
#include <time.h>
#include "types.h"
#include <vector>
#include <chrono>
#include <math.h>
#include <assert.h>

#ifndef _MSC_VER
#include <csignal>
#include <ctime>
#include <cmath>
#else
#include <intrin.h>
#endif

namespace pcm {

#ifdef _MSC_VER
    using tstring = std::basic_string<TCHAR>;
#ifdef UNICODE
    static auto& tcerr = std::wcerr;
#else
    static auto& tcerr = std::cerr;
#endif
#endif // _MSC_VER

typedef void (* print_usage_func)(const std::string & progname);
double parse_delay(const char * arg, const std::string & progname, print_usage_func print_usage_func);
bool extract_argument_value(const char * arg, std::initializer_list<const char*> arg_names, std::string & value);
bool check_argument_equals(const char * arg, std::initializer_list<const char*> arg_names);

void exit_cleanup(void);
void set_signal_handlers(void);
void set_real_time_priority(const bool & silent);
void restore_signal_handlers(void);
#ifndef _MSC_VER
void sigINT_handler(int signum);
void sigHUP_handler(int signum);
void sigUSR_handler(int signum);
void sigSTOP_handler(int signum);
void sigCONT_handler(int signum);
#endif

void set_post_cleanup_callback(void(*cb)(void));

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
    int_type overflow(int_type) override { return {}; }
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
        return std::string{buffer};
    }
    if (n <= 9999999ULL)
    {
        snprintf(buffer, 1024, "%4d K", int32(n / 1000ULL));
        return std::string{buffer};
    }
    if (n <= 9999999999ULL)
    {
        snprintf(buffer, 1024, "%4d M", int32(n / 1000000ULL));
        return std::string{buffer};
    }
    if (n <= 9999999999999ULL)
    {
        snprintf(buffer, 1024, "%4d G", int32(n / 1000000000ULL));
        return std::string{buffer};
    }

    snprintf(buffer, 1024, "%4d T", int32(n / (1000000000ULL * 1000ULL)));
    return std::string{buffer};
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
    bool restore;

    ThreadGroupTempAffinity();                                              // forbidden
    ThreadGroupTempAffinity(const ThreadGroupTempAffinity &);               // forbidden
    ThreadGroupTempAffinity & operator = (const ThreadGroupTempAffinity &); // forbidden

public:
    ThreadGroupTempAffinity(uint32 core_id, bool checkStatus = true, const bool restore_ = false);
    ~ThreadGroupTempAffinity();
};
#endif

class checked_uint64 // uint64 with checking for overflows when computing differences
{
    uint64 data;
    uint64 overflows;
public:
    checked_uint64() : data(0), overflows(0) {}
    checked_uint64(const uint64 d, const uint64 o) : data(d), overflows(o) {}
    const checked_uint64& operator += (const checked_uint64& o)
    {
        data += o.data;
        overflows += o.overflows;
        return *this;
    }

    uint64 operator - (const checked_uint64& o) const
    {
        // computing data - o.data
        constexpr uint64 counter_width = 48;
        return data + overflows * (1ULL << counter_width) - o.data;
    }

    uint64 getRawData_NoOverflowProtection() const { return data; }
};

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

inline std::pair<tm, uint64> pcm_localtime() // returns <tm, milliseconds>
{
    const auto durationSinceEpoch = std::chrono::system_clock::now().time_since_epoch();
    const auto durationSinceEpochInSeconds = std::chrono::duration_cast<std::chrono::seconds>(durationSinceEpoch);
    time_t now = durationSinceEpochInSeconds.count();
    tm result;
#ifdef _MSC_VER
    localtime_s(&result, &now);
#else
    localtime_r(&now, &result);
#endif
    return std::make_pair(result, std::chrono::duration_cast<std::chrono::milliseconds>(durationSinceEpoch- durationSinceEpochInSeconds).count());
}

enum CsvOutputType
{
    Header1,
    Header2,
    Data,
    Header21, // merged headers 2 and 1
    Json
};

template <class H1, class H2, class D>
inline void choose(const CsvOutputType outputType, H1 h1Func, H2 h2Func, D dataFunc)
{
    switch (outputType)
    {
    case Header1:
    case Header21:
        h1Func();
        break;
    case Header2:
        h2Func();
        break;
    case Data:
    case Json:
        dataFunc();
        break;
    default:
        std::cerr << "PCM internal error: wrong CSvOutputType\n";
    }
}

inline void printDateForCSV(const CsvOutputType outputType, std::string separator = std::string(","))
{
    choose(outputType,
        [&separator]() {
            std::cout << separator << separator; // Time
        },
        [&separator]() {
            std::cout << "Date" << separator << "Time" << separator;
        },
        [&separator]() {
            std::pair<tm, uint64> tt{ pcm_localtime() };
            std::cout.precision(3);
            char old_fill = std::cout.fill('0');
            std::cout <<
                std::setw(4) <<  1900 + tt.first.tm_year << '-' <<
                std::setw(2) << 1 + tt.first.tm_mon << '-' <<
                std::setw(2) << tt.first.tm_mday << separator <<
                std::setw(2) << tt.first.tm_hour << ':' <<
                std::setw(2) << tt.first.tm_min << ':' <<
                std::setw(2) << tt.first.tm_sec << '.' <<
                std::setw(3) << tt.second << separator; // milliseconds
            std::cout.fill(old_fill);
            std::cout.setf(std::ios::fixed);
            std::cout.precision(2);
        });
}

inline void printDateForJson(const std::string& separator, const std::string &jsonSeparator)
{
    std::pair<tm, uint64> tt{ pcm_localtime() };
    std::cout.precision(3);
    char old_fill = std::cout.fill('0');
    std::cout <<
        "Date" << jsonSeparator << "\"" <<
        std::setw(4) <<  1900 + tt.first.tm_year << '-' <<
        std::setw(2) << 1 + tt.first.tm_mon << '-' <<
        std::setw(2) << tt.first.tm_mday << "\"" << separator <<
        "Time" << jsonSeparator << "\"" <<
        std::setw(2) << tt.first.tm_hour << ':' <<
        std::setw(2) << tt.first.tm_min << ':' <<
        std::setw(2) << tt.first.tm_sec << '.' <<
        std::setw(3) << tt.second << "\"" << separator; // milliseconds
    std::cout.fill(old_fill);
    std::cout.setf(std::ios::fixed);
    std::cout.precision(2);
}

std::vector<std::string> split(const std::string & str, const char delim);

class PCM;
bool CheckAndForceRTMAbortMode(const char * argv, PCM * m);

void print_help_force_rtm_abort_mode(const int alignment);

template <class F>
void parseParam(int argc, char* argv[], const char* param, F f)
{
    if (argc > 1) do
    {
        argv++;
        argc--;
        if ((std::string("-") + param == *argv) || (std::string("/") + param == *argv))
        {
            argv++;
            argc--;
            if (argc == 0)
            {
                std::cerr << "ERROR: no parameter provided for option " << param << "\n";
                exit(EXIT_FAILURE);
            }
            f(*argv);
            continue;
        }
    } while (argc > 1); // end of command line parsing loop
}

class MainLoop
{
    unsigned numberOfIterations = 0;
public:
    MainLoop() {}
    bool parseArg(const char * arg)
    {
        std::string arg_value;
        if (extract_argument_value(arg, {"-i", "/i"}, arg_value))
        {
            numberOfIterations = (unsigned int)atoi(arg_value.c_str());
            return true;
        }
        
        return false;
    }
    unsigned getNumberOfIterations() const
    {
        return numberOfIterations;
    }
    template <class Body>
    void operator ()(const Body & body)
    {
        unsigned int i = 1;
        // std::cerr << "DEBUG: numberOfIterations: " << numberOfIterations << "\n";
        while ((i <= numberOfIterations) || (numberOfIterations == 0))
        {
            if (body() == false)
            {
                break;
            }
            ++i;
        }
    }
};

#ifdef __linux__
FILE * tryOpen(const char * path, const char * mode);
std::string readSysFS(const char * path, bool silent);
bool writeSysFS(const char * path, const std::string & value, bool silent);
#endif

int calibratedSleep(const double delay, const char* sysCmd, const MainLoop& mainLoop, PCM* m);

struct StackedBarItem {
    double fraction{0.0};
    std::string label{""}; // not used currently
    char fill{'0'};
    StackedBarItem() {}
    StackedBarItem(double fraction_,
        const std::string & label_,
        char fill_) : fraction(fraction_), label(label_), fill(fill_) {}
};

void drawStackedBar(const std::string & label, std::vector<StackedBarItem> & h, const int width = 80);

// emulates scanf %i for hex 0x prefix otherwise assumes dec (no oct support)
bool match(const std::string& subtoken, const std::string& sname, uint64* result);

uint64 read_number(const char* str);

union PCM_CPUID_INFO
{
    int array[4];
    struct { unsigned int eax, ebx, ecx, edx; } reg;
};

inline void pcm_cpuid(int leaf, PCM_CPUID_INFO& info)
{
#ifdef _MSC_VER
    // version for Windows
    __cpuid(info.array, leaf);
#else
    __asm__ __volatile__("cpuid" : \
        "=a" (info.reg.eax), "=b" (info.reg.ebx), "=c" (info.reg.ecx), "=d" (info.reg.edx) : "a" (leaf));
#endif
}

inline void clear_screen() {
#ifdef _MSC_VER
    system("cls");
#else
    std::cout << "\033[2J\033[0;0H";
#endif
}

inline uint32 build_bit_ui(uint32 beg, uint32 end)
{
    assert(end <= 31);
    uint32 myll = 0;
    if (end == 31)
    {
        myll = (uint32)(-1);
    }
    else
    {
        myll = (1 << (end + 1)) - 1;
    }
    myll = myll >> beg;
    return myll;
}

inline uint32 extract_bits_ui(uint32 myin, uint32 beg, uint32 end)
{
    uint32 myll = 0;
    uint32 beg1, end1;

    // Let the user reverse the order of beg & end.
    if (beg <= end)
    {
        beg1 = beg;
        end1 = end;
    }
    else
    {
        beg1 = end;
        end1 = beg;
    }
    myll = myin >> beg1;
    myll = myll & build_bit_ui(beg1, end1);
    return myll;
}

inline uint64 build_bit(uint32 beg, uint32 end)
{
    uint64 myll = 0;
    if (end == 63)
    {
        myll = static_cast<uint64>(-1);
    }
    else
    {
        myll = (1LL << (end + 1)) - 1;
    }
    myll = myll >> beg;
    return myll;
}

inline uint64 extract_bits(uint64 myin, uint32 beg, uint32 end)
{
    uint64 myll = 0;
    uint32 beg1, end1;

    // Let the user reverse the order of beg & end.
    if (beg <= end)
    {
        beg1 = beg;
        end1 = end;
    }
    else
    {
        beg1 = end;
        end1 = beg;
    }
    myll = myin >> beg1;
    myll = myll & build_bit(beg1, end1);
    return myll;
}

std::string safe_getenv(const char* env);

#ifdef _MSC_VER
inline HANDLE openMSRDriver()
{
    return CreateFile(TEXT("\\\\.\\RDMSR"), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}
#endif

// called before everything else to read '-s' arg and
// silence all following err output
void check_and_set_silent(int argc, char * argv[], null_stream &nullStream2);

void print_pid_collection_message(int pid);

inline bool isPIDOption(char * argv [])
{
    return check_argument_equals(*argv, {"-pid", "/pid"});
}

inline void parsePID(int argc, char* argv[], int& pid)
{
    parseParam(argc, argv, "pid", [&pid](const char* p) { if (p) pid = atoi(p); });
}
} // namespace pcm
