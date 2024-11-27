// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
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
#include <list>
#include <chrono>
#include <math.h>
#include <assert.h>

#if defined(__FreeBSD__) || (defined(__DragonFly__) && __DragonFly_version >= 400707)
#include <pthread_np.h>
#include <sys/_cpuset.h>
#include <sys/cpuset.h>
#endif

#ifndef _MSC_VER
#include <unistd.h>
#include <csignal>
#include <ctime>
#include <cmath>
#else
#include <intrin.h>
#endif
#include <map>
#include <unordered_map>

#ifdef __linux__
#include <unistd.h>
#endif
namespace pcm {

    template <class T>
    inline void deleteAndNullify(T & p)
    {
        if (p)
        {
            delete p;
            p = nullptr;
        }
    }

    template <class T>
    inline void deleteAndNullifyArray(T & p)
    {
        if (p)
        {
            delete [] p;
            p = nullptr;
        }
    }

    template <class T>
    inline void freeAndNullify(T & p)
    {
        if (p)
        {
            free(p);
            p = nullptr;
        }
    }

    std::string safe_getenv(const char* env);
#ifdef _MSC_VER
    typedef std::wstring StringType;
    #define PCM_STRING(x) (L ## x)
#else
    typedef std::string StringType;
    #define PCM_STRING(x) (x)
#endif
    void eraseEnvironmentVariables(const std::vector<StringType>& keepList);
}

#ifdef _MSC_VER
#define PCM_SET_DLL_DIR SetDllDirectory(_T(""));
#else
#define PCM_SET_DLL_DIR
#endif

#define PCM_MAIN_NOTHROW \
int mainThrows(int argc, char * argv[]); \
int main(int argc, char * argv[]) \
{ \
    try { \
       eraseEnvironmentVariables({PCM_STRING("POSIXLY_CORRECT")}); \
    } catch(const std::exception & e) \
    { \
        std::cerr << "PCM ERROR. Exception in eraseEnvironmentVariables: " << e.what() << "\n"; \
        return -1; \
    } \
    PCM_SET_DLL_DIR \
    if (pcm::safe_getenv("PCM_NO_MAIN_EXCEPTION_HANDLER") == std::string("1")) return mainThrows(argc, argv); \
    try { \
        return mainThrows(argc, argv); \
    } catch(const std::runtime_error & e) \
    { \
        std::cerr << "PCM ERROR. Exception " << e.what() << "\n"; \
    } catch(const std::exception & e) \
    { \
        std::cerr << "PCM ERROR. Exception " << e.what() << "\n"; \
    } catch (...) \
    { \
        std::cerr << "PCM ERROR. Exception detected (no further details available).\n"; \
    } \
    return -1; \
}

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
std::list<int> extract_integer_list(const char *optarg);
double parse_delay(const char * arg, const std::string & progname, print_usage_func print_usage_func);
bool extract_argument_value(const char * arg, std::initializer_list<const char*> arg_names, std::string & value);
bool check_argument_equals(const char * arg, std::initializer_list<const char*> arg_names);
bool check_for_injections(const std::string & str);

void exit_cleanup(void);
void set_signal_handlers(void);
void set_real_time_priority(const bool & silent);
void restore_signal_handlers(void);
#ifndef _MSC_VER
void printBacktrace();
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

constexpr const char* ASCII_BLACK = "\033[0;30m";
constexpr const char* ASCII_RED = "\033[0;31m";
constexpr const char* ASCII_GREEN = "\033[0;32m";
constexpr const char* ASCII_YELLOW = "\033[0;33m";
constexpr const char* ASCII_BLUE = "\033[0;34m";
constexpr const char* ASCII_MAGENTA = "\033[0;35m";
constexpr const char* ASCII_CYAN = "\033[0;36m";
constexpr const char* ASCII_WHITE = "\033[0;37m";
constexpr const char* ASCII_BRIGHT_BLACK = "\033[1;30m";
constexpr const char* ASCII_BRIGHT_RED = "\033[1;31m";
constexpr const char* ASCII_BRIGHT_GREEN = "\033[1;32m";
constexpr const char* ASCII_BRIGHT_YELLOW = "\033[1;33m";
constexpr const char* ASCII_BRIGHT_BLUE = "\033[1;34m";
constexpr const char* ASCII_BRIGHT_MAGENTA = "\033[1;35m";
constexpr const char* ASCII_BRIGHT_CYAN = "\033[1;36m";
constexpr const char* ASCII_BRIGHT_WHITE = "\033[1;37m";
constexpr const char* ASCII_RESET_COLOR = "\033[0m";

void setColorEnabled(bool value = true);
const char * setColor(const char * colorStr);
const char * setNextColor();
const char * resetColor();

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


inline void printDebugCallstack()
{
#ifndef _MSC_VER
    if (safe_getenv("PCM_PRINT_DEBUG_CALLSTACK") == "1")
    {
        printBacktrace();
    }
#endif
}

