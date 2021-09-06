/*
Copyright (c) 2009-2020, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//            Otto Bruggeman
//            Thomas Willhalm
//            Pat Fay
//            Austen Ott
//            Jim Harris (FreeBSD)

/*!     \file cpucounters.cpp
        \brief The bulk of PCM implementation
  */

//#define PCM_TEST_FALLBACK_TO_ATOM

#include <stdio.h>
#include <assert.h>
#ifdef PCM_EXPORTS
// pcm-lib.h includes cpucounters.h
#include "PCM-Lib_Win\pcm-lib.h"
#else
#include "cpucounters.h"
#endif
#include "msr.h"
#include "pci.h"
#include "types.h"
#include "utils.h"
#include "topology.h"

#if defined (__FreeBSD__) || defined(__DragonFly__)
#include <sys/param.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/sem.h>
#include <sys/ioccom.h>
#include <sys/cpuctl.h>
#include <machine/cpufunc.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#include <windows.h>
#include <comdef.h>
#include <tchar.h>
#include "winring0/OlsApiInit.h"
#include "PCM_Win/windriver.h"
#else
#include <pthread.h>
#if defined(__FreeBSD__) || (defined(__DragonFly__) && __DragonFly_version >= 400707)
#include <pthread_np.h>
#endif
#include <errno.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/mman.h>
#endif
#endif

#include <string.h>
#include <limits>
#include <map>
#include <algorithm>
#include <thread>
#include <future>
#include <functional>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>

#ifdef __APPLE__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/sem.h>
#endif

namespace pcm {

#ifdef __APPLE__
// convertUnknownToInt is used in the safe sysctl call to convert an unknown size to an int
int convertUnknownToInt(size_t size, char* value);
#endif

#undef PCM_DEBUG_TOPOLOGY // debug of topology enumeration routine

// FreeBSD is much more restrictive about names for semaphores
#if defined (__FreeBSD__)
#define PCM_INSTANCE_LOCK_SEMAPHORE_NAME "/PCM_inst_lock"
#define PCM_NUM_INSTANCES_SEMAPHORE_NAME "/num_PCM_inst"
#else
#define PCM_INSTANCE_LOCK_SEMAPHORE_NAME "PCM inst lock"
#define PCM_NUM_INSTANCES_SEMAPHORE_NAME "Num PCM insts"
#endif

#ifdef _MSC_VER

HMODULE hOpenLibSys = NULL;

#ifndef NO_WINRING
bool PCM::initWinRing0Lib()
{
    const BOOL result = InitOpenLibSys(&hOpenLibSys);

    if (result == FALSE)
    {
        CloseHandle(hOpenLibSys);
        hOpenLibSys = NULL;
        return false;
    }

    BYTE major, minor, revision, release;
    GetDriverVersion(&major, &minor, &revision, &release);
    wchar_t buffer[128];
    swprintf_s(buffer, 128, _T("\\\\.\\WinRing0_%d_%d_%d"),(int)major,(int)minor, (int)revision);
    restrictDriverAccess(buffer);

    return true;
}
#endif // NO_WINRING

class InstanceLock
{
    HANDLE Mutex;

    InstanceLock();
public:
    InstanceLock(const bool global)
    {
        Mutex = CreateMutex(NULL, FALSE,
            global?(L"Global\\Processor Counter Monitor instance create/destroy lock"):(L"Local\\Processor Counter Monitor instance create/destroy lock"));
        // lock
        WaitForSingleObject(Mutex, INFINITE);
    }
    ~InstanceLock()
    {
        // unlock
        ReleaseMutex(Mutex);
        CloseHandle(Mutex);
    }
};
#else // Linux or Apple

pthread_mutex_t processIntanceMutex = PTHREAD_MUTEX_INITIALIZER;

class InstanceLock
{
    const char * globalSemaphoreName;
    sem_t * globalSemaphore;
    bool global;

    InstanceLock();
public:
    InstanceLock(const bool global_) : globalSemaphoreName(PCM_INSTANCE_LOCK_SEMAPHORE_NAME), globalSemaphore(NULL), global(global_)
    {
        if(!global)
        {
            pthread_mutex_lock(&processIntanceMutex);
            return;
        }
        umask(0);
        while (1)
        {
            //sem_unlink(globalSemaphoreName); // temporary
            globalSemaphore = sem_open(globalSemaphoreName, O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO, 1);
            if (SEM_FAILED == globalSemaphore)
            {
              if (EACCES == errno)
                {
                    std::cerr << "PCM Error, do not have permissions to open semaphores in /dev/shm/. Waiting one second and retrying...\n";
                    sleep(1);
                }
            }
            else
            {
                /*
                if (sem_post(globalSemaphore)) {
                    perror("sem_post error");
                }
                */
                break;         // success
            }
        }
        if (sem_wait(globalSemaphore)) {
            perror("sem_wait error");
        }
    }
    ~InstanceLock()
    {
        if(!global)
        {
            pthread_mutex_unlock(&processIntanceMutex);
            return;
        }
        if (sem_post(globalSemaphore)) {
            perror("sem_post error");
        }
    }
};
#endif // end of _MSC_VER else

#if defined(__FreeBSD__)
#define cpu_set_t cpuset_t
#endif

class TemporalThreadAffinity  // speedup trick for Linux, FreeBSD, DragonFlyBSD, Windows
{
    TemporalThreadAffinity(); // forbiden
#if defined(__FreeBSD__) || (defined(__DragonFly__) && __DragonFly_version >= 400707)
    cpu_set_t old_affinity;

public:
    TemporalThreadAffinity(uint32 core_id, bool checkStatus = true)
    {
        pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &old_affinity);

        cpu_set_t new_affinity;
        CPU_ZERO(&new_affinity);
        CPU_SET(core_id, &new_affinity);
        const auto res = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &new_affinity);
        if (res != 0 && checkStatus)
        {
            std::cerr << "ERROR: pthread_setaffinity_np for core " << core_id << " failed with code " << res << "\n";
            throw std::exception();
        }
    }
    ~TemporalThreadAffinity()
    {
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &old_affinity);
    }
    bool supported() const { return true; }

#elif defined(__linux__)
    cpu_set_t * old_affinity;
    static constexpr auto maxCPUs = 8192;
    const size_t set_size;

public:
    TemporalThreadAffinity(const uint32 core_id, bool checkStatus = true)
        : set_size(CPU_ALLOC_SIZE(maxCPUs))
    {
        old_affinity = CPU_ALLOC(maxCPUs);
        assert(old_affinity);
        pthread_getaffinity_np(pthread_self(), set_size, old_affinity);

        cpu_set_t * new_affinity = CPU_ALLOC(maxCPUs);
        assert(new_affinity);
        CPU_ZERO_S(set_size, new_affinity);
        CPU_SET_S(core_id, set_size, new_affinity);
        const auto res = pthread_setaffinity_np(pthread_self(), set_size, new_affinity);
        CPU_FREE(new_affinity);
        if (res != 0 && checkStatus)
        {
            std::cerr << "ERROR: pthread_setaffinity_np for core " << core_id << " failed with code " << res << "\n";
            throw std::exception();
        }
    }
    ~TemporalThreadAffinity()
    {
        pthread_setaffinity_np(pthread_self(), set_size, old_affinity);
        CPU_FREE(old_affinity);
    }
    bool supported() const { return true; }
#elif defined(_MSC_VER)
    ThreadGroupTempAffinity affinity;
public:
    TemporalThreadAffinity(uint32 core, bool checkStatus = true) : affinity(core, checkStatus) {}
    bool supported() const { return true; }
#else // not implemented for os x
public:
    TemporalThreadAffinity(uint32) { }
    TemporalThreadAffinity(uint32, bool) {}
    bool supported() const { return false;  }
#endif
};


PCM * PCM::instance = NULL;

/*
static int bitCount(uint64 n)
{
    int count = 0;
    while (n)
    {
        count += static_cast<int>(n & 0x00000001);
        n >>= static_cast<uint64>(1);
    }
    return count;
}
*/

PCM * PCM::getInstance()
{
    // no lock here
    if (instance) return instance;

    InstanceLock lock(false);
    if (instance) return instance;

    return instance = new PCM();
}

uint32 build_bit_ui(uint32 beg, uint32 end)
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

uint32 extract_bits_ui(uint32 myin, uint32 beg, uint32 end)
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

uint64 build_bit(uint32 beg, uint32 end)
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

uint64 extract_bits(uint64 myin, uint32 beg, uint32 end)
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

uint64 PCM::extractCoreGenCounterValue(uint64 val)
{
    if (canUsePerf) return val;

    if(core_gen_counter_width)
        return extract_bits(val, 0, core_gen_counter_width-1);

    return val;
}

uint64 PCM::extractCoreFixedCounterValue(uint64 val)
{
    if (canUsePerf) return val;

    if(core_fixed_counter_width)
        return extract_bits(val, 0, core_fixed_counter_width-1);

    return val;
}

uint64 PCM::extractUncoreGenCounterValue(uint64 val)
{
    if(uncore_gen_counter_width)
        return extract_bits(val, 0, uncore_gen_counter_width-1);

    return val;
}

uint64 PCM::extractUncoreFixedCounterValue(uint64 val)
{
    if(uncore_fixed_counter_width)
        return extract_bits(val, 0, uncore_fixed_counter_width-1);

    return val;
}

uint64 PCM::extractQOSMonitoring(uint64 val)
{
    //Check if any of the error bit(63) or Unavailable bit(62) of the IA32_QM_CTR MSR are 1
    if(val & (3ULL<<62))
    {
        // invalid reading
        return static_cast<uint64>(PCM_INVALID_QOS_MONITORING_DATA);
    }

    // valid reading
    return extract_bits(val,0,61);
}
int32 extractThermalHeadroom(uint64 val)
{
    if(val & (1ULL<<31ULL))
    {  // valid reading
       return static_cast<int32>(extract_bits(val, 16, 22));
    }

    // invalid reading
    return static_cast<int32>(PCM_INVALID_THERMAL_HEADROOM);
}


uint64 get_frequency_from_cpuid();



/* Adding the new version of cpuid with leaf and subleaf as an input */
void pcm_cpuid(const unsigned leaf, const unsigned subleaf, PCM_CPUID_INFO & info)
{
    #ifdef _MSC_VER
    __cpuidex(info.array, leaf, subleaf);
    #else
    __asm__ __volatile__ ("cpuid" : \
                          "=a" (info.reg.eax), "=b" (info.reg.ebx), "=c" (info.reg.ecx), "=d" (info.reg.edx) : "a" (leaf), "c" (subleaf));
    #endif
}

void PCM::readCoreCounterConfig(const bool complainAboutMSR)
{
    if (max_cpuid >= 0xa)
    {
        // get counter related info
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(0xa, cpuinfo);
        perfmon_version = extract_bits_ui(cpuinfo.array[0], 0, 7);
        core_gen_counter_num_max = extract_bits_ui(cpuinfo.array[0], 8, 15);
        core_gen_counter_width = extract_bits_ui(cpuinfo.array[0], 16, 23);
        if (perfmon_version > 1)
        {
            core_fixed_counter_num_max = extract_bits_ui(cpuinfo.array[3], 0, 4);
            core_fixed_counter_width = extract_bits_ui(cpuinfo.array[3], 5, 12);
        }
        else if (1 == perfmon_version)
        {
            core_fixed_counter_num_max = 3;
            core_fixed_counter_width = core_gen_counter_width;
        }
        if (isForceRTMAbortModeAvailable())
        {
            uint64 TSXForceAbort = 0;
            if (MSR.empty())
            {
                if (complainAboutMSR)
                {
                    std::cerr << "PCM Error: Can't determine the number of available counters reliably because of no access to MSR.\n";
                }
            }
            else if (MSR[0]->read(MSR_TSX_FORCE_ABORT, &TSXForceAbort) == sizeof(uint64))
            {
                TSXForceAbort &= 1;
                /*
                    TSXForceAbort is 0 (default mode) => the number of useful gen counters is 3
                    TSXForceAbort is 1                => the number of gen counters is unchanged
                */
                if (TSXForceAbort == 0)
                {
                    core_gen_counter_num_max = 3;
                }
            }
            else
            {
                std::cerr << "PCM Error: Can't determine the number of available counters reliably because reading MSR_TSX_FORCE_ABORT failed.\n";
            }
        }
#if defined(__linux__)
        const auto env = std::getenv("PCM_NO_AWS_WORKAROUND");
        auto aws_workaround = true;
        if (env != nullptr && std::string(env) == std::string("1"))
        {
            aws_workaround = false;
        }
        if (aws_workaround == true && vm == true && linux_arch_perfmon == true && core_gen_counter_num_max > 3)
        {
            core_gen_counter_num_max = 3;
            std::cerr << "INFO: Reducing the number of programmable counters to 3 to workaround the fixed cycle counter virtualization issue on AWS.\n";
            std::cerr << "      You can disable the workaround by setting PCM_NO_AWS_WORKAROUND=1 environment variable\n";
        }
#endif
    }
}

bool PCM::isFixedCounterSupported(unsigned c)
{
    if (max_cpuid >= 0xa)
    {
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(0xa, cpuinfo);
	return extract_bits_ui(cpuinfo.reg.ecx, c, c) || (extract_bits_ui(cpuinfo.reg.edx, 4, 0) > c);
    }
    return false;
}

bool PCM::isHWTMAL1Supported() const
{
    static int supported = -1;
    if (supported < 0)
    {
        supported = 0;
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(1, cpuinfo);
        if (extract_bits_ui(cpuinfo.reg.ecx, 15, 15) && MSR.size())
        {
            uint64 perf_cap;
            if (MSR[0]->read(MSR_PERF_CAPABILITIES, &perf_cap) == sizeof(uint64))
            {
                supported = (int)extract_bits(perf_cap, 15, 15);
            }
        }
    }
    return supported > 0;
}

void PCM::readCPUMicrocodeLevel()
{
    if (MSR.empty()) return;
    const int ref_core = 0;
    TemporalThreadAffinity affinity(ref_core);
    if (affinity.supported() && isCoreOnline(ref_core))
    {   // see "Update Signature and Verification" and "Determining the Signature"
        // sections in Intel SDM how to read ucode level
        if (MSR[ref_core]->write(MSR_IA32_BIOS_SIGN_ID, 0) == sizeof(uint64))
        {
            PCM_CPUID_INFO cpuinfo;
            pcm_cpuid(1, cpuinfo); // cpuid instructions updates MSR_IA32_BIOS_SIGN_ID
            uint64 result = 0;
            if (MSR[ref_core]->read(MSR_IA32_BIOS_SIGN_ID, &result) == sizeof(uint64))
            {
                cpu_microcode_level = result >> 32;
            }
        }
    }
}

int32 PCM::getMaxCustomCoreEvents()
{
    return core_gen_counter_num_max;
}

bool PCM::detectModel()
{
    char buffer[1024];
    union {
        char cbuf[16];
        int  ibuf[16 / sizeof(int)];
    } buf;
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0, cpuinfo);
    memset(buffer, 0, 1024);
    memset(buf.cbuf, 0, 16);
    buf.ibuf[0] = cpuinfo.array[1];
    buf.ibuf[1] = cpuinfo.array[3];
    buf.ibuf[2] = cpuinfo.array[2];
    if (strncmp(buf.cbuf, "GenuineIntel", 4 * 3) != 0)
    {
        std::cerr << getUnsupportedMessage() << "\n";
        return false;
    }
    max_cpuid = cpuinfo.array[0];

    pcm_cpuid(1, cpuinfo);
    cpu_family = (((cpuinfo.array[0]) >> 8) & 0xf) | ((cpuinfo.array[0] & 0xf00000) >> 16);
    cpu_model = (((cpuinfo.array[0]) & 0xf0) >> 4) | ((cpuinfo.array[0] & 0xf0000) >> 12);
    cpu_stepping = cpuinfo.array[0] & 0x0f;

    if (cpuinfo.reg.ecx & (1UL << 31UL)) {
        vm = true;
        std::cerr << "Detected a hypervisor/virtualization technology. Some metrics might not be available due to configuration or availability of virtual hardware features.\n";
    }

    readCoreCounterConfig();

    if (cpu_family != 6)
    {
        std::cerr << getUnsupportedMessage() << " CPU Family: " << cpu_family << "\n";
        return false;
    }

    pcm_cpuid(7, 0, cpuinfo);

#ifdef __linux__
    auto checkLinuxCpuinfoFlag = [](const std::string& flag) -> bool
    {
        std::ifstream linuxCpuinfo("/proc/cpuinfo");
        if (linuxCpuinfo.is_open())
        {
            std::string line;
            while (std::getline(linuxCpuinfo, line))
            {
                auto tokens = split(line, ':');
                if (tokens.size() >= 2 && tokens[0].find("flags") == 0)
                {
                    for (auto curFlag : split(tokens[1], ' '))
                    {
                        if (flag == curFlag)
                        {
                            return true;
                        }
                    }
                }
            }
            linuxCpuinfo.close();
        }
        return false;
    };
    linux_arch_perfmon = checkLinuxCpuinfoFlag("arch_perfmon");
    std::cerr << "Linux arch_perfmon flag  : " << (linux_arch_perfmon ? "yes" : "no") << "\n";
    if (vm == true && linux_arch_perfmon == false)
    {
        std::cerr << "ERROR: vPMU is not enabled in the hypervisor. Please see details in https://software.intel.com/content/www/us/en/develop/documentation/vtune-help/top/set-up-analysis-target/on-virtual-machine.html \n";
        std::cerr << "       you can force-continue by setting PCM_IGNORE_ARCH_PERFMON=1 environment variable.\n";
        auto env = std::getenv("PCM_IGNORE_ARCH_PERFMON");
        auto ignore_arch_perfmon = false;
        if (env != nullptr && std::string(env) == std::string("1"))
        {
            ignore_arch_perfmon = true;
        }
        if (!ignore_arch_perfmon)
        {
            return false;
        }
    }
#endif

    std::cerr << "IBRS and IBPB supported  : " << ((cpuinfo.reg.edx & (1 << 26)) ? "yes" : "no") << "\n";
    std::cerr << "STIBP supported          : " << ((cpuinfo.reg.edx & (1 << 27)) ? "yes" : "no") << "\n";
    std::cerr << "Spec arch caps supported : " << ((cpuinfo.reg.edx & (1 << 29)) ? "yes" : "no") << "\n";

    return true;
}

bool PCM::isRDTDisabled() const
{
    static int flag = -1;
    if (flag < 0)
    {
        // flag not yet initialized
        const char * varname = "PCM_NO_RDT";
        char* env = nullptr;
#ifdef _MSC_VER
        _dupenv_s(&env, NULL, varname);
#else
        env = std::getenv(varname);
#endif
        if (env != nullptr && std::string(env) == std::string("1"))
        {
            std::cout << "Disabling RDT usage because PCM_NO_RDT=1 environment variable is set.\n";
            flag = 1;
        }
        else
        {
            flag = 0;
        }
#ifdef _MSC_VER
        free(env);
#endif
    }
    return flag > 0;
}

bool PCM::QOSMetricAvailable() const
{
    if (isRDTDisabled()) return false;
#ifndef __linux__
    if (isSecureBoot()) return false;
#endif
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0x7,0,cpuinfo);
    return (cpuinfo.reg.ebx & (1<<12))?true:false;
}

bool PCM::L3QOSMetricAvailable() const
{
    if (isRDTDisabled()) return false;
#ifndef __linux__
    if (isSecureBoot()) return false;
#endif
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0xf,0,cpuinfo);
    return (cpuinfo.reg.edx & (1<<1))?true:false;
}

bool PCM::L3CacheOccupancyMetricAvailable() const
{
    PCM_CPUID_INFO cpuinfo;
    if (!(QOSMetricAvailable() && L3QOSMetricAvailable()))
        return false;
    pcm_cpuid(0xf,0x1,cpuinfo);
    return (cpuinfo.reg.edx & 1)?true:false;
}

bool PCM::CoreLocalMemoryBWMetricAvailable() const
{
    if (cpu_model == SKX && cpu_stepping < 5) return false; // SKZ4 errata
    PCM_CPUID_INFO cpuinfo;
    if (!(QOSMetricAvailable() && L3QOSMetricAvailable()))
            return false;
    pcm_cpuid(0xf,0x1,cpuinfo);
    return (cpuinfo.reg.edx & 2)?true:false;
}

bool PCM::CoreRemoteMemoryBWMetricAvailable() const
{
    if (cpu_model == SKX && cpu_stepping < 5) return false; // SKZ4 errata
    PCM_CPUID_INFO cpuinfo;
    if (!(QOSMetricAvailable() && L3QOSMetricAvailable()))
        return false;
    pcm_cpuid(0xf, 0x1, cpuinfo);
    return (cpuinfo.reg.edx & 4) ? true : false;
}

unsigned PCM::getMaxRMID() const
{
    unsigned maxRMID = 0;
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0xf,0,cpuinfo);
    maxRMID = (unsigned)cpuinfo.reg.ebx + 1;
    return maxRMID;
}

void PCM::initRDT()
{
    if (!(QOSMetricAvailable() && L3QOSMetricAvailable()))
        return;
#ifdef __linux__
    auto env = std::getenv("PCM_USE_RESCTRL");
    if (env != nullptr && std::string(env) == std::string("1"))
    {
        std::cout << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because environment variable PCM_USE_RESCTRL=1\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
    if (resctrl.isMounted())
    {
        std::cout << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because resctrl driver is mounted.\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
    if (isSecureBoot())
    {
        std::cout << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because Secure Boot mode is enabled.\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
#endif
    std::cout << "Initializing RMIDs" << std::endl;
    unsigned maxRMID;
    /* Calculate maximum number of RMID supported by socket */
    maxRMID = getMaxRMID();
    // std::cout << "Maximum RMIDs per socket in the system : " << maxRMID << "\n";
    std::vector<uint32> rmid(num_sockets);
    for(int32 i = 0; i < num_sockets; i ++)
            rmid[i] = maxRMID - 1;

    /* Associate each core with 1 RMID */
    for(int32 core = 0; core < num_cores; core ++ )
    {
        if(!isCoreOnline(core)) continue;

        uint64 msr_pqr_assoc = 0 ;
        uint64 msr_qm_evtsel = 0 ;
                MSR[core]->lock();
        //Read 0xC8F MSR for each core
        MSR[core]->read(IA32_PQR_ASSOC, &msr_pqr_assoc);
        //std::cout << "initRMID reading IA32_PQR_ASSOC 0x" << std::hex << msr_pqr_assoc << std::dec << "\n";

        //std::cout << "Socket Id : " << topology[core].socket;
        msr_pqr_assoc &= 0xffffffff00000000ULL;
        msr_pqr_assoc |= (uint64)(rmid[topology[core].socket] & ((1ULL<<10)-1ULL));
        //std::cout << "initRMID writing IA32_PQR_ASSOC 0x" << std::hex << msr_pqr_assoc << std::dec << "\n";
        //Write 0xC8F MSR with new RMID for each core
        MSR[core]->write(IA32_PQR_ASSOC,msr_pqr_assoc);

        msr_qm_evtsel = static_cast<uint64>(rmid[topology[core].socket] & ((1ULL<<10)-1ULL));
        msr_qm_evtsel <<= 32;
        //Write 0xC8D MSR with new RMID for each core
        //std::cout << "initRMID writing IA32_QM_EVTSEL 0x" << std::hex << msr_qm_evtsel << std::dec << "\n";
        MSR[core]->write(IA32_QM_EVTSEL,msr_qm_evtsel);
                MSR[core]->unlock();

        /* Initializing the memory bandwidth counters */
        if (CoreLocalMemoryBWMetricAvailable())
        {
            memory_bw_local.push_back(std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MBLCounter(MSR[core]), 24, 1000));
            if (CoreRemoteMemoryBWMetricAvailable())
            {
                memory_bw_total.push_back(std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MBTCounter(MSR[core]), 24, 1000));
            }
        }
        rmid[topology[core].socket] --;
        //std::cout << std::flush; // Explicitly flush after each iteration
    }
    /* Get The scaling factor by running CPUID.0xF.0x1 instruction */
    L3ScalingFactor = getL3ScalingFactor();
}

void PCM::initQOSevent(const uint64 event, const int32 core)
{
   if(!isCoreOnline(core)) return;
   uint64 msr_qm_evtsel = 0 ;
   //Write 0xC8D MSR with the event id
   MSR[core]->read(IA32_QM_EVTSEL, &msr_qm_evtsel);
   //std::cout << "initQOSevent reading IA32_QM_EVTSEL 0x" << std::hex << msr_qm_evtsel << std::dec << "\n";
   msr_qm_evtsel &= 0xfffffffffffffff0ULL;
   msr_qm_evtsel |= event & ((1ULL<<8)-1ULL);
   //std::cout << "initQOSevent writing IA32_QM_EVTSEL 0x" << std::hex << msr_qm_evtsel << std::dec << "\n";
   MSR[core]->write(IA32_QM_EVTSEL,msr_qm_evtsel);
   //std::cout << std::flush;
}


void PCM::initCStateSupportTables()
{
#define PCM_PARAM_PROTECT(...) __VA_ARGS__
#define PCM_CSTATE_ARRAY(array_ , val ) \
    { \
        static uint64 tmp[] = val; \
        PCM_COMPILE_ASSERT(sizeof(tmp) / sizeof(uint64) == (static_cast<int>(MAX_C_STATE)+1)); \
        array_ = tmp; \
        break; \
    }

    // fill package C state array
    switch(cpu_model)
    {
        case ATOM:
        case ATOM_2:
        case CENTERTON:
        case AVOTON:
        case BAYTRAIL:
        case CHERRYTRAIL:
        case APOLLO_LAKE:
        case DENVERTON:
	case SNOWRIDGE:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0x3F8, 0, 0x3F9, 0, 0x3FA, 0, 0, 0, 0 }) );
        case NEHALEM_EP:
        case NEHALEM:
        case CLARKDALE:
        case WESTMERE_EP:
        case NEHALEM_EX:
        case WESTMERE_EX:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0, 0x3F8, 0, 0, 0x3F9, 0x3FA, 0, 0, 0}) );
        case SANDY_BRIDGE:
        case JAKETOWN:
        case IVY_BRIDGE:
        case IVYTOWN:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0x60D, 0x3F8, 0, 0, 0x3F9, 0x3FA, 0, 0, 0}) );
        case HASWELL:
        case HASWELL_2:
        case HASWELLX:
        case BDX_DE:
        case BDX:
        case KNL:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0x60D, 0x3F8, 0, 0, 0x3F9,  0x3FA, 0, 0, 0}) );
        case SKX:
        case ICX:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0x60D, 0, 0, 0, 0x3F9, 0, 0, 0, 0}) );
        case HASWELL_ULT:
        case BROADWELL:
        PCM_SKL_PATH_CASES
        case BROADWELL_XEON_E3:
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({0, 0, 0x60D, 0x3F8, 0, 0, 0x3F9, 0x3FA, 0x630, 0x631, 0x632}) );

        default:
            std::cerr << "PCM error: package C-states support array is not initialized. Package C-states metrics will not be shown.\n";
            PCM_CSTATE_ARRAY(pkgCStateMsr, PCM_PARAM_PROTECT({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }) );
    };

    // fill core C state array
    switch(cpu_model)
    {
        case ATOM:
        case ATOM_2:
        case CENTERTON:
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }) );
        case NEHALEM_EP:
        case NEHALEM:
        case CLARKDALE:
        case WESTMERE_EP:
        case NEHALEM_EX:
        case WESTMERE_EX:
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({0, 0, 0, 0x3FC, 0, 0, 0x3FD, 0, 0, 0, 0}) );
        case SANDY_BRIDGE:
        case JAKETOWN:
        case IVY_BRIDGE:
        case IVYTOWN:
        case HASWELL:
        case HASWELL_2:
        case HASWELL_ULT:
        case HASWELLX:
        case BDX_DE:
        case BDX:
        case BROADWELL:
        case BROADWELL_XEON_E3:
        case BAYTRAIL:
        case AVOTON:
        case CHERRYTRAIL:
        case APOLLO_LAKE:
        case DENVERTON:
        PCM_SKL_PATH_CASES
	case SNOWRIDGE:
        case ICX:
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({0, 0, 0, 0x3FC, 0, 0, 0x3FD, 0x3FE, 0, 0, 0}) );
        case KNL:
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({0, 0, 0, 0, 0, 0, 0x3FF, 0, 0, 0, 0}) );
        case SKX:
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({0, 0, 0, 0, 0, 0, 0x3FD, 0, 0, 0, 0}) );
        default:
            std::cerr << "PCM error: core C-states support array is not initialized. Core C-states metrics will not be shown.\n";
            PCM_CSTATE_ARRAY(coreCStateMsr, PCM_PARAM_PROTECT({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }) );
    };
}


#ifdef __linux__
FILE * tryOpen(const char * path, const char * mode)
{
    FILE * f = fopen(path, mode);
    if (!f)
    {
        f = fopen((std::string("/pcm") + path).c_str(), mode);
    }
    return f;
}

std::string readSysFS(const char * path, bool silent = false)
{
    FILE * f = tryOpen(path, "r");
    if (!f)
    {
        if (silent == false) std::cerr << "ERROR: Can not open " << path << " file.\n";
        return std::string();
    }
    char buffer[1024];
    if(NULL == fgets(buffer, 1024, f))
    {
        if (silent == false) std::cerr << "ERROR: Can not read from " << path << ".\n";
        fclose(f);
        return std::string();
    }
    fclose(f);
    return std::string(buffer);
}

bool writeSysFS(const char * path, const std::string & value, bool silent = false)
{
    FILE * f = tryOpen(path, "w");
    if (!f)
    {
        if (silent == false) std::cerr << "ERROR: Can not open " << path << " file.\n";
        return false;
    }
    if (fputs(value.c_str(), f) < 0)
    {
        if (silent == false) std::cerr << "ERROR: Can not write to " << path << ".\n";
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

int readMaxFromSysFS(const char * path)
{
    std::string content = readSysFS(path);
    const char * buffer = content.c_str();
    int result = -1;
    pcm_sscanf(buffer) >> s_expect("0-") >> result;
    if(result == -1)
    {
       pcm_sscanf(buffer) >> result;
    }
    return result;
}

constexpr auto perfSlotsPath = "/sys/bus/event_source/devices/cpu/events/slots";
constexpr auto perfBadSpecPath = "/sys/bus/event_source/devices/cpu/events/topdown-bad-spec";
constexpr auto perfBackEndPath = "/sys/bus/event_source/devices/cpu/events/topdown-be-bound";
constexpr auto perfFrontEndPath = "/sys/bus/event_source/devices/cpu/events/topdown-fe-bound";
constexpr auto perfRetiringPath = "/sys/bus/event_source/devices/cpu/events/topdown-retiring";

bool perfSupportsTopDown()
{
    static int yes = -1;
    if (-1 == yes)
    {
        const auto slots = readSysFS(perfSlotsPath, true);
        const auto bad = readSysFS(perfBadSpecPath, true);
        const auto be = readSysFS(perfBackEndPath, true);
        const auto fe = readSysFS(perfFrontEndPath, true);
        const auto ret = readSysFS(perfRetiringPath, true);
        yes = (slots.size() && bad.size() && be.size() && fe.size() && ret.size()) ? 1 : 0;
    }
    return 1 == yes;
}

#endif

bool PCM::discoverSystemTopology()
{
    typedef std::map<uint32, uint32> socketIdMap_type;
    socketIdMap_type socketIdMap;

    PCM_CPUID_INFO cpuid_args;
    // init constants for CPU topology leaf 0xB
    // adapted from Topology Enumeration Reference code for Intel 64 Architecture
    // https://software.intel.com/en-us/articles/intel-64-architecture-processor-topology-enumeration
    int wasCoreReported = 0, wasThreadReported = 0;
    int subleaf = 0, levelType, levelShift;
    //uint32 coreSelectMask = 0, smtSelectMask = 0;
    uint32 smtMaskWidth = 0;
    //uint32 pkgSelectMask = (-1), pkgSelectMaskShift = 0;
    uint32 corePlusSMTMaskWidth = 0;
    uint32 coreMaskWidth = 0;

    {
        TemporalThreadAffinity aff0(0);
        do
        {
            pcm_cpuid(0xb, subleaf, cpuid_args);
            if (cpuid_args.array[1] == 0)
            { // if EBX ==0 then this subleaf is not valid, we can exit the loop
                break;
            }
            levelType = extract_bits_ui(cpuid_args.array[2], 8, 15);
            levelShift = extract_bits_ui(cpuid_args.array[0], 0, 4);
            switch (levelType)
            {
            case 1: //level type is SMT, so levelShift is the SMT_Mask_Width
                smtMaskWidth = levelShift;
                wasThreadReported = 1;
                break;
            case 2: //level type is Core, so levelShift is the CorePlusSMT_Mask_Width
                corePlusSMTMaskWidth = levelShift;
                wasCoreReported = 1;
                break;
            default:
                break;
            }
            subleaf++;
        } while (1);
    }

    if (wasThreadReported && wasCoreReported)
    {
        coreMaskWidth = corePlusSMTMaskWidth - smtMaskWidth;
    }
    else if (!wasCoreReported && wasThreadReported)
    {
        coreMaskWidth = smtMaskWidth;
    }
    else
    {
        std::cerr << "ERROR: Major problem? No leaf 0 under cpuid function 11.\n";
        return false;
    }

    uint32 l2CacheMaskShift = 0;
#ifdef PCM_DEBUG_TOPOLOGY
    uint32 threadsSharingL2;
#endif
    uint32 l2CacheMaskWidth;

    pcm_cpuid(0x4, 2, cpuid_args); // get ID for L2 cache
    l2CacheMaskWidth = 1 + extract_bits_ui(cpuid_args.array[0],14,25); // number of APIC IDs sharing L2 cache
#ifdef PCM_DEBUG_TOPOLOGY
    threadsSharingL2 = l2CacheMaskWidth;
#endif
    for( ; l2CacheMaskWidth > 1; l2CacheMaskWidth >>= 1)
    {
        l2CacheMaskShift++;
    }
#ifdef PCM_DEBUG_TOPOLOGY
    std::cerr << "DEBUG: Number of threads sharing L2 cache = " << threadsSharingL2
              << " [the most significant bit = " << l2CacheMaskShift << "]\n";
#endif

    auto populateEntry = [&smtMaskWidth, &coreMaskWidth, &l2CacheMaskShift](TopologyEntry & entry, const int apic_id)
    {
        entry.thread_id = smtMaskWidth ? extract_bits_ui(apic_id, 0, smtMaskWidth - 1) : 0;
        entry.core_id = extract_bits_ui(apic_id, smtMaskWidth, smtMaskWidth + coreMaskWidth - 1);
        entry.socket = extract_bits_ui(apic_id, smtMaskWidth + coreMaskWidth, 31);
        entry.tile_id = extract_bits_ui(apic_id, l2CacheMaskShift, 31);
    };

#ifdef _MSC_VER
// version for Windows 7 and later version

    char * slpi = new char[sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)];
    DWORD len = (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    BOOL res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);

    while (res == FALSE)
    {
        delete[] slpi;

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            slpi = new char[len];
            res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);
        }
        else
        {
            std::wcerr << "Error in Windows function 'GetLogicalProcessorInformationEx': " <<
                GetLastError() << " ";
            const TCHAR * strError = _com_error(GetLastError()).ErrorMessage();
            if (strError) std::wcerr << strError;
            std::wcerr << "\n";
            return false;
        }
    }

    char * base_slpi = slpi;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pi = NULL;

    for ( ; slpi < base_slpi + len; slpi += (DWORD)pi->Size)
    {
        pi = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi;
        if (pi->Relationship == RelationProcessorCore)
        {
            threads_per_core = (pi->Processor.Flags == LTP_PC_SMT) ? 2 : 1;
            // std::cout << "thr per core: " << threads_per_core << "\n";
            num_cores += threads_per_core;
        }
    }
    // std::cout << std::flush;

    num_online_cores = num_cores;

    if (num_cores != GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))
    {
        std::cerr << "Error in processor group size counting: " << num_cores << "!=" << GetActiveProcessorCount(ALL_PROCESSOR_GROUPS) << "\n";
        std::cerr << "Make sure your binary is compiled for 64-bit: using 'x64' platform configuration.\n";
        return false;
    }

    for (int i = 0; i < (int)num_cores; i++)
    {
        ThreadGroupTempAffinity affinity(i);

        pcm_cpuid(0xb, 0x0, cpuid_args);

        int apic_id = cpuid_args.array[3];

        TopologyEntry entry;
        entry.os_id = i;

        populateEntry(entry, apic_id);

        topology.push_back(entry);
        socketIdMap[entry.socket] = 0;
    }

    delete[] base_slpi;