template <unsigned Bytes>
inline void warnAlignment(const char* call, const bool silent, const uint64 offset)
{
    if (silent == false && (offset % Bytes) != 0)
    {
        std::cerr << "PCM Warning: " << call << " offset " << offset << " is not " << Bytes << "-byte aligned\n";
        printDebugCallstack();
    }
}

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
        deleteAndNullifyArray(buffer);
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

void print_help_force_rtm_abort_mode(const int alignment, const char * separator = "=>");

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

inline void clear_screen() {
#ifdef _MSC_VER
    system("cls");
#else
    std::cout << "\033[2J\033[0;0H";
#endif
}

#ifdef _MSC_VER

#define PCM_MSR_DRV_NAME TEXT("\\\\.\\RDMSR")

inline HANDLE openMSRDriver()
{
    return CreateFile(PCM_MSR_DRV_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}
#endif

#define PCM_ENFORCE_FLUSH_OPTION else if (check_argument_equals(*argv, { "-f", "/f" })) { enforceFlush = true; continue; }
void print_enforce_flush_option_help();

// called before everything else to read '-s' arg and
// silence all following err output
void check_and_set_silent(int argc, char * argv[], null_stream &nullStream2);

void print_pid_collection_message(int pid);

bool print_version(int argc, char * argv[]);

inline bool isPIDOption(char * argv [])
{
    return check_argument_equals(*argv, {"-pid", "/pid"});
}

inline void parsePID(int argc, char* argv[], int& pid)
{
    parseParam(argc, argv, "pid", [&pid](const char* p) { if (p) pid = atoi(p); });
}

struct counter {
    std::string h_event_name = "";
    std::string v_event_name = "";
    uint64_t ccr = 0;
    int idx = 0; /* Some counters need to be placed in specific index */
    int multiplier = 0;
    int divider = 0;
    uint32_t h_id = 0;
    uint32_t v_id = 0;
};

struct data{
    uint32_t width;
    uint64_t value;
};

typedef enum{
    EVT_LINE_START,
    EVT_LINE_FIELD,
    EVT_LINE_COMPLETE
}evt_cb_type;

std::string dos2unix(std::string in);
bool isRegisterEvent(const std::string & pmu);
std::string a_title (const std::string &init, const std::string &name);
std::string a_data (std::string init, struct data d);
std::string a_header_footer(std::string init, std::string name);
std::string build_line(std::string init, std::string name, bool last_char, char this_char);
std::string build_csv_row(const std::vector<std::string>& chunks, const std::string& delimiter);
std::vector<struct data> prepare_data(const std::vector<uint64_t> &values, const std::vector<std::string> &headers);
void display(const std::vector<std::string> &buff, std::ostream& stream);

void print_nameMap(std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>>& nameMap);
int load_events(const std::string &fn, std::map<std::string, uint32_t> &ofm,
                int (*p_fn_evtcb)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64),
                void *evtcb_ctx, std::map<std::string,std::pair<uint32_t,std::map<std::string,uint32_t>>> &nameMap);
int load_events(const std::string &fn, std::map<std::string, uint32_t> &ofm,
                int (*pfn_evtcb)(evt_cb_type, void *, counter &, std::map<std::string, uint32_t> &, std::string, uint64),
                void *evtcb_ctx);

bool get_cpu_bus(uint32 msmDomain, uint32 msmBus, uint32 msmDev, uint32 msmFunc, uint32 &cpuBusValid, std::vector<uint32> &cpuBusNo, int &cpuPackageId);

#ifdef __linux__
FILE * tryOpen(const char * path, const char * mode);
std::string readSysFS(const char * path, bool silent = false);
bool writeSysFS(const char * path, const std::string & value, bool silent = false);
int readMaxFromSysFS(const char * path);
bool readMapFromSysFS(const char * path, std::unordered_map<std::string, uint32> &result, bool silent = false);
#endif

inline uint64 insertBits(uint64 input, const uint64 value, const int64_t position, const uint64 width)
{
    const uint64 mask = (width == 64) ? (~0ULL) : ((1ULL << width) - 1ULL); // 1 -> 1b, 2 -> 11b, 3 -> 111b
    input &= ~(mask << position); // clear
    input |= (value & mask) << position;
    return input;
}

inline uint64 roundDownTo4K(uint64 number) {
    return number & ~0xFFFULL; // Mask the lower 12 bits to round down to 4K
}

inline uint64 roundUpTo4K(uint64 number) {
    if (number % 4096ULL == 0ULL) {
        // Already a multiple of 4K
        return number;
    } else {
        // Round up to the next multiple of 4K
        return ((number / 4096ULL) + 1ULL) * 4096ULL;
    }
}

#define PCM_STRINGIFY(x) #x
#define PCM_TOSTRING(x) PCM_STRINGIFY(x)

inline std::string getInstallPathPrefix()
{
#if defined (CMAKE_INSTALL_PREFIX)
    const std::string prefix{ PCM_TOSTRING(CMAKE_INSTALL_PREFIX) };
#else
    const std::string prefix{ "/usr" };
#endif
    return prefix + "/share/pcm/";
}