#else
    // for Linux, Mac OS, FreeBSD and DragonFlyBSD

    TopologyEntry entry;

#ifdef __linux__
    num_cores = readMaxFromSysFS("/sys/devices/system/cpu/present");
    if(num_cores == -1)
    {
      std::cerr << "Cannot read number of present cores\n";
      return false;
    }
    ++num_cores;

    // open /proc/cpuinfo
    FILE * f_cpuinfo = fopen("/proc/cpuinfo", "r");
    if (!f_cpuinfo)
    {
        std::cerr << "Cannot open /proc/cpuinfo file.\n";
        return false;
    }

    // map with key=pkg_apic_id (not necessarily zero based or sequential) and
    // associated value=socket_id that should be 0 based and sequential
    std::map<int, int> found_pkg_ids;
    topology.resize(num_cores);
    char buffer[1024];
    while (0 != fgets(buffer, 1024, f_cpuinfo))
    {
        if (strncmp(buffer, "processor", sizeof("processor") - 1) == 0)
        {
            pcm_sscanf(buffer) >> s_expect("processor\t: ") >> entry.os_id;
            //std::cout << "os_core_id: " << entry.os_id << "\n";
            TemporalThreadAffinity _(entry.os_id);
            pcm_cpuid(0xb, 0x0, cpuid_args);
            int apic_id = cpuid_args.array[3];

            populateEntry(entry, apic_id);

            topology[entry.os_id] = entry;
            socketIdMap[entry.socket] = 0;
            ++num_online_cores;
        }
    }
    //std::cout << std::flush;
    fclose(f_cpuinfo);

    // produce debug output similar to Intel MPI cpuinfo
#ifdef PCM_DEBUG_TOPOLOGY
    std::cerr << "=====  Processor identification  =====\n";
    std::cerr << "Processor       Thread Id.      Core Id.        Tile Id.        Package Id.\n";
    std::map<uint32, std::vector<uint32> > os_id_by_core, os_id_by_tile, core_id_by_socket;
    for(auto it = topology.begin(); it != topology.end(); ++it)
    {
        std::cerr << std::left << std::setfill(' ')
                  << std::setw(16) << it->os_id
                  << std::setw(16) << it->thread_id
                  << std::setw(16) << it->core_id
                  << std::setw(16) << it->tile_id
                  << std::setw(16) << it->socket
                  << "\n";
        if(std::find(core_id_by_socket[it->socket].begin(), core_id_by_socket[it->socket].end(), it->core_id)
                == core_id_by_socket[it->socket].end())
            core_id_by_socket[it->socket].push_back(it->core_id);
        // add socket offset to distinguish cores and tiles from different sockets
        os_id_by_core[(it->socket << 15) + it->core_id].push_back(it->os_id);
        os_id_by_tile[(it->socket << 15) + it->tile_id].push_back(it->os_id);
    }
    std::cerr << "=====  Placement on packages  =====\n";
    std::cerr << "Package Id.    Core Id.     Processors\n";
    for(auto pkg = core_id_by_socket.begin(); pkg != core_id_by_socket.end(); ++pkg)
    {
        auto core_id = pkg->second.begin();
        std::cerr << std::left << std::setfill(' ') << std::setw(15) << pkg->first << *core_id;
        for(++core_id; core_id != pkg->second.end(); ++core_id)
        {
            std::cerr << "," << *core_id;
        }
        std::cerr << "\n";
    }
    std::cerr << "\n=====  Core/Tile sharing  =====\n";
    std::cerr << "Level      Processors\nCore       ";
    for(auto core = os_id_by_core.begin(); core != os_id_by_core.end(); ++core)
    {
        auto os_id = core->second.begin();
        std::cerr << "(" << *os_id;
        for(++os_id; os_id != core->second.end(); ++os_id) {
            std::cerr << "," << *os_id;
        }
        std::cerr << ")";
    }
    std::cerr << "\nTile / L2$ ";
    for(auto core = os_id_by_tile.begin(); core != os_id_by_tile.end(); ++core)
    {
        auto os_id = core->second.begin();
        std::cerr << "(" << *os_id;
        for(++os_id; os_id != core->second.end(); ++os_id) {
            std::cerr << "," << *os_id;
        }
        std::cerr << ")";
    }
    std::cerr << "\n";
#endif // PCM_DEBUG_TOPOLOGY
#elif defined(__FreeBSD__) || defined(__DragonFly__)

    size_t size = sizeof(num_cores);
    cpuctl_cpuid_args_t cpuid_args_freebsd;
    int fd;

    if(0 != sysctlbyname("hw.ncpu", &num_cores, &size, NULL, 0))
    {
        std::cerr << "Unable to get hw.ncpu from sysctl.\n";
        return false;
    }
    num_online_cores = num_cores;

    if (modfind("cpuctl") == -1)
    {
        std::cerr << "cpuctl(4) not loaded.\n";
        return false;
    }

    for (int i = 0; i < num_cores; i++)
    {
        char cpuctl_name[64];
        int apic_id;

        snprintf(cpuctl_name, 64, "/dev/cpuctl%d", i);
        fd = ::open(cpuctl_name, O_RDWR);

        cpuid_args_freebsd.level = 0xb;

        ::ioctl(fd, CPUCTL_CPUID, &cpuid_args_freebsd);

        apic_id = cpuid_args_freebsd.data[3];

        entry.os_id = i;

        populateEntry(entry, apic_id);

        if (entry.socket == 0 && entry.core_id == 0) ++threads_per_core;

        topology.push_back(entry);
        socketIdMap[entry.socket] = 0;
    }

#else // Getting processor info for Mac OS
#define SAFE_SYSCTLBYNAME(message, ret_value)                                                              \
    {                                                                                                      \
        size_t size;                                                                                       \
        char *pParam;                                                                                      \
        if(0 != sysctlbyname(message, NULL, &size, NULL, 0))                                               \
        {                                                                                                  \
            std::cerr << "Unable to determine size of " << message << " sysctl return type.\n";            \
            return false;                                                                                  \
        }                                                                                                  \
        if(NULL == (pParam = (char *)malloc(size)))                                                        \
        {                                                                                                  \
            std::cerr << "Unable to allocate memory for " << message << "\n";                              \
            return false;                                                                                  \
        }                                                                                                  \
        if(0 != sysctlbyname(message, (void*)pParam, &size, NULL, 0))                                      \
        {                                                                                                  \
            std::cerr << "Unable to get " << message << " from sysctl.\n";                                 \
            return false;                                                                                  \
        }                                                                                                  \
        ret_value = convertUnknownToInt(size, pParam);                                                     \
        free(pParam);                                                                                      \
    }
// End SAFE_SYSCTLBYNAME

    // Using OSXs sysctl to get the number of CPUs right away
    SAFE_SYSCTLBYNAME("hw.logicalcpu", num_cores)
    num_online_cores = num_cores;

#undef SAFE_SYSCTLBYNAME

    // The OSX version needs the MSR handle earlier so that it can build the CPU topology.
    // This topology functionality should potentially go into a different KEXT
    for(int i = 0; i < num_cores; i++)
    {
        MSR.push_back(std::make_shared<SafeMsrHandle>(i));
    }

    TopologyEntry *entries = new TopologyEntry[num_cores];
    MSR[0]->buildTopology(num_cores, entries);
    for(int i = 0; i < num_cores; i++){
        socketIdMap[entries[i].socket] = 0;
        if(entries[i].os_id >= 0)
        {
            if(entries[i].core_id == 0 && entries[i].socket == 0) ++threads_per_core;
            topology.push_back(entries[i]);
        }
    }
    delete[] entries;
// End of OSX specific code
#endif // end of ifndef __APPLE__

#endif //end of ifdef _MSC_VER

    if(num_cores == 0) {
        num_cores = (int32)topology.size();
    }
    if(num_sockets == 0) {
        num_sockets = (int32)(std::max)(socketIdMap.size(), (size_t)1);
    }

    socketIdMap_type::iterator s = socketIdMap.begin();
    for (uint32 sid = 0; s != socketIdMap.end(); ++s)
    {
        s->second = sid++;
        // first is apic id, second is logical socket id
        systemTopology->addSocket( s->first, s->second );
    }

    for (int32 cid = 0; cid < num_cores; ++cid)
    {
        //std::cerr << "Cid: " << cid << "\n";
        systemTopology->addThread( cid, topology[cid] );
    }

    // All threads are here now so we can set the refCore for a socket
    for ( auto socket : systemTopology->sockets() )
        socket->setRefCore();

    // use map to change apic socket id to the logical socket id
    for (int i = 0; (i < (int)num_cores) && (!socketIdMap.empty()); ++i)
    {
        if(isCoreOnline((int32)i))
          topology[i].socket = socketIdMap[topology[i].socket];
    }

#if 0
    std::cerr << "Number of socket ids: " << socketIdMap.size() << "\n";
    std::cerr << "Topology:\nsocket os_id core_id\n";
    for (int i = 0; i < num_cores; ++i)
    {
        std::cerr << topology[i].socket << " " << topology[i].os_id << " " << topology[i].core_id << "\n";
    }
#endif
    if (threads_per_core == 0)
    {
        for (int i = 0; i < (int)num_cores; ++i)
        {
            if (topology[i].socket == topology[0].socket && topology[i].core_id == topology[0].core_id)
                ++threads_per_core;
        }
        assert(threads_per_core != 0);
    }
    if(num_phys_cores_per_socket == 0 && num_cores == num_online_cores) num_phys_cores_per_socket = num_cores / num_sockets / threads_per_core;
    if(num_online_cores == 0) num_online_cores = num_cores;

    int32 i = 0;

    socketRefCore.resize(num_sockets, -1);
    for(i = 0; i < num_cores; ++i)
    {
        if(isCoreOnline(i))
        {
            socketRefCore[topology[i].socket] = i;
        }
    }

    num_online_sockets = 0;
    for(i = 0; i < num_sockets; ++i)
    {
        if(isSocketOnline(i))
        {
            ++num_online_sockets;
        }
    }

    FrontendBoundSlots.resize(num_cores, 0);
    BadSpeculationSlots.resize(num_cores, 0);
    BackendBoundSlots.resize(num_cores, 0);
    RetiringSlots.resize(num_cores, 0);

#if 0
    std::cerr << "Socket reference cores:\n";
    for(int32 i=0; i< num_sockets;++i)
    {
        std::cerr << "socketRefCore[" << i << "]=" << socketRefCore[i] << "\n";
    }
#endif

    return true;
}

void PCM::printSystemTopology() const
{
    if(num_cores == num_online_cores)
    {
      std::cerr << "Number of physical cores: " << (num_cores/threads_per_core) << "\n";
    }

    std::cerr << "Number of logical cores: " << num_cores << "\n";
    std::cerr << "Number of online logical cores: " << num_online_cores << "\n";

    if(num_cores == num_online_cores)
    {
      std::cerr << "Threads (logical cores) per physical core: " << threads_per_core << "\n";
    }
    else
    {
        std::cerr << "Offlined cores: ";
        for (int i = 0; i < (int)num_cores; ++i)
            if(isCoreOnline((int32)i) == false)
                std::cerr << i << " ";
        std::cerr << "\n";
    }
    std::cerr << "Num sockets: " << num_sockets << "\n";
    if (num_phys_cores_per_socket > 0)
    {
        std::cerr << "Physical cores per socket: " << num_phys_cores_per_socket << "\n";
    }
    std::cerr << "Last level cache slices per socket: " << getMaxNumOfCBoxes() << "\n";
    std::cerr << "Core PMU (perfmon) version: " << perfmon_version << "\n";
    std::cerr << "Number of core PMU generic (programmable) counters: " << core_gen_counter_num_max << "\n";
    std::cerr << "Width of generic (programmable) counters: " << core_gen_counter_width << " bits\n";
    if (perfmon_version > 0)
    {
        std::cerr << "Number of core PMU fixed counters: " << core_fixed_counter_num_max << "\n";
        std::cerr << "Width of fixed counters: " << core_fixed_counter_width << " bits\n";
    }
    if (perfmon_version < 2 && vm == true)
    {
        std::cerr << "Warning: detected an unsupported virtualized environment: the hypervisor has limited the core PMU (perfmon) version to " << perfmon_version << "\n";
    }
}

bool PCM::initMSR()
{
#ifndef __APPLE__
    try
    {
        for (int i = 0; i < (int)num_cores; ++i)
        {
            if ( isCoreOnline( (int32)i ) ) {
                MSR.push_back(std::make_shared<SafeMsrHandle>(i));
                systemTopology->addMSRHandleToOSThread( MSR.back(), (uint32)i );
            } else { // the core is offlined, assign an invalid MSR handle
                MSR.push_back(std::make_shared<SafeMsrHandle>());
                systemTopology->addMSRHandleToOSThread( MSR.back(), (uint32)i );
            }
        }
    }
    catch (...)
    {
        // failed
        MSR.clear();

        std::cerr << "Can not access CPUs Model Specific Registers (MSRs).\n";
#ifdef _MSC_VER
        std::cerr << "You must have signed msr.sys driver in your current directory and have administrator rights to run this program.\n";
#elif defined(__linux__)
        std::cerr << "Try to execute 'modprobe msr' as root user and then\n";
        std::cerr << "you also must have read and write permissions for /dev/cpu/*/msr devices (/dev/msr* for Android). The 'chown' command can help.\n";
#elif defined(__FreeBSD__) || defined(__DragonFly__)
        std::cerr << "Ensure cpuctl module is loaded and that you have read and write\n";
        std::cerr << "permissions for /dev/cpuctl* devices (the 'chown' command can help).\n";
#endif
        return false;
    }
#endif
    return true;
}

bool PCM::detectNominalFrequency()
{
    if (MSR.size())
    {
        uint64 freq = 0;
        MSR[socketRefCore[0]]->read(PLATFORM_INFO_ADDR, &freq);
        const uint64 bus_freq = (
                  cpu_model == SANDY_BRIDGE
               || cpu_model == JAKETOWN
               || cpu_model == IVYTOWN
               || cpu_model == HASWELLX
               || cpu_model == BDX_DE
               || cpu_model == BDX
               || cpu_model == IVY_BRIDGE
               || cpu_model == HASWELL
               || cpu_model == BROADWELL
               || cpu_model == AVOTON
               || cpu_model == APOLLO_LAKE
               || cpu_model == DENVERTON
               || useSKLPath()
               || cpu_model == SNOWRIDGE
               || cpu_model == KNL
               || cpu_model == SKX
               || cpu_model == ICX
               ) ? (100000000ULL) : (133333333ULL);

        nominal_frequency = ((freq >> 8) & 255) * bus_freq;

        if(!nominal_frequency)
            nominal_frequency = get_frequency_from_cpuid();

        if(!nominal_frequency)
        {
            std::cerr << "Error: Can not detect core frequency.\n";
            destroyMSR();
            return false;
        }

#ifndef PCM_SILENT
        std::cerr << "Nominal core frequency: " << nominal_frequency << " Hz\n";
#endif
    }

    return true;
}

void PCM::initEnergyMonitoring()
{
    if(packageEnergyMetricsAvailable() && MSR.size())
    {
        uint64 rapl_power_unit = 0;
        MSR[socketRefCore[0]]->read(MSR_RAPL_POWER_UNIT,&rapl_power_unit);
        uint64 energy_status_unit = extract_bits(rapl_power_unit,8,12);
        if (cpu_model == PCM::CHERRYTRAIL || cpu_model == PCM::BAYTRAIL)
            joulesPerEnergyUnit = double(1ULL << energy_status_unit)/1000000.; // (2)^energy_status_unit microJoules
        else
            joulesPerEnergyUnit = 1./double(1ULL<<energy_status_unit); // (1/2)^energy_status_unit
        //std::cout << "MSR_RAPL_POWER_UNIT: " << energy_status_unit << "; Joules/unit " << joulesPerEnergyUnit << "\n";
        uint64 power_unit = extract_bits(rapl_power_unit,0,3);
        double wattsPerPowerUnit = 1./double(1ULL<<power_unit);

        uint64 package_power_info = 0;
        MSR[socketRefCore[0]]->read(MSR_PKG_POWER_INFO,&package_power_info);
        pkgThermalSpecPower = (int32) (double(extract_bits(package_power_info, 0, 14))*wattsPerPowerUnit);
        pkgMinimumPower = (int32) (double(extract_bits(package_power_info, 16, 30))*wattsPerPowerUnit);
        pkgMaximumPower = (int32) (double(extract_bits(package_power_info, 32, 46))*wattsPerPowerUnit);

#ifndef PCM_SILENT
        std::cerr << "Package thermal spec power: " << pkgThermalSpecPower << " Watt; ";
        std::cerr << "Package minimum power: " << pkgMinimumPower << " Watt; ";
        std::cerr << "Package maximum power: " << pkgMaximumPower << " Watt;\n";
#endif

        int i = 0;

        if(energy_status.empty())
            for (i = 0; i < (int)num_sockets; ++i)
                energy_status.push_back(
                    std::make_shared<CounterWidthExtender>(
                        new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[i]], MSR_PKG_ENERGY_STATUS), 32, 10000));

        if(dramEnergyMetricsAvailable() && dram_energy_status.empty())
            for (i = 0; i < (int)num_sockets; ++i)
                dram_energy_status.push_back(
                    std::make_shared<CounterWidthExtender>(
                    new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[i]], MSR_DRAM_ENERGY_STATUS), 32, 10000));
    }
}

static const uint32 UBOX0_DEV_IDS[] = {
    0x3451
};

std::vector<std::pair<uint32, uint32> > socket2UBOX0bus;

void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize);

void initSocket2Ubox0Bus()
{
    initSocket2Bus(socket2UBOX0bus, SERVER_UBOX0_REGISTER_DEV_ADDR, SERVER_UBOX0_REGISTER_FUNC_ADDR,
        UBOX0_DEV_IDS, (uint32)sizeof(UBOX0_DEV_IDS) / sizeof(UBOX0_DEV_IDS[0]));
}

void PCM::initUncoreObjects()
{
    if (hasPCICFGUncore() && MSR.size())
    {
        int i = 0;
        bool failed = false;
        try
        {
            for (i = 0; i < (int)num_sockets; ++i)
            {
                server_pcicfg_uncore.push_back(std::make_shared<ServerPCICFGUncore>(i, this));
            }
        }
        catch (std::runtime_error & e)
        {
            std::cerr << e.what() << "\n";
            failed = true;
        }
        catch (...)
        {
            failed = true;
        }
        if (failed)
        {
            server_pcicfg_uncore.clear();
            std::cerr << "Can not access server uncore PCI configuration space. Access to uncore counters (memory and QPI bandwidth) is disabled.\n";
#ifdef _MSC_VER
            std::cerr << "You must have signed msr.sys driver in your current directory and have administrator rights to run this program.\n";
#else
            //std::cerr << "you must have read and write permissions for /proc/bus/pci/7f/10.* and /proc/bus/pci/ff/10.* devices (the 'chown' command can help).\n";
            //std::cerr << "you must have read and write permissions for /dev/mem device (the 'chown' command can help).\n";
            //std::cerr << "you must have read permission for /sys/firmware/acpi/tables/MCFG device (the 'chmod' command can help).\n";
            std::cerr << "You must be root to access server uncore counters in PCM.\n";
#endif
        }
    } else if(hasClientMCCounters() && MSR.size())
    {
       // initialize memory bandwidth counting
       try
       {
           switch (cpu_model)
           {
           case TGL:
               clientBW = std::make_shared<TGLClientBW>();
               break;
           default:
               clientBW = std::make_shared<ClientBW>();
           }
           clientImcReads = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientImcReadsCounter(clientBW), 32, 10000);
           clientImcWrites = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientImcWritesCounter(clientBW), 32, 10000);
           clientIoRequests = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientIoRequestsCounter(clientBW), 32, 10000);

       } catch(...)
       {
           std::cerr << "Can not read memory controller counter information from PCI configuration space. Access to memory bandwidth counters is not possible.\n";
           #ifdef _MSC_VER
           // TODO: add message here
           #endif
           #ifdef __linux__
           std::cerr << "You must be root to access these SandyBridge/IvyBridge/Haswell counters in PCM. \n";
           #endif
       }
    }
    if (cpu_model == ICX || cpu_model == SNOWRIDGE)
    {
        initSocket2Ubox0Bus();
        for (size_t s = 0; s < (size_t)num_sockets && s < socket2UBOX0bus.size() && s < server_pcicfg_uncore.size(); ++s)
        {
            serverBW.push_back(std::make_shared<ServerBW>(server_pcicfg_uncore[s]->getNumMC(), socket2UBOX0bus[s].first, socket2UBOX0bus[s].second));
            // std::cout << " Added serverBW object server_pcicfg_uncore[s]->getNumMC() = " << server_pcicfg_uncore[s]->getNumMC() << std::endl;
        }
        if (socket2UBOX0bus.size() != (size_t)num_sockets)
        {
            std::cerr << "PCM warning: found " << socket2UBOX0bus.size() << " uboxes. Expected " << num_sockets << std::endl;
        }
    }

    if (useLinuxPerfForUncore())
    {
        initUncorePMUsPerf();
    }
    else
    {
        initUncorePMUsDirect();
    }
}

void PCM::initUncorePMUsDirect()
{
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        auto & handle = MSR[socketRefCore[s]];
        // unfreeze uncore PMUs
        switch (cpu_model)
        {
        case SKX:
            handle->write(MSR_UNCORE_PMON_GLOBAL_CTL, 1ULL << 61ULL);
            break;
        case HASWELLX:
        case BDX:
            handle->write(MSR_UNCORE_PMON_GLOBAL_CTL, 1ULL << 29ULL);
            break;
        case IVYTOWN:
            handle->write(IVT_MSR_UNCORE_PMON_GLOBAL_CTL, 1ULL << 29ULL);
            break;
        }
        if (IVYTOWN == cpu_model || JAKETOWN == cpu_model)
        {
            uboxPMUs.push_back(
                UncorePMU(
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTL1_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTR1_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UCLK_FIXED_CTR_ADDR)
                )
            );
        }
        else
        {
            uboxPMUs.push_back(
                UncorePMU(
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTL1_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTR1_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, UCLK_FIXED_CTR_ADDR)
                )
            );
        }
        switch (cpu_model)
        {
        case IVYTOWN:
        case JAKETOWN:
            pcuPMUs.push_back(
                UncorePMU(
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_BOX_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTL1_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTL2_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTL3_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTR1_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTR2_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_CTR3_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, JKTIVT_PCU_MSR_PMON_BOX_FILTER_ADDR)
                )
            );
            break;
        case BDX_DE:
        case BDX:
        case KNL:
        case HASWELLX:
        case SKX:
        case ICX:
            pcuPMUs.push_back(
                UncorePMU(
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_BOX_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTL1_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTL2_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTL3_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTR1_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTR2_ADDR),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_CTR3_ADDR),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, HSX_PCU_MSR_PMON_BOX_FILTER_ADDR)
                )
            );
            break;
        }
    }
    // init IIO addresses
    if (getCPUModel() == PCM::SKX)
    {
        iioPMUs.resize(num_sockets);
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < SKX_IIO_STACK_COUNT; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_UNIT_CTL + SKX_IIO_PM_REG_STEP * unit),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTL0 + SKX_IIO_PM_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTL0 + SKX_IIO_PM_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTL0 + SKX_IIO_PM_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTL0 + SKX_IIO_PM_REG_STEP * unit + 3),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTR0 + SKX_IIO_PM_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTR0 + SKX_IIO_PM_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTR0 + SKX_IIO_PM_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SKX_IIO_CBDMA_CTR0 + SKX_IIO_PM_REG_STEP * unit + 3)
                );
            }
        }
    }
    else if (getCPUModel() == PCM::ICX)
    {
        iioPMUs.resize(num_sockets);
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < ICX_IIO_STACK_COUNT; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit]),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTL_REG_OFFSET + 0),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTL_REG_OFFSET + 1),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTL_REG_OFFSET + 2),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTL_REG_OFFSET + 3),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTR_REG_OFFSET + 0),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTR_REG_OFFSET + 1),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTR_REG_OFFSET + 2),
                    std::make_shared<MSRRegister>(handle, ICX_IIO_UNIT_CTL[unit] + ICX_IIO_CTR_REG_OFFSET + 3)
                );
            }
        }
    }
    else if (getCPUModel() == PCM::SNOWRIDGE)
    {
        iioPMUs.resize(num_sockets);
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < SNR_IIO_STACK_COUNT; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_UNIT_CTL + SNR_IIO_PM_REG_STEP * unit),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTL0 + SNR_IIO_PM_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTL0 + SNR_IIO_PM_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTL0 + SNR_IIO_PM_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTL0 + SNR_IIO_PM_REG_STEP * unit + 3),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTR0 + SNR_IIO_PM_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTR0 + SNR_IIO_PM_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTR0 + SNR_IIO_PM_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SNR_IIO_CBDMA_CTR0 + SNR_IIO_PM_REG_STEP * unit + 3)
                );
            }
        }
    }

    if (hasPCICFGUncore() && MSR.size())
    {
        cboPMUs.resize(num_sockets);
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (uint32 cbo = 0; cbo < getMaxNumOfCBoxes(); ++cbo)
            {
                const auto filter1MSR = CX_MSR_PMON_BOX_FILTER1(cbo);
                std::shared_ptr<HWRegister> filter1MSRHandle = filter1MSR ? std::make_shared<MSRRegister>(handle, filter1MSR) : std::shared_ptr<HWRegister>();
                cboPMUs[s].push_back(
                    UncorePMU(
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_BOX_CTL(cbo)),
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_CTLY(cbo, 0)),
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_CTLY(cbo, 1)),
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_CTLY(cbo, 2)),
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_CTLY(cbo, 3)),
                        std::make_shared<CounterWidthExtenderRegister>(
                            std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[s]], CX_MSR_PMON_CTRY(cbo, 0)), 48, 5555)),
                        std::make_shared<CounterWidthExtenderRegister>(
                            std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[s]], CX_MSR_PMON_CTRY(cbo, 1)), 48, 5555)),
                        std::make_shared<CounterWidthExtenderRegister>(
                            std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[s]], CX_MSR_PMON_CTRY(cbo, 2)), 48, 5555)),
                        std::make_shared<CounterWidthExtenderRegister>(
                            std::make_shared<CounterWidthExtender>(new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[s]], CX_MSR_PMON_CTRY(cbo, 3)), 48, 5555)),
                        std::shared_ptr<MSRRegister>(),
                        std::shared_ptr<MSRRegister>(),
                        std::make_shared<MSRRegister>(handle, CX_MSR_PMON_BOX_FILTER(cbo)),
                        filter1MSRHandle
                    )
                );
            }
        }
    }
}

#ifdef PCM_USE_PERF
std::vector<int> enumeratePerfPMUs(const std::string & type, int max_id);
void populatePerfPMUs(unsigned socket_, const std::vector<int> & ids, std::vector<UncorePMU> & pmus, bool fixed, bool filter0 = false, bool filter1 = false);
#endif

void PCM::initUncorePMUsPerf()
{
#ifdef PCM_USE_PERF
    iioPMUs.resize(num_sockets);
    cboPMUs.resize(num_sockets);
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        populatePerfPMUs(s, enumeratePerfPMUs("pcu", 100), pcuPMUs, false, true);
        populatePerfPMUs(s, enumeratePerfPMUs("ubox", 100), uboxPMUs, true);
        populatePerfPMUs(s, enumeratePerfPMUs("cbox", 100), cboPMUs[s], false, true, true);
        populatePerfPMUs(s, enumeratePerfPMUs("cha", 200), cboPMUs[s], false, true, true);
        std::vector<UncorePMU> iioPMUVector;
        populatePerfPMUs(s, enumeratePerfPMUs("iio", 100), iioPMUVector, false);
        for (size_t i = 0; i < iioPMUVector.size(); ++i)
        {
            iioPMUs[s][i] = iioPMUVector[i];
        }
    }
#endif
}

#ifdef __linux__

#define PCM_NMI_WATCHDOG_PATH "/proc/sys/kernel/nmi_watchdog"

bool isNMIWatchdogEnabled()
{
    const auto watchdog = readSysFS(PCM_NMI_WATCHDOG_PATH);
    if (watchdog.length() == 0)
    {
        return false;
    }

    return (std::atoi(watchdog.c_str()) == 1);
}

void disableNMIWatchdog()
{
    std::cerr << "Disabling NMI watchdog since it consumes one hw-PMU counter.\n";
    writeSysFS(PCM_NMI_WATCHDOG_PATH, "0");
}

void enableNMIWatchdog()
{
    std::cerr << " Re-enabling NMI watchdog.\n";
    writeSysFS(PCM_NMI_WATCHDOG_PATH, "1");
}
#endif

class CoreTaskQueue
{
    std::queue<std::packaged_task<void()> > wQueue;
    std::mutex m;
    std::condition_variable condVar;
    std::thread worker;
    CoreTaskQueue() = delete;
    CoreTaskQueue(CoreTaskQueue &) = delete;
public:
    CoreTaskQueue(int32 core) :
        worker([=]() {
            TemporalThreadAffinity tempThreadAffinity(core, false);
            std::unique_lock<std::mutex> lock(m);
            while (1) {
                while (wQueue.empty()) {
                    condVar.wait(lock);
                }
                while (!wQueue.empty()) {
                    wQueue.front()();
                    wQueue.pop();
                }
            }
        })
    {}
    void push(std::packaged_task<void()> & task)
    {
        std::unique_lock<std::mutex> lock(m);
        wQueue.push(std::move(task));
        condVar.notify_one();
    }
};

PCM::PCM() :
    cpu_family(-1),
    cpu_model(-1),
    cpu_stepping(-1),
    cpu_microcode_level(-1),
    max_cpuid(-1),
    threads_per_core(0),
    num_cores(0),
    num_sockets(0),
    num_phys_cores_per_socket(0),
    num_online_cores(0),
    num_online_sockets(0),
    core_gen_counter_num_max(0),
    core_gen_counter_num_used(0), // 0 means no core gen counters used
    core_gen_counter_width(0),
    core_fixed_counter_num_max(0),
    core_fixed_counter_num_used(0),
    core_fixed_counter_width(0),
    uncore_gen_counter_num_max(8),
    uncore_gen_counter_num_used(0),
    uncore_gen_counter_width(48),
    uncore_fixed_counter_num_max(1),
    uncore_fixed_counter_num_used(0),
    uncore_fixed_counter_width(48),
    perfmon_version(0),
    perfmon_config_anythread(1),
    nominal_frequency(0),
    max_qpi_speed(0),
    L3ScalingFactor(0),
    pkgThermalSpecPower(-1),
    pkgMinimumPower(-1),
    pkgMaximumPower(-1),
    systemTopology(new SystemRoot(this)),
    allow_multiple_instances(false),
    programmed_pmu(false),
    joulesPerEnergyUnit(0),
#ifdef __linux__
    resctrl(*this),
#endif
    useResctrl(false),
    disable_JKT_workaround(false),
    blocked(false),
    coreCStateMsr(NULL),
    pkgCStateMsr(NULL),
    L2CacheHitRatioAvailable(false),
    L3CacheHitRatioAvailable(false),
    L3CacheMissesAvailable(false),
    L2CacheMissesAvailable(false),
    L2CacheHitsAvailable(false),
    L3CacheHitsNoSnoopAvailable(false),
    L3CacheHitsSnoopAvailable(false),
    L3CacheHitsAvailable(false),
    forceRTMAbortMode(false),
    mode(INVALID_MODE),
    numInstancesSemaphore(NULL),
    canUsePerf(false),
    outfile(NULL),
    backup_ofile(NULL),
    run_state(1),
    needToRestoreNMIWatchdog(false)
{
#ifdef _MSC_VER
    // WARNING: This driver code (msr.sys) is only for testing purposes, not for production use
    Driver drv(Driver::msrLocalPath());
    // drv.stop();     // restart driver (usually not needed)
    if (!drv.start())
    {
        std::wcerr << "Cannot access CPU counters\n";
        std::wcerr << "You must have a signed  driver at " << drv.driverPath() << " and have administrator rights to run this program\n";
        return;
    }
#endif

    if(!detectModel()) return;

    if(!checkModel()) return;

    initCStateSupportTables();

    if(!discoverSystemTopology()) return;

    if(!initMSR()) return;

    readCoreCounterConfig(true);

#ifndef PCM_SILENT
    printSystemTopology();
#endif

    if(!detectNominalFrequency()) return;

    showSpecControlMSRs();

    initEnergyMonitoring();

    initUncoreObjects();

    initRDT();

    readCPUMicrocodeLevel();

#ifdef PCM_USE_PERF
    canUsePerf = true;
    std::vector<int> dummy(PERF_MAX_COUNTERS, -1);
    perfEventHandle.resize(num_cores, dummy);
#endif

    for (int32 i = 0; i < num_cores; ++i)
    {
        coreTaskQueues.push_back(std::make_shared<CoreTaskQueue>(i));
    }
}

void PCM::enableJKTWorkaround(bool enable)
{
    if(disable_JKT_workaround) return;
    std::cerr << "Using PCM on your system might have a performance impact as per http://software.intel.com/en-us/articles/performance-impact-when-sampling-certain-llc-events-on-snb-ep-with-vtune\n";
    std::cerr << "You can avoid the performance impact by using the option --noJKTWA, however the cache metrics might be wrong then.\n";
    if(MSR.size())
    {
        for(int32 i = 0; i < num_cores; ++i)
        {
            uint64 val64 = 0;
            MSR[i]->read(0x39C, &val64);
            if(enable)
                val64 |= 1ULL;
            else
                val64 &= (~1ULL);
            MSR[i]->write(0x39C, val64);
        }
    }
    for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
    {
            if(server_pcicfg_uncore[i].get()) server_pcicfg_uncore[i]->enableJKTWorkaround(enable);
    }
}

void PCM::showSpecControlMSRs()
{
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(7, 0, cpuinfo);

    if (MSR.size())
    {
        if ((cpuinfo.reg.edx & (1 << 26)) || (cpuinfo.reg.edx & (1 << 27)))
        {
            uint64 val64 = 0;
            MSR[0]->read(MSR_IA32_SPEC_CTRL, &val64);
            std::cerr << "IBRS enabled in the kernel   : " << ((val64 & 1) ? "yes" : "no") << "\n";
            std::cerr << "STIBP enabled in the kernel  : " << ((val64 & 2) ? "yes" : "no") << "\n";
        }
        if (cpuinfo.reg.edx & (1 << 29))
        {
            uint64 val64 = 0;
            MSR[0]->read(MSR_IA32_ARCH_CAPABILITIES, &val64);
            std::cerr << "The processor is not susceptible to Rogue Data Cache Load: " << ((val64 & 1) ? "yes" : "no") << "\n";
            std::cerr << "The processor supports enhanced IBRS                     : " << ((val64 & 2) ? "yes" : "no") << "\n";
        }
    }
}

bool PCM::isCoreOnline(int32 os_core_id) const
{
    return (topology[os_core_id].os_id != -1) && (topology[os_core_id].core_id != -1) && (topology[os_core_id].socket != -1);
}

bool PCM::isSocketOnline(int32 socket_id) const
{
    return socketRefCore[socket_id] != -1;
}

bool PCM::isCPUModelSupported(const int model_)
{
    return (   model_ == NEHALEM_EP
            || model_ == NEHALEM_EX
            || model_ == WESTMERE_EP
            || model_ == WESTMERE_EX
            || isAtom(model_)
            || model_ == SNOWRIDGE
            || model_ == CLARKDALE
            || model_ == SANDY_BRIDGE
            || model_ == JAKETOWN
            || model_ == IVY_BRIDGE
            || model_ == HASWELL
            || model_ == IVYTOWN
            || model_ == HASWELLX
            || model_ == BDX_DE
            || model_ == BDX
            || model_ == BROADWELL
            || model_ == KNL
            || model_ == SKL
            || model_ == SKL_UY
            || model_ == KBL
            || model_ == KBL_1
            || model_ == CML
            || model_ == ICL
            || model_ == RKL
            || model_ == TGL
            || model_ == SKX
            || model_ == ICX
           );
}

bool PCM::checkModel()
{
    if (cpu_model == NEHALEM) cpu_model = NEHALEM_EP;
    if (cpu_model == ATOM_2) cpu_model = ATOM;
    if (cpu_model == HASWELL_ULT || cpu_model == HASWELL_2) cpu_model = HASWELL;
    if (cpu_model == BROADWELL_XEON_E3) cpu_model = BROADWELL;
    if (cpu_model == ICX_D) cpu_model = ICX;
    if (cpu_model == CML_1) cpu_model = CML;
    if (cpu_model == ICL_1) cpu_model = ICL;
    if (cpu_model == TGL_1) cpu_model = TGL;

    if(!isCPUModelSupported((int)cpu_model))
    {
        std::cerr << getUnsupportedMessage() << " CPU model number: " << cpu_model << " Brand: \"" << getCPUBrandString().c_str() << "\"\n";
/* FOR TESTING PURPOSES ONLY */
#ifdef PCM_TEST_FALLBACK_TO_ATOM
        std::cerr << "Fall back to ATOM functionality.\n";
        cpu_model = ATOM;
        return true;
#endif
        return false;
    }
    return true;
}

void PCM::destroyMSR()
{
    MSR.clear();
}

PCM::~PCM()
{
    InstanceLock lock(allow_multiple_instances);
    if (instance)
    {
        destroyMSR();
        instance = NULL;
        delete systemTopology;
    }
}

bool PCM::good()
{
    return !MSR.empty();
}

#ifdef PCM_USE_PERF
perf_event_attr PCM_init_perf_event_attr(bool group = true)
{
    perf_event_attr e;
    bzero(&e,sizeof(perf_event_attr));
    e.type = -1; // must be set up later
    e.size = sizeof(e);
    e.config = -1; // must be set up later
    e.sample_period = 0;
    e.sample_type = 0;
    e.read_format = group ? PERF_FORMAT_GROUP : 0; /* PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING |
                          PERF_FORMAT_ID | PERF_FORMAT_GROUP ; */
    e.disabled = 0;
    e.inherit = 0;
    e.pinned = 1;
    e.exclusive = 0;
    e.exclude_user = 0;
    e.exclude_kernel = 0;
    e.exclude_hv = 0;
    e.exclude_idle = 0;
    e.mmap = 0;
    e.comm = 0;
    e.freq = 0;
    e.inherit_stat = 0;
    e.enable_on_exec = 0;
    e.task = 0;
    e.watermark = 0;
    e.wakeup_events = 0;
    return e;
}
#endif

PCM::ErrorCode PCM::program(const PCM::ProgramMode mode_, const void * parameter_)
{
#ifdef __linux__
    if (isNMIWatchdogEnabled())
    {
        disableNMIWatchdog();
        needToRestoreNMIWatchdog = true;
    }
#endif

    if(allow_multiple_instances && (EXT_CUSTOM_CORE_EVENTS == mode_ || CUSTOM_CORE_EVENTS == mode_))
    {
        allow_multiple_instances = false;
        std::cerr << "Warning: multiple PCM instance mode is not allowed with custom events.\n";
    }

    InstanceLock lock(allow_multiple_instances);
    if (MSR.empty()) return PCM::MSRAccessDenied;

    ExtendedCustomCoreEventDescription * pExtDesc = (ExtendedCustomCoreEventDescription *)parameter_;

#ifdef PCM_USE_PERF
    std::cerr << "Trying to use Linux perf events...\n";
    const char * no_perf_env = std::getenv("PCM_NO_PERF");
    if (no_perf_env != NULL && std::string(no_perf_env) == std::string("1"))
    {
        canUsePerf = false;
        std::cerr << "Usage of Linux perf events is disabled through PCM_NO_PERF environment variable. Using direct PMU programming...\n";
    }
/*
    if(num_online_cores < num_cores)
    {
        canUsePerf = false;
        std::cerr << "PCM does not support using Linux perf API on systems with offlined cores. Falling-back to direct PMU programming.\n";
    }
*/
    else if(PERF_COUNT_HW_MAX <= PCM_PERF_COUNT_HW_REF_CPU_CYCLES)
    {
        canUsePerf = false;
        std::cerr << "Can not use Linux perf because your Linux kernel does not support PERF_COUNT_HW_REF_CPU_CYCLES event. Falling-back to direct PMU programming.\n";
    }
    else if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->fixedCfg)
    {
        canUsePerf = false;
        std::cerr << "Can not use Linux perf because non-standard fixed counter configuration requested. Falling-back to direct PMU programming.\n";
    }
    else if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && (pExtDesc->OffcoreResponseMsrValue[0] || pExtDesc->OffcoreResponseMsrValue[1]))
    {
        const std::string offcore_rsp_format = readSysFS("/sys/bus/event_source/devices/cpu/format/offcore_rsp");
        if (offcore_rsp_format != "config1:0-63\n")
        {
            canUsePerf = false;
            std::cerr << "Can not use Linux perf because OffcoreResponse usage is not supported. Falling-back to direct PMU programming.\n";
        }
    }
    if (isHWTMAL1Supported() == true && perfSupportsTopDown() == false)
    {
        canUsePerf = false;
        std::cerr << "Installed Linux kernel perf does not support hardware top-down level-1 counters. Using direct PMU programming instead.\n";
    }
#endif

    if(allow_multiple_instances)
    {
        //std::cerr << "Checking for other instances of PCM...\n";
#ifdef _MSC_VER

        numInstancesSemaphore = CreateSemaphore(NULL, 0, 1 << 20, L"Global\\Number of running Processor Counter Monitor instances");
        if (!numInstancesSemaphore)
        {
            _com_error error(GetLastError());
            std::wcerr << "Error in Windows function 'CreateSemaphore': " << GetLastError() << " ";
            const TCHAR * strError = _com_error(GetLastError()).ErrorMessage();
            if (strError) std::wcerr << strError;
            std::wcerr << "\n";
            return PCM::UnknownError;
        }
        LONG prevValue = 0;
        if (!ReleaseSemaphore(numInstancesSemaphore, 1, &prevValue))
        {
            _com_error error(GetLastError());
            std::wcerr << "Error in Windows function 'ReleaseSemaphore': " << GetLastError() << " ";
            const TCHAR * strError = _com_error(GetLastError()).ErrorMessage();
            if (strError) std::wcerr << strError;
            std::wcerr << "\n";
            return PCM::UnknownError;
        }
        if (prevValue > 0)  // already programmed since another instance exists
        {
            std::cerr << "Number of PCM instances: " << (prevValue + 1) << "\n";
            if (hasPCICFGUncore() && max_qpi_speed==0)
            for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
                if (server_pcicfg_uncore[i].get())
                    max_qpi_speed = (std::max)(server_pcicfg_uncore[i]->computeQPISpeed(socketRefCore[i], cpu_model), max_qpi_speed); // parenthesis to avoid macro expansion on Windows

            reportQPISpeed();
            return PCM::Success;
        }

    #else // if linux, apple, freebsd or dragonflybsd
        numInstancesSemaphore = sem_open(PCM_NUM_INSTANCES_SEMAPHORE_NAME, O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO, 0);
        if (SEM_FAILED == numInstancesSemaphore)
        {
            if (EACCES == errno)
                std::cerr << "PCM Error, do not have permissions to open semaphores in /dev/shm/. Clean up them.\n";
            return PCM::UnknownError;
        }
    #ifndef __APPLE__
        sem_post(numInstancesSemaphore);
        int curValue = 0;
        sem_getvalue(numInstancesSemaphore, &curValue);
    #else //if it is apple
        uint32 curValue = PCM::incrementNumInstances();
        sem_post(numInstancesSemaphore);
    #endif // end ifndef __APPLE__

        if (curValue > 1)  // already programmed since another instance exists
        {
            std::cerr << "Number of PCM instances: " << curValue << "\n";
            if (hasPCICFGUncore() && max_qpi_speed==0)
            for (int i = 0; i < (int)server_pcicfg_uncore.size(); ++i) {
                if(server_pcicfg_uncore[i].get())
                    max_qpi_speed = std::max(server_pcicfg_uncore[i]->computeQPISpeed(socketRefCore[i],cpu_model), max_qpi_speed);
                reportQPISpeed();
            }
            if(!canUsePerf) return PCM::Success;
        }

    #endif // end ifdef _MSC_VER

    #ifdef PCM_USE_PERF
    /*
    numInst>1 &&  canUsePerf==false -> not reachable, already PMU programmed in another PCM instance
    numInst>1 &&  canUsePerf==true  -> perf programmed in different PCM, is not allowed
    numInst<=1 && canUsePerf==false -> we are first, perf cannot be used, *check* if PMU busy
    numInst<=1 && canUsePerf==true -> we are first, perf will be used, *dont check*, this is now perf business
    */
        if(curValue > 1 && (canUsePerf == true))
        {
            std::cerr << "Running several clients using the same counters is not possible with Linux perf. Recompile PCM without Linux Perf support to allow such usage. \n";
            decrementInstanceSemaphore();
            return PCM::UnknownError;
        }

        if((curValue <= 1) && (canUsePerf == false) && PMUinUse())
        {
            decrementInstanceSemaphore();
            return PCM::PMUBusy;
        }
    #else
        if (PMUinUse())
        {
            decrementInstanceSemaphore();
            return PCM::PMUBusy;
        }
    #endif
    }
    else
    {
        if((canUsePerf == false) && PMUinUse())
        {
            return PCM::PMUBusy;
        }
    }

    mode = mode_;

    // copy custom event descriptions
    if (mode == CUSTOM_CORE_EVENTS)
    {
        if (!parameter_)
        {
            std::cerr << "PCM Internal Error: data structure for custom event not initialized\n";
            return PCM::UnknownError;
        }
        CustomCoreEventDescription * pDesc = (CustomCoreEventDescription *)parameter_;
        coreEventDesc[0] = pDesc[0];
        coreEventDesc[1] = pDesc[1];
        if (isAtom() == false && cpu_model != KNL)
        {
            coreEventDesc[2] = pDesc[2];
            core_gen_counter_num_used = 3;
            if (core_gen_counter_num_max > 3) {
                coreEventDesc[3] = pDesc[3];
                core_gen_counter_num_used = 4;
            }
        }
        else
            core_gen_counter_num_used = 2;
    }
    else if (mode != EXT_CUSTOM_CORE_EVENTS)
    {
        if (isAtom() || cpu_model == KNL)
        {
            coreEventDesc[0].event_number = ARCH_LLC_MISS_EVTNR;
            coreEventDesc[0].umask_value = ARCH_LLC_MISS_UMASK;
            coreEventDesc[1].event_number = ARCH_LLC_REFERENCE_EVTNR;
            coreEventDesc[1].umask_value = ARCH_LLC_REFERENCE_UMASK;
            L2CacheHitRatioAvailable = true;
            L2CacheMissesAvailable = true;
            L2CacheHitsAvailable = true;
            core_gen_counter_num_used = 2;
        }
        else
        switch ( cpu_model ) {
            case SNOWRIDGE:
                coreEventDesc[0].event_number = ARCH_LLC_MISS_EVTNR;
                coreEventDesc[0].umask_value = ARCH_LLC_MISS_UMASK;
                coreEventDesc[1].event_number = ARCH_LLC_REFERENCE_EVTNR;
                coreEventDesc[1].umask_value = ARCH_LLC_REFERENCE_UMASK;
                coreEventDesc[2].event_number = SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR;
                coreEventDesc[2].umask_value = SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK;
                coreEventDesc[3].event_number = SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK;
                L2CacheHitRatioAvailable = true;
                L3CacheHitRatioAvailable = true;
                L3CacheMissesAvailable = true;
                L2CacheMissesAvailable = true;
                L2CacheHitsAvailable = true;
                L3CacheHitsSnoopAvailable = true;
                L3CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
                break;
            PCM_SKL_PATH_CASES
            case SKX:
            case ICX:
                assert(useSkylakeEvents());
                coreEventDesc[0].event_number = SKL_MEM_LOAD_RETIRED_L3_MISS_EVTNR;
                coreEventDesc[0].umask_value = SKL_MEM_LOAD_RETIRED_L3_MISS_UMASK;
                coreEventDesc[1].event_number = SKL_MEM_LOAD_RETIRED_L3_HIT_EVTNR;
                coreEventDesc[1].umask_value = SKL_MEM_LOAD_RETIRED_L3_HIT_UMASK;
                coreEventDesc[2].event_number = SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR;
                coreEventDesc[2].umask_value = SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK;
                coreEventDesc[3].event_number = SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK;
                if (core_gen_counter_num_max == 3)
                {
                    L3CacheHitRatioAvailable = true;
                    L3CacheMissesAvailable = true;
                    L2CacheMissesAvailable = true;
                    L3CacheHitsSnoopAvailable = true;
                    L3CacheHitsAvailable = true;
                    core_gen_counter_num_used = 3;
                    break;
                }
                L2CacheHitRatioAvailable = true;
                L3CacheHitRatioAvailable = true;
                L3CacheMissesAvailable = true;
                L2CacheMissesAvailable = true;
                L2CacheHitsAvailable = true;
                L3CacheHitsSnoopAvailable = true;
                L3CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
                break;
            case SANDY_BRIDGE:
            case JAKETOWN:
            case IVYTOWN:
            case IVY_BRIDGE:
            case HASWELL:
            case HASWELLX:
            case BROADWELL:
            case BDX_DE:
            case BDX:
                coreEventDesc[0].event_number = ARCH_LLC_MISS_EVTNR;
                coreEventDesc[0].umask_value = ARCH_LLC_MISS_UMASK;
                coreEventDesc[1].event_number = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_EVTNR;
                coreEventDesc[1].umask_value = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_NONE_UMASK;
                coreEventDesc[2].event_number = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_EVTNR;
                coreEventDesc[2].umask_value = MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_UMASK;
                coreEventDesc[3].event_number = MEM_LOAD_UOPS_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = MEM_LOAD_UOPS_RETIRED_L2_HIT_UMASK;
                if (core_gen_counter_num_max == 3)
                {
                    L3CacheHitRatioAvailable = true;
                    L3CacheMissesAvailable = true;
                    L2CacheMissesAvailable = true;
                    L3CacheHitsNoSnoopAvailable = true;
                    L3CacheHitsSnoopAvailable = true;
                    L3CacheHitsAvailable = true;
                    core_gen_counter_num_used = 3;
                    break;
                }
                L2CacheHitRatioAvailable = true;
                L3CacheHitRatioAvailable = true;
                L3CacheMissesAvailable = true;
                L2CacheMissesAvailable = true;
                L2CacheHitsAvailable = true;
                L3CacheHitsNoSnoopAvailable = true;
                L3CacheHitsSnoopAvailable = true;
                L3CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
                break;
            case NEHALEM_EP:
            case WESTMERE_EP:
            case CLARKDALE:
                coreEventDesc[0].event_number = MEM_LOAD_RETIRED_L3_MISS_EVTNR;
                coreEventDesc[0].umask_value = MEM_LOAD_RETIRED_L3_MISS_UMASK;
                coreEventDesc[1].event_number = MEM_LOAD_RETIRED_L3_UNSHAREDHIT_EVTNR;
                coreEventDesc[1].umask_value = MEM_LOAD_RETIRED_L3_UNSHAREDHIT_UMASK;
                coreEventDesc[2].event_number = MEM_LOAD_RETIRED_L2_HITM_EVTNR;
                coreEventDesc[2].umask_value = MEM_LOAD_RETIRED_L2_HITM_UMASK;
                coreEventDesc[3].event_number = MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = MEM_LOAD_RETIRED_L2_HIT_UMASK;
                L2CacheHitRatioAvailable = true;
                L3CacheHitRatioAvailable = true;
                L3CacheMissesAvailable = true;
                L2CacheMissesAvailable = true;
                L2CacheHitsAvailable = true;
                L3CacheHitsNoSnoopAvailable = true;
                L3CacheHitsSnoopAvailable = true;
                L3CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
                break;
            default:
                assert(!useSkylakeEvents());
                coreEventDesc[0].event_number = ARCH_LLC_MISS_EVTNR;
                coreEventDesc[0].umask_value = ARCH_LLC_MISS_UMASK;
                coreEventDesc[1].event_number = MEM_LOAD_RETIRED_L3_UNSHAREDHIT_EVTNR;
                coreEventDesc[1].umask_value = MEM_LOAD_RETIRED_L3_UNSHAREDHIT_UMASK;
                coreEventDesc[2].event_number = MEM_LOAD_RETIRED_L2_HITM_EVTNR;
                coreEventDesc[2].umask_value = MEM_LOAD_RETIRED_L2_HITM_UMASK;
                coreEventDesc[3].event_number = MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = MEM_LOAD_RETIRED_L2_HIT_UMASK;
                L2CacheHitRatioAvailable = true;
                L3CacheHitRatioAvailable = true;
                L3CacheMissesAvailable = true;
                L2CacheMissesAvailable = true;
                L2CacheHitsAvailable = true;
                L3CacheHitsNoSnoopAvailable = true;
                L3CacheHitsSnoopAvailable = true;
                L3CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
        }
    }

    core_fixed_counter_num_used = 3;

    if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->gpCounterCfg)
    {
        core_gen_counter_num_used = pExtDesc->nGPCounters;
    }

    if(cpu_model == JAKETOWN)
    {
        bool enableWA = false;
        for(uint32 i = 0; i< core_gen_counter_num_used; ++i)
        {
            if(coreEventDesc[i].event_number == MEM_LOAD_UOPS_LLC_HIT_RETIRED_XSNP_EVTNR)
                enableWA = true;
        }
        enableJKTWorkaround(enableWA); // this has a performance penalty on memory access
    }

    if (core_gen_counter_num_used > core_gen_counter_num_max)
    {
        std::cerr << "PCM ERROR: Trying to program " << core_gen_counter_num_used << " general purpose counters with only "
            << core_gen_counter_num_max << " available\n";
        return PCM::UnknownError;
    }
    if (core_fixed_counter_num_used > core_fixed_counter_num_max)
    {
        std::cerr << "PCM ERROR: Trying to program " << core_fixed_counter_num_used << " fixed counters with only "
            << core_fixed_counter_num_max << " available\n";
        return PCM::UnknownError;
    }

    programmed_pmu = true;

    lastProgrammedCustomCounters.clear();
    lastProgrammedCustomCounters.resize(num_cores);
    // Version for linux/windows/freebsd/dragonflybsd
    for (int i = 0; i < (int)num_cores; ++i)
    {
        if (isCoreOnline(i) == false) continue;
        TemporalThreadAffinity tempThreadAffinity(i, false); // speedup trick for Linux

        const auto status = programCoreCounters(i, mode_, pExtDesc, lastProgrammedCustomCounters[i]);
        if (status != PCM::Success)
        {
            return status;
        }

        // program uncore counters

        if (cpu_model == NEHALEM_EP || cpu_model == WESTMERE_EP || cpu_model == CLARKDALE)
        {
            programNehalemEPUncore(i);
        }
        else if (hasBecktonUncore())
        {
            programBecktonUncore(i);
        }
    }

    if(canUsePerf)
    {
        std::cerr << "Successfully programmed on-core PMU using Linux perf\n";
    }

    if (hasPCICFGUncore())
    {
        std::vector<std::future<uint64>> qpi_speeds;
        for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
        {
            server_pcicfg_uncore[i]->program();
            qpi_speeds.push_back(std::async(std::launch::async,
                &ServerPCICFGUncore::computeQPISpeed, server_pcicfg_uncore[i].get(), socketRefCore[i], cpu_model));
        }
        for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
        {
            max_qpi_speed = (std::max)(qpi_speeds[i].get(), max_qpi_speed);
        }

	programCbo();
    }

    reportQPISpeed();

    return PCM::Success;
}

PCM::ErrorCode PCM::programCoreCounters(const int i /* core */,
    const PCM::ProgramMode mode_,
    const ExtendedCustomCoreEventDescription * pExtDesc,
    std::vector<EventSelectRegister> & result)
{
    // program core counters

    result.clear();
    FixedEventControlRegister ctrl_reg;
#ifdef PCM_USE_PERF
    int leader_counter = -1;
    perf_event_attr e = PCM_init_perf_event_attr();
    auto programPerfEvent = [this, &e, &leader_counter, &i](const int eventPos, const std::string & eventName) -> bool
    {
        // if (i == 0) std::cerr << "DEBUG: programming event "<< std::hex << e.config << std::dec << "\n";
        if ((perfEventHandle[i][eventPos] = syscall(SYS_perf_event_open, &e, -1,
            i /* core id */, leader_counter /* group leader */, 0)) <= 0)
        {
            std::cerr << "Linux Perf: Error when programming " << eventName << ", error: " << strerror(errno) << "\n";
            if (24 == errno)
            {
                std::cerr << "try executing 'ulimit -n 10000' to increase the limit on the number of open files.\n";
            }
            else
            {
                std::cerr << "try running with environment variable PCM_NO_PERF=1\n";
            }
            decrementInstanceSemaphore();
            return false;
        }
        return true;
    };
    if (canUsePerf)
    {
        e.type = PERF_TYPE_HARDWARE;
        e.config = PERF_COUNT_HW_INSTRUCTIONS;
        if (programPerfEvent(PERF_INST_RETIRED_POS, "INST_RETIRED") == false)
        {
            return PCM::UnknownError;
        }
        leader_counter = perfEventHandle[i][PERF_INST_RETIRED_POS];
        e.pinned = 0; // all following counter are not leaders, thus need not be pinned explicitly
        e.config = PERF_COUNT_HW_CPU_CYCLES;
        if (programPerfEvent(PERF_CPU_CLK_UNHALTED_THREAD_POS, "CPU_CLK_UNHALTED_THREAD") == false)
        {
            return PCM::UnknownError;
        }
        e.config = PCM_PERF_COUNT_HW_REF_CPU_CYCLES;
        if (programPerfEvent(PERF_CPU_CLK_UNHALTED_REF_POS, "CPU_CLK_UNHALTED_REF") == false)
        {
            return PCM::UnknownError;
        }
    }
    else
#endif
    {
        // disable counters while programming
        MSR[i]->write(IA32_CR_PERF_GLOBAL_CTRL, 0);
        MSR[i]->read(IA32_CR_FIXED_CTR_CTRL, &ctrl_reg.value);


        if (EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->fixedCfg)
        {
            ctrl_reg = *(pExtDesc->fixedCfg);
        }
        else
        {
	    ctrl_reg.value = 0;

	    ctrl_reg.fields.os0 = 1;
            ctrl_reg.fields.usr0 = 1;

            ctrl_reg.fields.os1 = 1;
            ctrl_reg.fields.usr1 = 1;

            ctrl_reg.fields.os2 = 1;
            ctrl_reg.fields.usr2 = 1;

            if (isFixedCounterSupported(3))
	    {
	        ctrl_reg.fields.os3 = 1;
                ctrl_reg.fields.usr3 = 1;
	    }
        }

        MSR[i]->write(INST_RETIRED_ADDR, 0);
        MSR[i]->write(CPU_CLK_UNHALTED_THREAD_ADDR, 0);
        MSR[i]->write(CPU_CLK_UNHALTED_REF_ADDR, 0);
        MSR[i]->write(IA32_CR_FIXED_CTR_CTRL, ctrl_reg.value);
    }

    if (EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc)
    {
        if (pExtDesc->OffcoreResponseMsrValue[0]) // still need to do also if perf API is used due to a bug in perf
            MSR[i]->write(MSR_OFFCORE_RSP0, pExtDesc->OffcoreResponseMsrValue[0]);
        if (pExtDesc->OffcoreResponseMsrValue[1])
            MSR[i]->write(MSR_OFFCORE_RSP1, pExtDesc->OffcoreResponseMsrValue[1]);
    }

    auto setEvent = [] (EventSelectRegister & reg, const uint64 event,  const uint64 umask)
    {
            reg.fields.event_select = event;
            reg.fields.umask = umask;
            reg.fields.usr = 1;
            reg.fields.os = 1;
            reg.fields.edge = 0;
            reg.fields.pin_control = 0;
            reg.fields.apic_int = 0;
            reg.fields.any_thread = 0;
            reg.fields.enable = 1;
            reg.fields.invert = 0;
            reg.fields.cmask = 0;
            reg.fields.in_tx = 0;
            reg.fields.in_txcp = 0;
    };
    EventSelectRegister event_select_reg;
    for (uint32 j = 0; j < core_gen_counter_num_used; ++j)
    {
        if (EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->gpCounterCfg)
        {
            event_select_reg = pExtDesc->gpCounterCfg[j];
            event_select_reg.fields.enable = 1;
        }
        else
        {
            MSR[i]->read(IA32_PERFEVTSEL0_ADDR + j, &event_select_reg.value); // read-only also safe for perf

            setEvent(event_select_reg, coreEventDesc[j].event_number, coreEventDesc[j].umask_value);
        }
        result.push_back(event_select_reg);
#ifdef PCM_USE_PERF
        if (canUsePerf)
        {
            e.type = PERF_TYPE_RAW;
            e.config = (1ULL << 63ULL) + event_select_reg.value;
            if (event_select_reg.fields.event_select == OFFCORE_RESPONSE_0_EVTNR)
                e.config1 = pExtDesc->OffcoreResponseMsrValue[0];
            if (event_select_reg.fields.event_select == OFFCORE_RESPONSE_1_EVTNR)
                e.config1 = pExtDesc->OffcoreResponseMsrValue[1];
            if (programPerfEvent(PERF_GEN_EVENT_0_POS + j, std::string("generic event #") + std::to_string(i)) == false)
            {
                return PCM::UnknownError;
            }
        }
        else
#endif
        {
            MSR[i]->write(IA32_PMC0 + j, 0);
            MSR[i]->write(IA32_PERFEVTSEL0_ADDR + j, event_select_reg.value);
        }
    }

    if (!canUsePerf)
    {
        // start counting, enable all (4 programmable + 3 fixed) counters
        uint64 value = (1ULL << 0) + (1ULL << 1) + (1ULL << 2) + (1ULL << 3) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34);

	if (isFixedCounterSupported(3))
	{
	    value |= (1ULL << 35);
	    MSR[i]->write(TOPDOWN_SLOTS_ADDR, 0);
	}

	if (isHWTMAL1Supported())
	{
	    value |= (1ULL << 48);
	    MSR[i]->write(PERF_METRICS_ADDR, 0);
	}

        if (isAtom() || cpu_model == KNL)       // KNL and Atom have 3 fixed + only 2 programmable counters
            value = (1ULL << 0) + (1ULL << 1) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34);

        for (uint32 j = 0; j < core_gen_counter_num_used; ++j)
        {
            value |= (1ULL << j); // enable all custom counters (if > 4)
        }

        MSR[i]->write(IA32_PERF_GLOBAL_OVF_CTRL, value);
        MSR[i]->write(IA32_CR_PERF_GLOBAL_CTRL, value);
    }
#ifdef PCM_USE_PERF
    else
    {
	    if (isFixedCounterSupported(3) && isHWTMAL1Supported() && perfSupportsTopDown())
        {
            const auto topDownEvents = {  std::make_pair(perfSlotsPath, PERF_TOPDOWN_SLOTS_POS),
                                          std::make_pair(perfBadSpecPath, PERF_TOPDOWN_BADSPEC_POS),
                                          std::make_pair(perfBackEndPath, PERF_TOPDOWN_BACKEND_POS),
                                          std::make_pair(perfFrontEndPath, PERF_TOPDOWN_FRONTEND_POS),
                                          std::make_pair(perfRetiringPath, PERF_TOPDOWN_RETIRING_POS)};
            int readPos = core_fixed_counter_num_used + core_gen_counter_num_used;
            leader_counter = -1;
            for (auto event : topDownEvents)
            {
                uint64 eventSel = 0, umask = 0;
                const auto eventDesc = readSysFS(event.first);
                const auto tokens = split(eventDesc, ',');
                for (auto token : tokens)
                {
                    if (match(token, "event=", &eventSel)) {}
                    else if (match(token, "umask=", &umask)) {}
                    else
                    {
                        std::cerr << "ERROR: unknown token " << token << " in event description \"" << eventDesc << "\" from " << event.first << "\n";
                        decrementInstanceSemaphore();
                        return PCM::UnknownError;
                    }
                }
                EventSelectRegister reg;
                setEvent(reg, eventSel, umask);
                e.type = PERF_TYPE_RAW;
                e.config = (1ULL << 63ULL) + reg.value;
                if (programPerfEvent(event.second, std::string("event ") + event.first + " " + eventDesc) == false)
                {
                    return PCM::UnknownError;
                }
                leader_counter = perfEventHandle[i][PERF_TOPDOWN_SLOTS_POS];
                perfTopDownPos[event.second] = readPos++;
            }
        }
    }
#endif
    return PCM::Success;
}

void PCM::reportQPISpeed() const
{
    if (!max_qpi_speed) return;

    if (hasPCICFGUncore()) {
        for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
        {
            std::cerr << "Socket " << i << "\n";
            if(server_pcicfg_uncore[i].get()) server_pcicfg_uncore[i]->reportQPISpeed();
        }
    } else {
        std::cerr << "Max QPI speed: " << max_qpi_speed / (1e9) << " GBytes/second (" << max_qpi_speed / (1e9*getBytesPerLinkTransfer()) << " GT/second)\n";
    }

}

void PCM::programNehalemEPUncore(int32 core)
{

#define CPUCNT_INIT_THE_REST_OF_EVTCNT \
    unc_event_select_reg.fields.occ_ctr_rst = 1; \
    unc_event_select_reg.fields.edge = 0; \
    unc_event_select_reg.fields.enable_pmi = 0; \
    unc_event_select_reg.fields.enable = 1; \
    unc_event_select_reg.fields.invert = 0; \
    unc_event_select_reg.fields.cmask = 0;

    uncore_gen_counter_num_used = 8;

    UncoreEventSelectRegister unc_event_select_reg;

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL0_ADDR, &unc_event_select_reg.value);

    unc_event_select_reg.fields.event_select = UNC_QMC_WRITES_FULL_ANY_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QMC_WRITES_FULL_ANY_UMASK;

    CPUCNT_INIT_THE_REST_OF_EVTCNT

        MSR[core]->write(MSR_UNCORE_PERFEVTSEL0_ADDR, unc_event_select_reg.value);


    MSR[core]->read(MSR_UNCORE_PERFEVTSEL1_ADDR, &unc_event_select_reg.value);

    unc_event_select_reg.fields.event_select = UNC_QMC_NORMAL_READS_ANY_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QMC_NORMAL_READS_ANY_UMASK;

    CPUCNT_INIT_THE_REST_OF_EVTCNT

        MSR[core]->write(MSR_UNCORE_PERFEVTSEL1_ADDR, unc_event_select_reg.value);


    MSR[core]->read(MSR_UNCORE_PERFEVTSEL2_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_IOH_READS_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL2_ADDR, unc_event_select_reg.value);

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL3_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_IOH_WRITES_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL3_ADDR, unc_event_select_reg.value);

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL4_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_REMOTE_READS_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL4_ADDR, unc_event_select_reg.value);

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL5_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_REMOTE_WRITES_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL5_ADDR, unc_event_select_reg.value);

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL6_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_LOCAL_READS_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL6_ADDR, unc_event_select_reg.value);

    MSR[core]->read(MSR_UNCORE_PERFEVTSEL7_ADDR, &unc_event_select_reg.value);
    unc_event_select_reg.fields.event_select = UNC_QHL_REQUESTS_EVTNR;
    unc_event_select_reg.fields.umask = UNC_QHL_REQUESTS_LOCAL_WRITES_UMASK;
    CPUCNT_INIT_THE_REST_OF_EVTCNT
        MSR[core]->write(MSR_UNCORE_PERFEVTSEL7_ADDR, unc_event_select_reg.value);