std::pair<int64,int64> parseBitsParameter(const char * param);
template <class T, class R>
inline bool readOldValueHelper(const std::pair<int64,int64> & bits, T & value, const bool & write, R readValue)
{
    if (bits.first >= 0 && write)
    {
        // to write bits need to read the old value first
        T old_value = 0;
        if (!readValue(old_value))
        {
            return false;
        }
        value = insertBits(old_value, value, bits.first, bits.second - bits.first + 1);
    }
    return true;
}

template <class T>
inline void extractBitsPrintHelper(const std::pair<int64,int64> & bits, T & value, const bool & dec)
{
    std::cout << " Read ";
    if (bits.first >= 0)
    {
        std::cout << "bits "<< std::dec << bits.first << ":" << bits.second << " ";
        if (!dec) std::cout << std::hex << std::showbase;
        value = extract_bits(value, bits.first, bits.second);
    }
    std::cout << "value " << value;
}

#ifdef _MSC_VER
void restrictDriverAccessNative(LPCTSTR path);
#endif

#ifdef __linux__
std::vector<std::string> findPathsFromPattern(const char* pattern);
#endif

class TemporalThreadAffinity
{
    TemporalThreadAffinity(); // forbidden
#if defined(__FreeBSD__) || (defined(__DragonFly__) && __DragonFly_version >= 400707)
    cpu_set_t old_affinity;
    bool restore;

public:
    TemporalThreadAffinity(uint32 core_id, bool checkStatus = true, const bool restore_ = true)
        : restore(restore_)
    {
        assert(core_id < 1024);
        auto res = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &old_affinity);
        if (res != 0)
        {
            std::cerr << "ERROR: pthread_getaffinity_np for core " << core_id << " failed with code " << res << "\n";
            throw std::exception();
        }
        cpu_set_t new_affinity;
        CPU_ZERO(&new_affinity);
        CPU_SET(core_id, &new_affinity);
        // CPU_CMP() returns true if old_affinity is NOT equal to new_affinity
        if (!(CPU_CMP(&old_affinity, &new_affinity)))
        {
            restore = false;
            return; // the same affinity => return
        }
        res = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &new_affinity);
        if (res != 0 && checkStatus)
        {
            std::cerr << "ERROR: pthread_setaffinity_np for core " << core_id << " failed with code " << res << "\n";
            throw std::exception();
        }
    }
    ~TemporalThreadAffinity()
    {
        if (restore) pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &old_affinity);
    }
    bool supported() const { return true; }

#elif defined(__linux__)
    cpu_set_t* old_affinity = nullptr;
    static constexpr auto maxCPUs = 8192;
    const size_t set_size;
    bool restore;

public:
    TemporalThreadAffinity(const uint32 core_id, bool checkStatus = true, const bool restore_ = true)
        : set_size(CPU_ALLOC_SIZE(maxCPUs)), restore(restore_)
    {
        assert(core_id < maxCPUs);
        old_affinity = CPU_ALLOC(maxCPUs);
        assert(old_affinity);
        auto res = pthread_getaffinity_np(pthread_self(), set_size, old_affinity);
        if (res != 0)
        {
            std::cerr << "ERROR: pthread_getaffinity_np for core " << core_id << " failed with code " << res << "\n";
            CPU_FREE(old_affinity);
            old_affinity = nullptr;
            throw std::runtime_error("pthread_getaffinity_np failed");
        }
        cpu_set_t* new_affinity = CPU_ALLOC(maxCPUs);
        assert(new_affinity);
        CPU_ZERO_S(set_size, new_affinity);
        CPU_SET_S(core_id, set_size, new_affinity);
        if (CPU_EQUAL_S(set_size, old_affinity, new_affinity))
        {
            CPU_FREE(new_affinity);
            restore = false;
            return;
        }
        res = pthread_setaffinity_np(pthread_self(), set_size, new_affinity);
        CPU_FREE(new_affinity);
        if (res != 0 && checkStatus)
        {
            std::cerr << "ERROR: pthread_setaffinity_np for core " << core_id << " failed with code " << res << "\n";
            CPU_FREE(old_affinity);
            old_affinity = nullptr;
            throw std::runtime_error("pthread_setaffinity_np failed");
        }
    }
    ~TemporalThreadAffinity()
    {
        if (restore) pthread_setaffinity_np(pthread_self(), set_size, old_affinity);
        CPU_FREE(old_affinity);
        old_affinity = nullptr;
    }
    bool supported() const { return true; }
#elif defined(_MSC_VER)
    ThreadGroupTempAffinity affinity;
public:
    TemporalThreadAffinity(uint32 core, bool checkStatus = true, const bool restore = true)
        : affinity(core, checkStatus, restore)
    {
    }
    bool supported() const { return true; }
#else // not implemented for os x
public:
    TemporalThreadAffinity(uint32) { }
    TemporalThreadAffinity(uint32, bool) {}
    bool supported() const { return false; }
#endif
};


} // namespace pcm