#undef CPUCNT_INIT_THE_REST_OF_EVTCNT

    // start uncore counting
    uint64 value = 255 + (1ULL << 32);           // enable all counters
    MSR[core]->write(MSR_UNCORE_PERF_GLOBAL_CTRL_ADDR, value);

    // synchronise counters
    MSR[core]->write(MSR_UNCORE_PMC0, 0);
    MSR[core]->write(MSR_UNCORE_PMC1, 0);
    MSR[core]->write(MSR_UNCORE_PMC2, 0);
    MSR[core]->write(MSR_UNCORE_PMC3, 0);
    MSR[core]->write(MSR_UNCORE_PMC4, 0);
    MSR[core]->write(MSR_UNCORE_PMC5, 0);
    MSR[core]->write(MSR_UNCORE_PMC6, 0);
    MSR[core]->write(MSR_UNCORE_PMC7, 0);
}

void PCM::programBecktonUncore(int32 core)
{
    // program Beckton uncore
    if (core == socketRefCore[0]) computeQPISpeedBeckton((int)core);

    uint64 value = 1 << 29ULL;           // reset all counters
    MSR[core]->write(U_MSR_PMON_GLOBAL_CTL, value);

    BecktonUncorePMUZDPCTLFVCRegister FVCreg;
    FVCreg.value = 0;
    if (cpu_model == NEHALEM_EX)
    {
        FVCreg.fields.bcmd = 0;             // rd_bcmd
        FVCreg.fields.resp = 0;             // ack_resp
        FVCreg.fields.evnt0 = 5;            // bcmd_match
        FVCreg.fields.evnt1 = 6;            // resp_match
        FVCreg.fields.pbox_init_err = 0;
    }
    else
    {
        FVCreg.fields_wsm.bcmd = 0;             // rd_bcmd
        FVCreg.fields_wsm.resp = 0;             // ack_resp
        FVCreg.fields_wsm.evnt0 = 5;            // bcmd_match
        FVCreg.fields_wsm.evnt1 = 6;            // resp_match
        FVCreg.fields_wsm.pbox_init_err = 0;
    }
    MSR[core]->write(MB0_MSR_PMU_ZDP_CTL_FVC, FVCreg.value);
    MSR[core]->write(MB1_MSR_PMU_ZDP_CTL_FVC, FVCreg.value);

    BecktonUncorePMUCNTCTLRegister CNTCTLreg;
    CNTCTLreg.value = 0;
    CNTCTLreg.fields.en = 1;
    CNTCTLreg.fields.pmi_en = 0;
    CNTCTLreg.fields.count_mode = 0;
    CNTCTLreg.fields.storage_mode = 0;
    CNTCTLreg.fields.wrap_mode = 1;
    CNTCTLreg.fields.flag_mode = 0;
    CNTCTLreg.fields.inc_sel = 0x0d;           // FVC_EV0
    MSR[core]->write(MB0_MSR_PMU_CNT_CTL_0, CNTCTLreg.value);
    MSR[core]->write(MB1_MSR_PMU_CNT_CTL_0, CNTCTLreg.value);
    CNTCTLreg.fields.inc_sel = 0x0e;           // FVC_EV1
    MSR[core]->write(MB0_MSR_PMU_CNT_CTL_1, CNTCTLreg.value);
    MSR[core]->write(MB1_MSR_PMU_CNT_CTL_1, CNTCTLreg.value);

    value = 1 + ((0x0C) << 1ULL);              // enable bit + (event select IMT_INSERTS_WR)
    MSR[core]->write(BB0_MSR_PERF_CNT_CTL_1, value);
    MSR[core]->write(BB1_MSR_PERF_CNT_CTL_1, value);

    MSR[core]->write(MB0_MSR_PERF_GLOBAL_CTL, 3); // enable two counters
    MSR[core]->write(MB1_MSR_PERF_GLOBAL_CTL, 3); // enable two counters

    MSR[core]->write(BB0_MSR_PERF_GLOBAL_CTL, 2); // enable second counter
    MSR[core]->write(BB1_MSR_PERF_GLOBAL_CTL, 2); // enable second counter

    // program R-Box to monitor QPI traffic

    // enable counting on all counters on the left side (port 0-3)
    MSR[core]->write(R_MSR_PMON_GLOBAL_CTL_7_0, 255);
    // ... on the right side (port 4-7)
    MSR[core]->write(R_MSR_PMON_GLOBAL_CTL_15_8, 255);

    // pick the event
    value = (1 << 7ULL) + (1 << 6ULL) + (1 << 2ULL); // count any (incoming) data responses
    MSR[core]->write(R_MSR_PORT0_IPERF_CFG0, value);
    MSR[core]->write(R_MSR_PORT1_IPERF_CFG0, value);
    MSR[core]->write(R_MSR_PORT4_IPERF_CFG0, value);
    MSR[core]->write(R_MSR_PORT5_IPERF_CFG0, value);

    // pick the event
    value = (1ULL << 30ULL); // count null idle flits sent
    MSR[core]->write(R_MSR_PORT0_IPERF_CFG1, value);
    MSR[core]->write(R_MSR_PORT1_IPERF_CFG1, value);
    MSR[core]->write(R_MSR_PORT4_IPERF_CFG1, value);
    MSR[core]->write(R_MSR_PORT5_IPERF_CFG1, value);

    // choose counter 0 to monitor R_MSR_PORT0_IPERF_CFG0
    MSR[core]->write(R_MSR_PMON_CTL0, 1 + 2 * (0));
    // choose counter 1 to monitor R_MSR_PORT1_IPERF_CFG0
    MSR[core]->write(R_MSR_PMON_CTL1, 1 + 2 * (6));
    // choose counter 8 to monitor R_MSR_PORT4_IPERF_CFG0
    MSR[core]->write(R_MSR_PMON_CTL8, 1 + 2 * (0));
    // choose counter 9 to monitor R_MSR_PORT5_IPERF_CFG0
    MSR[core]->write(R_MSR_PMON_CTL9, 1 + 2 * (6));

    // choose counter 2 to monitor R_MSR_PORT0_IPERF_CFG1
    MSR[core]->write(R_MSR_PMON_CTL2, 1 + 2 * (1));
    // choose counter 3 to monitor R_MSR_PORT1_IPERF_CFG1
    MSR[core]->write(R_MSR_PMON_CTL3, 1 + 2 * (7));
    // choose counter 10 to monitor R_MSR_PORT4_IPERF_CFG1
    MSR[core]->write(R_MSR_PMON_CTL10, 1 + 2 * (1));
    // choose counter 11 to monitor R_MSR_PORT5_IPERF_CFG1
    MSR[core]->write(R_MSR_PMON_CTL11, 1 + 2 * (7));

    // enable uncore TSC counter (fixed one)
    MSR[core]->write(W_MSR_PMON_GLOBAL_CTL, 1ULL << 31ULL);
    MSR[core]->write(W_MSR_PMON_FIXED_CTR_CTL, 1ULL);

    value = (1 << 28ULL) + 1;                  // enable all counters
    MSR[core]->write(U_MSR_PMON_GLOBAL_CTL, value);
}

uint64 RDTSC();

void PCM::computeNominalFrequency()
{
    const int ref_core = 0;
    uint64 before = 0, after = 0;
    MSR[ref_core]->read(IA32_TIME_STAMP_COUNTER, &before);
    MySleepMs(1000);
    MSR[ref_core]->read(IA32_TIME_STAMP_COUNTER, &after);
    nominal_frequency = after-before;
}
std::string PCM::getCPUBrandString()
{
    char buffer[sizeof(int)*4*3+1];
    PCM_CPUID_INFO * info = (PCM_CPUID_INFO *) buffer;
    pcm_cpuid(0x80000002, *info);
    ++info;
    pcm_cpuid(0x80000003, *info);
    ++info;
    pcm_cpuid(0x80000004, *info);
    buffer[sizeof(int)*4*3] = 0;
    std::string result(buffer);
    while(result[0]==' ') result.erase(0,1);
    std::string::size_type i;
    while((i = result.find("  ")) != std::string::npos) result.replace(i,2," "); // remove duplicate spaces
    return result;
}

std::string PCM::getCPUFamilyModelString()
{
    char buffer[sizeof(int)*4*3+6];
    memset(buffer,0,sizeof(buffer));
#ifdef _MSC_VER
    sprintf_s(buffer,sizeof(buffer),"GenuineIntel-%d-%2X-%X",this->cpu_family,this->cpu_model,this->cpu_stepping);
#else
    snprintf(buffer,sizeof(buffer),"GenuineIntel-%d-%2X-%X",this->cpu_family,this->cpu_model,this->cpu_stepping);
#endif
    std::string result(buffer);
    return result;
}

void PCM::enableForceRTMAbortMode()
{
    // std::cout << "enableForceRTMAbortMode(): forceRTMAbortMode=" << forceRTMAbortMode << "\n";
    if (!forceRTMAbortMode)
    {
        if (isForceRTMAbortModeAvailable() && (core_gen_counter_num_max < 4))
        {
            for (auto m : MSR)
            {
                const auto res = m->write(MSR_TSX_FORCE_ABORT, 1);
                if (res != sizeof(uint64))
                {
                    std::cerr << "Warning: writing 1 to MSR_TSX_FORCE_ABORT failed with error "
                        << res << " on core " << m->getCoreId() << "\n";
                }
            }
            readCoreCounterConfig(true); // re-read core_gen_counter_num_max from CPUID
            std::cerr << "The number of custom counters is now " << core_gen_counter_num_max << "\n";
            if (core_gen_counter_num_max < 4)
            {
                std::cerr << "PCM Warning: the number of custom counters did not increase (" << core_gen_counter_num_max << ")\n";
            }
            forceRTMAbortMode = true;
        }
    }
}

bool PCM::isForceRTMAbortModeEnabled() const
{
    return forceRTMAbortMode;
}

void PCM::disableForceRTMAbortMode()
{
    // std::cout << "disableForceRTMAbortMode(): forceRTMAbortMode=" << forceRTMAbortMode << "\n";
    if (forceRTMAbortMode)
    {
        for (auto m : MSR)
        {
            const auto res = m->write(MSR_TSX_FORCE_ABORT, 0);
            if (res != sizeof(uint64))
            {
                std::cerr << "Warning: writing 0 to MSR_TSX_FORCE_ABORT failed with error "
                    << res << " on core " << m->getCoreId() << "\n";
            }
        }
        readCoreCounterConfig(true); // re-read core_gen_counter_num_max from CPUID
        std::cerr << "The number of custom counters is now " << core_gen_counter_num_max << "\n";
        if (core_gen_counter_num_max != 3)
        {
            std::cerr << "PCM Warning: the number of custom counters is not 3 (" << core_gen_counter_num_max << ")\n";
        }
        forceRTMAbortMode = false;
    }
}

bool PCM::isForceRTMAbortModeAvailable() const
{
    PCM_CPUID_INFO info;
    pcm_cpuid(7, 0, info); // leaf 7, subleaf 0
    return (info.reg.edx & (0x1 << 13)) ? true : false;
}

uint64 get_frequency_from_cpuid() // from Pat Fay (Intel)
{
    double speed=0;
    std::string brand = PCM::getCPUBrandString();
    if (brand.length() > std::string::size_type(0))
    {
        std::string::size_type unitsg = brand.find("GHz");
        if(unitsg != std::string::npos)
        {
            std::string::size_type atsign = brand.rfind(' ', unitsg);
            if(atsign != std::string::npos)
            {
                std::istringstream(brand.substr(atsign)) >> speed;
                speed *= 1000;
            }
        }
        else
        {
            std::string::size_type unitsg = brand.find("MHz");
            if(unitsg != std::string::npos)
            {
                std::string::size_type atsign = brand.rfind(' ', unitsg);
                if(atsign != std::string::npos)
                {
                    std::istringstream(brand.substr(atsign)) >> speed;
                }
            }
        }
    }
    return (uint64)(speed * 1000. * 1000.);
}

std::string PCM::getSupportedUarchCodenames() const
{
    std::ostringstream ostr;
    for(int32 i=0; i < static_cast<int32>(PCM::END_OF_MODEL_LIST) ; ++i)
        if(isCPUModelSupported((int)i))
            ostr << getUArchCodename(i) << ", ";
    return std::string(ostr.str().substr(0, ostr.str().length() - 2));
}

std::string PCM::getUnsupportedMessage() const
{
    std::ostringstream ostr;
    ostr << "Error: unsupported processor. Only Intel(R) processors are supported (Atom(R) and microarchitecture codename " << getSupportedUarchCodenames() << ").";
    return std::string(ostr.str());
}

void PCM::computeQPISpeedBeckton(int core_nr)
{
    uint64 startFlits = 0;
    // reset all counters
    MSR[core_nr]->write(U_MSR_PMON_GLOBAL_CTL, 1 << 29ULL);

    // enable counting on all counters on the left side (port 0-3)
    MSR[core_nr]->write(R_MSR_PMON_GLOBAL_CTL_7_0, 255);
    // disable on the right side (port 4-7)
    MSR[core_nr]->write(R_MSR_PMON_GLOBAL_CTL_15_8, 0);

    // count flits sent
    MSR[core_nr]->write(R_MSR_PORT0_IPERF_CFG0, 1ULL << 31ULL);

    // choose counter 0 to monitor R_MSR_PORT0_IPERF_CFG0
    MSR[core_nr]->write(R_MSR_PMON_CTL0, 1 + 2 * (0));

    // enable all counters
    MSR[core_nr]->write(U_MSR_PMON_GLOBAL_CTL, (1 << 28ULL) + 1);

    MSR[core_nr]->read(R_MSR_PMON_CTR0, &startFlits);

    const uint64 timerGranularity = 1000000ULL; // mks
    uint64 startTSC = getTickCount(timerGranularity, (uint32) core_nr);
    uint64 endTSC;
    do
    {
        endTSC = getTickCount(timerGranularity, (uint32) core_nr);
    } while (endTSC - startTSC < 200000ULL); // spin for 200 ms

    uint64 endFlits = 0;
    MSR[core_nr]->read(R_MSR_PMON_CTR0, &endFlits);
    max_qpi_speed = (endFlits - startFlits) * 8ULL * timerGranularity / (endTSC - startTSC);

}

uint32 PCM::checkCustomCoreProgramming(std::shared_ptr<SafeMsrHandle> msr)
{
    const auto core = msr->getCoreId();
    if (size_t(core) >= lastProgrammedCustomCounters.size() || canUsePerf)
    {
        // checking 'canUsePerf'because corruption detection curently works
        // only if perf is not used, see https://github.com/opcm/pcm/issues/106
        return 0;
    }
    uint32 corruptedCountersMask = 0;

    for (size_t ctr = 0; ctr < lastProgrammedCustomCounters[core].size(); ++ctr)
    {
        EventSelectRegister current;
        if (msr->read(IA32_PERFEVTSEL0_ADDR + ctr, &current.value) != sizeof(current.value))
        {
            std::cerr << "PCM Error: can not read MSR 0x" << std::hex << (IA32_PERFEVTSEL0_ADDR + ctr) <<
                " on core " << std::dec << core << "\n";
            continue;
        }
        if (canUsePerf)
        {
            current.fields.apic_int = 0; // perf sets this bit
        }
        if (current.value != lastProgrammedCustomCounters[core][ctr].value)
        {
            std::cerr << "PCM Error: someone has corrupted custom counter " << ctr << " on core " << core
                << " expected value " << lastProgrammedCustomCounters[core][ctr].value << " value read "
                << current.value << "\n";

            corruptedCountersMask |= (1<<ctr);
        }
    }
    return corruptedCountersMask;
}

bool PCM::PMUinUse()
{
    // follow the "Performance Monitoring Unit Sharing Guide" by P. Irelan and Sh. Kuo
    for (int i = 0; i < (int)num_cores; ++i)
    {
        //std::cout << "Core " << i << " examine registers\n";
        uint64 value = 0;
        if (perfmon_version >= 4)
        {
            MSR[i]->read(MSR_PERF_GLOBAL_INUSE, &value);
            for (uint32 j = 0; j < core_gen_counter_num_max; ++j)
            {
                if (value & (1ULL << j))
                {
                    std::cerr << "WARNING: Custom counter " << j << " is in use. MSR_PERF_GLOBAL_INUSE on core " << i << ": 0x" << std::hex << value << std::dec << "\n";
                    /*
                    Testing MSR_PERF_GLOBAL_INUSE mechanism for a moment. At a later point in time will report BUSY.
                    return true;
                    */
                }
            }
        }

        MSR[i]->read(IA32_CR_PERF_GLOBAL_CTRL, &value);
        // std::cout << "Core " << i << " IA32_CR_PERF_GLOBAL_CTRL is " << std::hex << value << std::dec << "\n";

        EventSelectRegister event_select_reg;
        event_select_reg.value = 0xFFFFFFFFFFFFFFFF;

        for (uint32 j = 0; j < core_gen_counter_num_max; ++j)
        {
            MSR[i]->read(IA32_PERFEVTSEL0_ADDR + j, &event_select_reg.value);

            if (event_select_reg.fields.event_select != 0 || event_select_reg.fields.apic_int != 0)
            {
                std::cerr << "WARNING: Core " << i <<" IA32_PERFEVTSEL" << j << "_ADDR is not zeroed " << event_select_reg.value << "\n";

                if (needToRestoreNMIWatchdog == true && event_select_reg.fields.event_select == 0x3C && event_select_reg.fields.umask == 0)
                {
                    // NMI watchdog did not clear its event, ignore it
                    continue;
                }
                return true;
            }
        }

        FixedEventControlRegister ctrl_reg;
        ctrl_reg.value = 0xffffffffffffffff;

        MSR[i]->read(IA32_CR_FIXED_CTR_CTRL, &ctrl_reg.value);

        // Check if someone has installed pmi handler on counter overflow.
        // If so, that agent might potentially need to change counter value
        // for the "sample after"-mode messing up PCM measurements
        if(ctrl_reg.fields.enable_pmi0 || ctrl_reg.fields.enable_pmi1 || ctrl_reg.fields.enable_pmi2)
        {
            std::cerr << "WARNING: Core " << i << " fixed ctrl:" << ctrl_reg.value << "\n";
            if (needToRestoreNMIWatchdog == false) // if NMI watchdog did not clear the fields, ignore it
            {
                return true;
            }
        }
        // either os=0,usr=0 (not running) or os=1,usr=1 (fits PCM modus) are ok, other combinations are not
        if(ctrl_reg.fields.os0 != ctrl_reg.fields.usr0 ||
           ctrl_reg.fields.os1 != ctrl_reg.fields.usr1 ||
           ctrl_reg.fields.os2 != ctrl_reg.fields.usr2)
        {
           std::cerr << "WARNING: Core " << i << " fixed ctrl:" << ctrl_reg.value << "\n";
           return true;
        }
    }
    //std::cout << std::flush
    return false;
}

const char * PCM::getUArchCodename(const int32 cpu_model_param) const
{
    auto cpu_model_ = cpu_model_param;
    if(cpu_model_ < 0)
        cpu_model_ = this->cpu_model ;

    switch(cpu_model_)
    {
        case CENTERTON:
            return "Centerton";
        case BAYTRAIL:
            return "Baytrail";
        case AVOTON:
            return "Avoton";
        case CHERRYTRAIL:
            return "Cherrytrail";
        case APOLLO_LAKE:
            return "Apollo Lake";
        case DENVERTON:
            return "Denverton";
        case SNOWRIDGE:
            return "Snowridge";
        case NEHALEM_EP:
        case NEHALEM:
            return "Nehalem/Nehalem-EP";
        case ATOM:
            return "Atom(tm)";
        case CLARKDALE:
            return "Westmere/Clarkdale";
        case WESTMERE_EP:
            return "Westmere-EP";
        case NEHALEM_EX:
            return "Nehalem-EX";
        case WESTMERE_EX:
            return "Westmere-EX";
        case SANDY_BRIDGE:
            return "Sandy Bridge";
        case JAKETOWN:
            return "Sandy Bridge-EP/Jaketown";
        case IVYTOWN:
            return "Ivy Bridge-EP/EN/EX/Ivytown";
        case HASWELLX:
            return "Haswell-EP/EN/EX";
        case BDX_DE:
            return "Broadwell-DE";
        case BDX:
            return "Broadwell-EP/EX";
        case KNL:
            return "Knights Landing";
        case IVY_BRIDGE:
            return "Ivy Bridge";
        case HASWELL:
            return "Haswell";
        case BROADWELL:
            return "Broadwell";
        case SKL:
            return "Skylake";
        case SKL_UY:
            return "Skylake U/Y";
        case KBL:
            return "Kabylake";
        case KBL_1:
            return "Kabylake/Whiskey Lake";
        case CML:
            return "Comet Lake";
        case ICL:
            return "Icelake";
        case RKL:
            return "Rocket Lake";
        case TGL:
            return "Tiger Lake";
        case SKX:
            if (cpu_model_param >= 0)
            {
                // query for specified cpu_model_param, stepping not provided
                return "Skylake-SP, Cascade Lake-SP";
            }
            if (isCLX())
            {
                return "Cascade Lake-SP";
            }
            if (isCPX())
            {
                return "Cooper Lake";
            }
            return "Skylake-SP";
        case ICX:
            return "Icelake-SP";
    }
    return "unknown";
}

void PCM::cleanupPMU()
{
#ifdef PCM_USE_PERF
    if(canUsePerf)
    {
      for (int i = 0; i < num_cores; ++i)
        for(int c = 0; c < PERF_MAX_COUNTERS; ++c)
            ::close(perfEventHandle[i][c]);

      return;
    }
#endif

    // follow the "Performance Monitoring Unit Sharing Guide" by P. Irelan and Sh. Kuo
    for (int i = 0; i < (int)num_cores; ++i)
    {
        // disable generic counters and continue free running counting for fixed counters
        MSR[i]->write(IA32_CR_PERF_GLOBAL_CTRL, (1ULL << 32) + (1ULL << 33) + (1ULL << 34));

        for (uint32 j = 0; j < core_gen_counter_num_max; ++j)
        {
            MSR[i]->write(IA32_PERFEVTSEL0_ADDR + j, 0);
        }
    }

    if(cpu_model == JAKETOWN)
        enableJKTWorkaround(false);

#ifndef PCM_SILENT
    std::cerr << " Zeroed PMU registers\n";
#endif
}

void PCM::cleanupUncorePMUs()
{
    for (auto & sPMUs : iioPMUs)
    {
        for (auto & pmu : sPMUs)
        {
            pmu.second.cleanup();
        }
    }
    for (auto & sCBOPMUs : cboPMUs)
    {
        for (auto & pmu : sCBOPMUs)
        {
            pmu.cleanup();
        }
    }
    for (auto & pmu : pcuPMUs)
    {
        pmu.cleanup();
    }
    for (auto & uncore : server_pcicfg_uncore)
    {
        uncore->cleanupPMUs();
    }
#ifndef PCM_SILENT
    std::cerr << " Zeroed uncore PMU registers\n";
#endif
}

void PCM::resetPMU()
{
    for (int i = 0; i < (int)MSR.size(); ++i)
    {
        // disable all counters
        MSR[i]->write(IA32_CR_PERF_GLOBAL_CTRL, 0);

        for (uint32 j = 0; j < core_gen_counter_num_max; ++j)
        {
            MSR[i]->write(IA32_PERFEVTSEL0_ADDR + j, 0);
        }


        FixedEventControlRegister ctrl_reg;
        ctrl_reg.value = 0xffffffffffffffff;

        MSR[i]->read(IA32_CR_FIXED_CTR_CTRL, &ctrl_reg.value);
        if ((ctrl_reg.fields.os0 ||
             ctrl_reg.fields.usr0 ||
             ctrl_reg.fields.enable_pmi0 ||
             ctrl_reg.fields.os1 ||
             ctrl_reg.fields.usr1 ||
             ctrl_reg.fields.enable_pmi1 ||
             ctrl_reg.fields.os2 ||
             ctrl_reg.fields.usr2 ||
             ctrl_reg.fields.enable_pmi2)
            != 0)
            MSR[i]->write(IA32_CR_FIXED_CTR_CTRL, 0);
    }

#ifndef PCM_SILENT
    std::cerr << " Zeroed PMU registers\n";
#endif
}
void PCM::cleanupRDT()
{
    if(!(QOSMetricAvailable() && L3QOSMetricAvailable())) {
        return;
    }
#ifdef __linux__
    if (useResctrl)
    {
        resctrl.cleanup();
        return;
    }
#endif

    for(int32 core = 0; core < num_cores; core ++ )
    {
                if(!isCoreOnline(core)) continue;
        uint64 msr_pqr_assoc = 0 ;
        uint64 msr_qm_evtsel = 0;
        int32 rmid = 0;
        int32 event = 0;

        //Read 0xC8F MSR for each core
        MSR[core]->read(IA32_PQR_ASSOC, &msr_pqr_assoc);
        msr_pqr_assoc &= 0xffffffff00000000ULL;

        //Write 0xC8F MSR with RMID 0
        MSR[core]->write(IA32_PQR_ASSOC,msr_pqr_assoc);

        msr_qm_evtsel = rmid & ((1ULL<<10)-1ULL) ;
        msr_qm_evtsel <<= 32 ;
        msr_qm_evtsel |= event & ((1ULL<<8)-1ULL);

        //Write Event Id as 0 and RMID 0 to the MSR for each core
        MSR[core]->write(IA32_QM_EVTSEL,msr_qm_evtsel);

    }


    std::cerr << " Freeing up all RMIDs\n";
}

void PCM::setOutput(const std::string filename)
{
     outfile = new std::ofstream(filename.c_str());
     backup_ofile = std::cout.rdbuf();
     std::cout.rdbuf(outfile->rdbuf());
}

void PCM::restoreOutput()
{
    // restore cout back to what it was originally
    if(backup_ofile)
        std::cout.rdbuf(backup_ofile);

// close output file
    if(outfile)
        outfile->close();
}

void PCM::cleanup()
{
    InstanceLock lock(allow_multiple_instances);

    if (MSR.empty()) return;

    std::cerr << "Cleaning up\n";

    if (decrementInstanceSemaphore())
        cleanupPMU();

    disableForceRTMAbortMode();

    cleanupUncorePMUs();
    cleanupRDT();
#ifdef __linux__
    if (needToRestoreNMIWatchdog)
    {
        enableNMIWatchdog();
        needToRestoreNMIWatchdog = false;
    }
#endif
}

// hle is only available when cpuid has this:
// HLE: CPUID.07H.EBX.HLE [bit 4]  = 1
bool PCM::supportsHLE() const
{
    PCM_CPUID_INFO info;
    pcm_cpuid(7, 0, info); // leaf 7, subleaf 0

   return (info.reg.ebx & (0x1 << 4)) ? true : false;
}

// rtm is only available when cpuid has this:
// RTM: CPUID.07H.EBX.RTM [bit 11] = 1
bool PCM::supportsRTM() const
{
    PCM_CPUID_INFO info;
    pcm_cpuid(7, 0, info); // leaf 7, subleaf 0

    return (info.reg.ebx & (0x1 << 11)) ? true : false;
}

#ifdef __APPLE__

uint32 PCM::getNumInstances()
{
    return MSR[0]->getNumInstances();
}


uint32 PCM::incrementNumInstances()
{
    return MSR[0]->incrementNumInstances();
}

uint32 PCM::decrementNumInstances()
{
    return MSR[0]->decrementNumInstances();;
}

int convertUnknownToInt(size_t size, char* value)
{
    if(sizeof(int) == size)
    {
        return *(int*)value;
    }
    else if(sizeof(long) == size)
    {
        return *(long *)value;
    }
    else if(sizeof(long long) == size)
    {
        return *(long long *)value;
    }
    else
    {
        // In this case, we don't know what it is so we guess int
        return *(int *)value;
    }
}

#endif

bool PCM::decrementInstanceSemaphore()
{
    if(allow_multiple_instances == false)
    {
        return programmed_pmu;
    }
    bool isLastInstance = false;
    // when decrement was called before program() the numInstancesSemaphore
    // may not be initialized, causing SIGSEGV. This fixes it.
    if(numInstancesSemaphore == NULL)
        return true;

                #ifdef _MSC_VER
    WaitForSingleObject(numInstancesSemaphore, 0);

    DWORD res = WaitForSingleObject(numInstancesSemaphore, 0);
    if (res == WAIT_TIMEOUT)
    {
        // I have the last instance of monitor

        isLastInstance = true;

        CloseHandle(numInstancesSemaphore);
    }
    else if (res == WAIT_OBJECT_0)
    {
        ReleaseSemaphore(numInstancesSemaphore, 1, NULL);

        // std::cerr << "Someone else is running monitor instance, no cleanup needed\n";
    }
    else
    {
        // unknown error
        std::cerr << "ERROR: Bad semaphore. Performed cleanup twice?\n";
    }

        #elif __APPLE__
    sem_wait(numInstancesSemaphore);
    uint32 oldValue = PCM::getNumInstances();
    sem_post(numInstancesSemaphore);
    if(oldValue == 0)
    {
    // see same case for linux
    return false;
    }
    sem_wait(numInstancesSemaphore);
    uint32 currValue = PCM::decrementNumInstances();
    sem_post(numInstancesSemaphore);
    if(currValue == 0){
    isLastInstance = true;
    }

    #else // if linux
    int oldValue = -1;
    sem_getvalue(numInstancesSemaphore, &oldValue);
    if(oldValue == 0)
    {
       // the current value is already zero, somewhere the semaphore has been already decremented (and thus the clean up has been done if needed)
       // that means logically we are do not own the last instance anymore, thus returning false
       return false;
    }
    sem_wait(numInstancesSemaphore);
    int curValue = -1;
    sem_getvalue(numInstancesSemaphore, &curValue);
    if (curValue == 0)
    {
        // I have the last instance of monitor

        isLastInstance = true;

        // std::cerr << "I am the last one\n";
    }
        #endif // end ifdef _MSC_VER

    return isLastInstance;
}

uint64 PCM::getTickCount(uint64 multiplier, uint32 core)
{
    return (multiplier * getInvariantTSC(CoreCounterState(), getCoreCounterState(core))) / getNominalFrequency();
}

uint64 PCM::getTickCountRDTSCP(uint64 multiplier)
{
    return (multiplier*RDTSCP())/getNominalFrequency();
}

SystemCounterState getSystemCounterState()
{
    PCM * inst = PCM::getInstance();
    SystemCounterState result;
    if (inst) result = inst->getSystemCounterState();
    return result;
}

SocketCounterState getSocketCounterState(uint32 socket)
{
    PCM * inst = PCM::getInstance();
    SocketCounterState result;
    if (inst) result = inst->getSocketCounterState(socket);
    return result;
}

CoreCounterState getCoreCounterState(uint32 core)
{
    PCM * inst = PCM::getInstance();
    CoreCounterState result;
    if (inst) result = inst->getCoreCounterState(core);
    return result;
}

#ifdef PCM_USE_PERF
void PCM::readPerfData(uint32 core, std::vector<uint64> & outData)
{
    auto readPerfDataHelper = [this](const uint32 core, std::vector<uint64>& outData, const uint32 leader, const uint32 num_counters)
    {
        if (perfEventHandle[core][leader] < 0)
        {
            std::fill(outData.begin(), outData.end(), 0);
            return;
        }
        uint64 data[1 + PERF_MAX_COUNTERS];
        const int32 bytes2read = sizeof(uint64) * (1 + num_counters);
        int result = ::read(perfEventHandle[core][leader], data, bytes2read);
        // data layout: nr counters; counter 0, counter 1, counter 2,...
        if (result != bytes2read)
        {
            std::cerr << "Error while reading perf data. Result is " << result << "\n";
            std::cerr << "Check if you run other competing Linux perf clients.\n";
        }
        else if (data[0] != num_counters)
        {
            std::cerr << "Number of counters read from perf is wrong. Elements read: " << data[0] << "\n";
        }
        else
        {  // copy all counters, they start from position 1 in data
            std::copy((data + 1), (data + 1) + data[0], outData.begin());
        }
    };
    readPerfDataHelper(core, outData, PERF_GROUP_LEADER_COUNTER, core_fixed_counter_num_used + core_gen_counter_num_used);
    if (isHWTMAL1Supported() && perfSupportsTopDown())
    {
        std::vector<uint64> outTopDownData(outData.size(), 0);
        readPerfDataHelper(core, outTopDownData, PERF_TOPDOWN_GROUP_LEADER_COUNTER, PERF_TOPDOWN_COUNTERS);
        std::copy(outTopDownData.begin(), outTopDownData.begin() + PERF_TOPDOWN_COUNTERS, outData.begin() + core_fixed_counter_num_used + core_gen_counter_num_used);
    }
}
#endif

void BasicCounterState::readAndAggregateTSC(std::shared_ptr<SafeMsrHandle> msr)
{
    uint64 cInvariantTSC = 0;
    PCM * m = PCM::getInstance();
    const auto cpu_model = m->getCPUModel();
    if(m->isAtom() == false || cpu_model == PCM::AVOTON) msr->read(IA32_TIME_STAMP_COUNTER, &cInvariantTSC);
    else
    {
#ifdef _MSC_VER
        cInvariantTSC = ((static_cast<uint64>(GetTickCount()/1000ULL)))*m->getNominalFrequency();
#else
        struct timeval tp;
        gettimeofday(&tp, NULL);
        cInvariantTSC = (double(tp.tv_sec) + tp.tv_usec / 1000000.)*m->getNominalFrequency();
#endif
    }
    InvariantTSC += cInvariantTSC;
}

void BasicCounterState::readAndAggregate(std::shared_ptr<SafeMsrHandle> msr)
{
    uint64 cInstRetiredAny = 0, cCpuClkUnhaltedThread = 0, cCpuClkUnhaltedRef = 0;
    uint64 cL3Occupancy = 0;
    uint64 cCustomEvents[PERF_MAX_CUSTOM_COUNTERS] = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL };
    uint64 cCStateResidency[PCM::MAX_C_STATE + 1];
    memset(cCStateResidency, 0, sizeof(cCStateResidency));
    uint64 thermStatus = 0;
    uint64 cSMICount = 0;
    uint64 cFrontendBoundSlots = 0;
    uint64 cBadSpeculationSlots = 0;
    uint64 cBackendBoundSlots = 0;
    uint64 cRetiringSlots = 0;
    const int32 core_id = msr->getCoreId();
    TemporalThreadAffinity tempThreadAffinity(core_id); // speedup trick for Linux

    PCM * m = PCM::getInstance();
    const int32 core_gen_counter_num_max = m->getMaxCustomCoreEvents();
    uint64 overflows = 0;

    const auto corruptedCountersMask = m->checkCustomCoreProgramming(msr);
    // reading core PMU counters
#ifdef PCM_USE_PERF
    if(m->canUsePerf)
    {
        std::vector<uint64> perfData(PERF_MAX_COUNTERS, 0ULL);
        m->readPerfData(msr->getCoreId(), perfData);
        cInstRetiredAny =       perfData[PCM::PERF_INST_RETIRED_POS];
        cCpuClkUnhaltedThread = perfData[PCM::PERF_CPU_CLK_UNHALTED_THREAD_POS];
        cCpuClkUnhaltedRef =    perfData[PCM::PERF_CPU_CLK_UNHALTED_REF_POS];
        for (int i = 0; i < core_gen_counter_num_max; ++i)
        {
            cCustomEvents[i] = perfData[PCM::PERF_GEN_EVENT_0_POS + i];
        }
        if (m->isHWTMAL1Supported() && perfSupportsTopDown())
        {
            cFrontendBoundSlots =   perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_FRONTEND_POS]];
            cBadSpeculationSlots =  perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_BADSPEC_POS]];
            cBackendBoundSlots =    perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_BACKEND_POS]];
            cRetiringSlots =        perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_RETIRING_POS]];
//          uint64 slots =          perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_SLOTS_POS]];
//          if (core_id == 0) std::cout << "DEBUG: "<< slots << " " << cFrontendBoundSlots << " " << cBadSpeculationSlots << " " << cBackendBoundSlots << " " << cRetiringSlots << std::endl;
        }
    }
    else
#endif
    {
        uint64 overflows_after = 0;

        do
        {
            msr->read(IA32_PERF_GLOBAL_STATUS, &overflows); // read overflows
            // std::cerr << "Debug " << core_id << " IA32_PERF_GLOBAL_STATUS: " << overflows << std::endl;

            msr->read(INST_RETIRED_ADDR, &cInstRetiredAny);
            msr->read(CPU_CLK_UNHALTED_THREAD_ADDR, &cCpuClkUnhaltedThread);
            msr->read(CPU_CLK_UNHALTED_REF_ADDR, &cCpuClkUnhaltedRef);
            for (int i = 0; i < core_gen_counter_num_max; ++i)
            {
                msr->read(IA32_PMC0 + i, &cCustomEvents[i]);
            }

            msr->read(IA32_PERF_GLOBAL_STATUS, &overflows_after); // read overflows again
            // std::cerr << "Debug " << core_id << " IA32_PERF_GLOBAL_STATUS: " << overflows << std::endl;

        } while (overflows != overflows_after); // repeat the reading if an overflow happened during the reading

        msr->write(IA32_PERF_GLOBAL_OVF_CTRL, overflows); // clear overflows
        if (m->isHWTMAL1Supported())
        {
            uint64 perfMetrics = 0, slots = 0;
            msr->lock();
            msr->read(PERF_METRICS_ADDR, &perfMetrics);
            msr->read(TOPDOWN_SLOTS_ADDR, &slots);
            msr->write(PERF_METRICS_ADDR, 0);
            msr->write(TOPDOWN_SLOTS_ADDR, 0);
            cFrontendBoundSlots = extract_bits(perfMetrics, 16, 23);
            cBadSpeculationSlots = extract_bits(perfMetrics, 8, 15);
            cBackendBoundSlots = extract_bits(perfMetrics, 24, 31);
            cRetiringSlots = extract_bits(perfMetrics, 0, 7);
            const double total = double(cFrontendBoundSlots + cBadSpeculationSlots + cBackendBoundSlots + cRetiringSlots);
            cFrontendBoundSlots = m->FrontendBoundSlots[core_id] += uint64((double(cFrontendBoundSlots) / total) * double(slots));
            cBadSpeculationSlots = m->BadSpeculationSlots[core_id] += uint64((double(cBadSpeculationSlots) / total) * double(slots));
            cBackendBoundSlots = m->BackendBoundSlots[core_id] += uint64((double(cBackendBoundSlots) / total) * double(slots));
            cRetiringSlots = m->RetiringSlots[core_id] += uint64((double(cRetiringSlots) / total) * double(slots));
            // std::cout << "DEBUG: "<< slots << " " << cFrontendBoundSlots << " " << cBadSpeculationSlots << " " << cBackendBoundSlots << " " << cRetiringSlots << std::endl;
            msr->unlock();
        }
    }

    for (int i = 0; i < core_gen_counter_num_max; ++i)
    {
        if (corruptedCountersMask & (1<<i)) cCustomEvents[i] = ~0ULL;
    }

    // std::cout << "DEBUG1: " << msr->getCoreId() << " " << cInstRetiredAny << " \n";
    if (m->L3CacheOccupancyMetricAvailable() && m->useResctrl == false)
    {
        msr->lock();
        uint64 event = 1;
        m->initQOSevent(event, core_id);
        msr->read(IA32_QM_CTR, &cL3Occupancy);
        //std::cout << "readAndAggregate reading IA32_QM_CTR " << std::dec << cL3Occupancy << std::dec << "\n";
        msr->unlock();
    }

    m->readAndAggregateMemoryBWCounters(static_cast<uint32>(core_id), *this);

    readAndAggregateTSC(msr);

    // reading core C state counters
    for(int i=0; i <= (int)(PCM::MAX_C_STATE) ;++i)
        if(m->coreCStateMsr && m->coreCStateMsr[i])
                msr->read(m->coreCStateMsr[i], &(cCStateResidency[i]));

    // reading temperature
    msr->read(MSR_IA32_THERM_STATUS, &thermStatus);

    msr->read(MSR_SMI_COUNT, &cSMICount);

    InstRetiredAny += checked_uint64(m->extractCoreFixedCounterValue(cInstRetiredAny), extract_bits(overflows, 32, 32));
    CpuClkUnhaltedThread += checked_uint64(m->extractCoreFixedCounterValue(cCpuClkUnhaltedThread), extract_bits(overflows, 33, 33));
    CpuClkUnhaltedRef += checked_uint64(m->extractCoreFixedCounterValue(cCpuClkUnhaltedRef), extract_bits(overflows, 34, 34));
    for (int i = 0; i < core_gen_counter_num_max; ++i)
    {
        Event[i] += checked_uint64(m->extractCoreGenCounterValue(cCustomEvents[i]), extract_bits(overflows, i, i));
    }
#ifdef __linux__
    if (m->useResctrl)
    {
        L3Occupancy = m->resctrl.getL3OCC(core_id) / 1024;
    }
    else
#endif
    {
        //std::cout << "Scaling Factor " << m->L3ScalingFactor;
        cL3Occupancy = m->extractQOSMonitoring(cL3Occupancy);
        L3Occupancy = (cL3Occupancy==PCM_INVALID_QOS_MONITORING_DATA)? PCM_INVALID_QOS_MONITORING_DATA : (uint64)((double)(cL3Occupancy * m->L3ScalingFactor) / 1024.0);
    }
    for(int i=0; i <= int(PCM::MAX_C_STATE);++i)
        CStateResidency[i] += cCStateResidency[i];
    ThermalHeadroom = extractThermalHeadroom(thermStatus);
    SMICount += cSMICount;
    FrontendBoundSlots  += cFrontendBoundSlots;
    BadSpeculationSlots += cBadSpeculationSlots;
    BackendBoundSlots   += cBackendBoundSlots;
    RetiringSlots       += cRetiringSlots;
}

PCM::ErrorCode PCM::programServerUncoreLatencyMetrics(bool enable_pmm)
{
    uint32 DDRConfig[4] = {0,0,0,0};

    if (enable_pmm == false)
    {   //DDR is false
        if (ICX == cpu_model)
	{ 
            DDRConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(0x80) + MC_CH_PCI_PMON_CTL_UMASK(1);  // DRAM RPQ occupancy
            DDRConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(0x10) + MC_CH_PCI_PMON_CTL_UMASK(1);  // DRAM RPQ Insert
            DDRConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(0x81) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM WPQ Occupancy
            DDRConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(0x20) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM WPQ Insert
	    
	} else {
	    
            DDRConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(0x80) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM RPQ occupancy
            DDRConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(0x10) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM RPQ Insert
            DDRConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(0x81) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM WPQ Occupancy
            DDRConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(0x20) + MC_CH_PCI_PMON_CTL_UMASK(0);  // DRAM WPQ Insert
	}
    } else {
        DDRConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(0xe0) + MC_CH_PCI_PMON_CTL_UMASK(1);  // PMM RDQ occupancy
        DDRConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(0xe3) + MC_CH_PCI_PMON_CTL_UMASK(0);  // PMM RDQ Insert
        DDRConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(0xe4) + MC_CH_PCI_PMON_CTL_UMASK(1);  // PMM WPQ Occupancy
        DDRConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(0xe7) + MC_CH_PCI_PMON_CTL_UMASK(0);  // PMM WPQ Insert
    }

    if (DDRLatencyMetricsAvailable())
    {
        for (size_t i = 0; i < (size_t)server_pcicfg_uncore.size(); ++i)
        {
            server_pcicfg_uncore[i]->programIMC(DDRConfig);
        }
    }
    return PCM::Success;
}

PCM::ErrorCode PCM::programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, int rankA, int rankB)
{
    if(MSR.empty() || server_pcicfg_uncore.empty())  return PCM::MSRAccessDenied;

    for (int i = 0; (i < (int)server_pcicfg_uncore.size()) && MSR.size(); ++i)
    {
        server_pcicfg_uncore[i]->programServerUncoreMemoryMetrics(metrics, rankA, rankB);
    }

    return PCM::Success;
}

PCM::ErrorCode PCM::programServerUncorePowerMetrics(int mc_profile, int pcu_profile, int * freq_bands)
{
    if(MSR.empty() || server_pcicfg_uncore.empty())  return PCM::MSRAccessDenied;

    uint32 PCUCntConf[4] = {0,0,0,0};

    PCUCntConf[0] = PCU_MSR_PMON_CTL_EVENT(0); // clock ticks

    switch(pcu_profile)
    {
    case 0:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0xB); // FREQ_BAND0_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0xC); // FREQ_BAND1_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0xD); // FREQ_BAND2_CYCLES
         break;
    case 1:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(1); // POWER_STATE_OCCUPANCY.C0 using CLOCKTICKS + 8th-bit
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(2); // POWER_STATE_OCCUPANCY.C3 using CLOCKTICKS + 8th-bit
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(3); // POWER_STATE_OCCUPANCY.C6 using CLOCKTICKS + 8th-bit
         break;
    case 2:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x09); // PROCHOT_INTERNAL_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x0A); // PROCHOT_EXTERNAL_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x04); // Thermal frequency limit cycles: FREQ_MAX_LIMIT_THERMAL_CYCLES
         break;
    case 3:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x04); // Thermal frequency limit cycles: FREQ_MAX_LIMIT_THERMAL_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x05); // Power frequency limit cycles: FREQ_MAX_POWER_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x07); // Clipped frequency limit cycles: FREQ_MAX_CURRENT_CYCLES (not supported on SKX and ICX and SNOWRIDGE)
         break;
    case 4: // not supported on SKX and ICX and SNOWRIDGE
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x06); // OS frequency limit cycles: FREQ_MAX_OS_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x05); // Power frequency limit cycles: FREQ_MAX_POWER_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x07); // Clipped frequency limit cycles: FREQ_MAX_CURRENT_CYCLES (not supported on SKX and ICX and SNOWRIDGE)
         break;
    case 5:
         if(JAKETOWN == cpu_model)
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0) + PCU_MSR_PMON_CTL_EXTRA_SEL + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0) + PCU_MSR_PMON_CTL_EXTRA_SEL ; // cycles spent changing frequency
         } else if (IVYTOWN == cpu_model )
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x60) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x60) ; // cycles spent changing frequency: FREQ_TRANS_CYCLES
         } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model || ICX == cpu_model || SNOWRIDGE == cpu_model)
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x74) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x74) ; // cycles spent changing frequency: FREQ_TRANS_CYCLES
             if(HASWELLX == cpu_model)
             {
                 PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x79) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of UFS transitions
                 PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x79)                             ; // UFS transition cycles
             }
         } else
         {
             std::cerr << "ERROR: no frequency transition events defined for CPU model " << cpu_model << "\n";
         }
         break;
    case 6:
         if (IVYTOWN == cpu_model )
         {
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x2B) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC2 transitions
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x2D) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC6 transitions
         } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model || ICX == cpu_model || SNOWRIDGE == cpu_model)
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x4E)                             ; // PC1e residenicies (not supported on SKX and ICX and SNOWRIDGE)
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x4E) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC1 transitions (not supported on SKX and ICX and SNOWRIDGE)
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x2B) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC2 transitions
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x2D) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC6 transitions
         } else
         {
             std::cerr << "ERROR: no package C-state transition events defined for CPU model " << cpu_model << "\n";
         }
         break;
     case 7:
         if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model)
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x7E) ; // UFS_TRANSITIONS_PERF_P_LIMIT
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x7D) ; // UFS_TRANSITIONS_IO_P_LIMIT
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x7A) ; // UFS_TRANSITIONS_UP_RING_TRAFFIC
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x7B) ; // UFS_TRANSITIONS_UP_STALL_CYCLES
         } else
         {
             std::cerr << "ERROR: no UFS transition events defined for CPU model " << cpu_model << "\n";
         }
         break;
    case 8:
         if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model)
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x7C) ; // UFS_TRANSITIONS_DOWN
         } else
         {
             std::cerr << "ERROR: no UFS transition events defined for CPU model " << cpu_model << "\n";
         }
         break;
    default:
         std::cerr << "ERROR: unsupported PCU profile " << pcu_profile << "\n";
    }

    for (auto u : server_pcicfg_uncore)
    {
        u->program_power_metrics(mc_profile);
    }
    uint64 filter = 0;
    if (freq_bands == NULL)
    {
        filter =
            PCU_MSR_PMON_BOX_FILTER_BAND_0(10) + // 1000 MHz
            PCU_MSR_PMON_BOX_FILTER_BAND_1(20) + // 2000 MHz
            PCU_MSR_PMON_BOX_FILTER_BAND_2(30);  // 3000 MHz
    }
    else
    {
        filter =
            PCU_MSR_PMON_BOX_FILTER_BAND_0(freq_bands[0]) +
            PCU_MSR_PMON_BOX_FILTER_BAND_1(freq_bands[1]) +
            PCU_MSR_PMON_BOX_FILTER_BAND_2(freq_bands[2]);
    }
    programPCU(PCUCntConf, filter);

    return PCM::Success;
}

void PCM::programPCU(uint32* PCUCntConf, const uint64 filter)
{
    for (int i = 0; (i < (int)server_pcicfg_uncore.size()) && MSR.size(); ++i)
    {
        if (i >= (int)pcuPMUs.size())
        {
            continue;
        }

        uint32 refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        pcuPMUs[i].initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        if (pcuPMUs[i].filter[0].get())
        {
            *pcuPMUs[i].filter[0] = filter;
        }

        program(pcuPMUs[i], &PCUCntConf[0], &PCUCntConf[4], UNC_PMON_UNIT_CTL_FRZ_EN);
    }
}

PCM::ErrorCode PCM::program(const RawPMUConfigs& allPMUConfigs_)
{
    if (MSR.empty())  return PCM::MSRAccessDenied;
    RawPMUConfigs allPMUConfigs = allPMUConfigs_;
    constexpr auto globalRegPos = 0;
    if (allPMUConfigs.count("core"))
    {
        // need to program core PMU first
        EventSelectRegister regs[PERF_MAX_CUSTOM_COUNTERS];
        PCM::ExtendedCustomCoreEventDescription conf;
        conf.OffcoreResponseMsrValue[0] = 0;
        conf.OffcoreResponseMsrValue[1] = 0;
        FixedEventControlRegister fixedReg;

        auto corePMUConfig = allPMUConfigs["core"];
        if (corePMUConfig.programmable.size() > (size_t)getMaxCustomCoreEvents())
        {
            std::cerr << "ERROR: trying to program " << corePMUConfig.programmable.size() << " core PMU counters, which exceeds the max num possible ("<< getMaxCustomCoreEvents() << ").";
            return PCM::UnknownError;
        }
        size_t c = 0;
        for (; c < corePMUConfig.programmable.size() && c < (size_t)getMaxCustomCoreEvents() && c < PERF_MAX_CUSTOM_COUNTERS; ++c)
        {
            regs[c].value = corePMUConfig.programmable[c].first[0];
        }
        if (globalRegPos < corePMUConfig.programmable.size())
        {
            conf.OffcoreResponseMsrValue[0] = corePMUConfig.programmable[globalRegPos].first[1];
            conf.OffcoreResponseMsrValue[1] = corePMUConfig.programmable[globalRegPos].first[2];
        }
        conf.nGPCounters = (uint32)c;
        conf.gpCounterCfg = regs;
        if (corePMUConfig.fixed.empty())
        {
            conf.fixedCfg = NULL; // default
        }
        else
        {
            fixedReg.value = corePMUConfig.fixed[0].first[0];
            conf.fixedCfg = &fixedReg;
        }

        const auto status = program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf);
        if (status != PCM::Success)
        {
            return status;
        }
        allPMUConfigs.erase("core");
    }
    for (auto pmuConfig : allPMUConfigs)
    {
        const auto & type = pmuConfig.first;
        const auto & events = pmuConfig.second;
        if (events.programmable.empty() && events.fixed.empty())
        {
            continue;
        }
        if (events.programmable.size() > ServerUncoreCounterState::maxCounters)
        {
            std::cerr << "ERROR: trying to program " << events.programmable.size() << " core PMU counters, which exceeds the max num possible (" << ServerUncoreCounterState::maxCounters << ").";
            return PCM::UnknownError;
        }
        uint32 events32[ServerUncoreCounterState::maxCounters] = { 0,0,0,0 };
        uint64 events64[ServerUncoreCounterState::maxCounters] = { 0,0,0,0 };
        for (size_t c = 0; c < events.programmable.size() && c < ServerUncoreCounterState::maxCounters; ++c)
        {
            events32[c] = (uint32)events.programmable[c].first[0];
            events64[c] = events.programmable[c].first[0];
        }
        if (type == "m3upi")
        {
            for (auto uncore : server_pcicfg_uncore)
            {
                uncore->programM3UPI(events32);
            }
        }
        else if (type == "xpi" || type == "upi" || type == "qpi")
        {
            for (auto uncore : server_pcicfg_uncore)
            {
                uncore->programXPI(events32);
            }
        }
        else if (type == "imc")
        {
            for (auto uncore : server_pcicfg_uncore)
            {
                uncore->programIMC(events32);
            }
        }
        else if (type == "m2m")
        {
            for (auto uncore : server_pcicfg_uncore)
            {
                uncore->programM2M(events64);
            }
        }
        else if (type == "pcu")
        {
            uint64 filter = 0;
            if (globalRegPos < events.programmable.size())
            {
                filter = events.programmable[globalRegPos].first[1];
            }
            programPCU(events32, filter);
        }
        else if (type == "ubox")
        {
            programUBOX(events64);
        }
        else if (type == "cbo" || type == "cha")
        {
            uint64 filter0 = 0, filter1 = 0;
            if (globalRegPos < events.programmable.size())
            {
                filter0 = events.programmable[globalRegPos].first[1];
                filter1 = events.programmable[globalRegPos].first[2];
            }
            programCboRaw(events64, filter0, filter1);
        }
        else if (type == "iio")
        {
            programIIOCounters(events64);
        }
        else
        {
            std::cerr << "ERROR: unrecognized PMU type \"" << type << "\"\n";
            return PCM::UnknownError;
        }
    }
    return PCM::Success;
}

void PCM::freezeServerUncoreCounters()
{
    for (int i = 0; (i < (int)server_pcicfg_uncore.size()) && MSR.size(); ++i)
    {
        server_pcicfg_uncore[i]->freezeCounters();
        pcuPMUs[i].freeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        if (IIOEventsAvailable())
        {
            for (auto & pmu : iioPMUs[i])
            {
                pmu.second.freeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        const auto refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux
        for (auto & pmu : cboPMUs[i])
        {
            pmu.freeze(UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}
void PCM::unfreezeServerUncoreCounters()
{
    for (int i = 0; (i < (int)server_pcicfg_uncore.size()) && MSR.size(); ++i)
    {
        server_pcicfg_uncore[i]->unfreezeCounters();
        pcuPMUs[i].unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        if (IIOEventsAvailable())
        {
            for (auto & pmu : iioPMUs[i])
            {
                pmu.second.unfreeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        const auto refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux
        for (auto & pmu : cboPMUs[i])
        {
            pmu.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}
void UncoreCounterState::readAndAggregate(std::shared_ptr<SafeMsrHandle> msr)
{
    const auto coreID = msr->getCoreId();
    TemporalThreadAffinity tempThreadAffinity(coreID); // speedup trick for Linux

    auto pcm = PCM::getInstance();
    pcm->readAndAggregatePackageCStateResidencies(msr, *this);
}

SystemCounterState PCM::getSystemCounterState()
{
    SystemCounterState result;
    if (MSR.size())
    {
        // read core and uncore counter state
        for (int32 core = 0; core < num_cores; ++core)
            if ( isCoreOnline( core ) )
                result.readAndAggregate(MSR[core]);

        for (uint32 s = 0; s < (uint32)num_sockets; s++)
        {
            if ( isSocketOnline( s ) ) {
                readAndAggregateUncoreMCCounters(s, result);
                readAndAggregateEnergyCounters(s, result);
            }
        }

        readQPICounters(result);

        result.ThermalHeadroom = static_cast<int32>(PCM_INVALID_THERMAL_HEADROOM); // not available for system
    }
    return result;
}

template <class CounterStateType>
void PCM::readAndAggregateMemoryBWCounters(const uint32 core, CounterStateType & result)
{
#ifdef __linux__
    if (useResctrl)
    {
        if (CoreLocalMemoryBWMetricAvailable())
        {
            result.MemoryBWLocal += resctrl.getMBL(core) / (1024*1024);
        }
        if (CoreRemoteMemoryBWMetricAvailable())
        {
            result.MemoryBWTotal += resctrl.getMBT(core) / (1024*1024);
        }
        return;
    }
#endif
     uint64 cMemoryBWLocal = 0;
     uint64 cMemoryBWTotal = 0;

     if(core < memory_bw_local.size())
     {
         cMemoryBWLocal = memory_bw_local[core]->read();
         cMemoryBWLocal = extractQOSMonitoring(cMemoryBWLocal);
         //std::cout << "Read MemoryBWLocal " << cMemoryBWLocal << "\n";
         if(cMemoryBWLocal==PCM_INVALID_QOS_MONITORING_DATA)
             result.MemoryBWLocal = PCM_INVALID_QOS_MONITORING_DATA; // do not accumulate invalid reading
         else
             result.MemoryBWLocal += (uint64)((double)(cMemoryBWLocal * L3ScalingFactor) / (1024.0 * 1024.0));
     }
     if(core < memory_bw_total.size())
     {
         cMemoryBWTotal = memory_bw_total[core]->read();
         cMemoryBWTotal = extractQOSMonitoring(cMemoryBWTotal);
         //std::cout << "Read MemoryBWTotal " << cMemoryBWTotal << "\n";
         if(cMemoryBWTotal==PCM_INVALID_QOS_MONITORING_DATA)
             result.MemoryBWTotal = PCM_INVALID_QOS_MONITORING_DATA; // do not accumulate invalid reading
         else
             result.MemoryBWTotal  += (uint64)((double)(cMemoryBWTotal * L3ScalingFactor) / (1024.0 * 1024.0));
     }
     //std::cout << std::flush;
}

template <class CounterStateType>
void PCM::readAndAggregateUncoreMCCounters(const uint32 socket, CounterStateType & result)
{
    if (LLCReadMissLatencyMetricsAvailable())
    {
        result.TOROccupancyIAMiss += getCBOCounterState(socket, EventPosition::TOR_OCCUPANCY);
        result.TORInsertsIAMiss += getCBOCounterState(socket, EventPosition::TOR_INSERTS);
        result.UncClocks += getUncoreClocks(socket);
    }

    const bool ReadMCStatsFromServerBW = (socket < serverBW.size());
    if (ReadMCStatsFromServerBW)
    {
        result.UncMCNormalReads += serverBW[socket]->getImcReads();
        result.UncMCFullWrites += serverBW[socket]->getImcWrites();
        if (PMMTrafficMetricsAvailable())
        {
            result.UncPMMReads += serverBW[socket]->getPMMReads();
            result.UncPMMWrites += serverBW[socket]->getPMMWrites();
        }
    }

    if (hasPCICFGUncore())
    {
        if (server_pcicfg_uncore.size() && server_pcicfg_uncore[socket].get())
        {
            server_pcicfg_uncore[socket]->freezeCounters();
	    if (ReadMCStatsFromServerBW == false)
            {
                result.UncMCNormalReads += server_pcicfg_uncore[socket]->getImcReads();
                result.UncMCFullWrites += server_pcicfg_uncore[socket]->getImcWrites();
            }
            if (localMemoryRequestRatioMetricAvailable())
            {
                if (hasCHA())
                {
                    result.UncHARequests += getCBOCounterState(socket, EventPosition::REQUESTS_ALL);
                    result.UncHALocalRequests += getCBOCounterState(socket, EventPosition::REQUESTS_LOCAL);
                }
                else
                {
                    result.UncHARequests += server_pcicfg_uncore[socket]->getHARequests();
                    result.UncHALocalRequests += server_pcicfg_uncore[socket]->getHALocalRequests();
                }
            }
            if (PMMTrafficMetricsAvailable() && (ReadMCStatsFromServerBW == false))
            {
                result.UncPMMReads += server_pcicfg_uncore[socket]->getPMMReads();
                result.UncPMMWrites += server_pcicfg_uncore[socket]->getPMMWrites();
            }
            if (MCDRAMmemoryTrafficMetricsAvailable())
            {
                result.UncEDCNormalReads += server_pcicfg_uncore[socket]->getEdcReads();
                result.UncEDCFullWrites += server_pcicfg_uncore[socket]->getEdcWrites();
            }
            server_pcicfg_uncore[socket]->unfreezeCounters();
        }
    }
    else if(clientBW.get() && socket == 0)
    {
        result.UncMCNormalReads += clientImcReads->read();
        result.UncMCFullWrites += clientImcWrites->read();
        result.UncMCIORequests += clientIoRequests->read();
    }
    else
    {
        std::shared_ptr<SafeMsrHandle> msr = MSR[socketRefCore[socket]];
        TemporalThreadAffinity tempThreadAffinity(socketRefCore[socket]); // speedup trick for Linux
        switch (cpu_model)
        {
            case PCM::WESTMERE_EP:
            case PCM::NEHALEM_EP:
            {
                uint64 cUncMCFullWrites = 0;
                uint64 cUncMCNormalReads = 0;
                msr->read(MSR_UNCORE_PMC0, &cUncMCFullWrites);
                msr->read(MSR_UNCORE_PMC1, &cUncMCNormalReads);
                result.UncMCFullWrites += extractUncoreGenCounterValue(cUncMCFullWrites);
                result.UncMCNormalReads += extractUncoreGenCounterValue(cUncMCNormalReads);
            }
            break;
            case PCM::NEHALEM_EX:
            case PCM::WESTMERE_EX:
            {
                uint64 cUncMCNormalReads = 0;
                msr->read(MB0_MSR_PMU_CNT_0, &cUncMCNormalReads);
                result.UncMCNormalReads += extractUncoreGenCounterValue(cUncMCNormalReads);
                msr->read(MB1_MSR_PMU_CNT_0, &cUncMCNormalReads);
                result.UncMCNormalReads += extractUncoreGenCounterValue(cUncMCNormalReads);

                uint64 cUncMCFullWrites = 0;                         // really good approximation of
                msr->read(BB0_MSR_PERF_CNT_1, &cUncMCFullWrites);
                result.UncMCFullWrites += extractUncoreGenCounterValue(cUncMCFullWrites);
                msr->read(BB1_MSR_PERF_CNT_1, &cUncMCFullWrites);
                result.UncMCFullWrites += extractUncoreGenCounterValue(cUncMCFullWrites);
            }
            break;

            default:;
        }
    }
}

template <class CounterStateType>
void PCM::readAndAggregateEnergyCounters(const uint32 socket, CounterStateType & result)
{
    if(socket < (uint32)energy_status.size())
        result.PackageEnergyStatus += energy_status[socket]->read();

    if (socket < (uint32)dram_energy_status.size())
        result.DRAMEnergyStatus += dram_energy_status[socket]->read();
}

template <class CounterStateType>
void PCM::readAndAggregatePackageCStateResidencies(std::shared_ptr<SafeMsrHandle> msr, CounterStateType & result)
{
    // reading package C state counters
    uint64 cCStateResidency[PCM::MAX_C_STATE + 1];
    memset(cCStateResidency, 0, sizeof(cCStateResidency));

    for(int i=0; i <= int(PCM::MAX_C_STATE) ;++i)
        if(pkgCStateMsr && pkgCStateMsr[i])
                msr->read(pkgCStateMsr[i], &(cCStateResidency[i]));

    for (int i = 0; i <= int(PCM::MAX_C_STATE); ++i)
    {
        atomic_fetch_add((std::atomic<uint64> *)(result.CStateResidency + i), cCStateResidency[i]);
    }
}

void PCM::readQPICounters(SystemCounterState & result)
{
        // read QPI counters
        std::vector<bool> SocketProcessed(num_sockets, false);
        if (cpu_model == PCM::NEHALEM_EX || cpu_model == PCM::WESTMERE_EX)
        {
            for (int32 core = 0; core < num_cores; ++core)
            {
                if(isCoreOnline(core) == false) continue;

                if(core == socketRefCore[0]) MSR[core]->read(W_MSR_PMON_FIXED_CTR, &(result.uncoreTSC));

                uint32 s = topology[core].socket;

                if (!SocketProcessed[s])
                {
                    TemporalThreadAffinity tempThreadAffinity(core); // speedup trick for Linux

                    // incoming data responses from QPI link 0
                    MSR[core]->read(R_MSR_PMON_CTR1, &(result.incomingQPIPackets[s][0]));
                    // incoming data responses from QPI link 1 (yes, from CTR0)
                    MSR[core]->read(R_MSR_PMON_CTR0, &(result.incomingQPIPackets[s][1]));
                    // incoming data responses from QPI link 2
                    MSR[core]->read(R_MSR_PMON_CTR8, &(result.incomingQPIPackets[s][2]));
                    // incoming data responses from QPI link 3
                    MSR[core]->read(R_MSR_PMON_CTR9, &(result.incomingQPIPackets[s][3]));

                    // outgoing idle flits from QPI link 0
                    MSR[core]->read(R_MSR_PMON_CTR3, &(result.outgoingQPIFlits[s][0]));
                    // outgoing idle flits from QPI link 1 (yes, from CTR0)
                    MSR[core]->read(R_MSR_PMON_CTR2, &(result.outgoingQPIFlits[s][1]));
                    // outgoing idle flits from QPI link 2
                    MSR[core]->read(R_MSR_PMON_CTR10, &(result.outgoingQPIFlits[s][2]));
                    // outgoing idle flits from QPI link 3
                    MSR[core]->read(R_MSR_PMON_CTR11, &(result.outgoingQPIFlits[s][3]));

                    SocketProcessed[s] = true;
                }
            }
        }
        else if ((cpu_model == PCM::NEHALEM_EP || cpu_model == PCM::WESTMERE_EP))
        {
            if (num_sockets == 2)
            {
                uint32 SCore[2] = { 0, 0 };
                uint64 Total_Reads[2] = { 0, 0 };
                uint64 Total_Writes[2] = { 0, 0 };
                uint64 IOH_Reads[2] = { 0, 0 };
                uint64 IOH_Writes[2] = { 0, 0 };
                uint64 Remote_Reads[2] = { 0, 0 };
                uint64 Remote_Writes[2] = { 0, 0 };
                uint64 Local_Reads[2] = { 0, 0 };
                uint64 Local_Writes[2] = { 0, 0 };

                while (topology[SCore[0]].socket != 0) ++(SCore[0]);
                while (topology[SCore[1]].socket != 1) ++(SCore[1]);
                for (int s = 0; s < 2; ++s)
                {
                    TemporalThreadAffinity tempThreadAffinity(SCore[s]); // speedup trick for Linux

                    MSR[SCore[s]]->read(MSR_UNCORE_PMC0, &Total_Writes[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC1, &Total_Reads[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC2, &IOH_Reads[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC3, &IOH_Writes[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC4, &Remote_Reads[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC5, &Remote_Writes[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC6, &Local_Reads[s]);
                    MSR[SCore[s]]->read(MSR_UNCORE_PMC7, &Local_Writes[s]);
                }

#if 1
                // compute Remote_Reads differently
                for (int s = 0; s < 2; ++s)
                {
                    uint64 total = Total_Writes[s] + Total_Reads[s];
                    uint64 rem = IOH_Reads[s]
                                 + IOH_Writes[s]
                                 + Local_Reads[s]
                                 + Local_Writes[s]
                                 + Remote_Writes[s];
                    Remote_Reads[s] = (total > rem) ? (total - rem) : 0;
                }
#endif


                // only an estimation (lower bound) - does not count NT stores correctly
                result.incomingQPIPackets[0][0] = Remote_Reads[1] + Remote_Writes[0];
                result.incomingQPIPackets[0][1] = IOH_Reads[0];
                result.incomingQPIPackets[1][0] = Remote_Reads[0] + Remote_Writes[1];
                result.incomingQPIPackets[1][1] = IOH_Reads[1];
            }
            else
            {
                // for a single socket systems no information is available
                result.incomingQPIPackets[0][0] = 0;
            }
        }
        else if (hasPCICFGUncore())
        {
                for (int32 s = 0; (s < (int32)server_pcicfg_uncore.size()); ++s)
                {
                    server_pcicfg_uncore[s]->freezeCounters();
                    for (uint32 port = 0; port < (uint32)getQPILinksPerSocket(); ++port)
                    {
                        result.incomingQPIPackets[s][port] = uint64(double(server_pcicfg_uncore[s]->getIncomingDataFlits(port)) / (64./getDataBytesPerFlit()));
                        result.outgoingQPIFlits[s][port] = server_pcicfg_uncore[s]->getOutgoingFlits(port);
                        result.TxL0Cycles[s][port] = server_pcicfg_uncore[s]->getUPIL0TxCycles(port);
                    }
                    server_pcicfg_uncore[s]->unfreezeCounters();
                }
        }
        // end of reading QPI counters
}

template <class CounterStateType>
void PCM::readPackageThermalHeadroom(const uint32 socket, CounterStateType & result)
{
    if(packageThermalMetricsAvailable())
    {
        uint64 val = 0;
        MSR[socketRefCore[socket]]->read(MSR_PACKAGE_THERM_STATUS,&val);
        result.ThermalHeadroom = extractThermalHeadroom(val);
    }
    else
        result.ThermalHeadroom = PCM_INVALID_THERMAL_HEADROOM; // not available
}

// Explicit instantiation needed in topology.cpp
template void PCM::readAndAggregatePackageCStateResidencies(std::shared_ptr<SafeMsrHandle>, UncoreCounterState &);
template void PCM::readAndAggregateUncoreMCCounters<UncoreCounterState>(const uint32, UncoreCounterState&);
template void PCM::readAndAggregateEnergyCounters<UncoreCounterState>(const uint32, UncoreCounterState&);
template void PCM::readPackageThermalHeadroom<SocketCounterState>(const uint32, SocketCounterState &);

SocketCounterState PCM::getSocketCounterState(uint32 socket)
{
    SocketCounterState result;
    if (MSR.size())
    {
        // reading core and uncore counter states
        for (int32 core = 0; core < num_cores; ++core)
            if (isCoreOnline(core) && (topology[core].socket == int32(socket)))
                result.readAndAggregate(MSR[core]);

        readAndAggregateUncoreMCCounters(socket, result);

        readAndAggregateEnergyCounters(socket, result);

        readPackageThermalHeadroom(socket, result);

    }
    return result;
}

void PCM::getAllCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates, std::vector<CoreCounterState> & coreStates)
{
    // clear and zero-initialize all inputs
    systemState = SystemCounterState();
    socketStates.clear();
    socketStates.resize(num_sockets);
    coreStates.clear();
    coreStates.resize(num_cores);

    std::vector<std::future<void> > asyncCoreResults;

    for (int32 core = 0; core < num_cores; ++core)
    {
        // read core counters
        if (isCoreOnline(core))
        {
            std::packaged_task<void()> task([this,&coreStates,&socketStates,core]() -> void
                {
                    coreStates[core].readAndAggregate(MSR[core]);
                    socketStates[topology[core].socket].UncoreCounterState::readAndAggregate(MSR[core]); // read package C state counters
                }
            );
            asyncCoreResults.push_back(task.get_future());
            coreTaskQueues[core]->push(task);
        }
        // std::cout << "DEBUG2: " << core << " " << coreStates[core].InstRetiredAny << " \n";
    }
    // std::cout << std::flush;
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        int32 refCore = socketRefCore[s];
        if (refCore<0) refCore = 0;
        std::packaged_task<void()> task([this, s, &socketStates]() -> void
            {
                readAndAggregateUncoreMCCounters(s, socketStates[s]);
                readAndAggregateEnergyCounters(s, socketStates[s]);
                readPackageThermalHeadroom(s, socketStates[s]);
            } );
        asyncCoreResults.push_back(task.get_future());
        coreTaskQueues[refCore]->push(task);
    }

    readQPICounters(systemState);

    for (auto & ar : asyncCoreResults)
        ar.wait();

    for (int32 core = 0; core < num_cores; ++core)
    {   // aggregate core counters into sockets
        if(isCoreOnline(core))
          socketStates[topology[core].socket] += coreStates[core];
    }

    for (int32 s = 0; s < num_sockets; ++s)
    {   // aggregate core counters from sockets into system state and
        // aggregate socket uncore iMC, energy and package C state counters into system
        systemState += socketStates[s];
    }
}

void PCM::getUncoreCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates)
{
    // clear and zero-initialize all inputs
    systemState = SystemCounterState();
    socketStates.clear();
    socketStates.resize(num_sockets);
    std::vector<CoreCounterState> refCoreStates(num_sockets);

    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        const int32 refCore = socketRefCore[s];
        if(isCoreOnline(refCore))
        {
            refCoreStates[s].readAndAggregateTSC(MSR[refCore]);
        }
        readAndAggregateUncoreMCCounters(s, socketStates[s]);
        readAndAggregateEnergyCounters(s, socketStates[s]);
        readPackageThermalHeadroom(s, socketStates[s]);
    }

    readQPICounters(systemState);

    for (int32 s = 0; s < num_sockets; ++s)
    {
        const int32 refCore = socketRefCore[s];
        if(isCoreOnline(refCore))
        {
            for(uint32 core=0; core < getNumCores(); ++core)
            {
                if(topology[core].socket == s && isCoreOnline(core))
                    socketStates[s] += refCoreStates[s];
            }
        }
        // aggregate socket uncore iMC, energy counters into system
        systemState += socketStates[s];
    }
}

CoreCounterState PCM::getCoreCounterState(uint32 core)
{
    CoreCounterState result;
    if (MSR.size()) result.readAndAggregate(MSR[core]);
    return result;
}

uint32 PCM::getNumCores() const
{
    return (uint32)num_cores;
}

uint32 PCM::getNumOnlineCores() const
{
    return (uint32)num_online_cores;
}

uint32 PCM::getNumSockets() const
{
    return (uint32)num_sockets;
}

uint32 PCM::getNumOnlineSockets() const
{
    return (uint32)num_online_sockets;
}


uint32 PCM::getThreadsPerCore() const
{
    return (uint32)threads_per_core;
}

bool PCM::getSMT() const
{
    return threads_per_core > 1;
}

uint64 PCM::getNominalFrequency() const
{
    return nominal_frequency;
}

uint32 PCM::getL3ScalingFactor() const
{
    PCM_CPUID_INFO cpuinfo;
    pcm_cpuid(0xf,0x1,cpuinfo);

    return (uint32)cpuinfo.reg.ebx;

}

bool PCM::isSomeCoreOfflined()
{
    PCM_CPUID_INFO cpuid_args;
    pcm_cpuid(0xB,1,cpuid_args);
    uint32 max_num_lcores_per_socket = cpuid_args.reg.ebx & 0xFFFF;
    uint32 max_num_lcores = max_num_lcores_per_socket * getNumSockets();
    if(threads_per_core == 1 && (getNumOnlineCores() * 2 == max_num_lcores)) // HT is disabled in the BIOS
    {
       return false;
    }
    return !(getNumOnlineCores() == max_num_lcores);
}

ServerUncoreCounterState PCM::getServerUncoreCounterState(uint32 socket)
{
    ServerUncoreCounterState result;
    if (socket < serverBW.size() && serverBW[socket].get())
    {
        result.freeRunningCounter[ServerUncoreCounterState::ImcReads] = serverBW[socket]->getImcReads();
        result.freeRunningCounter[ServerUncoreCounterState::ImcWrites] = serverBW[socket]->getImcWrites();
        result.freeRunningCounter[ServerUncoreCounterState::PMMReads] = serverBW[socket]->getPMMReads();
        result.freeRunningCounter[ServerUncoreCounterState::PMMWrites] = serverBW[socket]->getPMMWrites();
    }
    if(server_pcicfg_uncore.size() && server_pcicfg_uncore[socket].get())
    {
        server_pcicfg_uncore[socket]->freezeCounters();
        for(uint32 port=0;port < (uint32)server_pcicfg_uncore[socket]->getNumQPIPorts();++port)
        {
            assert(port < result.xPICounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.xPICounter[port][cnt] = server_pcicfg_uncore[socket]->getQPILLCounter(port, cnt);
            assert(port < result.M3UPICounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.M3UPICounter[port][cnt] = server_pcicfg_uncore[socket]->getM3UPICounter(port, cnt);
        }
        for (uint32 channel = 0; channel < (uint32)server_pcicfg_uncore[socket]->getNumMCChannels(); ++channel)
        {
            assert(channel < result.DRAMClocks.size());
            result.DRAMClocks[channel] = server_pcicfg_uncore[socket]->getDRAMClocks(channel);
            assert(channel < result.MCCounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.MCCounter[channel][cnt] = server_pcicfg_uncore[socket]->getMCCounter(channel, cnt);
        }
        for (uint32 channel = 0; channel < (uint32)server_pcicfg_uncore[socket]->getNumEDCChannels(); ++channel)
        {
            assert(channel < result.MCDRAMClocks.size());
            result.MCDRAMClocks[channel] = server_pcicfg_uncore[socket]->getMCDRAMClocks(channel);
            assert(channel < result.EDCCounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.EDCCounter[channel][cnt] = server_pcicfg_uncore[socket]->getEDCCounter(channel, cnt);
        }
    for (uint32 controller = 0; controller < (uint32)server_pcicfg_uncore[socket]->getNumMC(); ++controller)
    {
      assert(controller < result.M2MCounter.size());
      for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
          result.M2MCounter[controller][cnt] = server_pcicfg_uncore[socket]->getM2MCounter(controller, cnt);
    }
        server_pcicfg_uncore[socket]->unfreezeCounters();
    }
    if (MSR.size())
    {
        uint32 refCore = socketRefCore[socket];
        TemporalThreadAffinity tempThreadAffinity(refCore);
        for (uint32 cbo = 0; socket < cboPMUs.size() && cbo < cboPMUs[socket].size() && cbo < ServerUncoreCounterState::maxCBOs; ++cbo)
        {
            for (int i = 0; i < ServerUncoreCounterState::maxCounters; ++i)
            {
                result.CBOCounter[cbo][i] = *(cboPMUs[socket][cbo].counterValue[i]);
            }
        }
        for (uint32 stack = 0; socket < iioPMUs.size() && stack < iioPMUs[socket].size() && stack < ServerUncoreCounterState::maxIIOStacks; ++stack)
        {
            for (int i = 0; i < ServerUncoreCounterState::maxCounters; ++i)
            {
                result.IIOCounter[stack][i] = *(iioPMUs[socket][stack].counterValue[i]);
            }
        }
        for (int i = 0; i < 2 && socket < uboxPMUs.size(); ++i)
        {
            result.UBOXCounter[i] = *(uboxPMUs[socket].counterValue[i]);
            result.UncClocks = getUncoreClocks(socket);
        }
        for (int i = 0; i < ServerUncoreCounterState::maxCounters && socket < pcuPMUs.size(); ++i)
            result.PCUCounter[i] = *pcuPMUs[socket].counterValue[i];
        // std::cout << "values read: " << result.PCUCounter[0] << " " << result.PCUCounter[1] << " " << result.PCUCounter[2] << " " << result.PCUCounter[3] << "\n";
        uint64 val=0;
        //MSR[refCore]->read(MSR_PKG_ENERGY_STATUS,&val);
        //std::cout << "Energy status: " << val << "\n";
        MSR[refCore]->read(MSR_PACKAGE_THERM_STATUS,&val);
        result.PackageThermalHeadroom = extractThermalHeadroom(val);
        MSR[refCore]->read(IA32_TIME_STAMP_COUNTER, &result.InvariantTSC);
        readAndAggregatePackageCStateResidencies(MSR[refCore], result);
    }
    // std::cout << std::flush;
    readAndAggregateEnergyCounters(socket, result);

    return result;
}

#ifndef _MSC_VER
void print_mcfg(const char * path)
{
    int mcfg_handle = ::open(path, O_RDONLY);

    if (mcfg_handle < 0)
    {
        std::cerr << "PCM Error: Cannot open " << path << "\n";
        throw std::exception();
    }

    MCFGHeader header;

    ssize_t read_bytes = ::read(mcfg_handle, (void *)&header, sizeof(MCFGHeader));

    if(read_bytes == 0)
    {
        std::cerr << "PCM Error: Cannot read " << path << "\n";
        throw std::exception();
    }

    const unsigned segments = header.nrecords();
    header.print();
    std::cout << "Segments: " << segments << "\n";

    for(unsigned int i=0; i<segments;++i)
    {
        MCFGRecord record;
        read_bytes = ::read(mcfg_handle, (void *)&record, sizeof(MCFGRecord));
        if(read_bytes == 0)
        {
              std::cerr << "PCM Error: Cannot read " << path << " (2)\n";
              throw std::exception();
        }
        std::cout << "Segment " << std::dec << i << " ";
        record.print();
    }

    ::close(mcfg_handle);
}
#endif


static const uint32 IMC_DEV_IDS[] = {
    0x03cb0,
    0x03cb1,
    0x03cb4,
    0x03cb5,
    0x0EB4,
    0x0EB5,
    0x0EB0,
    0x0EB1,
    0x0EF4,
    0x0EF5,
    0x0EF0,
    0x0EF1,
    0x2fb0,
    0x2fb1,
    0x2fb4,
    0x2fb5,
    0x2fd0,
    0x2fd1,
    0x2fd4,
    0x2fd5,
    0x6fb0,
    0x6fb1,
    0x6fb4,
    0x6fb5,
    0x6fd0,
    0x6fd1,
    0x6fd4,
    0x6fd5,
    0x2042,
    0x2046,
    0x204a,
    0x7840,
    0x7841,
    0x7842,
    0x7843,
    0x7844,
    0x781f
};

static const uint32 UPI_DEV_IDS[] = {
    0x2058,
    0x3441
};

static const uint32 M2M_DEV_IDS[] = {
    0x2066,
    0x344A
};

Mutex socket2busMutex;
std::vector<std::pair<uint32,uint32> > ServerPCICFGUncore::socket2iMCbus;
std::vector<std::pair<uint32,uint32> > ServerPCICFGUncore::socket2UPIbus;
std::vector<std::pair<uint32,uint32> > ServerPCICFGUncore::socket2M2Mbus;

void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize)
{
    if (device == PCM_INVALID_DEV_ADDR || function == PCM_INVALID_FUNC_ADDR)
    {
        return;
    }
    Mutex::Scope _(socket2busMutex);
    if(!socket2bus.empty()) return;

    #ifdef __linux__
    const std::vector<MCFGRecord> & mcfg = PciHandleMM::getMCFGRecords();
    #else
    std::vector<MCFGRecord> mcfg;
    MCFGRecord segment;
    segment.PCISegmentGroupNumber = 0;
    segment.startBusNumber = 0;
    segment.endBusNumber = 0xff;
    mcfg.push_back(segment);
    #endif

    for(uint32 s = 0; s < (uint32)mcfg.size(); ++s)
    for (uint32 bus = (uint32)mcfg[s].startBusNumber; bus <= (uint32)mcfg[s].endBusNumber; ++bus)
    {
        uint32 value = 0;
        try
        {
            PciHandleType h(mcfg[s].PCISegmentGroupNumber, bus, device, function);
            h.read32(0, &value);

        } catch(...)
        {
            // invalid bus:devicei:function
            continue;
        }
        const uint32 vendor_id = value & 0xffff;
        const uint32 device_id = (value >> 16) & 0xffff;
        if (vendor_id != PCM_INTEL_PCI_VENDOR_ID)
           continue;

        for (uint32 i = 0; i < devIdsSize; ++i)
        {
           // match
           if(DEV_IDS[i] == device_id)
           {
               // std::cout << "DEBUG: found bus " << std::hex << bus << " with device ID " << device_id << std::dec << "\n";
               socket2bus.push_back(std::make_pair(mcfg[s].PCISegmentGroupNumber,bus));
               break;
           }
        }
    }
    //std::cout << std::flush;
}

int getBusFromSocket(const uint32 socket)
{
    int cur_bus = 0;
    uint32 cur_socket = 0;
    // std::cout << "socket: " << socket << "\n";
    while(cur_socket <= socket)
    {
        // std::cout << "reading from bus 0x" << std::hex << cur_bus << std::dec << " ";
        PciHandleType h(0, cur_bus, 5, 0);
        uint32 cpubusno = 0;
        h.read32(0x108, &cpubusno); // CPUBUSNO register
        cur_bus = (cpubusno >> 8)& 0x0ff;
        // std::cout << "socket: " << cur_socket << std::hex << " cpubusno: 0x" << std::hex << cpubusno << " " << cur_bus << std::dec << "\n";
        if(socket == cur_socket)
            return cur_bus;
        ++cur_socket;
        ++cur_bus;
        if(cur_bus > 0x0ff)
           return -1;
    }
    //std::cout << std::flush;

    return -1;
}

PciHandleType * ServerPCICFGUncore::createIntelPerfMonDevice(uint32 groupnr_, int32 bus_, uint32 dev_, uint32 func_, bool checkVendor)
{
    if (PciHandleType::exists(groupnr_, (uint32)bus_, dev_, func_))
    {
        PciHandleType * handle = new PciHandleType(groupnr_, bus_, dev_, func_);

        if(!checkVendor) return handle;

        uint32 vendor_id = 0;
        handle->read32(PCM_PCI_VENDOR_ID_OFFSET,&vendor_id);
        vendor_id &= 0x0ffff;

        if(vendor_id == PCM_INTEL_PCI_VENDOR_ID) return handle;

        delete handle;
    }
    return NULL;
}

bool PCM::isSecureBoot() const
{
    static int flag = -1;
    if (MSR.size() > 0 && flag == -1)
    {
        // std::cerr << "DEBUG: checking MSR in isSecureBoot\n";
        uint64 val = 0;
        if (MSR[0]->read(IA32_PERFEVTSEL0_ADDR, &val) != sizeof(val))
        {
            flag = 0; // some problem with MSR read, not secure boot
        }
        // read works
        if (MSR[0]->write(IA32_PERFEVTSEL0_ADDR, val) != sizeof(val)/* && errno == 1 */) // errno works only on windows
        { // write does not work -> secure boot
            flag = 1;
        }
        else
        {
            flag = 0; // can write MSR -> no secure boot
        }
    }
    return flag == 1;
}

bool PCM::useLinuxPerfForUncore() const
{
    static int use = -1;
    if (use != -1)
    {
        return 1 == use;
    }
    use = 0;
    bool secureBoot = isSecureBoot();
#ifdef PCM_USE_PERF
    const auto imcIDs = enumeratePerfPMUs("imc", 100);
    std::cout << "INFO: Linux perf interface to program uncore PMUs is " << (imcIDs.empty()?"NOT ":"") << "present\n";
    const char * perf_env = std::getenv("PCM_USE_UNCORE_PERF");
    if (perf_env != NULL && std::string(perf_env) == std::string("1"))
    {
        std::cout << "INFO: using Linux perf interface to program uncore PMUs because env variable PCM_USE_UNCORE_PERF=1\n";
        use = 1;
    }
    if (secureBoot)
    {
        std::cout << "INFO: Secure Boot detected. Using Linux perf for uncore PMU programming.\n";
        use = 1;
    }
    else
#endif
    {
        if (secureBoot)
        {
            std::cerr << "ERROR: Secure Boot detected. Recompile PCM with -DPCM_USE_PERF or disable Secure Boot.\n";
        }
    }
    return 1 == use;
}

ServerPCICFGUncore::ServerPCICFGUncore(uint32 socket_, const PCM * pcm) :
     iMCbus(-1)
   , UPIbus(-1)
   , M2Mbus(-1)
   , groupnr(0)
   , cpu_model(pcm->getCPUModel())
   , qpi_speed(0)
{
    initRegisterLocations(pcm);
    initBuses(socket_, pcm);

    if (pcm->useLinuxPerfForUncore())
    {
        initPerf(socket_, pcm);
    }
    else
    {
        initDirect(socket_, pcm);
    }

    std::cerr << "Socket " << socket_ << ": " <<
        getNumMC() << " memory controllers detected with total number of " << getNumMCChannels() << " channels. " <<
        getNumQPIPorts() << " QPI ports detected." <<
        " " << m2mPMUs.size() << " M2M (mesh to memory) blocks detected."
        " " << haPMUs.size()  << " Home Agents detected."
        " " << m3upiPMUs.size() << " M3UPI blocks detected."
        "\n";
}

void ServerPCICFGUncore::initRegisterLocations(const PCM * pcm)
{
#define PCM_PCICFG_MC_INIT(controller, channel, arch) \
    MCRegisterLocation.resize(controller + 1); \
    MCRegisterLocation[controller].resize(channel + 1); \
    MCRegisterLocation[controller][channel] =  \
        std::make_pair(arch##_MC##controller##_CH##channel##_REGISTER_DEV_ADDR, arch##_MC##controller##_CH##channel##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_QPI_INIT(port, arch) \
    XPIRegisterLocation.resize(port + 1); \
    XPIRegisterLocation[port] = std::make_pair(arch##_QPI_PORT##port##_REGISTER_DEV_ADDR, arch##_QPI_PORT##port##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_M3UPI_INIT(port, arch) \
    M3UPIRegisterLocation.resize(port + 1); \
    M3UPIRegisterLocation[port] = std::make_pair(arch##_M3UPI_PORT##port##_REGISTER_DEV_ADDR, arch##_M3UPI_PORT##port##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_EDC_INIT(controller, clock, arch) \
    EDCRegisterLocation.resize(controller + 1); \
    EDCRegisterLocation[controller] = std::make_pair(arch##_EDC##controller##_##clock##_REGISTER_DEV_ADDR, arch##_EDC##controller##_##clock##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_M2M_INIT(x, arch) \
    M2MRegisterLocation.resize(x + 1); \
    M2MRegisterLocation[x] = std::make_pair(arch##_M2M_##x##_REGISTER_DEV_ADDR, arch##_M2M_##x##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_HA_INIT(x, arch) \
    HARegisterLocation.resize(x + 1); \
    HARegisterLocation[x] = std::make_pair(arch##_HA##x##_REGISTER_DEV_ADDR, arch##_HA##x##_REGISTER_FUNC_ADDR);

    if(cpu_model == PCM::JAKETOWN || cpu_model == PCM::IVYTOWN)
    {
        PCM_PCICFG_MC_INIT(0, 0, JKTIVT)
        PCM_PCICFG_MC_INIT(0, 1, JKTIVT)
        PCM_PCICFG_MC_INIT(0, 2, JKTIVT)
        PCM_PCICFG_MC_INIT(0, 3, JKTIVT)
        PCM_PCICFG_MC_INIT(1, 0, JKTIVT)
        PCM_PCICFG_MC_INIT(1, 1, JKTIVT)
        PCM_PCICFG_MC_INIT(1, 2, JKTIVT)
        PCM_PCICFG_MC_INIT(1, 3, JKTIVT)

        PCM_PCICFG_QPI_INIT(0, JKTIVT);
        PCM_PCICFG_QPI_INIT(1, JKTIVT);
        PCM_PCICFG_QPI_INIT(2, JKTIVT);
    }
    else if(cpu_model == PCM::HASWELLX || cpu_model == PCM::BDX_DE || cpu_model == PCM::BDX)
    {
        PCM_PCICFG_MC_INIT(0, 0, HSX)
        PCM_PCICFG_MC_INIT(0, 1, HSX)
        PCM_PCICFG_MC_INIT(0, 2, HSX)
        PCM_PCICFG_MC_INIT(0, 3, HSX)
        PCM_PCICFG_MC_INIT(1, 0, HSX)
        PCM_PCICFG_MC_INIT(1, 1, HSX)
        PCM_PCICFG_MC_INIT(1, 2, HSX)
        PCM_PCICFG_MC_INIT(1, 3, HSX)

        PCM_PCICFG_QPI_INIT(0, HSX);
        PCM_PCICFG_QPI_INIT(1, HSX);
        PCM_PCICFG_QPI_INIT(2, HSX);

        PCM_PCICFG_HA_INIT(0, HSX);
        PCM_PCICFG_HA_INIT(1, HSX);
    }
    else if(cpu_model == PCM::SKX)
    {
        PCM_PCICFG_MC_INIT(0, 0, SKX)
        PCM_PCICFG_MC_INIT(0, 1, SKX)
        PCM_PCICFG_MC_INIT(0, 2, SKX)
        PCM_PCICFG_MC_INIT(0, 3, SKX)
        PCM_PCICFG_MC_INIT(1, 0, SKX)
        PCM_PCICFG_MC_INIT(1, 1, SKX)
        PCM_PCICFG_MC_INIT(1, 2, SKX)
        PCM_PCICFG_MC_INIT(1, 3, SKX)

        PCM_PCICFG_QPI_INIT(0, SKX);
        PCM_PCICFG_QPI_INIT(1, SKX);
        PCM_PCICFG_QPI_INIT(2, SKX);

        if (pcm->isCPX())
        {
            PCM_PCICFG_QPI_INIT(3, CPX);
            PCM_PCICFG_QPI_INIT(4, CPX);
            PCM_PCICFG_QPI_INIT(5, CPX);
        }

        PCM_PCICFG_M2M_INIT(0, SKX)
        PCM_PCICFG_M2M_INIT(1, SKX)

        // M3UPI
        if (pcm->isCPX())
        {
            // CPX
            PCM_PCICFG_M3UPI_INIT(0, CPX);
            PCM_PCICFG_M3UPI_INIT(1, CPX);
            PCM_PCICFG_M3UPI_INIT(2, CPX);
            PCM_PCICFG_M3UPI_INIT(3, CPX);
            PCM_PCICFG_M3UPI_INIT(4, CPX);
            PCM_PCICFG_M3UPI_INIT(5, CPX);
        }
        else
        {
            // SKX/CLX
            PCM_PCICFG_M3UPI_INIT(0, SKX);
            PCM_PCICFG_M3UPI_INIT(1, SKX);
            PCM_PCICFG_M3UPI_INIT(2, SKX);
        }
    }
    else if (cpu_model == PCM::ICX)
    {
        PCM_PCICFG_QPI_INIT(0, ICX);
        PCM_PCICFG_QPI_INIT(1, ICX);
        PCM_PCICFG_QPI_INIT(2, ICX);

        PCM_PCICFG_M3UPI_INIT(0, ICX);
        PCM_PCICFG_M3UPI_INIT(1, ICX);
        PCM_PCICFG_M3UPI_INIT(2, ICX);

        PCM_PCICFG_M2M_INIT(0, SERVER)
        PCM_PCICFG_M2M_INIT(1, SERVER)
        PCM_PCICFG_M2M_INIT(2, SERVER)
        PCM_PCICFG_M2M_INIT(3, SERVER)
    }
    else if(cpu_model == PCM::KNL)
    {
        // 2 DDR4 Memory Controllers with 3 channels each
        PCM_PCICFG_MC_INIT(0, 0, KNL)
        PCM_PCICFG_MC_INIT(0, 1, KNL)
        PCM_PCICFG_MC_INIT(0, 2, KNL)
        PCM_PCICFG_MC_INIT(1, 0, KNL)
        PCM_PCICFG_MC_INIT(1, 1, KNL)
        PCM_PCICFG_MC_INIT(1, 2, KNL)

    // 8 MCDRAM (Multi-Channel [Stacked] DRAM) Memory Controllers
        PCM_PCICFG_EDC_INIT(0, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(1, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(2, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(3, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(4, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(5, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(6, ECLK, KNL)
        PCM_PCICFG_EDC_INIT(7, ECLK, KNL)
    }
    else if (cpu_model == PCM::SNOWRIDGE)
    {
        PCM_PCICFG_M2M_INIT(0, SERVER)
        PCM_PCICFG_M2M_INIT(1, SERVER)
        PCM_PCICFG_M2M_INIT(2, SERVER)
        PCM_PCICFG_M2M_INIT(3, SERVER)
    }
    else
    {
        std::cerr << "Error: Uncore PMU for processor with model id " << cpu_model << " is not supported.\n";
        throw std::exception();
    }

#undef PCM_PCICFG_MC_INIT
#undef PCM_PCICFG_QPI_INIT
#undef PCM_PCICFG_M3UPI_INIT
#undef PCM_PCICFG_EDC_INIT
#undef PCM_PCICFG_M2M_INIT
#undef PCM_PCICFG_HA_INIT
}

void ServerPCICFGUncore::initBuses(uint32 socket_, const PCM * pcm)
{
    const uint32 total_sockets_ = pcm->getNumSockets();

    if (M2MRegisterLocation.size())
    {
        initSocket2Bus(socket2M2Mbus, M2MRegisterLocation[0].first, M2MRegisterLocation[0].second, M2M_DEV_IDS, (uint32)sizeof(M2M_DEV_IDS) / sizeof(M2M_DEV_IDS[0]));
        if (socket_ < socket2M2Mbus.size())
        {
            groupnr = socket2M2Mbus[socket_].first;
            M2Mbus = socket2M2Mbus[socket_].second;
        }
        else
        {
            std::cerr << "PCM error: socket_ " << socket_ << " >= socket2M2Mbus.size() " << socket2M2Mbus.size() << "\n";
        }
        if (total_sockets_ != socket2M2Mbus.size())
        {
            std::cerr << "PCM warning: total_sockets_ " << total_sockets_ << " does not match socket2M2Mbus.size() " << socket2M2Mbus.size() << "\n";
        }
    }

    if (MCRegisterLocation.size() > 0 && MCRegisterLocation[0].size() > 0)
    {
        initSocket2Bus(socket2iMCbus, MCRegisterLocation[0][0].first, MCRegisterLocation[0][0].second, IMC_DEV_IDS, (uint32)sizeof(IMC_DEV_IDS) / sizeof(IMC_DEV_IDS[0]));

        if (total_sockets_ == socket2iMCbus.size())
        {
            if (total_sockets_ == socket2M2Mbus.size() && socket2iMCbus[socket_].first != socket2M2Mbus[socket_].first)
            {
                std::cerr << "PCM error: mismatching PCICFG group number for M2M and IMC perfmon devices.\n";
                M2Mbus = -1;
            }
            groupnr = socket2iMCbus[socket_].first;
            iMCbus = socket2iMCbus[socket_].second;
        }
        else if (total_sockets_ <= 4)
        {
            iMCbus = getBusFromSocket(socket_);
            if (iMCbus < 0)
            {
                std::cerr << "Cannot find bus for socket " << socket_ << " on system with " << total_sockets_ << " sockets.\n";
                throw std::exception();
            }
            else
            {
                std::cerr << "PCM Warning: the bus for socket " << socket_ << " on system with " << total_sockets_ << " sockets could not find via PCI bus scan. Using cpubusno register. Bus = " << iMCbus << "\n";
            }
        }
        else
        {
            std::cerr << "Cannot find bus for socket " << socket_ << " on system with " << total_sockets_ << " sockets.\n";
            throw std::exception();
        }
    }

#if 1
    if (total_sockets_ == 1) {
        /*
         * For single socket systems, do not worry at all about QPI ports.  This
         *  eliminates QPI LL programming error messages on single socket systems
         *  with BIOS that hides QPI performance counting PCI functions.  It also
         *  eliminates register programming that is not needed since no QPI traffic
         *  is possible with single socket systems.
         */
        return;
    }
#endif

#ifdef PCM_NOQPI
    return;
#endif

    if (PCM::hasUPI(cpu_model))
    {
        initSocket2Bus(socket2UPIbus, XPIRegisterLocation[0].first, XPIRegisterLocation[0].second, UPI_DEV_IDS, (uint32)sizeof(UPI_DEV_IDS) / sizeof(UPI_DEV_IDS[0]));
        if(total_sockets_ == socket2UPIbus.size())
        {
            UPIbus = socket2UPIbus[socket_].second;
            if(groupnr != socket2UPIbus[socket_].first)
            {
                UPIbus = -1;
                std::cerr << "PCM error: mismatching PCICFG group number for UPI and IMC perfmon devices.\n";
            }
        }
        else
        {
            std::cerr << "PCM error: Did not find UPI perfmon device on every socket in a multisocket system.\n";
        }
    }
    else
    {
        UPIbus = iMCbus;
    }
}

void ServerPCICFGUncore::initDirect(uint32 socket_, const PCM * pcm)
{
    {
        std::vector<std::shared_ptr<PciHandleType> > imcHandles;

        auto lastWorkingChannels = imcHandles.size();
        for (auto & ctrl: MCRegisterLocation)
        {
            for (auto & channel : ctrl)
            {
                PciHandleType * handle = createIntelPerfMonDevice(groupnr, iMCbus, channel.first, channel.second, true);
                if (handle) imcHandles.push_back(std::shared_ptr<PciHandleType>(handle));
            }
            if (imcHandles.size() > lastWorkingChannels)
            {
                num_imc_channels.push_back((uint32)(imcHandles.size() - lastWorkingChannels));
            }
            lastWorkingChannels = imcHandles.size();
        }

        for (auto & handle : imcHandles)
        {
            if (cpu_model == PCM::KNL) {
                imcPMUs.push_back(
                    UncorePMU(
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_BOX_CTL_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_CTL0_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_CTL1_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_CTL2_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_CTL3_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, KNX_MC_CH_PCI_PMON_CTR0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, KNX_MC_CH_PCI_PMON_CTR1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, KNX_MC_CH_PCI_PMON_CTR2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, KNX_MC_CH_PCI_PMON_CTR3_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, KNX_MC_CH_PCI_PMON_FIXED_CTL_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, KNX_MC_CH_PCI_PMON_FIXED_CTR_ADDR))
                );
            }
            else {
                imcPMUs.push_back(
                    UncorePMU(
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_BOX_CTL_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_CTL0_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_CTL1_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_CTL2_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_CTL3_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, XPF_MC_CH_PCI_PMON_CTR0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, XPF_MC_CH_PCI_PMON_CTR1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, XPF_MC_CH_PCI_PMON_CTR2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, XPF_MC_CH_PCI_PMON_CTR3_ADDR),
                        std::make_shared<PCICFGRegister32>(handle, XPF_MC_CH_PCI_PMON_FIXED_CTL_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, XPF_MC_CH_PCI_PMON_FIXED_CTR_ADDR))
                );
            }
        }
    }

    {
        std::vector<std::shared_ptr<PciHandleType> > m2mHandles;

        if (M2Mbus >= 0)
        {
            for (auto & reg : M2MRegisterLocation)
            {
                PciHandleType * handle = createIntelPerfMonDevice(groupnr, M2Mbus, reg.first, reg.second, true);
                if (handle) m2mHandles.push_back(std::shared_ptr<PciHandleType>(handle));
            }
        }

        for (auto & handle : m2mHandles)
        {
            if (cpu_model == PCM::ICX || cpu_model == PCM::SNOWRIDGE)
            {
                m2mPMUs.push_back(
                    UncorePMU(
                        std::make_shared<PCICFGRegister32>(handle, SERVER_M2M_PCI_PMON_BOX_CTL_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTL0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTL1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTL2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTL3_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTR0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTR1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTR2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SERVER_M2M_PCI_PMON_CTR3_ADDR)
                    )
                );
            }
            else
            {
                m2mPMUs.push_back(
                    UncorePMU(
                        std::make_shared<PCICFGRegister32>(handle, SKX_M2M_PCI_PMON_BOX_CTL_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTL0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTL1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTL2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTL3_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTR0_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTR1_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTR2_ADDR),
                        std::make_shared<PCICFGRegister64>(handle, SKX_M2M_PCI_PMON_CTR3_ADDR)
                    )
                );
            }
        }
    }

    int numChannels = 0;

    if (cpu_model == PCM::SNOWRIDGE || cpu_model == PCM::ICX)
    {
        numChannels = 2;
    }

    if (numChannels > 0)
    {
        initSocket2Ubox0Bus();
        if (socket_ < socket2UBOX0bus.size())
        {
            auto memBars = getServerMemBars((uint32)m2mPMUs.size(), socket2UBOX0bus[socket_].first, socket2UBOX0bus[socket_].second);
            for (auto & memBar : memBars)
            {
                for (int channel = 0; channel < numChannels; ++channel)
                {
                    auto handle = std::make_shared<MMIORange>(memBar + SERVER_MC_CH_PMON_BASE_ADDR + channel * SERVER_MC_CH_PMON_STEP, SERVER_MC_CH_PMON_SIZE, false);
                    imcPMUs.push_back(
                        UncorePMU(
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_BOX_CTL_OFFSET),
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL0_OFFSET),
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL1_OFFSET),
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL2_OFFSET),
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL3_OFFSET),
                            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR0_OFFSET),
                            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR1_OFFSET),
                            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR2_OFFSET),
                            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR3_OFFSET),
                            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_FIXED_CTL_OFFSET),
                            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_FIXED_CTR_OFFSET)
                        )
                    );
                }
                num_imc_channels.push_back(numChannels);
            }
        }
        else
        {
            std::cerr << "ERROR: socket " << socket_ << " is not found in socket2UBOX0bus. socket2UBOX0bus.size =" << socket2UBOX0bus.size() << std::endl;
        }
    }

    if (imcPMUs.empty())
    {
        std::cerr << "PCM error: no memory controllers found.\n";
        throw std::exception();
    }

    if (cpu_model == PCM::KNL)
    {
        std::vector<std::shared_ptr<PciHandleType> > edcHandles;

        for (auto & reg : EDCRegisterLocation)
        {
            PciHandleType * handle = createIntelPerfMonDevice(groupnr, iMCbus, reg.first, reg.second, true);
            if (handle) edcHandles.push_back(std::shared_ptr<PciHandleType>(handle));
        }

        for (auto & handle : edcHandles)
        {
            edcPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, KNX_EDC_CH_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, KNX_EDC_CH_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, KNX_EDC_CH_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, KNX_EDC_CH_PCI_PMON_CTR3_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, KNX_EDC_CH_PCI_PMON_FIXED_CTL_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, KNX_EDC_CH_PCI_PMON_FIXED_CTR_ADDR))
            );
        }
    }

    std::vector<std::shared_ptr<PciHandleType> > m3upiHandles;
    if (UPIbus >= 0)
    {
        for (auto& reg : M3UPIRegisterLocation)
        {
            PciHandleType* handle = createIntelPerfMonDevice(groupnr, UPIbus, reg.first, reg.second, true);
            if (handle) m3upiHandles.push_back(std::shared_ptr<PciHandleType>(handle));
        }
    }
    for (auto& handle : m3upiHandles)
    {
        if (cpu_model == PCM::ICX)
        {
            m3upiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, ICX_M3UPI_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_M3UPI_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_M3UPI_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_M3UPI_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_M3UPI_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_M3UPI_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_M3UPI_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_M3UPI_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_M3UPI_PCI_PMON_CTR3_ADDR)
                )
            );
        }
        else
        {
            m3upiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, M3UPI_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, M3UPI_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, M3UPI_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, M3UPI_PCI_PMON_CTL2_ADDR),
                    std::shared_ptr<PCICFGRegister32>(),
                    std::make_shared<PCICFGRegister64>(handle, M3UPI_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, M3UPI_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, M3UPI_PCI_PMON_CTR2_ADDR),
                    std::shared_ptr<PCICFGRegister64>()
                )
            );
        }
    }

    {
        std::vector<std::shared_ptr<PciHandleType> > haHandles;
        for (auto & reg : HARegisterLocation)
        {
            auto handle = createIntelPerfMonDevice(groupnr, iMCbus, reg.first, reg.second, true);
            if (handle) haHandles.push_back(std::shared_ptr<PciHandleType>(handle));
        }

        for (auto & handle : haHandles)
        {
            haPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, XPF_HA_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, XPF_HA_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, XPF_HA_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, XPF_HA_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, XPF_HA_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, XPF_HA_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, XPF_HA_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, XPF_HA_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, XPF_HA_PCI_PMON_CTR3_ADDR)
                )
            );
        }
    }

    if (pcm->getNumSockets() == 1) {
        /*
         * For single socket systems, do not worry at all about QPI ports.  This
         *  eliminates QPI LL programming error messages on single socket systems
         *  with BIOS that hides QPI performance counting PCI functions.  It also
         *  eliminates register programming that is not needed since no QPI traffic
         *  is possible with single socket systems.
         */
        xpiPMUs.clear();
        return;
    }

#ifdef PCM_NOQPI
    xpiPMUs.clear();
    std::cerr << getNumMC() << " memory controllers detected with total number of " << imcPMUs.size() << " channels. " <<
        m2mPMUs.size() << " M2M (mesh to memory) blocks detected. "
        << haPMUs.size() << " Home Agents detected. "
        << m3upiPMUs.size() << " M3UPI blocks detected. "
        "\n";
    return;
#endif

    std::vector<std::shared_ptr<PciHandleType> > qpiLLHandles;
    auto xPI = pcm->xPI();
    try
    {
        for (size_t i = 0; i < XPIRegisterLocation.size(); ++i)
        {
            PciHandleType * handle = createIntelPerfMonDevice(groupnr, UPIbus, XPIRegisterLocation[i].first, XPIRegisterLocation[i].second, true);
            if (handle)
                qpiLLHandles.push_back(std::shared_ptr<PciHandleType>(handle));
            else
            {
                if (i == 0 || i == 1)
                {
                    std::cerr << "ERROR: " << xPI << " LL monitoring device (" << std::hex << groupnr << ":" << UPIbus << ":" << XPIRegisterLocation[i].first << ":" <<
                        XPIRegisterLocation[i].second << ") is missing. The " << xPI << " statistics will be incomplete or missing." << std::dec << "\n";
                }
                else if (pcm->getCPUBrandString().find("E7") != std::string::npos) // Xeon E7
                {
                    std::cerr << "ERROR: " << xPI << " LL performance monitoring device for the third " << xPI << " link was not found on " << pcm->getCPUBrandString() <<
                        " processor in socket " << socket_ << ". Possibly BIOS hides the device. The " << xPI << " statistics will be incomplete or missing.\n";
                }
            }
        }
    }
    catch (...)
    {
        std::cerr << "PCM Error: can not create " << xPI << " LL handles.\n";
        throw std::exception();
    }

    for (auto & handle : qpiLLHandles)
    {
        if (cpu_model == PCM::SKX)
        {
            xpiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, U_L_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, U_L_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, U_L_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, U_L_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, U_L_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, U_L_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, U_L_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, U_L_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, U_L_PCI_PMON_CTR3_ADDR)
                    )
            );
        }
        else if (cpu_model == PCM::ICX)
        {
            xpiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, ICX_UPI_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_UPI_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_UPI_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_UPI_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, ICX_UPI_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_UPI_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_UPI_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_UPI_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, ICX_UPI_PCI_PMON_CTR3_ADDR)
                )
            );
        }
        else
        {
            xpiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, Q_P_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, Q_P_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, Q_P_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, Q_P_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, Q_P_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, Q_P_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, Q_P_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, Q_P_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, Q_P_PCI_PMON_CTR3_ADDR)
                    )
            );
        }
    }
}


#ifdef PCM_USE_PERF
class PerfVirtualDummyUnitControlRegister : public HWRegister
{
    uint64 lastValue;
public:
    PerfVirtualDummyUnitControlRegister() : lastValue(0) {}
    void operator = (uint64 val) override
    {
        lastValue = val;
    }
    operator uint64 () override
    {
        return lastValue;
    }
};

class PerfVirtualFilterRegister;

class PerfVirtualControlRegister : public HWRegister
{
    friend class PerfVirtualCounterRegister;
    friend class PerfVirtualFilterRegister;
    int fd;
    int socket;
    int pmuID;
    perf_event_attr event;
    bool fixed;
    void close()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }
public:
    PerfVirtualControlRegister(int socket_, int pmuID_, bool fixed_ = false) :
        fd(-1),
        socket(socket_),
        pmuID(pmuID_),
        fixed(fixed_)
    {
        event = PCM_init_perf_event_attr(false);
        event.type = pmuID;
    }
    void operator = (uint64 val) override
    {
        close();
        event.config = fixed ? 0xff : val;
        const auto core = PCM::getInstance()->socketRefCore[socket];
        if ((fd = syscall(SYS_perf_event_open, &event, -1, core, -1, 0)) <= 0)
        {
            std::cerr << "Linux Perf: Error on programming PMU " << pmuID << ":  " << strerror(errno) << "\n";
            std::cerr << "config: 0x" << std::hex << event.config << " config1: 0x" << event.config1 << " config2: 0x" << event.config2 << std::dec << "\n";
            if (errno == 24) std::cerr << "try executing 'ulimit -n 10000' to increase the limit on the number of open files.\n";
            return;
        }
    }
    operator uint64 () override
    {
        return event.config;
    }
    ~PerfVirtualControlRegister()
    {
        close();
    }
    int getFD() const { return fd; }
    int getPMUID() const { return pmuID; }
};

class PerfVirtualCounterRegister : public HWRegister
{
    std::shared_ptr<PerfVirtualControlRegister> controlReg;
public:
    PerfVirtualCounterRegister(const std::shared_ptr<PerfVirtualControlRegister> & controlReg_) : controlReg(controlReg_)
    {
    }
    void operator = (uint64 /* val */) override
    {
        // no-op
    }
    operator uint64 () override
    {
        uint64 result = 0;
        if (controlReg.get() && (controlReg->getFD() >= 0))
        {
            int status = ::read(controlReg->getFD(), &result, sizeof(result));
            if (status != sizeof(result))
            {
                std::cerr << "PCM Error: failed to read from Linux perf handle " << controlReg->getFD() << " PMU " << controlReg->getPMUID() << "\n";
            }
        }
        return result;
    }
};

class PerfVirtualFilterRegister : public HWRegister
{
    uint64 lastValue;
    std::array<std::shared_ptr<PerfVirtualControlRegister>, 4> controlRegs;
    int filterNr;
public:
    PerfVirtualFilterRegister(std::array<std::shared_ptr<PerfVirtualControlRegister>, 4> & controlRegs_, int filterNr_) :
            lastValue(0),
            controlRegs(controlRegs_),
            filterNr(filterNr_)
    {
    }
    void operator = (uint64 val) override
    {
        lastValue = val;
        for (auto & ctl: controlRegs)
        {
            union {
                uint64 config1;
                uint32 config1HL[2];
            } cvt;
            cvt.config1 = ctl->event.config1;
	    cvt.config1HL[filterNr] = val;
	    ctl->event.config1 = cvt.config1;
        }
    }
    operator uint64 () override
    {
        return lastValue;
    }
};

std::vector<int> enumeratePerfPMUs(const std::string & type, int max_id)
{
    auto getPerfPMUID = [](const std::string & type, int num)
    {
        int id = -1;
        std::ostringstream pmuIDPath(std::ostringstream::out);
        pmuIDPath << std::string("/sys/bus/event_source/devices/uncore_") << type;
        if (num != -1)
        {
            pmuIDPath << "_" << num;
        }
        pmuIDPath << "/type";
        const std::string pmuIDStr = readSysFS(pmuIDPath.str().c_str(), true);
        if (pmuIDStr.size())
        {
            id = std::atoi(pmuIDStr.c_str());
        }
        return id;
    };
    std::vector<int> ids;
    for (int i = -1; i < max_id; ++i)
    {
        int pmuID = getPerfPMUID(type, i);
        if (pmuID > 0)
        {
            // std::cout << "DEBUG: " << type << " pmu id " << pmuID << " found\n";
            ids.push_back(pmuID);
        }
    }
    return ids;
}

void populatePerfPMUs(unsigned socket_, const std::vector<int> & ids, std::vector<UncorePMU> & pmus, bool fixed, bool filter0, bool filter1)
{
    for (const auto & id : ids)
    {
        std::array<std::shared_ptr<PerfVirtualControlRegister>, 4> controlRegs = {
            std::make_shared<PerfVirtualControlRegister>(socket_, id),
                    std::make_shared<PerfVirtualControlRegister>(socket_, id),
                    std::make_shared<PerfVirtualControlRegister>(socket_, id),
                    std::make_shared<PerfVirtualControlRegister>(socket_, id)
        };
        std::shared_ptr<PerfVirtualCounterRegister> counterReg0 = std::make_shared<PerfVirtualCounterRegister>(controlRegs[0]);
        std::shared_ptr<PerfVirtualCounterRegister> counterReg1 = std::make_shared<PerfVirtualCounterRegister>(controlRegs[1]);
        std::shared_ptr<PerfVirtualCounterRegister> counterReg2 = std::make_shared<PerfVirtualCounterRegister>(controlRegs[2]);
        std::shared_ptr<PerfVirtualCounterRegister> counterReg3 = std::make_shared<PerfVirtualCounterRegister>(controlRegs[3]);
        std::shared_ptr<PerfVirtualControlRegister> fixedControlReg = std::make_shared<PerfVirtualControlRegister>(socket_, id, true);
        std::shared_ptr<PerfVirtualCounterRegister> fixedCounterReg = std::make_shared<PerfVirtualCounterRegister>(fixedControlReg);
        std::shared_ptr<PerfVirtualFilterRegister> filterReg0 = std::make_shared<PerfVirtualFilterRegister>(controlRegs, 0);
        std::shared_ptr<PerfVirtualFilterRegister> filterReg1 = std::make_shared<PerfVirtualFilterRegister>(controlRegs, 1);
        pmus.push_back(
            UncorePMU(
                std::make_shared<PerfVirtualDummyUnitControlRegister>(),
                controlRegs[0],
                controlRegs[1],
                controlRegs[2],
                controlRegs[3],
                counterReg0,
                counterReg1,
                counterReg2,
                counterReg3,
                fixed ? fixedControlReg : std::shared_ptr<HWRegister>(),
                fixed ? fixedCounterReg : std::shared_ptr<HWRegister>(),
                filter0 ? filterReg0 : std::shared_ptr<HWRegister>(),
                filter1 ? filterReg1 : std::shared_ptr<HWRegister>()
            )
        );
    }
}
#endif

void ServerPCICFGUncore::initPerf(uint32 socket_, const PCM * /*pcm*/)
{
#ifdef PCM_USE_PERF
    auto imcIDs = enumeratePerfPMUs("imc", 100);
    auto m2mIDs = enumeratePerfPMUs("m2m", 100);
    auto haIDs = enumeratePerfPMUs("ha", 100);
    auto numMemControllers = std::max(m2mIDs.size(), haIDs.size());
    for (size_t i = 0; i < numMemControllers; ++i)
    {
        const int channelsPerController = imcIDs.size() / numMemControllers;
        num_imc_channels.push_back(channelsPerController);
    }
    populatePerfPMUs(socket_, imcIDs, imcPMUs, true);
    populatePerfPMUs(socket_, m2mIDs, m2mPMUs, false);
    populatePerfPMUs(socket_, enumeratePerfPMUs("qpi", 100), xpiPMUs, false);
    populatePerfPMUs(socket_, enumeratePerfPMUs("upi", 100), xpiPMUs, false);
    populatePerfPMUs(socket_, enumeratePerfPMUs("m3upi", 100), m3upiPMUs, false);
    populatePerfPMUs(socket_, haIDs, haPMUs, false);
#endif
}

size_t ServerPCICFGUncore::getNumMCChannels(const uint32 controller) const
{
    if (controller < num_imc_channels.size())
    {
        return num_imc_channels[controller];
    }
    return 0;
}

ServerPCICFGUncore::~ServerPCICFGUncore()
{
}


void ServerPCICFGUncore::programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, const int rankA, const int rankB)
{
    PCM * pcm = PCM::getInstance();
    uint32 MCCntConfig[4] = {0,0,0,0};
    uint32 EDCCntConfig[4] = {0,0,0,0};
    if(rankA < 0 && rankB < 0)
    {
        auto setEvents2_3 = [&](const uint32 partial_write_event) {
            auto noPmem = [&pcm]() -> bool
            {
                if (pcm->PMMTrafficMetricsAvailable() == false)
                {
                    std::cerr << "PCM Error: PMM/Pmem metrics are not available on your platform\n";
                    return true;
                }
                return false;
            };
            switch (metrics)
            {
                case PmemMemoryMode:
                case PmemMixedMode:
                    if (noPmem()) return false;
                    MCCntConfig[EventPosition::PMM_MM_MISS_CLEAN] = MC_CH_PCI_PMON_CTL_EVENT(0xd3) + MC_CH_PCI_PMON_CTL_UMASK(2); // monitor TAGCHK.MISS_CLEAN on counter 2
                    MCCntConfig[EventPosition::PMM_MM_MISS_DIRTY] = MC_CH_PCI_PMON_CTL_EVENT(0xd3) + MC_CH_PCI_PMON_CTL_UMASK(4); // monitor TAGCHK.MISS_DIRTY on counter 3
                    break;
                case Pmem:
                    if (noPmem()) return false;
                    MCCntConfig[EventPosition::PMM_READ] = MC_CH_PCI_PMON_CTL_EVENT(0xe3);  // monitor PMM_RDQ_REQUESTS on counter 2
                    MCCntConfig[EventPosition::PMM_WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0xe7); // monitor PMM_WPQ_REQUESTS on counter 3
                    break;
                case PartialWrites:
                    MCCntConfig[EventPosition::PARTIAL] = partial_write_event;
                    break;
                default:
                    std::cerr << "PCM Error: unknown metrics: " << metrics << "\n";
                    return false;
            }
            return true;
        };
        switch(cpu_model)
        {
        case PCM::KNL:
            MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: CAS.RD
            MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(2);  // monitor reads on counter 1: CAS.WR
            EDCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x01) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: RPQ
            EDCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x02) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 1: WPQ
            break;
        case PCM::SNOWRIDGE:
        case PCM::ICX:
            if (metrics == PmemMemoryMode)
            {
                MCCntConfig[EventPosition::NM_HIT] = MC_CH_PCI_PMON_CTL_EVENT(0xd3) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: UNC_M_TAGCHK.HIT
            }
            else
            {
                MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(0x0f);  // monitor reads on counter 0: CAS_COUNT.RD
                MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(0x30); // monitor writes on counter 1: CAS_COUNT.WR
            }
            if (setEvents2_3(MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(0x0c)) == false) // monitor partial writes on counter 2: CAS_COUNT.RD_UNDERFILL
            {
                return;
            }
            break;
        default:
            MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(3);  // monitor reads on counter 0: CAS_COUNT.RD
            MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(12); // monitor writes on counter 1: CAS_COUNT.WR
            if (setEvents2_3(MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(2)) == false) // monitor partial writes on counter 2: CAS_COUNT.RD_UNDERFILL
            {
                return;
            }
        }
    } else {
        switch(cpu_model)
        {
        case PCM::IVYTOWN:
            MCCntConfig[EventPosition::READ_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // RD_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::WRITE_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // WR_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::READ_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // RD_CAS_RANK(rankB) all banks
            MCCntConfig[EventPosition::WRITE_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // WR_CAS_RANK(rankB) all banks
            break;
        case PCM::HASWELLX:
        case PCM::BDX_DE:
        case PCM::BDX:
        case PCM::SKX:
            MCCntConfig[EventPosition::READ_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(16); // RD_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::WRITE_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(16); // WR_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::READ_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(16); // RD_CAS_RANK(rankB) all banks
            MCCntConfig[EventPosition::WRITE_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(16); // WR_CAS_RANK(rankB) all banks
            break;
        case PCM::ICX:
        case PCM::SNOWRIDGE:
            MCCntConfig[EventPosition::READ_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0x28); // RD_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::WRITE_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0x28); // WR_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::READ_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0x28); // RD_CAS_RANK(rankB) all banks
            MCCntConfig[EventPosition::WRITE_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0x28); // WR_CAS_RANK(rankB) all banks
            break;
        case PCM::KNL:
            MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: CAS.RD
            MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(2);  // monitor reads on counter 1: CAS.WR
            EDCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x01) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: RPQ
            EDCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x02) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 1: WPQ
            break;
        default:
            std::cerr << "PCM Error: your processor " << pcm->getCPUBrandString() << " model " << cpu_model << " does not support the required performance events \n";
            return;
        }
    }
    programIMC(MCCntConfig);
    if(cpu_model == PCM::KNL) programEDC(EDCCntConfig);

    programM2M();

    xpiPMUs.clear(); // no QPI events used
    return;
}

void ServerPCICFGUncore::program()
{
    PCM * pcm = PCM::getInstance();
    uint32 MCCntConfig[4] = {0, 0, 0, 0};
    uint32 EDCCntConfig[4] = {0, 0, 0, 0};
    switch(cpu_model)
    {
    case PCM::KNL:
        MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: CAS_COUNT.RD
        MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(2); // monitor writes on counter 1: CAS_COUNT.WR
        EDCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x01) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: RPQ
        EDCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x02) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 1: WPQ
        break;
    case PCM::SNOWRIDGE:
    case PCM::ICX:
        MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(0x0f);  // monitor reads on counter 0: CAS_COUNT.RD
        MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(0x30); // monitor writes on counter 1: CAS_COUNT.WR
        break;
    default:
        MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(3);  // monitor reads on counter 0: CAS_COUNT.RD
        MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x04) + MC_CH_PCI_PMON_CTL_UMASK(12); // monitor writes on counter 1: CAS_COUNT.WR
    }

    if (pcm->PMMTrafficMetricsAvailable())
    {
        MCCntConfig[EventPosition::PMM_READ] = MC_CH_PCI_PMON_CTL_EVENT(0xe3); // monitor PMM_RDQ_REQUESTS on counter 2
        MCCntConfig[EventPosition::PMM_WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0xe7); // monitor PMM_WPQ_REQUESTS on counter 3
    }

    programIMC(MCCntConfig);
    if(cpu_model == PCM::KNL) programEDC(EDCCntConfig);

    programM2M();

    uint32 event[4];
    if (PCM::hasUPI(cpu_model))
    {
        // monitor TxL0_POWER_CYCLES
        event[0] = Q_P_PCI_PMON_CTL_EVENT(0x26);
        // monitor RxL_FLITS.ALL_DATA on counter 1
        event[1] = Q_P_PCI_PMON_CTL_EVENT(0x03) + Q_P_PCI_PMON_CTL_UMASK(0xF);
        // monitor TxL_FLITS.NON_DATA+ALL_DATA on counter 2
        event[2] = Q_P_PCI_PMON_CTL_EVENT(0x02) + Q_P_PCI_PMON_CTL_UMASK((0x97|0x0F));
        // monitor UPI CLOCKTICKS
        event[ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS] = Q_P_PCI_PMON_CTL_EVENT(0x01);
    }
    else
    {
        // monitor DRS data received on counter 0: RxL_FLITS_G1.DRS_DATA
        event[0] = Q_P_PCI_PMON_CTL_EVENT(0x02) + Q_P_PCI_PMON_CTL_EVENT_EXT + Q_P_PCI_PMON_CTL_UMASK(8);
        // monitor NCB data received on counter 1: RxL_FLITS_G2.NCB_DATA
        event[1] = Q_P_PCI_PMON_CTL_EVENT(0x03) + Q_P_PCI_PMON_CTL_EVENT_EXT + Q_P_PCI_PMON_CTL_UMASK(4);
        // monitor outgoing data+nondata flits on counter 2: TxL_FLITS_G0.DATA + TxL_FLITS_G0.NON_DATA
        event[2] = Q_P_PCI_PMON_CTL_EVENT(0x00) + Q_P_PCI_PMON_CTL_UMASK(6);
        // monitor QPI clocks
        event[ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS] = Q_P_PCI_PMON_CTL_EVENT(0x14); // QPI clocks (CLOCKTICKS)
    }
    programXPI(event);
    programHA();
}

void ServerPCICFGUncore::programXPI(const uint32 * event)
{
    const uint32 extra = PCM::hasUPI(cpu_model) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN;
    for (uint32 i = 0; i < (uint32)xpiPMUs.size(); ++i)
    {
        // QPI LL PMU

        if (xpiPMUs[i].initFreeze(extra,
            "       Please see BIOS options to enable the export of QPI/UPI performance monitoring devices (devices 8 and 9: function 2).\n")
            == false)
        {
            std::cout << "Link " << (i + 1) << " is disabled\n";
            continue;
        }

        PCM::program(xpiPMUs[i], event, event + 4, extra);
    }
    cleanupQPIHandles();
}

void ServerPCICFGUncore::cleanupQPIHandles()
{
    for(auto i = xpiPMUs.begin(); i != xpiPMUs.end(); ++i)
    {
        if (!i->valid())
        {
            xpiPMUs.erase(i);
            cleanupQPIHandles();
            return;
        }
    }
}

void ServerPCICFGUncore::cleanupPMUs()
{
    for (auto & pmu : xpiPMUs)
    {
        pmu.cleanup();
    }
    for (auto & pmu : imcPMUs)
    {
        pmu.cleanup();
    }
    for (auto & pmu : edcPMUs)
    {
        pmu.cleanup();
    }
    for (auto & pmu : m2mPMUs)
    {
        pmu.cleanup();
    }
    for (auto & pmu : haPMUs)
    {
        pmu.cleanup();
    }
}

uint64 ServerPCICFGUncore::getImcReads()
{
    return getImcReadsForChannels((uint32)0, (uint32)imcPMUs.size());
}

uint64 ServerPCICFGUncore::getImcReadsForController(uint32 controller)
{
    assert(controller < num_imc_channels.size());
    uint32 beginChannel = 0;
    for (uint32 i = 0; i < controller; ++i)
    {
        beginChannel += num_imc_channels[i];
    }
    const uint32 endChannel = beginChannel + num_imc_channels[controller];
    return getImcReadsForChannels(beginChannel, endChannel);
}

uint64 ServerPCICFGUncore::getImcReadsForChannels(uint32 beginChannel, uint32 endChannel)
{
    uint64 result = 0;
    for (uint32 i = beginChannel; i < endChannel && i < imcPMUs.size(); ++i)
    {
        result += getMCCounter(i, EventPosition::READ);
    }
    return result;
}

uint64 ServerPCICFGUncore::getImcWrites()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)imcPMUs.size(); ++i)
    {
        result += getMCCounter(i, EventPosition::WRITE);
    }

    return result;
}

uint64 ServerPCICFGUncore::getPMMReads()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::PMM_READ);
    }
    return result;
}

uint64 ServerPCICFGUncore::getPMMWrites()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::PMM_WRITE);
    }
    return result;
}

uint64 ServerPCICFGUncore::getEdcReads()
{
    uint64 result = 0;

    for (auto & pmu: edcPMUs)
    {
        result += *pmu.counterValue[EventPosition::READ];
    }

    return result;
}

uint64 ServerPCICFGUncore::getEdcWrites()
{
    uint64 result = 0;

    for (auto & pmu : edcPMUs)
    {
        result += *pmu.counterValue[EventPosition::WRITE];
    }

    return result;
}

uint64 ServerPCICFGUncore::getIncomingDataFlits(uint32 port)
{
    uint64 drs = 0, ncb = 0;

    if (port >= (uint32)xpiPMUs.size())
        return 0;

    if (PCM::hasUPI(cpu_model) == false)
    {
        drs = *xpiPMUs[port].counterValue[0];
    }
    ncb = *xpiPMUs[port].counterValue[1];

    return drs + ncb;
}

uint64 ServerPCICFGUncore::getOutgoingFlits(uint32 port)
{
    return getQPILLCounter(port,2);
}

uint64 ServerPCICFGUncore::getUPIL0TxCycles(uint32 port)
{
    if (PCM::hasUPI(cpu_model))
        return getQPILLCounter(port,0);
    return 0;
}

void ServerPCICFGUncore::program_power_metrics(int mc_profile)
{
    uint32 xPIEvents[4] = { 0,0,0,0 };
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_TxL0P_POWER_CYCLES] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_model) ? 0x27 : 0x0D)); // L0p Tx Cycles (TxL0P_POWER_CYCLES)
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_L1_POWER_CYCLES] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_model) ? 0x21 : 0x12)); // L1 Cycles (L1_POWER_CYCLES)
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_model) ? 0x01 : 0x14)); // QPI/UPI clocks (CLOCKTICKS)

    programXPI(xPIEvents);

    uint32 MCCntConfig[4] = {0,0,0,0};
    unsigned int UNC_M_POWER_CKE_CYCLES = 0x83;
    if (cpu_model == PCM::ICX || cpu_model == PCM::SNOWRIDGE)
    {
        UNC_M_POWER_CKE_CYCLES = 0x47;
    }
    switch(mc_profile)
    {
        case 0: // POWER_CKE_CYCLES.RANK0 and POWER_CKE_CYCLES.RANK1
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(1) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(1) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(2) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(2) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            break;
        case  1: // POWER_CKE_CYCLES.RANK2 and POWER_CKE_CYCLES.RANK3
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(4) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(4) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(8) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(8) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            break;
        case 2: // POWER_CKE_CYCLES.RANK4 and POWER_CKE_CYCLES.RANK5
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x10) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x10) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x20) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x20) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            break;
        case 3: // POWER_CKE_CYCLES.RANK6 and POWER_CKE_CYCLES.RANK7
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x40) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x40) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x80) + MC_CH_PCI_PMON_CTL_INVERT + MC_CH_PCI_PMON_CTL_THRESH(1);
            MCCntConfig[3] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CKE_CYCLES) + MC_CH_PCI_PMON_CTL_UMASK(0x80) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
           break;
        case 4: // POWER_SELF_REFRESH
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(0x43);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(0x43) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(0x85);
            break;
    }

    programIMC(MCCntConfig);
}

void ServerPCICFGUncore::programIMC(const uint32 * MCCntConfig)
{
    const uint32 extraIMC = (cpu_model == PCM::SKX)?UNC_PMON_UNIT_CTL_RSV:UNC_PMON_UNIT_CTL_FRZ_EN;

    for (uint32 i = 0; i < (uint32)imcPMUs.size(); ++i)
    {
        // imc PMU
        imcPMUs[i].initFreeze(extraIMC);

        // enable fixed counter (DRAM clocks)
        *imcPMUs[i].fixedCounterControl = MC_CH_PCI_PMON_FIXED_CTL_EN;

        // reset it
        *imcPMUs[i].fixedCounterControl = MC_CH_PCI_PMON_FIXED_CTL_EN + MC_CH_PCI_PMON_FIXED_CTL_RST;

        PCM::program(imcPMUs[i], MCCntConfig, MCCntConfig + 4, extraIMC);
    }
}

void ServerPCICFGUncore::programEDC(const uint32 * EDCCntConfig)
{
    for (uint32 i = 0; i < (uint32)edcPMUs.size(); ++i)
    {
        edcPMUs[i].initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        // MCDRAM clocks enabled by default
        *edcPMUs[i].fixedCounterControl = EDC_CH_PCI_PMON_FIXED_CTL_EN;

        PCM::program(edcPMUs[i], EDCCntConfig, EDCCntConfig + 4, UNC_PMON_UNIT_CTL_FRZ_EN);
    }
}

void ServerPCICFGUncore::programM2M()
{
    uint64 cfg[4] = {0, 0, 0, 0};
    switch (cpu_model)
    {
    case PCM::ICX:
        cfg[EventPosition::NM_HIT] = M2M_PCI_PMON_CTL_EVENT(0x2c) + M2M_PCI_PMON_CTL_UMASK(3);    // UNC_M2M_TAG_HIT.NM_DRD_HIT_* events (CLEAN | DIRTY)
        cfg[EventPosition::M2M_CLOCKTICKS] = 0;                                                      // CLOCKTICKS
        cfg[EventPosition::PMM_READ] = M2M_PCI_PMON_CTL_EVENT(0x37) + M2M_PCI_PMON_CTL_UMASK(0x20) + UNC_PMON_CTL_UMASK_EXT(0x07);  // UNC_M2M_IMC_READS.TO_PMM
        cfg[EventPosition::PMM_WRITE] = M2M_PCI_PMON_CTL_EVENT(0x38) + M2M_PCI_PMON_CTL_UMASK(0x80) + UNC_PMON_CTL_UMASK_EXT(0x1C); // UNC_M2M_IMC_WRITES.TO_PMM
        break;
    default:
        cfg[EventPosition::NM_HIT] = M2M_PCI_PMON_CTL_EVENT(0x2c) + M2M_PCI_PMON_CTL_UMASK(3);    // UNC_M2M_TAG_HIT.NM_DRD_HIT_* events (CLEAN | DIRTY)
        cfg[EventPosition::M2M_CLOCKTICKS] = 0;                                                      // CLOCKTICKS
        cfg[EventPosition::PMM_READ] = M2M_PCI_PMON_CTL_EVENT(0x37) + M2M_PCI_PMON_CTL_UMASK(0x8);  // UNC_M2M_IMC_READS.TO_PMM
        cfg[EventPosition::PMM_WRITE] = M2M_PCI_PMON_CTL_EVENT(0x38) + M2M_PCI_PMON_CTL_UMASK(0x20); // UNC_M2M_IMC_WRITES.TO_PMM
    }
    programM2M(cfg);
}

void ServerPCICFGUncore::programM2M(const uint64* M2MCntConfig)
{
    {
        for (auto & pmu : m2mPMUs)
        {
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
            PCM::program(pmu, M2MCntConfig, M2MCntConfig + 4, UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void ServerPCICFGUncore::programM3UPI(const uint32* M3UPICntConfig)
{
    {
        for (auto& pmu : m3upiPMUs)
        {
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
            PCM::program(pmu, M3UPICntConfig, M3UPICntConfig + 4, UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void ServerPCICFGUncore::programHA(const uint32 * config)
{
    for (auto & pmu : haPMUs)
    {
        pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
        PCM::program(pmu, config, config + 4, UNC_PMON_UNIT_CTL_RSV);
    }
}

uint64 ServerPCICFGUncore::getHARequests()
{
    uint64 result = 0;
    for (auto & pmu: haPMUs)
    {
        result += *pmu.counterValue[PCM::EventPosition::REQUESTS_ALL];
    }
    return result;
}

uint64 ServerPCICFGUncore::getHALocalRequests()
{
    uint64 result = 0;
    for (auto & pmu: haPMUs)
    {
        result += *pmu.counterValue[PCM::EventPosition::REQUESTS_LOCAL];
    }
    return result;
}

void ServerPCICFGUncore::programHA()
{
	uint32 config[4];
	config[0] = 0;
	config[1] = 0;
#ifdef PCM_HA_REQUESTS_READS_ONLY
	// HA REQUESTS READ: LOCAL + REMOTE
	config[PCM::EventPosition::REQUESTS_ALL] = HA_PCI_PMON_CTL_EVENT(0x01) + HA_PCI_PMON_CTL_UMASK((1 + 2));
	// HA REQUESTS READ: LOCAL ONLY
	config[PCM::EventPosition::REQUESTS_LOCAL] = HA_PCI_PMON_CTL_EVENT(0x01) + HA_PCI_PMON_CTL_UMASK((1));
#else
	// HA REQUESTS READ+WRITE+REMOTE+LOCAL
	config[PCM::EventPosition::REQUESTS_ALL] = HA_PCI_PMON_CTL_EVENT(0x01) + HA_PCI_PMON_CTL_UMASK((1 + 2 + 4 + 8));
	// HA REQUESTS READ+WRITE (LOCAL only)
	config[PCM::EventPosition::REQUESTS_LOCAL] = HA_PCI_PMON_CTL_EVENT(0x01) + HA_PCI_PMON_CTL_UMASK((1 + 4));
#endif
	programHA(config);
}

void ServerPCICFGUncore::freezeCounters()
{
    writeAllUnitControl(UNC_PMON_UNIT_CTL_FRZ + ((cpu_model == PCM::SKX) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN));
}

void ServerPCICFGUncore::writeAllUnitControl(const uint32 value)
{
    for (auto& pmuVector : allPMUs)
    {
        for (auto& pmu : *pmuVector)
        {
            pmu.writeUnitControl(value);
        }
    }
}

void ServerPCICFGUncore::unfreezeCounters()
{
    writeAllUnitControl((cpu_model == PCM::SKX) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN);
}

uint64 ServerPCICFGUncore::getQPIClocks(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS);
}

uint64 ServerPCICFGUncore::getQPIL0pTxCycles(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_TxL0P_POWER_CYCLES);
}

uint64 ServerPCICFGUncore::getQPIL1Cycles(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_L1_POWER_CYCLES);
}

uint64 ServerPCICFGUncore::getDRAMClocks(uint32 channel)
{
    uint64 result = 0;

    if (channel < (uint32)imcPMUs.size())
        result = *(imcPMUs[channel].fixedCounterValue);

    // std::cout << "DEBUG: DRAMClocks on channel " << channel << " = " << result << "\n";
    return result;
}

uint64 ServerPCICFGUncore::getMCDRAMClocks(uint32 channel)
{
    uint64 result = 0;

    if (channel < (uint32)edcPMUs.size())
        result = *edcPMUs[channel].fixedCounterValue;

    // std::cout << "DEBUG: MCDRAMClocks on EDC" << channel << " = " << result << "\n";
    return result;
}

uint64 ServerPCICFGUncore::getPMUCounter(std::vector<UncorePMU> & pmu, const uint32 id, const uint32 counter)
{
    uint64 result = 0;

    if (id < (uint32)pmu.size() && counter < 4 && pmu[id].counterValue[counter].get() != nullptr)
    {
        result = *(pmu[id].counterValue[counter]);
    }
    else
    {
        //std::cout << "DEBUG: Invalid ServerPCICFGUncore::getPMUCounter(" << id << ", " << counter << ") \n";
    }
    // std::cout << "DEBUG: ServerPCICFGUncore::getPMUCounter(" << id << ", " << counter << ") = " << result << "\n";
    return result;
}

uint64 ServerPCICFGUncore::getMCCounter(uint32 channel, uint32 counter)
{
    return getPMUCounter(imcPMUs, channel, counter);
}

uint64 ServerPCICFGUncore::getEDCCounter(uint32 channel, uint32 counter)
{
    return getPMUCounter(edcPMUs, channel, counter);
}

uint64 ServerPCICFGUncore::getM2MCounter(uint32 box, uint32 counter)
{
    return getPMUCounter(m2mPMUs, box, counter);
}

uint64 ServerPCICFGUncore::getQPILLCounter(uint32 port, uint32 counter)
{
    return getPMUCounter(xpiPMUs, port, counter);
}

uint64 ServerPCICFGUncore::getM3UPICounter(uint32 port, uint32 counter)
{
    // std::cout << "DEBUG: ServerPCICFGUncore::getM3UPICounter(" << port << ", " << counter << ") = " << getPMUCounter(m3upiPMUs, port, counter) << "\n";
    return getPMUCounter(m3upiPMUs, port, counter);
}

void ServerPCICFGUncore::enableJKTWorkaround(bool enable)
{
    {
        PciHandleType reg(groupnr,iMCbus,14,0);
        uint32 value = 0;
        reg.read32(0x84, &value);
        if(enable)
            value |= 2;
        else
            value &= (~2);
        reg.write32(0x84, value);
    }
    {
        PciHandleType reg(groupnr,iMCbus,8,0);
        uint32 value = 0;
        reg.read32(0x80, &value);
        if(enable)
            value |= 2;
        else
            value &= (~2);
        reg.write32(0x80, value);
    }
    {
        PciHandleType reg(groupnr,iMCbus,9,0);
        uint32 value = 0;
        reg.read32(0x80, &value);
        if(enable)
            value |= 2;
        else
            value &= (~2);
        reg.write32(0x80, value);
    }
}

#define PCM_MEM_CAPACITY (1024ULL*1024ULL*64ULL) // 64 MByte

void ServerPCICFGUncore::initMemTest(ServerPCICFGUncore::MemTestParam & param)
{
    auto & memBufferBlockSize = param.first;
    auto & memBuffers = param.second;
#ifdef __linux__
    size_t capacity = PCM_MEM_CAPACITY;
    char * buffer = (char *)mmap(NULL, capacity, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (buffer == MAP_FAILED) {
        std::cerr << "ERROR: mmap failed\n";
        return;
    }
    unsigned long long maxNode = (unsigned long long)(readMaxFromSysFS("/sys/devices/system/node/online") + 1);
    if (maxNode == 0)
    {
        std::cerr << "ERROR: max node is 0 \n";
        return;
    }
    if (maxNode >= 63) maxNode = 63;
    const unsigned long long nodeMask = (1ULL << maxNode) - 1ULL;
    if (0 != syscall(SYS_mbind, buffer, capacity, 3 /* MPOL_INTERLEAVE */,
        &nodeMask, maxNode, 0))
    {
        std::cerr << "ERROR: mbind failed. nodeMask: " << nodeMask << " maxNode: " << maxNode << "\n";
        return;
    }
    memBuffers.push_back((uint64 *)buffer);
    memBufferBlockSize = capacity;
#elif defined(_MSC_VER)
    ULONG HighestNodeNumber;
    if (!GetNumaHighestNodeNumber(&HighestNodeNumber))
    {
        std::cerr << "ERROR: GetNumaHighestNodeNumber call failed.\n";
        return;
    }
    memBufferBlockSize = 4096;
    for (int i = 0; i < PCM_MEM_CAPACITY / memBufferBlockSize; ++i)
    {
        LPVOID result = VirtualAllocExNuma(
            GetCurrentProcess(),
            NULL,
            memBufferBlockSize,
            MEM_RESERVE | MEM_COMMIT,
            PAGE_READWRITE,
            i % (HighestNodeNumber + 1)
        );

        if (result == NULL)
        {
            std::cerr << "ERROR: " << i << " VirtualAllocExNuma failed.\n";
            for (auto b : memBuffers)
            {
                VirtualFree(b, memBufferBlockSize, MEM_RELEASE);
            }
            memBuffers.clear();
            break;
        }
        else
        {
            memBuffers.push_back((uint64 *)result);
        }
    }
    #else
    std::cerr << "ERROR: memory test is not implemented. QPI/UPI speed and utilization metrics may not be reliable.\n";
    #endif
    for (auto b : memBuffers)
        std::fill(b, b + (memBufferBlockSize / sizeof(uint64)), 0ULL);
}

void ServerPCICFGUncore::doMemTest(const ServerPCICFGUncore::MemTestParam & param)
{
    const auto & memBufferBlockSize = param.first;
    const auto & memBuffers = param.second;
    // read and write each cache line once
    for (auto b : memBuffers)
        for (unsigned int i = 0; i < memBufferBlockSize / sizeof(uint64); i += 64 / sizeof(uint64))
        {
            (b[i])++;
        }
}

void ServerPCICFGUncore::cleanupMemTest(const ServerPCICFGUncore::MemTestParam & param)
{
    const auto & memBufferBlockSize = param.first;
    const auto & memBuffers = param.second;
    for (auto b : memBuffers)
    {
#if defined(__linux__)
        munmap(b, memBufferBlockSize);
#elif defined(_MSC_VER)
        VirtualFree(b, memBufferBlockSize, MEM_RELEASE);
#elif defined(__FreeBSD__)
        (void) b;                  // avoid the unused variable warning
        (void) memBufferBlockSize; // avoid the unused variable warning
#else
#endif
    }
}

uint64 ServerPCICFGUncore::computeQPISpeed(const uint32 core_nr, const int cpumodel)
{
    if(qpi_speed.empty())
    {
        PCM * pcm = PCM::getInstance();
        TemporalThreadAffinity aff(core_nr);
        qpi_speed.resize(getNumQPIPorts());

        auto getSpeed = [&] (size_t i) {
           if (i == 1) return 0ULL; // link 1 should have the same speed as link 0, skip it
           uint64 result = 0;
           if (PCM::hasUPI(cpumodel) == false && i < XPIRegisterLocation.size())
           {
               PciHandleType reg(groupnr,UPIbus, XPIRegisterLocation[i].first, QPI_PORT0_MISC_REGISTER_FUNC_ADDR);
               uint32 value = 0;
               reg.read32(QPI_RATE_STATUS_ADDR, &value);
               value &= 7; // extract lower 3 bits
               if(value) result = static_cast<uint64>((4000000000ULL + ((uint64)value)*800000000ULL)*2ULL);
           }
           if(result == 0ULL)
           {
               if (PCM::hasUPI(cpumodel) == false)
                   std::cerr << "Warning: QPI_RATE_STATUS register is not available on port " << i << ". Computing QPI speed using a measurement loop.\n";

               // compute qpi speed
               const uint64 timerGranularity = 1000000ULL; // mks

               MemTestParam param;
               initMemTest(param);
               uint64 startClocks = getQPIClocks((uint32)i);
               uint64 startTSC = pcm->getTickCount(timerGranularity, core_nr);
               uint64 endTSC;
               do
               {
                    doMemTest(param);
                    endTSC = pcm->getTickCount(timerGranularity, core_nr);
               } while (endTSC - startTSC < 200000ULL); // spin for 200 ms

               uint64 endClocks = getQPIClocks((uint32)i);
               cleanupMemTest(param);

               result = (uint64(double(endClocks - startClocks) * PCM::getBytesPerLinkCycle(cpumodel) * double(timerGranularity) / double(endTSC - startTSC)));
               if(cpumodel == PCM::HASWELLX || cpumodel == PCM::BDX) /* BDX_DE does not have QPI. */{
                  result /=2; // HSX runs QPI clocks with doubled speed
               }
           }
           return result;
         };
         std::vector<std::future<uint64> > getSpeedsAsync;
         for (size_t i = 0; i < getNumQPIPorts(); ++i) {
             getSpeedsAsync.push_back(std::async(std::launch::async, getSpeed, i));
         }
         for (size_t i = 0; i < getNumQPIPorts(); ++i) {
             qpi_speed[i] = (i==1)? qpi_speed[0] : getSpeedsAsync[i].get(); // link 1 does not have own speed register, it runs with the speed of link 0
         }
         if (PCM::hasUPI(cpumodel))
         {
             // check the speed of link 3
             if(qpi_speed.size() == 3 && qpi_speed[2] == 0)
             {
                std::cerr << "UPI link 3 is disabled\n";
                qpi_speed.resize(2);
                xpiPMUs.resize(2);
             }
         }
    }
    if(!qpi_speed.empty())
    {
        return *std::max_element(qpi_speed.begin(),qpi_speed.end());
    }
    else
    {
        return 0;
    }
}

void ServerPCICFGUncore::reportQPISpeed() const
{
    PCM * m = PCM::getInstance();
    std::cerr.precision(1);
    std::cerr << std::fixed;
    for (uint32 i = 0; i < (uint32)qpi_speed.size(); ++i)
        std::cerr << "Max QPI link " << i << " speed: " << qpi_speed[i] / (1e9) << " GBytes/second (" << qpi_speed[i] / (1e9 * m->getBytesPerLinkTransfer()) << " GT/second)\n";
}

uint64 PCM::CX_MSR_PMON_CTRY(uint32 Cbo, uint32 Ctr) const
{
    if(JAKETOWN == cpu_model || IVYTOWN == cpu_model)
    {
        return JKT_C0_MSR_PMON_CTR0 + ((JKTIVT_CBO_MSR_STEP)*Cbo) + Ctr;

    } else if(HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model)
    {
        return HSX_C0_MSR_PMON_CTR0 + ((HSX_CBO_MSR_STEP)*Cbo) + Ctr;
    }
    else if (ICX == cpu_model || SNOWRIDGE == cpu_model)
    {
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_CTR0_OFFSET + Ctr;
    }
    return 0;
}

uint64 PCM::CX_MSR_PMON_BOX_FILTER(uint32 Cbo) const
{
    if(JAKETOWN == cpu_model || IVYTOWN == cpu_model)
    {
        return JKT_C0_MSR_PMON_BOX_FILTER + ((JKTIVT_CBO_MSR_STEP)*Cbo);

    } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model)
    {
        return HSX_C0_MSR_PMON_BOX_FILTER + ((HSX_CBO_MSR_STEP)*Cbo);
    } else if (KNL == cpu_model)
    {
        return KNL_CHA0_MSR_PMON_BOX_CTL + ((KNL_CHA_MSR_STEP)*Cbo);
    }
    else if (ICX == cpu_model)
    {
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_BOX_FILTER_OFFSET;
    }

    return 0;
}

uint64 PCM::CX_MSR_PMON_BOX_FILTER1(uint32 Cbo) const
{
    if(IVYTOWN == cpu_model)
    {
        return IVT_C0_MSR_PMON_BOX_FILTER1 + ((JKTIVT_CBO_MSR_STEP)*Cbo);

    } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model)
    {
        return HSX_C0_MSR_PMON_BOX_FILTER1 + ((HSX_CBO_MSR_STEP)*Cbo);
    }
    return 0;
}

uint64 PCM::CX_MSR_PMON_CTLY(uint32 Cbo, uint32 Ctl) const
{
    if(JAKETOWN == cpu_model || IVYTOWN == cpu_model)
    {
        return JKT_C0_MSR_PMON_CTL0 + ((JKTIVT_CBO_MSR_STEP)*Cbo) + Ctl;

    } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model)
    {
        return HSX_C0_MSR_PMON_CTL0 + ((HSX_CBO_MSR_STEP)*Cbo) + Ctl;
    }
    else if (ICX == cpu_model || SNOWRIDGE == cpu_model)
    {
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_CTL0_OFFSET + Ctl;
    }
    return 0;
}

uint64 PCM::CX_MSR_PMON_BOX_CTL(uint32 Cbo) const
{
    if(JAKETOWN == cpu_model || IVYTOWN == cpu_model)
    {
        return JKT_C0_MSR_PMON_BOX_CTL + ((JKTIVT_CBO_MSR_STEP)*Cbo);

    } else if (HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model)
    {
        return HSX_C0_MSR_PMON_BOX_CTL + ((HSX_CBO_MSR_STEP)*Cbo);
    } else if (KNL == cpu_model)
    {
        return KNL_CHA0_MSR_PMON_BOX_CTRL + ((KNL_CHA_MSR_STEP)*Cbo);
    }
    else if (ICX == cpu_model)
    {
        return ICX_CHA_MSR_PMON_BOX_CTL[Cbo];
    }
    else if (SNOWRIDGE == cpu_model)
    {
        return SNR_CHA_MSR_PMON_BOX_CTL[Cbo];
    }
    return 0;
}

uint32 PCM::getMaxNumOfCBoxes() const
{
    static int num = -1;
    if (num >= 0)
    {
        return (uint32)num;
    }
    if (KNL == cpu_model || SKX == cpu_model || ICX == cpu_model)
    {
        /*
         *  on KNL two physical cores share CHA.
         *  The number of CHAs in the processor is stored in bits 5:0
         *  of NCUPMONConfig [0x702] MSR.
         */
        uint64 val;
        uint32 refCore = socketRefCore[0];
        uint32 NCUPMONConfig = 0x702;
        MSR[refCore]->read(NCUPMONConfig, &val);
        num = (uint32)(val & 63);
    }
    else if (SNOWRIDGE == cpu_model)
    {
        num = (uint32)num_phys_cores_per_socket / 4;
    }
    else
    {
        /*
         *  on other supported CPUs there is one CBox per physical core.  This calculation will get us
         *  the number of physical cores per socket which is the expected
         *  value to be returned.
         */
        num = (uint32)num_phys_cores_per_socket;
    }
    return num;
}

uint32 PCM::getMaxNumOfIIOStacks() const
{
    if (iioPMUs.size() > 0)
    {
        return (uint32)iioPMUs[0].size();
    }
    return 0;
}

void PCM::programCboOpcodeFilter(const uint32 opc0, UncorePMU & pmu, const uint32 nc_, const uint32 opc1, const uint32 loc, const uint32 rem)
{
    if(JAKETOWN == cpu_model)
    {
        *pmu.filter[0] = JKT_CBO_MSR_PMON_BOX_FILTER_OPC(opc0);

    } else if(IVYTOWN == cpu_model || HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model)
    {
        *pmu.filter[1] = IVTHSX_CBO_MSR_PMON_BOX_FILTER1_OPC(opc0);
    } else if(SKX == cpu_model)
    {
        *pmu.filter[1] = SKX_CHA_MSR_PMON_BOX_FILTER1_OPC0(opc0) +
                SKX_CHA_MSR_PMON_BOX_FILTER1_OPC1(opc1) +
                (rem?SKX_CHA_MSR_PMON_BOX_FILTER1_REM(1):0ULL) +
                (loc?SKX_CHA_MSR_PMON_BOX_FILTER1_LOC(1):0ULL) +
                SKX_CHA_MSR_PMON_BOX_FILTER1_NM(1) +
                SKX_CHA_MSR_PMON_BOX_FILTER1_NOT_NM(1) +
                (nc_?SKX_CHA_MSR_PMON_BOX_FILTER1_NC(1):0ULL);
    }
    else
    {
        std::cerr << "ERROR: programCboOpcodeFilter function is not implemented for cpu model " << cpu_model << std::endl;
        throw std::exception();
    }
}

void PCM::programIIOCounters(uint64 rawEvents[4], int IIOStack)
{
    std::vector<int32> IIO_units;
    if (IIOStack == -1)
    {
        int stacks_count;
        switch (getCPUModel())
        {
        case PCM::ICX:
            stacks_count = ICX_IIO_STACK_COUNT;
            break;
        case PCM::SNOWRIDGE:
            stacks_count = SNR_IIO_STACK_COUNT;
            break;
        case PCM::SKX:
        default:
            stacks_count = SKX_IIO_STACK_COUNT;
            break;
        }
        IIO_units.reserve(stacks_count);
        for (int stack = 0; stack < stacks_count; ++stack) {
            IIO_units.push_back(stack);
        }
    }
    else
        IIO_units.push_back(IIOStack);

    for (int32 i = 0; (i < num_sockets) && MSR.size() && iioPMUs.size(); ++i)
    {
        uint32 refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        for (const auto & unit: IIO_units)
        {
            if (iioPMUs[i].count(unit) == 0)
            {
                std::cerr << "IIO PMU unit (stack) " << unit << " is not found \n";
                continue;
            }
            auto & pmu = iioPMUs[i][unit];
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);

            program(pmu, &rawEvents[0], &rawEvents[4], UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void PCM::programPCIeEventGroup(eventGroup_t &eventGroup)
{
    assert(eventGroup.size() > 0);
    uint64 events[4] = {0};
    uint64 umask[4] = {0};

    switch (cpu_model)
    {
        case PCM::ICX:
        case PCM::SNOWRIDGE:
            for (uint32 idx = 0; idx < eventGroup.size(); ++idx)
                events[idx] = eventGroup[idx];
            programCbo(events);
            break;
        case PCM::SKX:
        //JKT through СLX generations allow programming only one required event at a time.
            if (eventGroup[0] & SKX_CHA_MSR_PMON_BOX_FILTER1_NC(1))
                umask[0] |= (uint64)(SKX_CHA_TOR_INSERTS_UMASK_IRQ(1));
                else
                umask[0] |= (uint64)(SKX_CHA_TOR_INSERTS_UMASK_PRQ(1));

            if (eventGroup[0] & SKX_CHA_MSR_PMON_BOX_FILTER1_RSV(1))
                umask[0] |= (uint64)(SKX_CHA_TOR_INSERTS_UMASK_HIT(1));
                else
                umask[0] |= (uint64)(SKX_CHA_TOR_INSERTS_UMASK_MISS(1));

            events[0] += CBO_MSR_PMON_CTL_EVENT(0x35) + CBO_MSR_PMON_CTL_UMASK(umask[0]);
            programCbo(events, SKX_CHA_MSR_PMON_BOX_GET_OPC0(eventGroup[0]),
                                    SKX_CHA_MSR_PMON_BOX_GET_NC(eventGroup[0]));
            break;
        case PCM::BDX_DE:
        case PCM::BDX:
        case PCM::KNL:
        case PCM::HASWELLX:
        case PCM::IVYTOWN:
        case PCM::JAKETOWN:
            events[0] = CBO_MSR_PMON_CTL_EVENT(0x35);
            events[0] += BDX_CBO_MSR_PMON_BOX_GET_FLT(eventGroup[0]) ? CBO_MSR_PMON_CTL_UMASK(0x3) : CBO_MSR_PMON_CTL_UMASK(1);
            events[0] += BDX_CBO_MSR_PMON_BOX_GET_TID(eventGroup[0]) ? CBO_MSR_PMON_CTL_TID_EN : 0ULL;

            programCbo(events, BDX_CBO_MSR_PMON_BOX_GET_OPC0(eventGroup[0]),
                    0, BDX_CBO_MSR_PMON_BOX_GET_TID(eventGroup[0]) ? 0x3e : 0ULL);
            break;
    }
}

void PCM::programCbo(const uint64 * events, const uint32 opCode, const uint32 nc_, const uint32 llc_lookup_tid_filter, const uint32 loc, const uint32 rem)
{
    for (size_t i = 0; (i < cboPMUs.size()) && MSR.size(); ++i)
    {
        uint32 refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        for(uint32 cbo = 0; cbo < getMaxNumOfCBoxes(); ++cbo)
        {
            cboPMUs[i][cbo].initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

            if (ICX != cpu_model && SNOWRIDGE != cpu_model)
                programCboOpcodeFilter(opCode, cboPMUs[i][cbo], nc_, 0, loc, rem);

            if((HASWELLX == cpu_model || BDX_DE == cpu_model || BDX == cpu_model || SKX == cpu_model) && llc_lookup_tid_filter != 0)
                *cboPMUs[i][cbo].filter[0] = llc_lookup_tid_filter;

            PCM::program(cboPMUs[i][cbo], events, events + ServerUncoreCounterState::maxCounters, UNC_PMON_UNIT_CTL_FRZ_EN);

            for (int c = 0; c < ServerUncoreCounterState::maxCounters; ++c)
            {
                *cboPMUs[i][cbo].counterValue[c] = 0;
            }
        }
    }
}

void PCM::programCboRaw(const uint64* events, const uint64 filter0, const uint64 filter1)
{
    for (size_t i = 0; (i < cboPMUs.size()) && MSR.size(); ++i)
    {
        uint32 refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        for (uint32 cbo = 0; cbo < getMaxNumOfCBoxes(); ++cbo)
        {
            cboPMUs[i][cbo].initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

            if (cboPMUs[i][cbo].filter[0].get())
            {
                *cboPMUs[i][cbo].filter[0] = filter0;
            }

            if (cboPMUs[i][cbo].filter[1].get())
            {
                *cboPMUs[i][cbo].filter[1] = filter1;
            }

            PCM::program(cboPMUs[i][cbo], events, events + 4, UNC_PMON_UNIT_CTL_FRZ_EN);

            for (int c = 0; c < 4; ++c)
            {
                *cboPMUs[i][cbo].counterValue[c] = 0;
            }
        }
    }
}

void PCM::programUBOX(const uint64* events)
{
    for (size_t s = 0; (s < uboxPMUs.size()) && MSR.size(); ++s)
    {
        uint32 refCore = socketRefCore[s];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        *uboxPMUs[s].fixedCounterControl = UCLK_FIXED_CTL_EN;

        PCM::program(uboxPMUs[s], events, events + 2, 0);
    }
}

uint64 PCM::getCBOCounterState(const uint32 socket_, const uint32 ctr_)
{
    uint64 result = 0;

    const uint32 refCore = socketRefCore[socket_];
    TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

    for(auto & pmu: cboPMUs[socket_])
    {
        result += *pmu.counterValue[ctr_];
    }
    return result;
}

uint64 PCM::getUncoreClocks(const uint32 socket_)
{
    uint64 result = 0;
    if (socket_ < uboxPMUs.size())
    {
        result = *uboxPMUs[socket_].fixedCounterValue;
    }
    return result;
}

PCIeCounterState PCM::getPCIeCounterState(const uint32 socket_, const uint32 ctr_)
{
    PCIeCounterState result;
    result.data = getCBOCounterState(socket_, ctr_);
    return result;
}

uint64 PCM::getPCIeCounterData(const uint32 socket_, const uint32 ctr_)
{
    return getCBOCounterState(socket_, ctr_);
}

void PCM::initLLCReadMissLatencyEvents(uint64 * events, uint32 & opCode)
{
    if (LLCReadMissLatencyMetricsAvailable() == false)
    {
        return;
    }
    uint64 umask = 3ULL; // MISS_OPCODE
    switch (cpu_model)
    {
        case ICX:
        case SNOWRIDGE:
            umask = 1ULL;
            break;
        case SKX:
            umask = (uint64)(SKX_CHA_TOR_INSERTS_UMASK_IRQ(1)) + (uint64)(SKX_CHA_TOR_INSERTS_UMASK_MISS(1));
            break;
    }

    uint64 umask_ext = 0;
    switch (cpu_model)
    {
        case ICX:
            umask_ext = 0xC817FE;
            break;
        case SNOWRIDGE:
            umask_ext = 0xC827FE;
            break;
    }

    const uint64 all_umasks = CBO_MSR_PMON_CTL_UMASK(umask) + UNC_PMON_CTL_UMASK_EXT(umask_ext);
    events[EventPosition::TOR_OCCUPANCY] = CBO_MSR_PMON_CTL_EVENT(0x36) + all_umasks; // TOR_OCCUPANCY (must be on counter 0)
    events[EventPosition::TOR_INSERTS] = CBO_MSR_PMON_CTL_EVENT(0x35) + all_umasks; // TOR_INSERTS

    opCode = (SKX == cpu_model) ? 0x202 : 0x182;
}

void PCM::programCbo()
{
    uint64 events[ServerUncoreCounterState::maxCounters];
    std::fill(events, events + ServerUncoreCounterState::maxCounters, 0);
    uint32 opCode = 0;

    initLLCReadMissLatencyEvents(events, opCode);
    initCHARequestEvents(events);

    programCbo(events, opCode);

    programUBOX(nullptr);
}

void PCM::initCHARequestEvents(uint64 * config)
{
    if (localMemoryRequestRatioMetricAvailable() && hasCHA())
    {
#ifdef PCM_HA_REQUESTS_READS_ONLY
        // HA REQUESTS READ: LOCAL + REMOTE
        config[EventPosition::REQUESTS_ALL] = CBO_MSR_PMON_CTL_EVENT(0x50) + CBO_MSR_PMON_CTL_UMASK((1 + 2));
        // HA REQUESTS READ: LOCAL ONLY
        config[EventPosition::REQUESTS_LOCAL] = CBO_MSR_PMON_CTL_EVENT(0x50) + CBO_MSR_PMON_CTL_UMASK((1));
#else
        // HA REQUESTS READ+WRITE+REMOTE+LOCAL
        config[EventPosition::REQUESTS_ALL] = CBO_MSR_PMON_CTL_EVENT(0x50) + CBO_MSR_PMON_CTL_UMASK((1 + 2 + 4 + 8));
        // HA REQUESTS READ+WRITE (LOCAL only)
        config[EventPosition::REQUESTS_LOCAL] = CBO_MSR_PMON_CTL_EVENT(0x50) + CBO_MSR_PMON_CTL_UMASK((1 + 4));
#endif
    }
}

CounterWidthExtender::CounterWidthExtender(AbstractRawCounter * raw_counter_, uint64 counter_width_, uint32 watchdog_delay_ms_) : raw_counter(raw_counter_), counter_width(counter_width_), watchdog_delay_ms(watchdog_delay_ms_)
{
    last_raw_value = (*raw_counter)();
    extended_value = last_raw_value;
    //std::cout << "Initial Value " << extended_value << "\n";
    UpdateThread = new std::thread(
        [&]() {
        while (1)
        {
            MySleepMs(static_cast<int>(this->watchdog_delay_ms));
            /* uint64 dummy = */ this->read();
        }
    }
    );
}
CounterWidthExtender::~CounterWidthExtender()
{
    delete UpdateThread;
    if (raw_counter) delete raw_counter;
}

void UncorePMU::cleanup()
{
    for (int i = 0; i < 4; ++i)
    {
        if (counterControl[i].get()) *counterControl[i] = 0;
    }
    if (unitControl.get()) *unitControl = 0;
    if (fixedCounterControl.get()) *fixedCounterControl = 0;
}

void UncorePMU::freeze(const uint32 extra)
{
    *unitControl = extra + UNC_PMON_UNIT_CTL_FRZ;
}

void UncorePMU::unfreeze(const uint32 extra)
{
    *unitControl = extra;
}

bool UncorePMU::initFreeze(const uint32 extra, const char* xPICheckMsg)
{
    // freeze enable
    *unitControl = extra;
    if (xPICheckMsg)
    {
        if ((extra & UNC_PMON_UNIT_CTL_VALID_BITS_MASK) != ((*unitControl) & UNC_PMON_UNIT_CTL_VALID_BITS_MASK))
        {
            unitControl = nullptr;
            return false;
        }
    }
    // freeze
    *unitControl = extra + UNC_PMON_UNIT_CTL_FRZ;

#ifdef PCM_UNCORE_PMON_BOX_CHECK_STATUS
    const uint64 val = *unitControl;
    if ((val & UNC_PMON_UNIT_CTL_VALID_BITS_MASK) != (extra + UNC_PMON_UNIT_CTL_FRZ))
    {
        std::cerr << "ERROR: PMU counter programming seems not to work. PMON_BOX_CTL=0x" << std::hex << val << " needs to be =0x" << (UNC_PMON_UNIT_CTL_FRZ_EN + UNC_PMON_UNIT_CTL_FRZ) << "\n";
        if (xPICheckMsg)
        {
            std::cerr << xPICheckMsg;
        }
    }
#endif
    return true;
}

void UncorePMU::resetUnfreeze(const uint32 extra)
{
    // reset counter values
    *unitControl = extra + UNC_PMON_UNIT_CTL_FRZ + UNC_PMON_UNIT_CTL_RST_COUNTERS;

    // unfreeze counters
    *unitControl = extra;
}

IIOCounterState PCM::getIIOCounterState(int socket, int IIOStack, int counter)
{
    IIOCounterState result;
    result.data = 0;
    if (socket < (int)iioPMUs.size() && iioPMUs[socket].count(IIOStack) > 0)
    {
        result.data = *iioPMUs[socket][IIOStack].counterValue[counter];
    }
    return result;
}

void PCM::getIIOCounterStates(int socket, int IIOStack, IIOCounterState * result)
{
    uint32 refCore = socketRefCore[socket];
    TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

    for (int c = 0; c < 4; ++c) {
        result[c] = getIIOCounterState(socket, IIOStack, c);
    }
}

void PCM::setupCustomCoreEventsForNuma(PCM::ExtendedCustomCoreEventDescription& conf) const
{
    switch (this->getCPUModel())
    {
    case PCM::WESTMERE_EX:
        // OFFCORE_RESPONSE.ANY_REQUEST.LOCAL_DRAM:  Offcore requests satisfied by the local DRAM
        conf.OffcoreResponseMsrValue[0] = 0x40FF;
        // OFFCORE_RESPONSE.ANY_REQUEST.REMOTE_DRAM: Offcore requests satisfied by a remote DRAM
        conf.OffcoreResponseMsrValue[1] = 0x20FF;
        break;
    case PCM::JAKETOWN:
    case PCM::IVYTOWN:
        // OFFCORE_RESPONSE.*.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x780400000 | 0x08FFF;
        // OFFCORE_RESPONSE.*.REMOTE_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x7ff800000 | 0x08FFF;
        break;
    case PCM::HASWELLX:
        // OFFCORE_RESPONSE.*.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x600400000 | 0x08FFF;
        // OFFCORE_RESPONSE.*.REMOTE_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x63f800000 | 0x08FFF;
        break;
    case PCM::BDX:
        // OFFCORE_RESPONSE.ALL_REQUESTS.L3_MISS.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x0604008FFF;
        // OFFCORE_RESPONSE.ALL_REQUESTS.L3_MISS.REMOTE_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x067BC08FFF;
        break;
    case PCM::SKX:
        // OFFCORE_RESPONSE.ALL_REQUESTS.L3_MISS_LOCAL_DRAM.ANY_SNOOP
        conf.OffcoreResponseMsrValue[0] = 0x3FC0008FFF | (1 << 26);
        // OFFCORE_RESPONSE.ALL_REQUESTS.L3_MISS_REMOTE_(HOP0,HOP1,HOP2P)_DRAM.ANY_SNOOP
        conf.OffcoreResponseMsrValue[1] = 0x3FC0008FFF | (1 << 27) | (1 << 28) | (1 << 29);
        break;
    case PCM::ICX:
        std::cout << "INFO: Monitored accesses include demand + L2 cache prefetcher, code read and RFO.\n";
        // OCR.READS_TO_CORE.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x0104000477;
        // OCR.READS_TO_CORE.REMOTE_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x0730000477;
        break;
    default:
        throw UnsupportedProcessorException();
    }
}

} // namespace pcm
