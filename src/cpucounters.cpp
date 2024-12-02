// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2024, Intel Corporation
// written by Roman Dementiev
//            Otto Bruggeman
//            Thomas Willhalm
//            Pat Fay
//            Austen Ott
//            Jim Harris (FreeBSD)
//            and many others

/*!     \file cpucounters.cpp
        \brief The bulk of PCM implementation
  */

//#define PCM_TEST_FALLBACK_TO_ATOM

#include <stdio.h>
#include <assert.h>
#ifdef PCM_EXPORTS
// pcm-lib.h includes cpucounters.h
#include "windows\pcm-lib.h"
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
#include "windows/windriver.h"
#else
#include <pthread.h>
#if defined(__FreeBSD__) || (defined(__DragonFly__) && __DragonFly_version >= 400707)
#include <pthread_np.h>
#include <sys/_cpuset.h>
#include <sys/cpuset.h>
#endif
#include <errno.h>
#include <sys/time.h>
#ifdef __linux__
#include <sys/mman.h>
#include <dirent.h>
#include <sys/resource.h>
#endif
#endif

#include <string.h>
#include <limits>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <future>
#include <functional>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <system_error>

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

#ifdef _MSC_VER

void PCM_API restrictDriverAccess(LPCTSTR path)
{
    restrictDriverAccessNative(path);
}

HMODULE hOpenLibSys = NULL;

#ifndef NO_WINRING
bool PCM::initWinRing0Lib()
{
    const BOOL result = InitOpenLibSys(&hOpenLibSys);

    if (result == FALSE)
    {
        DeinitOpenLibSys(&hOpenLibSys);
        hOpenLibSys = NULL;
        return false;
    }

    BYTE major, minor, revision, release;
    GetDriverVersion(&major, &minor, &revision, &release);
    TCHAR buffer[128];
    _stprintf_s(buffer, 128, TEXT("\\\\.\\WinRing0_%d_%d_%d"),(int)major,(int)minor, (int)revision);
    restrictDriverAccess(buffer);

    return true;
}
#endif // NO_WINRING

#endif


#if defined(__FreeBSD__)
#define cpu_set_t cpuset_t
#endif

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

std::mutex instanceCreationMutex;

PCM * PCM::getInstance()
{
    // lock-free read
    // cppcheck-suppress identicalConditionAfterEarlyExit
    if (instance) return instance;

    std::unique_lock<std::mutex> _(instanceCreationMutex);
    // cppcheck-suppress identicalConditionAfterEarlyExit
    if (instance) return instance;

    return instance = new PCM();
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



#if defined(__FreeBSD__) || defined(__DragonFly__)
void pcm_cpuid_bsd(int leaf, PCM_CPUID_INFO& info, int core)
{
    cpuctl_cpuid_args_t cpuid_args_freebsd;
    char cpuctl_name[64];

    snprintf(cpuctl_name, 64, "/dev/cpuctl%d", core);
    auto fd = ::open(cpuctl_name, O_RDWR);

    cpuid_args_freebsd.level = leaf;

    ::ioctl(fd, CPUCTL_CPUID, &cpuid_args_freebsd);
    for (int i = 0; i < 4; ++i)
    {
        info.array[i] = cpuid_args_freebsd.data[i];
    }
    ::close(fd);
}
#endif

#ifdef __linux__
bool isNMIWatchdogEnabled(const bool silent);
bool keepNMIWatchdogEnabled();
#endif

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
        if (isNMIWatchdogEnabled(true) && keepNMIWatchdogEnabled())
        {
            --core_gen_counter_num_max;
            std::cerr << "INFO: Reducing the number of programmable counters to " << core_gen_counter_num_max  << " because NMI watchdog is enabled.\n";
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
    #ifdef PCM_USE_PERF
    if (perfEventTaskHandle.empty() == false)
    {
       return false; // per PID/task perf collection does not support HW TMA L1
    }
    #endif
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
        if (hybrid)
        {
            supported = 0;
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

/*
int PCM::getCPUModelFromCPUID()
{
    static int result = -1;
    if (result < 0)
    {
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(1, cpuinfo);
        result = (((cpuinfo.array[0]) & 0xf0) >> 4) | ((cpuinfo.array[0] & 0xf0000) >> 12);
    }
    return result;
}
*/

int PCM::getCPUFamilyModelFromCPUID()
{
    static int result = -1;
    if (result < 0)
    {
        PCM_CPUID_INFO cpuinfo;
        pcm_cpuid(1, cpuinfo);
        const auto cpu_family_ = (((cpuinfo.array[0]) >> 8) & 0xf) | ((cpuinfo.array[0] & 0xf00000) >> 16);
        const auto cpu_model_ = (((cpuinfo.array[0]) & 0xf0) >> 4) | ((cpuinfo.array[0] & 0xf0000) >> 12);
        result = PCM_CPU_FAMILY_MODEL(cpu_family_, cpu_model_);
    }
    return result;
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
    std::fill(buffer, buffer + 1024, 0);
    std::fill(buf.cbuf, buf.cbuf + 16, 0);
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
    cpu_model_private = (((cpuinfo.array[0]) & 0xf0) >> 4) | ((cpuinfo.array[0] & 0xf0000) >> 12);
    cpu_family_model = PCM_CPU_FAMILY_MODEL(cpu_family, cpu_model_private);
    cpu_stepping = cpuinfo.array[0] & 0x0f;

    if (cpuinfo.reg.ecx & (1UL << 31UL)) {
        vm = true;
        std::cerr << "Detected a hypervisor/virtualization technology. Some metrics might not be available due to configuration or availability of virtual hardware features.\n";
    }

    readCoreCounterConfig();

    pcm_cpuid(7, 0, cpuinfo);

    std::cerr << "\n=====  Processor information  =====\n";

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
                    for (const auto & curFlag : split(tokens[1], ' '))
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
    hybrid = (cpuinfo.reg.edx & (1 << 15)) ? true : false;
    std::cerr << "Hybrid processor         : " << (hybrid ? "yes" : "no") << "\n";
    std::cerr << "IBRS and IBPB supported  : " << ((cpuinfo.reg.edx & (1 << 26)) ? "yes" : "no") << "\n";
    std::cerr << "STIBP supported          : " << ((cpuinfo.reg.edx & (1 << 27)) ? "yes" : "no") << "\n";
    std::cerr << "Spec arch caps supported : " << ((cpuinfo.reg.edx & (1 << 29)) ? "yes" : "no") << "\n";
    std::cerr << "Max CPUID level          : " << max_cpuid << "\n";
    std::cerr << "CPU family               : " << cpu_family << "\n";
    std::cerr << "CPU model number         : " << cpu_model_private << "\n";

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
        freeAndNullify(env);
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

bool isMBMEnforced()
{
    static int flag = -1;
    if (flag < 0)
    {
        // flag not yet initialized
        flag = pcm::safe_getenv("PCM_ENFORCE_MBM") == std::string("1") ? 1 : 0;
    }
    return flag > 0;
}

bool PCM::CoreLocalMemoryBWMetricAvailable() const
{
    if (isMBMEnforced() == false && cpu_family_model == SKX && cpu_stepping < 5) return false; // SKZ4 errata
    PCM_CPUID_INFO cpuinfo;
    if (!(QOSMetricAvailable() && L3QOSMetricAvailable()))
            return false;
    pcm_cpuid(0xf,0x1,cpuinfo);
    return (cpuinfo.reg.edx & 2)?true:false;
}

bool PCM::CoreRemoteMemoryBWMetricAvailable() const
{
    if (isMBMEnforced() == false && cpu_family_model == SKX && cpu_stepping < 5) return false; // SKZ4 errata
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
        std::cerr << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because environment variable PCM_USE_RESCTRL=1\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
    if (resctrl.isMounted())
    {
        std::cerr << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because resctrl driver is mounted.\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
    if (isSecureBoot())
    {
        std::cerr << "INFO: using Linux resctrl driver for RDT metrics (L3OCC, LMB, RMB) because Secure Boot mode is enabled.\n";
        resctrl.init();
        useResctrl = true;
        return;
    }
#endif
    std::cerr << "Initializing RMIDs" << std::endl;
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

        //std::cout << "Socket Id : " << topology[core].socket_id;
        msr_pqr_assoc &= 0xffffffff00000000ULL;
        msr_pqr_assoc |= (uint64)(rmid[topology[core].socket_id] & ((1ULL<<10)-1ULL));
        //std::cout << "initRMID writing IA32_PQR_ASSOC 0x" << std::hex << msr_pqr_assoc << std::dec << "\n";
        //Write 0xC8F MSR with new RMID for each core
        MSR[core]->write(IA32_PQR_ASSOC,msr_pqr_assoc);

        msr_qm_evtsel = static_cast<uint64>(rmid[topology[core].socket_id] & ((1ULL<<10)-1ULL));
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
        rmid[topology[core].socket_id] --;
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
    switch(cpu_family_model)
    {
        case ATOM:
        case ATOM_2:
        case CENTERTON:
        case AVOTON:
        case BAYTRAIL:
        case CHERRYTRAIL:
        case APOLLO_LAKE:
        case GEMINI_LAKE:
        case DENVERTON:
        case ADL:
        case RPL:
        case MTL:
        case LNL:
        case ARL:
        case SNOWRIDGE:
        case ELKHART_LAKE:
        case JASPER_LAKE:
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
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
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
    switch(cpu_family_model)
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
        case GEMINI_LAKE:
        case DENVERTON:
        PCM_SKL_PATH_CASES
        case ADL:
        case RPL:
        case MTL:
        case LNL:
        case ARL:
        case SNOWRIDGE:
        case ELKHART_LAKE:
        case JASPER_LAKE:
        case ICX:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
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
constexpr auto perfSlotsPath = "/sys/bus/event_source/devices/cpu/events/slots";
constexpr auto perfBadSpecPath = "/sys/bus/event_source/devices/cpu/events/topdown-bad-spec";
constexpr auto perfBackEndPath = "/sys/bus/event_source/devices/cpu/events/topdown-be-bound";
constexpr auto perfFrontEndPath = "/sys/bus/event_source/devices/cpu/events/topdown-fe-bound";
constexpr auto perfRetiringPath = "/sys/bus/event_source/devices/cpu/events/topdown-retiring";
// L2 extensions:
constexpr auto perfBrMispred = "/sys/bus/event_source/devices/cpu/events/topdown-br-mispredict";
constexpr auto perfFetchLat = "/sys/bus/event_source/devices/cpu/events/topdown-fetch-lat";
constexpr auto perfHeavyOps = "/sys/bus/event_source/devices/cpu/events/topdown-heavy-ops";
constexpr auto perfMemBound = "/sys/bus/event_source/devices/cpu/events/topdown-mem-bound";

bool PCM::perfSupportsTopDown()
{
    static int yes = -1;
    if (-1 == yes)
    {
        const auto slots = readSysFS(perfSlotsPath, true);
        const auto bad = readSysFS(perfBadSpecPath, true);
        const auto be = readSysFS(perfBackEndPath, true);
        const auto fe = readSysFS(perfFrontEndPath, true);
        const auto ret = readSysFS(perfRetiringPath, true);
        bool supported = slots.size() && bad.size() && be.size() && fe.size() && ret.size();
        if (isHWTMAL2Supported())
        {
            supported = supported &&
                readSysFS("/sys/bus/event_source/devices/cpu/events/topdown-br-mispredict", true).size() &&
                readSysFS("/sys/bus/event_source/devices/cpu/events/topdown-fetch-lat", true).size() &&
                readSysFS("/sys/bus/event_source/devices/cpu/events/topdown-heavy-ops", true).size() &&
                readSysFS("/sys/bus/event_source/devices/cpu/events/topdown-mem-bound", true).size();
        }
        yes = supported ? 1 : 0;
    }
    return 1 == yes;
}
#endif

const std::vector<std::string> qat_evtsel_mapping = 
{
    { "sample_cnt" },               //0x0
    { "pci_trans_cnt" },            //0x1
    { "max_rd_lat" },               //0x2
    { "rd_lat_acc_avg" },           //0x3
    { "max_lat" },                  //0x4
    { "lat_acc_avg" },              //0x5
    { "bw_in" },                    //0x6
    { "bw_out" },                   //0x7
    { "at_page_req_lat_acc_avg" },  //0x8
    { "at_trans_lat_acc_avg" },     //0x9
    { "at_max_tlb_used" },          //0xA
    { "util_cpr0" },                //0xB
    { "util_dcpr0" },               //0xC
    { "util_dcpr1" },               //0xD
    { "util_dcpr2" },               //0xE
    { "util_xlt0" },                //0xF
    { "util_xlt1" },                //0x10
    { "util_cph0" },                //0x11
    { "util_cph1" },                //0x12
    { "util_cph2" },                //0x13
    { "util_cph3" },                //0x14
    { "util_cph4" },                //0x15
    { "util_cph5" },                //0x16
    { "util_cph6" },                //0x17
    { "util_cph7" },                //0x18
    { "util_ath0" },                //0x19
    { "util_ath1" },                //0x1A
    { "util_ath2" },                //0x1B
    { "util_ath3" },                //0x1C
    { "util_ath4" },                //0x1D
    { "util_ath5" },                //0x1E
    { "util_ath6" },                //0x1F
    { "util_ath7" },                //0x20
    { "util_ucs0" },                //0x21
    { "util_ucs1" },                //0x22
    { "util_ucs2" },                //0x23
    { "util_ucs3" },                //0x24
    { "util_pke0" },                //0x25
    { "util_pke1" },                //0x26
    { "util_pke2" },                //0x27
    { "util_pke3" },                //0x28
    { "util_pke4" },                //0x29
    { "util_pke5" },                //0x2A
    { "util_pke6" },                //0x2B
    { "util_pke7" },                //0x2C
    { "util_pke8" },                //0x2D
    { "util_pke9" },                //0x2E
    { "util_pke10" },               //0x2F
    { "util_pke11" },               //0x30
    { "util_pke12" },               //0x31
    { "util_pke13" },               //0x32
    { "util_pke14" },               //0x33
    { "util_pke15" },               //0x34
    { "util_pke16" },               //0x35
    { "util_pke17" },               //0x36
    { "unknown" }                   //0x37
};

class VirtualDummyRegister : public HWRegister
{
    uint64 lastValue;
public:
    VirtualDummyRegister() : lastValue(0) {}
    void operator = (uint64 val) override
    {
        lastValue = val;
    }
    operator uint64 () override
    {
        return lastValue;
    }
};

class QATTelemetryVirtualGeneralConfigRegister : public HWRegister
{
    friend class QATTelemetryVirtualCounterRegister;
    int domain, b, d, f;
    PCM::IDX_OPERATION operation;
    PCM::IDX_STATE state;
    std::unordered_map<std::string, uint32> data_cache; //data cache
public:
    QATTelemetryVirtualGeneralConfigRegister(int domain_, int b_, int d_, int f_) :
        domain(domain_),
        b(b_),
        d(d_),
        f(f_),
        operation(PCM::QAT_TLM_STOP),
        state(PCM::IDX_STATE_OFF)
    {
    }
    void operator = (uint64 val) override
    {
        operation = PCM::IDX_OPERATION(val);
#ifdef __linux__
        std::ostringstream sysfs_path(std::ostringstream::out);
        std::string telemetry_filename;
        switch (operation)
        {
            case PCM::QAT_TLM_START: //enable
                state = PCM::IDX_STATE_ON; 
                // falls through
            case PCM::QAT_TLM_STOP: //disable
                if (state == PCM::IDX_STATE_ON)
                {
                    //std::cerr << "QAT telemetry operation = " << operation << ".\n";
                    sysfs_path << std::string("/sys/bus/pci/devices/") <<
                        std::hex << std::setw(4) << std::setfill('0') << domain << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << b << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << d << "." <<
                        std::hex << f << "/telemetry/control";

                    /*check telemetry for out-of tree driver*/
                    telemetry_filename = readSysFS(sysfs_path.str().c_str(), true);
                    if(!telemetry_filename.size()){
                        /*is not oot driver, check telemetry for in tree driver  (since kernel 6.8)*/
                        sysfs_path.str("");
                        sysfs_path << std::string("/sys/kernel/debug/qat_4xxx_") <<
                            std::hex << std::setw(4) << std::setfill('0') << domain << ":" <<
                            std::hex << std::setw(2) << std::setfill('0') << b << ":" <<
                            std::hex << std::setw(2) << std::setfill('0') << d << "." <<
                            std::hex << f << "/telemetry/control";
                    }

                    if (writeSysFS(sysfs_path.str().c_str(), (operation == PCM::QAT_TLM_START  ? "1" : "0")) == false)
                    {
                        std::cerr << "Linux sysfs: Error on control QAT telemetry operation = " << operation << ".\n";
                    }
                }
                break;
            case PCM::QAT_TLM_REFRESH: //refresh data
                if (state == PCM::IDX_STATE_ON)
                {
                    //std::cerr << "QAT telemetry operation = " << operation << ".\n";
                    sysfs_path << std::string("/sys/bus/pci/devices/") <<
                        std::hex << std::setw(4) << std::setfill('0') << domain << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << b << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << d << "." <<
                        std::hex << f << "/telemetry/device_data";
                    /*check telemetry for out-of tree driver*/
                    telemetry_filename = readSysFS(sysfs_path.str().c_str(), true);
                    if(!telemetry_filename.size()){
                        /*is not oot driver, check telemetry for in tree driver  (since kernel 6.8)*/
                        sysfs_path.str("");
                        sysfs_path << std::string("/sys/kernel/debug/qat_4xxx_") <<
                            std::hex << std::setw(4) << std::setfill('0') << domain << ":" <<
                            std::hex << std::setw(2) << std::setfill('0') << b << ":" <<
                            std::hex << std::setw(2) << std::setfill('0') << d << "." <<
                            std::hex << f << "/telemetry/device_data";
                    }
                    data_cache.clear();
                    readMapFromSysFS(sysfs_path.str().c_str(), data_cache);
                }
                break;
            default:
                break;
        }
#endif
    }
    operator uint64 () override
    {
        return operation;
    }
    ~QATTelemetryVirtualGeneralConfigRegister()
    {
        //std::cerr << "~QATTelemetryVirtualGeneralConfigRegister.\n" << std::flush;
    }
};

class QATTelemetryVirtualControlRegister : public HWRegister
{
    friend class QATTelemetryVirtualCounterRegister;
    uint64 event;
public:
    QATTelemetryVirtualControlRegister() : event(0x0)
    {
    }
    void operator = (uint64 val) override
    {
        event = extract_bits(val, 32, 59);
    }
    operator uint64 () override
    {
        return event;
    }
};

class QATTelemetryVirtualCounterRegister : public HWRegister
{
    std::shared_ptr<QATTelemetryVirtualGeneralConfigRegister> gConfigReg;
    std::shared_ptr<QATTelemetryVirtualControlRegister> controlReg;
    // int ctr_id; // unused
public:
    QATTelemetryVirtualCounterRegister( std::shared_ptr<QATTelemetryVirtualGeneralConfigRegister> gConfigReg_,
        std::shared_ptr<QATTelemetryVirtualControlRegister> controlReg_,
        int /* ctr_id_ */ ) :
        gConfigReg(gConfigReg_),
        controlReg(controlReg_)
    {
    }
    void operator = (uint64 /* val */) override
    {
        // no-op
    }
    operator uint64 () override
    {
        uint64 result = 0;
        uint32 eventsel = controlReg->event;
        if (eventsel < qat_evtsel_mapping.size())
        {
            std::string key = qat_evtsel_mapping[eventsel];
            if (gConfigReg->data_cache.find(key) != gConfigReg->data_cache.end())
            {
                result = gConfigReg->data_cache.at(key);
            }
        }
        //std::cerr << std::hex << "QAT-CTR(0x" << ctr_id << "), key="<< key << ", val=0x" << std::hex << result << ".\n" << std::dec;
        return result;
    }
};

bool PCM::discoverSystemTopology()
{
    typedef std::map<uint32, uint32> socketIdMap_type;
    socketIdMap_type socketIdMap;

    PCM_CPUID_INFO cpuid_args;
    uint32 smtMaskWidth = 0;
    uint32 coreMaskWidth = 0;
    uint32 l2CacheMaskShift = 0;

    struct domain
    {
        TopologyEntry::DomainTypeID type = TopologyEntry::DomainTypeID::InvalidDomainTypeID;
        unsigned levelShift = 0, nextLevelShift = 0, width = 0;
    };
    std::unordered_map<int, domain> topologyDomainMap;
    {
        TemporalThreadAffinity aff0(0);

        if (initCoreMasks(smtMaskWidth, coreMaskWidth, l2CacheMaskShift) == false)
        {
            std::cerr << "ERROR: Major problem? No leaf 0 under cpuid function 11.\n";
            return false;
        }

        int subleaf = 0;

        std::vector<domain> topologyDomains;
        if (max_cpuid >= 0x1F)
        {
            subleaf = 0;
            do
            {
                pcm_cpuid(0x1F, subleaf, cpuid_args);
                domain d;
                d.type = (TopologyEntry::DomainTypeID)extract_bits_ui(cpuid_args.reg.ecx, 8, 15);
                if (d.type == TopologyEntry::DomainTypeID::InvalidDomainTypeID)
                {
                    break;
                }
                d.nextLevelShift = extract_bits_ui(cpuid_args.reg.eax, 0, 4);
                d.levelShift = topologyDomains.empty() ? 0 : topologyDomains.back().nextLevelShift;
                d.width = d.nextLevelShift - d.levelShift;
                topologyDomains.push_back(d);
                ++subleaf;
            } while (true);

            if (topologyDomains.size())
            {
                domain d;
                d.type = TopologyEntry::DomainTypeID::SocketPackageDomain;
                d.levelShift = topologyDomains.back().nextLevelShift;
                d.nextLevelShift = 32;
                d.width = d.nextLevelShift - d.levelShift;
                topologyDomains.push_back(d);
            }
            for (size_t l = 0; l < topologyDomains.size(); ++l)
            {
                topologyDomainMap[topologyDomains[l].type] = topologyDomains[l];
#if 0
                std::cerr << "Topology level: " << l <<
                                      " type: " << topologyDomains[l].type <<
                                      " (" << TopologyEntry::getDomainTypeStr(topologyDomains[l].type) << ")" <<
                                      " width: " << topologyDomains[l].width <<
                                      " levelShift: " << topologyDomains[l].levelShift <<
                                      " nextLevelShift: " << topologyDomains[l].nextLevelShift << "\n";
#endif
            }
        }
    }

#ifndef __APPLE__
    auto populateEntry = [&topologyDomainMap,&smtMaskWidth, &coreMaskWidth, &l2CacheMaskShift](TopologyEntry& entry)
    {
        auto getAPICID = [&](const uint32 leaf)
        {
            PCM_CPUID_INFO cpuid_args;
#if defined(__FreeBSD__) || defined(__DragonFly__)
            pcm_cpuid_bsd(leaf, cpuid_args, entry.os_id);
#else
            pcm_cpuid(leaf, 0x0, cpuid_args);
#endif
            return cpuid_args.array[3];
        };
        if (topologyDomainMap.size())
        {
            auto getID = [&topologyDomainMap](const int apic_id, const TopologyEntry::DomainTypeID t)
            {
                const auto di = topologyDomainMap.find(t);
                if (di != topologyDomainMap.end())
                {
                    const auto & d = di->second;
                    return extract_bits_ui(apic_id, d.levelShift, d.nextLevelShift - 1);
                }
                return 0U;
            };
            entry.tile_id = extract_bits_ui(getAPICID(0xb), l2CacheMaskShift, 31);
            const int apic_id = getAPICID(0x1F);
            entry.thread_id = getID(apic_id, TopologyEntry::DomainTypeID::LogicalProcessorDomain);
            entry.core_id = getID(apic_id, TopologyEntry::DomainTypeID::CoreDomain);
            entry.module_id = getID(apic_id, TopologyEntry::DomainTypeID::ModuleDomain);
            if (entry.tile_id == 0)
            {
                entry.tile_id = getID(apic_id, TopologyEntry::DomainTypeID::TileDomain);
            }
            entry.die_id = getID(apic_id, TopologyEntry::DomainTypeID::DieDomain);
            entry.die_grp_id = getID(apic_id, TopologyEntry::DomainTypeID::DieGrpDomain);
            entry.socket_id = getID(apic_id, TopologyEntry::DomainTypeID::SocketPackageDomain);
        }
        else
        {
            fillEntry(entry, smtMaskWidth, coreMaskWidth, l2CacheMaskShift, getAPICID(0xb));
        }
    };
#endif

    auto populateHybridEntry = [this](TopologyEntry& entry, int core) -> bool
    {
        if (hybrid == false) return true;
        PCM_CPUID_INFO cpuid_args;
#if defined(__FreeBSD__) || defined(__DragonFly__)
        pcm_cpuid_bsd(0x1a, cpuid_args, core);
#elif defined (_MSC_VER) || defined(__linux__)
        pcm_cpuid(0x1a, 0x0, cpuid_args);
        (void)core;
#else
        std::cerr << "PCM Error: Hybrid processors are not supported for your OS\n";
        (void)core;
        return false;
#endif
        entry.native_cpu_model = extract_bits_ui(cpuid_args.reg.eax, 0, 23);
        entry.core_type = (TopologyEntry::CoreType) extract_bits_ui(cpuid_args.reg.eax, 24, 31);
        return true;
    };

#ifdef _MSC_VER
// version for Windows 7 and later version

    char * slpi = new char[sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)];
    DWORD len = (DWORD)sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX);
    BOOL res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);

    while (res == FALSE)
    {
        deleteAndNullifyArray(slpi);

        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        {
            slpi = new char[len];
            res = GetLogicalProcessorInformationEx(RelationAll, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)slpi, &len);
        }
        else
        {
            tcerr << "Error in Windows function 'GetLogicalProcessorInformationEx': " <<
                GetLastError() << " ";
            const TCHAR * strError = _com_error(GetLastError()).ErrorMessage();
            if (strError) tcerr << strError;
            tcerr << "\n";
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

        TopologyEntry entry;
        entry.os_id = i;

        populateEntry(entry);
        if (populateHybridEntry(entry, i) == false)
        {
            return false;
        }

        topology.push_back(entry);
        socketIdMap[entry.socket_id] = 0;
    }

    deleteAndNullifyArray(base_slpi);

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
            try {
                TemporalThreadAffinity _(entry.os_id);

                populateEntry(entry);
                if (populateHybridEntry(entry, entry.os_id) == false)
                {
                    return false;
                }

                topology[entry.os_id] = entry;
                socketIdMap[entry.socket_id] = 0;
                ++num_online_cores;
            }
            catch (std::exception &)
            {
                std::cerr << "Marking core " << entry.os_id << " offline\n";
            }
        }
    }
    //std::cout << std::flush;
    fclose(f_cpuinfo);

#elif defined(__FreeBSD__) || defined(__DragonFly__)

    size_t size = sizeof(num_cores);

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
        entry.os_id = i;

        populateEntry(entry);
        if (populateHybridEntry(entry, i) == false)
        {
            return false;
        }

        if (entry.socket_id == 0 && entry.core_id == 0) ++threads_per_core;

        topology.push_back(entry);
        socketIdMap[entry.socket_id] = 0;
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
        freeAndNullify(pParam);                                                                            \
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

    assert(num_cores > 0);
    TopologyEntry entries[num_cores];
    if (MSR[0]->buildTopology(num_cores, entries) != 0) {
      std::cerr << "Unable to build CPU topology" << std::endl;
      return false;
    }
    for(int i = 0; i < num_cores; i++){
        socketIdMap[entries[i].socket_id] = 0;
        if(entries[i].os_id >= 0)
        {
            if(entries[i].core_id == 0 && entries[i].socket_id == 0) ++threads_per_core;
            if (populateHybridEntry(entries[i], i) == false)
            {
                return false;
            }
            topology.push_back(entries[i]);
        }
    }
// End of OSX specific code
#endif

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
    for ( auto& socket : systemTopology->sockets() )
        socket->setRefCore();

    // use map to change apic socket id to the logical socket id
    for (int i = 0; (i < (int)num_cores) && (!socketIdMap.empty()); ++i)
    {
        if(isCoreOnline((int32)i))
          topology[i].socket_id = socketIdMap[topology[i].socket_id];
    }

#if 0
    std::cerr << "Number of socket ids: " << socketIdMap.size() << "\n";
    std::cerr << "Topology:\nsocket os_id core_id\n";
    for (int i = 0; i < num_cores; ++i)
    {
        std::cerr << topology[i].socket_id << " " << topology[i].os_id << " " << topology[i].core_id << "\n";
    }
#endif
    if (threads_per_core == 0)
    {
        for (int i = 0; i < (int)num_cores; ++i)
        {
            if (topology[i].socket_id == topology[0].socket_id && topology[i].core_id == topology[0].core_id)
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
            socketRefCore[topology[i].socket_id] = i;
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
    AllSlotsRaw.resize(num_cores, 0);
    MemBoundSlots.resize(num_cores, 0);
    FetchLatSlots.resize(num_cores, 0);
    BrMispredSlots.resize(num_cores, 0);
    HeavyOpsSlots.resize(num_cores, 0);

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
    const bool all_cores_online_no_hybrid = (num_cores == num_online_cores && hybrid == false);

    if (all_cores_online_no_hybrid)
    {
      std::cerr << "Number of physical cores: " << (num_cores/threads_per_core) << "\n";
    }

    std::cerr << "Number of logical cores: " << num_cores << "\n";
    std::cerr << "Number of online logical cores: " << num_online_cores << "\n";

    if (all_cores_online_no_hybrid)
    {
      std::cerr << "Threads (logical cores) per physical core: " << threads_per_core << "\n";
    }
    else
    {
        std::cerr << "Threads (logical cores) per physical core: " << threads_per_core << " (maybe imprecise due to core offlining/hybrid CPU)\n";
        std::cerr << "Offlined cores: ";
        for (int i = 0; i < (int)num_cores; ++i)
            if(isCoreOnline((int32)i) == false)
                std::cerr << i << " ";
        std::cerr << "\n";
    }
    std::cerr << "Num sockets: " << num_sockets << "\n";
    if (all_cores_online_no_hybrid)
    {
        std::cerr << "Physical cores per socket: " << num_phys_cores_per_socket << "\n";
    }
    else
    {
        std::cerr << "Physical cores per socket: " << num_cores / num_sockets / threads_per_core << " (maybe imprecise due to core offlining/hybrid CPU)\n";
    }

    if (hybrid == false)
    {
        // TODO: deprecate this output and move it to uncore PMU section (use getMaxNumOfUncorePMUs(CBO_PMU_ID) )
        std::cerr << "Last level cache slices per socket: " << getMaxNumOfCBoxesInternal() << "\n";
    }
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
#ifdef __APPLE__
    for (size_t i=0; i < MSR.size(); ++i)
    {
        systemTopology->addMSRHandleToOSThread(MSR[i], (uint32)i);
    }
#else
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
        std::cerr << "execute 'modprobe msr' as root user, then execute pcm as root user.\n";
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
        if (max_cpuid >= 0x16)
        {
            PCM_CPUID_INFO cpuinfo;
            pcm_cpuid(0x16, cpuinfo);
            nominal_frequency = uint64(extract_bits_ui(cpuinfo.reg.eax, 0, 15)) * 1000000ULL;;
        }
        if (!nominal_frequency)
        {
            uint64 freq = 0;
            MSR[socketRefCore[0]]->read(PLATFORM_INFO_ADDR, &freq);
            const uint64 bus_freq = (
                  cpu_family_model == SANDY_BRIDGE
               || cpu_family_model == JAKETOWN
               || cpu_family_model == IVYTOWN
               || cpu_family_model == HASWELLX
               || cpu_family_model == BDX_DE
               || cpu_family_model == BDX
               || cpu_family_model == IVY_BRIDGE
               || cpu_family_model == HASWELL
               || cpu_family_model == BROADWELL
               || cpu_family_model == AVOTON
               || cpu_family_model == APOLLO_LAKE
               || cpu_family_model == GEMINI_LAKE
               || cpu_family_model == DENVERTON
               || useSKLPath()
               || cpu_family_model == SNOWRIDGE
               || cpu_family_model == ELKHART_LAKE
               || cpu_family_model == JASPER_LAKE
               || cpu_family_model == KNL
               || cpu_family_model == ADL
               || cpu_family_model == RPL
               || cpu_family_model == MTL
               || cpu_family_model == LNL
               || cpu_family_model == ARL
               || cpu_family_model == SKX
               || cpu_family_model == ICX
               || cpu_family_model == SPR
               || cpu_family_model == EMR
               || cpu_family_model == GNR
               || cpu_family_model == SRF
               || cpu_family_model == GRR
               ) ? (100000000ULL) : (133333333ULL);

            nominal_frequency = ((freq >> 8) & 255) * bus_freq;
        }

        if(!nominal_frequency)
            nominal_frequency = get_frequency_from_cpuid();

        if(!nominal_frequency)
        {
            computeNominalFrequency();
        }

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
        if (cpu_family_model == PCM::CHERRYTRAIL || cpu_family_model == PCM::BAYTRAIL)
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

    if (ppEnergyMetricsAvailable() && MSR.size() && num_sockets == 1 && pp_energy_status.empty())
    {
        pp_energy_status.push_back(std::make_shared<CounterWidthExtender>(
            new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[0]], MSR_PP0_ENERGY_STATUS), 32, 10000));
        pp_energy_status.push_back(std::make_shared<CounterWidthExtender>(
            new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[0]], MSR_PP1_ENERGY_STATUS), 32, 10000));
    }

    if (systemEnergyMetricAvailable() && MSR.size() && (system_energy_status.get() == nullptr))
    {
        system_energy_status = std::make_shared<CounterWidthExtender>(
            new CounterWidthExtender::MsrHandleCounter(MSR[socketRefCore[0]], MSR_SYS_ENERGY_STATUS, 0x00000000FFFFFFFF), 32, 10000);
    }
}

static const uint32 UBOX0_DEV_IDS[] = {
    0x3451,
    0x3251
};

std::vector<std::pair<uint32, uint32> > socket2UBOX0bus;

void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize);

void initSocket2Ubox0Bus()
{
    initSocket2Bus(socket2UBOX0bus, SERVER_UBOX0_REGISTER_DEV_ADDR, SERVER_UBOX0_REGISTER_FUNC_ADDR,
        UBOX0_DEV_IDS, (uint32)sizeof(UBOX0_DEV_IDS) / sizeof(UBOX0_DEV_IDS[0]));
}

bool initRootBusMap(std::map<int, int> &rootbus_map)
{
    bool mapped = false;
    static const uint32 MSM_DEV_IDS[] = { SPR_MSM_DEV_ID };

    std::vector<std::pair<uint32, uint32> > socket2MSMbus;
    initSocket2Bus(socket2MSMbus, SPR_MSM_DEV_ADDR, SPR_MSM_FUNC_ADDR, MSM_DEV_IDS, (uint32)sizeof(MSM_DEV_IDS) / sizeof(MSM_DEV_IDS[0]));

    for (auto & s2bus : socket2MSMbus)
    {
        uint32 cpuBusValid = 0x0;
        int cpuBusPackageId;
        std::vector<uint32> cpuBusNo;

        if (get_cpu_bus(s2bus.first, s2bus.second, SPR_MSM_DEV_ADDR, SPR_MSM_FUNC_ADDR, cpuBusValid, cpuBusNo, cpuBusPackageId) == false)
            return false;

        for (int cpuBusId = 0; cpuBusId < SPR_MSM_CPUBUSNO_MAX; ++cpuBusId)
        {
            if (!((cpuBusValid >> cpuBusId) & 0x1))
            {
                //std::cout << "CPU bus " << cpuBusId << " is disabled on package " << cpuBusPackageId << std::endl;
                continue;
            }

            int rootBus = (cpuBusNo[(int)(cpuBusId / 4)] >> ((cpuBusId % 4) * 8)) & 0xff;
            rootbus_map[((s2bus.first << 8) | rootBus)] = cpuBusPackageId;
            //std::cout << "Mapped CPU bus #" << std::dec << cpuBusId << std::hex << " (domain=0x" << s2bus.first << " bus=0x" << rootBus << ") to " << std::dec << "package" << cpuBusPackageId << std::endl;
        }

        mapped = true;
    }

    return mapped;
}

#define SPR_IDX_ACCEL_COUNTER_MAX_NUM (8)
#define SPR_QAT_ACCEL_COUNTER_MAX_NUM (16)

struct idx_accel_dev_info {
    uint64 mem_bar;
    uint32 numa_node;
    uint32 socket_id;
    uint32 domain;
    uint32 bus;
    uint32 dev;
    uint32 func;
};

bool getIDXDevBAR(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 dev, uint32 func, std::map<int, int> &bus2socket, std::vector<struct idx_accel_dev_info> &idx_devs)
{
    uint64 memBar = 0x0;
    uint32 pciCmd = 0x0, pmCsr= 0x0;
    uint32 numaNode = 0xff;
    struct idx_accel_dev_info idx_dev;

    for (auto & s2bus : socket2bus)
    {
        memBar = 0x0;
        pciCmd = 0x0;

        PciHandleType IDXHandle(s2bus.first, s2bus.second, dev, func);
        IDXHandle.read64(SPR_IDX_ACCEL_BAR0_OFFSET, &memBar);
        IDXHandle.read32(SPR_IDX_ACCEL_PCICMD_OFFSET, &pciCmd);
        IDXHandle.read32(SPR_IDX_ACCEL_PMCSR_OFFSET, &pmCsr);      
        if (memBar == 0x0 || (pciCmd & 0x02) == 0x0) //Check BAR0 is valid or NOT.
        {
            std::cerr << "Warning: IDX - BAR0 of B:0x" << std::hex << s2bus.second << ",D:0x" << std::hex << dev << ",F:0x" << std::hex << func
                << " is invalid(memBar=0x" << std::hex << memBar << ", pciCmd=0x" << std::hex << pciCmd <<"), skipped." << std::dec << std::endl;
            continue;
        }

        if ((pmCsr & 0x03) == 0x3) //Check power state
        {
            std::cout << "Warning: IDX - Power state of B:0x" << std::hex << s2bus.second << ",D:0x" << std::hex << dev << ",F:0x" << std::hex << func \
                << " is off, skipped." << std::endl;
            continue;
        }

        numaNode = 0xff;
#ifdef __linux__
        std::ostringstream devNumaNodePath(std::ostringstream::out);
        devNumaNodePath << std::string("/sys/bus/pci/devices/") <<
            std::hex << std::setw(4) << std::setfill('0') << s2bus.first << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << s2bus.second << ":" <<
            std::hex << std::setw(2) << std::setfill('0') << dev << "." <<
            std::hex << func << "/numa_node";
        const std::string devNumaNodeStr = readSysFS(devNumaNodePath.str().c_str(), true);
        if (devNumaNodeStr.size())
        {
            numaNode = std::atoi(devNumaNodeStr.c_str());
            if (numaNode == (std::numeric_limits<uint32>::max)())
            {
                numaNode = 0xff; //translate to special value for numa disable case. 
            }
        }
        //std::cout << "IDX DEBUG: numa node file path=" << devNumaNodePath.str().c_str()  << ", value=" << numaNode << std::endl;
#endif
        idx_dev.mem_bar = memBar;
        idx_dev.numa_node = numaNode;
        idx_dev.socket_id = 0xff;
        idx_dev.domain = s2bus.first;
        idx_dev.bus = s2bus.second;
        idx_dev.dev = dev;
        idx_dev.func = func;
        if (bus2socket.find(((s2bus.first << 8 ) | s2bus.second)) != bus2socket.end())
        {
            idx_dev.socket_id = bus2socket.at(((s2bus.first << 8 ) | s2bus.second));
        }

        idx_devs.push_back(idx_dev);
    }

    return true;
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
                serverUncorePMUs.push_back(std::make_shared<ServerUncorePMUs>(i, this));
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
            serverUncorePMUs.clear();
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
           switch (cpu_family_model)
           {
           case TGL:
           case ADL: // TGLClientBW works fine for ADL
           case RPL: // TGLClientBW works fine for RPL
           case MTL: // TGLClientBW works fine for MTL
           case LNL: // TGLClientBW works fine for LNL
           case ARL: // TGLClientBW works fine for ARL
               clientBW = std::make_shared<TGLClientBW>();
               break;
/*         Disabled since ADLClientBW requires 2x multiplier for BW on top
           case ADL:
           case RPL:
               clientBW = std::make_shared<ADLClientBW>();
               break;
*/
           default:
               clientBW = std::make_shared<ClientBW>();
           }
           clientImcReads = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientImcReadsCounter(clientBW), 32, 10000);
           clientImcWrites = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientImcWritesCounter(clientBW), 32, 10000);
           clientGtRequests = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientGtRequestsCounter(clientBW), 32, 10000);
           clientIaRequests = std::make_shared<CounterWidthExtender>(
               new CounterWidthExtender::ClientIaRequestsCounter(clientBW), 32, 10000);
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
    switch (cpu_family_model)
    {
    case ICX:
    case SNOWRIDGE:
    case SPR:
    case EMR:
    case GNR:
    case GRR:
    case SRF:
        {
            bool failed = false;
            try
            {
                initSocket2Ubox0Bus();
            }
            catch (std::exception& e)
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
                std::cerr << "Can not read PCI configuration space bus mapping. Access to uncore counters is disabled.\n";
            }
        }
        break;
    }
    if (cpu_family_model == ICX || cpu_family_model == SNOWRIDGE)
    {
        for (size_t s = 0; s < (size_t)num_sockets && s < socket2UBOX0bus.size() && s < serverUncorePMUs.size(); ++s)
        {
            serverBW.push_back(std::make_shared<ServerBW>(serverUncorePMUs[s]->getNumMC(), socket2UBOX0bus[s].first, socket2UBOX0bus[s].second));
            // std::cout << " Added serverBW object serverUncorePMUs[s]->getNumMC() = " << serverUncorePMUs[s]->getNumMC() << std::endl;
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

    //TPMIHandle::setVerbose(true);
    try {
        if (TPMIHandle::getNumInstances() == (size_t)num_sockets)
        {
            // std::cerr << "DEBUG: TPMIHandle::getNumInstances(): " << TPMIHandle::getNumInstances() << "\n";
            UFSStatus.resize(num_sockets);
            for (uint32 s = 0; s < (uint32)num_sockets; ++s)
            {
                try {
                    TPMIHandle h(s, UFS_ID, UFS_FABRIC_CLUSTER_OFFSET * sizeof(uint64));
                    // std::cerr << "DEBUG: Socket " << s << " dies: " << h.getNumEntries() << "\n";
                    for (size_t die = 0; die < h.getNumEntries(); ++die)
                    {
                        const auto clusterOffset = extract_bits(h.read64(die), 0, 7);
                        UFSStatus[s].push_back(std::make_shared<TPMIHandle>(s, UFS_ID, (clusterOffset + UFS_STATUS)* sizeof(uint64)));
                    }
                } catch (std::exception & e)
                {
                    std::cerr << "ERROR: Could not open UFS TPMI register on socket " << s << ". Uncore frequency metrics will be unavailable. Exception details: " << e.what() << "\n";
                }
            }
        }
    } catch (std::exception & e)
    {
        std::cerr << "ERROR: Could not initialize TPMI. Uncore frequency metrics will be unavailable. Exception details: " << e.what() << "\n";
    }

    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        std::cerr << "Socket " << s << ":" <<
            " " << getMaxNumOfUncorePMUs(PCU_PMU_ID, s) << " PCU units detected."
            " " << ((s < iioPMUs.size()) ? iioPMUs[s].size() : 0) << " IIO units detected."
            " " << ((s < irpPMUs.size()) ? irpPMUs[s].size() : 0) << " IRP units detected."
            " " << getMaxNumOfUncorePMUs(CBO_PMU_ID, s) << " CHA/CBO units detected."
            " " << getMaxNumOfUncorePMUs(MDF_PMU_ID, s) << " MDF units detected."
            " " << getMaxNumOfUncorePMUs(UBOX_PMU_ID, s) << " UBOX units detected."
            " " << ((s < cxlPMUs.size()) ? cxlPMUs[s].size() : 0) << " CXL units detected."
            " " << getMaxNumOfUncorePMUs(PCIE_GEN5x16_PMU_ID, s) << " PCIE_GEN5x16 units detected."
            " " << getMaxNumOfUncorePMUs(PCIE_GEN5x8_PMU_ID, s) << " PCIE_GEN5x8 units detected."
            "\n";
    }
}

void PCM::globalFreezeUncoreCounters()
{
    globalFreezeUncoreCountersInternal(1ULL);
}

void PCM::globalUnfreezeUncoreCounters()
{
    globalFreezeUncoreCountersInternal(0ULL);
}

// 1 : freeze
// 0 : unfreeze
void PCM::globalFreezeUncoreCountersInternal(const unsigned long long int freeze)
{
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        auto& handle = MSR[socketRefCore[s]];
        switch (cpu_family_model)
        {
        case SPR:
        case EMR:
            handle->write(SPR_MSR_UNCORE_PMON_GLOBAL_CTL, freeze);
            break;
        case SKX:
        case ICX:
            handle->write(MSR_UNCORE_PMON_GLOBAL_CTL, (1ULL - freeze) << 61ULL);
            break;
        case HASWELLX:
        case BDX:
            handle->write(MSR_UNCORE_PMON_GLOBAL_CTL, (1ULL - freeze) << 29ULL);
            break;
        case IVYTOWN:
            handle->write(IVT_MSR_UNCORE_PMON_GLOBAL_CTL, (1ULL - freeze) << 29ULL);
            break;
        }
    }
}


void PCM::initUncorePMUsDirect()
{
    uncorePMUs.resize(num_sockets);
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        auto & handle = MSR[socketRefCore[s]];
        // unfreeze uncore PMUs
        globalUnfreezeUncoreCounters();

        switch (cpu_family_model)
        {
        case IVYTOWN:
        case JAKETOWN:
            uncorePMUs[s].resize(1);
            {
            std::vector<std::shared_ptr<HWRegister> >   CounterControlRegs{
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTL1_ADDR)
                },
                                                        CounterValueRegs{
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UBOX_MSR_PMON_CTR1_ADDR)
                };
            uncorePMUs[s][0][UBOX_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
                    std::shared_ptr<MSRRegister>(),
                    CounterControlRegs,
                    CounterValueRegs,
                    std::make_shared<MSRRegister>(handle, JKTIVT_UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, JKTIVT_UCLK_FIXED_CTR_ADDR)
                )
            );
            }
            break;
        case SPR:
        case EMR:
            uncorePMUs[s].resize(1);
            {
            std::vector<std::shared_ptr<HWRegister> >   CounterControlRegs{
                    std::make_shared<MSRRegister>(handle, SPR_UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, SPR_UBOX_MSR_PMON_CTL1_ADDR)
            },
                CounterValueRegs{
                    std::make_shared<MSRRegister>(handle, SPR_UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, SPR_UBOX_MSR_PMON_CTR1_ADDR)
            };
            uncorePMUs[s][0][UBOX_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
                    std::make_shared<MSRRegister>(handle, SPR_UBOX_MSR_PMON_BOX_CTL_ADDR),
                    CounterControlRegs,
                    CounterValueRegs,
                    std::make_shared<MSRRegister>(handle, SPR_UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, SPR_UCLK_FIXED_CTR_ADDR)
                )
            );
            }
            break;
        case SRF:
        case GNR:
            uncorePMUs[s].resize(1);
            {
            std::vector<std::shared_ptr<HWRegister> >   CounterControlRegs{
                    std::make_shared<MSRRegister>(handle, BHS_UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, BHS_UBOX_MSR_PMON_CTL1_ADDR)
            },
                CounterValueRegs{
                    std::make_shared<MSRRegister>(handle, BHS_UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, BHS_UBOX_MSR_PMON_CTR1_ADDR),
            };
            uncorePMUs[s][0][UBOX_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
                    std::make_shared<MSRRegister>(handle, BHS_UBOX_MSR_PMON_BOX_CTL_ADDR),
                    CounterControlRegs,
                    CounterValueRegs,
                    std::make_shared<MSRRegister>(handle, BHS_UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, BHS_UCLK_FIXED_CTR_ADDR)
                )
            );
            }
            break;
        case GRR:
            uncorePMUs[s].resize(1);
            {
            std::vector<std::shared_ptr<HWRegister> >   CounterControlRegs{
                    std::make_shared<MSRRegister>(handle, GRR_UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, GRR_UBOX_MSR_PMON_CTL1_ADDR)
            },
                CounterValueRegs{
                    std::make_shared<MSRRegister>(handle, GRR_UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, GRR_UBOX_MSR_PMON_CTR1_ADDR)
            };
            uncorePMUs[s][0][UBOX_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
                    std::make_shared<MSRRegister>(handle, GRR_UBOX_MSR_PMON_BOX_CTL_ADDR),
                    CounterControlRegs,
                    CounterValueRegs,
                    std::make_shared<MSRRegister>(handle, GRR_UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, GRR_UCLK_FIXED_CTR_ADDR)
                )
            );
            }
            break;
        default:
            if (isServerCPU() && hasPCICFGUncore())
            {
            uncorePMUs[s].resize(1);
            {
            std::vector<std::shared_ptr<HWRegister> >   CounterControlRegs{
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTL0_ADDR),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTL1_ADDR),
            },
                CounterValueRegs{
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTR0_ADDR),
                    std::make_shared<MSRRegister>(handle, UBOX_MSR_PMON_CTR1_ADDR),
            };
            uncorePMUs[s][0][UBOX_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
                    std::shared_ptr<MSRRegister>(),
                    CounterControlRegs,
                    CounterValueRegs,
                    std::make_shared<MSRRegister>(handle, UCLK_FIXED_CTL_ADDR),
                    std::make_shared<MSRRegister>(handle, UCLK_FIXED_CTR_ADDR)
                )
            );
            }
            }
        }

        auto addPMUsFromDiscoveryRef = [this, &handle, &s](std::vector<UncorePMURef>& out, const unsigned int pmuType, const int filter0 = -1)
        {
            if (uncorePMUDiscovery.get())
            {
                for (size_t box = 0; box < uncorePMUDiscovery->getNumBoxes(pmuType, s); ++box)
                {
                    if (uncorePMUDiscovery->getBoxAccessType(pmuType, s, box) == UncorePMUDiscovery::accessTypeEnum::MSR
                        && uncorePMUDiscovery->getBoxNumRegs(pmuType, s, box) >= 4)
                    {
                        out.push_back(
                            std::make_shared<UncorePMU>(
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box, 0)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box, 1)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box, 2)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box, 3)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtrAddr(pmuType, s, box, 0)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtrAddr(pmuType, s, box, 1)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtrAddr(pmuType, s, box, 2)),
                                std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtrAddr(pmuType, s, box, 3)),
                                std::shared_ptr<MSRRegister>(),
                                std::shared_ptr<MSRRegister>(),
                                (filter0 < 0) ? std::shared_ptr<MSRRegister>() : std::make_shared<MSRRegister>(handle, uncorePMUDiscovery->getBoxCtlAddr(pmuType, s, box) + filter0) // filters not supported by discovery
                            )
                        );
                    }
                }
            }
        };

        switch (cpu_family_model)
        {
        case IVYTOWN:
        case JAKETOWN:
            uncorePMUs[s].resize(1);
            uncorePMUs[s][0][PCU_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
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
            uncorePMUs[s].resize(1);
            uncorePMUs[s][0][PCU_PMU_ID].push_back(
                std::make_shared<UncorePMU>(
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
        case SPR:
        case EMR:
        case GNR:
        case SRF:
            uncorePMUs[s].resize(1);
            addPMUsFromDiscoveryRef(uncorePMUs[s][0][PCU_PMU_ID], SPR_PCU_BOX_TYPE, 0xE);
            if (uncorePMUs[s][0][PCU_PMU_ID].empty())
            {
                std::cerr << "ERROR: PCU PMU not found\n";
            }
            break;
        }

        // add MDF PMUs
        auto addMDFPMUs = [&](const unsigned int boxType)
        {
            uncorePMUs[s].resize(1);
            addPMUsFromDiscoveryRef(uncorePMUs[s][0][MDF_PMU_ID], boxType);
            if (uncorePMUs[s][0][MDF_PMU_ID].empty())
            {
                std::cerr << "ERROR: MDF PMU not found\n";
            }
        };
        switch (cpu_family_model)
        {
        case SPR:
        case EMR:
            addMDFPMUs(SPR_MDF_BOX_TYPE);
            break;
        case GNR:
        case SRF:
            addMDFPMUs(BHS_MDF_BOX_TYPE);
            break;
        }

        auto addPCICFGPMUsFromDiscoveryRef = [this, &s](std::vector<UncorePMURef>& out, const unsigned int BoxType)
        {
            getPCICFGPMUsFromDiscovery(BoxType, s, [&out](const UncorePMU& pmu) {
                out.push_back(std::make_shared<UncorePMU>(pmu));
            });
        };

        auto addPCICFGPMUsFallback = [&s](std::vector<UncorePMURef>& out, const std::vector<uint32> & DIDs, const char * info = nullptr)
        {
                if (s == 0)
                {
                    if (info)
                    {
#ifndef PCM_SILENT
                        std::cerr << info;
#endif
                    }
                    forAllIntelDevices([&DIDs, &out](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
                    {
                        for (const auto & did: DIDs)
                        {
                            if (device_id == did)
                            {
                                auto handle = std::make_shared<PciHandleType>(group, bus, device, function);
                                const size_t n_regs = 4;
                                std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs;
                                for (size_t r = 0; r < n_regs; ++r)
                                {
                                    CounterControlRegs.push_back(std::make_shared<PCICFGRegister64>(handle, BHS_PCIE_GEN5_PCI_PMON_CTL0_ADDR + sizeof(uint64)*r));
                                    CounterValueRegs.push_back(std::make_shared<PCICFGRegister64>(handle, BHS_PCIE_GEN5_PCI_PMON_CTR0_ADDR + sizeof(uint64)*r));
                                }
                                auto boxCtlRegister = std::make_shared<PCICFGRegister64>(handle, BHS_PCIE_GEN5_PCI_PMON_BOX_CTL_ADDR);
                                out.push_back(std::make_shared<UncorePMU>(boxCtlRegister, CounterControlRegs, CounterValueRegs));
                            }
                        }
                    });
                }
        };

        switch (cpu_family_model)
        {
            case GNR:
            case GRR:
            case SRF:
                uncorePMUs[s].resize(1);
                if (safe_getenv("PCM_NO_PCIE_GEN5_DISCOVERY") == std::string("1"))
                {
                    addPCICFGPMUsFallback(uncorePMUs[s][0][PCIE_GEN5x16_PMU_ID], { 0x0DB0, 0x0DB1, 0x0DB2, 0x0DB3 },
                        "Info: PCM_NO_PCIE_GEN5_DISCOVERY=1 is set, detecting PCIE_GEN5 x16 PMUs manually and mapping them to socket 0.\n");
                    addPCICFGPMUsFallback(uncorePMUs[s][0][PCIE_GEN5x8_PMU_ID], { 0x0DB6, 0x0DB7, 0x0DB8, 0x0DB9 },
                        "Info: PCM_NO_PCIE_GEN5_DISCOVERY=1 is set, detecting PCIE_GEN5 x8 PMUs manually and mapping them to socket 0.\n");
                }
                else
                {
                    addPCICFGPMUsFromDiscoveryRef(uncorePMUs[s][0][PCIE_GEN5x16_PMU_ID], BHS_PCIE_GEN5x16_TYPE);
                    addPCICFGPMUsFromDiscoveryRef(uncorePMUs[s][0][PCIE_GEN5x8_PMU_ID], BHS_PCIE_GEN5x8_TYPE);
                }
                break;
        }
    }

    // init IIO addresses
    iioPMUs.resize(num_sockets);
    switch (cpu_family_model)
    {
    case PCM::SKX:
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
        break;
    case PCM::ICX:
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
        break;
    case PCM::SNOWRIDGE:
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
        break;

    case PCM::SPR:
    case PCM::EMR:
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < SPR_M2IOSF_NUM; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_UNIT_CTL + SPR_M2IOSF_REG_STEP * unit),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTL0 + SPR_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTL0 + SPR_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTL0 + SPR_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTL0 + SPR_M2IOSF_REG_STEP * unit + 3),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTR0 + SPR_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTR0 + SPR_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTR0 + SPR_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, SPR_M2IOSF_IIO_CTR0 + SPR_M2IOSF_REG_STEP * unit + 3)
                );
            }
        }
        break;
    case PCM::GNR:
    case PCM::SRF:
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < BHS_M2IOSF_NUM; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_UNIT_CTL + BHS_M2IOSF_REG_STEP * unit),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTL0 + BHS_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTL0 + BHS_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTL0 + BHS_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTL0 + BHS_M2IOSF_REG_STEP * unit + 3),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTR0 + BHS_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTR0 + BHS_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTR0 + BHS_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, BHS_M2IOSF_IIO_CTR0 + BHS_M2IOSF_REG_STEP * unit + 3)
                );
            }
        }
        break;
    case PCM::GRR:
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto & handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < GRR_M2IOSF_NUM; ++unit)
            {
                iioPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_UNIT_CTL + GRR_M2IOSF_REG_STEP * unit),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTL0 + GRR_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTL0 + GRR_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTL0 + GRR_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTL0 + GRR_M2IOSF_REG_STEP * unit + 3),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTR0 + GRR_M2IOSF_REG_STEP * unit + 0),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTR0 + GRR_M2IOSF_REG_STEP * unit + 1),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTR0 + GRR_M2IOSF_REG_STEP * unit + 2),
                    std::make_shared<MSRRegister>(handle, GRR_M2IOSF_IIO_CTR0 + GRR_M2IOSF_REG_STEP * unit + 3)
                );
            }
        }
        break;
    }
    //init the IDX accelerator
    auto createIDXPMU = [](const size_t addr, const size_t mapSize, const size_t numaNode, const size_t socketId) -> IDX_PMU
    {
        const auto alignedAddr = addr & ~4095ULL;
        auto handle = std::make_shared<MMIORange>(alignedAddr, mapSize, false);
        auto pmon_offset = (handle->read64(SPR_IDX_ACCEL_PMON_BASE_OFFSET) & SPR_IDX_ACCEL_PMON_BASE_MASK)*SPR_IDX_ACCEL_PMON_BASE_RATIO;

        const auto n_regs = SPR_IDX_ACCEL_COUNTER_MAX_NUM;
        std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs, CounterFilterWQRegs, CounterFilterENGRegs;
        std::vector<std::shared_ptr<HWRegister> > CounterFilterTCRegs, CounterFilterPGSZRegs, CounterFilterXFERSZRegs;

        for (size_t r = 0; r < n_regs; ++r)
        {
            CounterControlRegs.push_back(std::make_shared<MMIORegister64>(handle, (SPR_IDX_PMON_CTL_OFFSET(r) + pmon_offset)));
            CounterValueRegs.push_back(std::make_shared<MMIORegister64>(handle, (SPR_IDX_PMON_CTR_OFFSET(r) + pmon_offset)));
            CounterFilterWQRegs.push_back(std::make_shared<MMIORegister32>(handle, (SPR_IDX_PMON_FILTER_WQ_OFFSET(r) + pmon_offset)));
            CounterFilterENGRegs.push_back(std::make_shared<MMIORegister32>(handle, (SPR_IDX_PMON_FILTER_ENG_OFFSET(r) + pmon_offset)));
            CounterFilterTCRegs.push_back(std::make_shared<MMIORegister32>(handle, (SPR_IDX_PMON_FILTER_TC_OFFSET(r) + pmon_offset)));
            CounterFilterPGSZRegs.push_back(std::make_shared<MMIORegister32>(handle, (SPR_IDX_PMON_FILTER_PGSZ_OFFSET(r) + pmon_offset)));
            CounterFilterXFERSZRegs.push_back(std::make_shared<MMIORegister32>(handle, (SPR_IDX_PMON_FILTER_XFERSZ_OFFSET(r) + pmon_offset)));
        }

        return IDX_PMU(
            false,
            numaNode,
            socketId,
            std::make_shared<MMIORegister32>(handle, SPR_IDX_PMON_RESET_CTL_OFFSET + pmon_offset),
            std::make_shared<MMIORegister32>(handle, SPR_IDX_PMON_FREEZE_CTL_OFFSET + pmon_offset),
            std::make_shared<VirtualDummyRegister>(),
            CounterControlRegs,
            CounterValueRegs,
            CounterFilterWQRegs,
            CounterFilterENGRegs,
            CounterFilterTCRegs,
            CounterFilterPGSZRegs,
            CounterFilterXFERSZRegs
        );
    };

    //init the QAT accelerator
    auto createQATPMU = [](const size_t numaNode, const size_t socketId, const size_t domain, const size_t bus, const size_t dev, const size_t func) -> IDX_PMU
    {
        const auto n_regs = SPR_QAT_ACCEL_COUNTER_MAX_NUM;
        auto GlobalConfigReg= std::make_shared<QATTelemetryVirtualGeneralConfigRegister>(domain, bus, dev, func);
        std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs, CounterFilterWQRegs, CounterFilterENGRegs;
        std::vector<std::shared_ptr<HWRegister> > CounterFilterTCRegs, CounterFilterPGSZRegs, CounterFilterXFERSZRegs;
        for (size_t r = 0; r < n_regs; ++r)
        {
            auto CounterControlReg= std::make_shared<QATTelemetryVirtualControlRegister>();
            CounterControlRegs.push_back(CounterControlReg);
            CounterValueRegs.push_back(std::make_shared<QATTelemetryVirtualCounterRegister>(GlobalConfigReg, CounterControlReg, r));
            CounterFilterWQRegs.push_back(std::make_shared<VirtualDummyRegister>()); //dummy
            CounterFilterENGRegs.push_back(std::make_shared<VirtualDummyRegister>()); //dummy
            CounterFilterTCRegs.push_back(std::make_shared<VirtualDummyRegister>()); //dummy
            CounterFilterPGSZRegs.push_back(std::make_shared<VirtualDummyRegister>()); //dummy
            CounterFilterXFERSZRegs.push_back(std::make_shared<VirtualDummyRegister>()); //dummy
        }

        return IDX_PMU(
            false,
            numaNode,
            socketId,
            std::make_shared<VirtualDummyRegister>(),
            std::make_shared<VirtualDummyRegister>(),
            GlobalConfigReg,
            CounterControlRegs,
            CounterValueRegs,
            CounterFilterWQRegs,
            CounterFilterENGRegs,
            CounterFilterTCRegs,
            CounterFilterPGSZRegs,
            CounterFilterXFERSZRegs
        );
    };

    if (supportIDXAccelDev() == true)
    {
        static const uint32 IAA_DEV_IDS[] = { 0x0CFE };
        static const uint32 DSA_DEV_IDS[] = { 0x0B25 };
        static const uint32 QAT_DEV_IDS[] = { 0x4940, 0x4942, 0x4944 };
        std::vector<std::pair<uint32, uint32> > socket2IAAbus;
        std::vector<std::pair<uint32, uint32> > socket2DSAbus;
        std::vector<std::pair<uint32, uint32> > socket2QATbus;
        std::map<int, int> rootbusMap;

        //Enumurate IDX devices by PCIe bus scan
        initSocket2Bus(socket2IAAbus, SPR_IDX_IAA_REGISTER_DEV_ADDR, SPR_IDX_IAA_REGISTER_FUNC_ADDR, IAA_DEV_IDS, (uint32)sizeof(IAA_DEV_IDS) / sizeof(IAA_DEV_IDS[0]));
        initSocket2Bus(socket2DSAbus, SPR_IDX_DSA_REGISTER_DEV_ADDR, SPR_IDX_DSA_REGISTER_FUNC_ADDR, DSA_DEV_IDS, (uint32)sizeof(DSA_DEV_IDS) / sizeof(DSA_DEV_IDS[0]));
        initSocket2Bus(socket2QATbus, SPR_IDX_QAT_REGISTER_DEV_ADDR, SPR_IDX_QAT_REGISTER_FUNC_ADDR, QAT_DEV_IDS, (uint32)sizeof(QAT_DEV_IDS) / sizeof(QAT_DEV_IDS[0]));
#ifndef PCM_SILENT
        std::cerr << "Info: IDX - Detected " << socket2IAAbus.size() << " IAA devices, " << socket2DSAbus.size() << " DSA devices, " << socket2QATbus.size() << " QAT devices. \n";
#endif
        initRootBusMap(rootbusMap);

        idxPMUs.resize(IDX_MAX);
        idxPMUs[IDX_IAA].clear();
        if (socket2IAAbus.size())
        {
            std::vector<struct idx_accel_dev_info> devInfos;
            getIDXDevBAR(socket2IAAbus, SPR_IDX_IAA_REGISTER_DEV_ADDR, SPR_IDX_IAA_REGISTER_FUNC_ADDR, rootbusMap, devInfos);
            for (auto & devInfo : devInfos)
            {
                idxPMUs[IDX_IAA].push_back(createIDXPMU(devInfo.mem_bar, SPR_IDX_ACCEL_BAR0_SIZE, devInfo.numa_node, devInfo.socket_id));
            }
        }

        idxPMUs[IDX_DSA].clear();
        if (socket2DSAbus.size())
        {
            std::vector<struct idx_accel_dev_info> devInfos;
            getIDXDevBAR(socket2DSAbus, SPR_IDX_DSA_REGISTER_DEV_ADDR, SPR_IDX_DSA_REGISTER_FUNC_ADDR, rootbusMap, devInfos);
            for (auto & devInfo : devInfos)
            {
                idxPMUs[IDX_DSA].push_back(createIDXPMU(devInfo.mem_bar, SPR_IDX_ACCEL_BAR0_SIZE, devInfo.numa_node, devInfo.socket_id));
            }
        }

        idxPMUs[IDX_QAT].clear();
#ifdef __linux__
        if (socket2QATbus.size())
        {
            std::vector<struct idx_accel_dev_info> devInfos;
            getIDXDevBAR(socket2QATbus, SPR_IDX_QAT_REGISTER_DEV_ADDR, SPR_IDX_QAT_REGISTER_FUNC_ADDR, rootbusMap, devInfos);
            for (auto & devInfo : devInfos)
            {
                std::ostringstream qat_TLMCTL_sysfs_path(std::ostringstream::out);
                /*parse telemetry follow rule of out of tree driver*/
                qat_TLMCTL_sysfs_path << std::string("/sys/bus/pci/devices/") <<
                    std::hex << std::setw(4) << std::setfill('0') << devInfo.domain << ":" <<
                    std::hex << std::setw(2) << std::setfill('0') << devInfo.bus << ":" <<
                    std::hex << std::setw(2) << std::setfill('0') << devInfo.dev << "." <<
                    std::hex << devInfo.func << "/telemetry/control";
                std::string qatTLMCTLStr = readSysFS(qat_TLMCTL_sysfs_path.str().c_str(), true);
                if (!qatTLMCTLStr.size()) //check TLM feature available or NOT.
                {
                    qat_TLMCTL_sysfs_path.str("");
                    /*parse telemetry follow rule of in tree driver*/
                    qat_TLMCTL_sysfs_path << std::string("/sys/kernel/debug/qat_4xxx_") <<
                        std::hex << std::setw(4) << std::setfill('0') << devInfo.domain << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << devInfo.bus << ":" <<
                        std::hex << std::setw(2) << std::setfill('0') << devInfo.dev << "." <<
                        std::hex << devInfo.func << "/telemetry/control";                    
                    qatTLMCTLStr = readSysFS(qat_TLMCTL_sysfs_path.str().c_str(), true);
                    if(!qatTLMCTLStr.size()){
                        std::cerr << "Warning: IDX - QAT telemetry feature of B:0x" << std::hex << devInfo.bus << ",D:0x" << devInfo.dev << ",F:0x" << devInfo.func \
                            << " is NOT available, skipped." << std::dec << std::endl;
                        continue;
                    }
                }
                idxPMUs[IDX_QAT].push_back(createQATPMU(devInfo.numa_node, devInfo.socket_id, devInfo.domain , devInfo.bus, devInfo.dev , devInfo.func));
            }
        }
#endif
    }

    // init IRP PMU
    int irpStacks = 0;
    size_t IRP_CTL_REG_OFFSET = 0;
    size_t IRP_CTR_REG_OFFSET = 0;
    const uint32* IRP_UNIT_CTL = nullptr;

    switch (getCPUFamilyModel())
    {
    case SKX:
        irpStacks = SKX_IIO_STACK_COUNT;
        IRP_CTL_REG_OFFSET = SKX_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = SKX_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = SKX_IRP_UNIT_CTL;
        break;
    case ICX:
        irpStacks = ICX_IIO_STACK_COUNT;
        IRP_CTL_REG_OFFSET = ICX_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = ICX_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = ICX_IRP_UNIT_CTL;
        break;
    case SNOWRIDGE:
        irpStacks = SNR_IIO_STACK_COUNT;
        IRP_CTL_REG_OFFSET = SNR_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = SNR_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = SNR_IRP_UNIT_CTL;
        break;
    case SPR:
    case EMR:
        irpStacks = SPR_M2IOSF_NUM;
        IRP_CTL_REG_OFFSET = SPR_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = SPR_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = SPR_IRP_UNIT_CTL;
        break;
    case GNR:
    case SRF:
        irpStacks = BHS_M2IOSF_NUM;
        IRP_CTL_REG_OFFSET = BHS_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = BHS_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = BHS_IRP_UNIT_CTL;
        break;
    case GRR:
        irpStacks = GRR_M2IOSF_NUM;
        IRP_CTL_REG_OFFSET = GRR_IRP_CTL_REG_OFFSET;
        IRP_CTR_REG_OFFSET = GRR_IRP_CTR_REG_OFFSET;
        IRP_UNIT_CTL = GRR_IRP_UNIT_CTL;
        break;
    }
    irpPMUs.resize(num_sockets);
    if (IRP_UNIT_CTL)
    {
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            auto& handle = MSR[socketRefCore[s]];
            for (int unit = 0; unit < irpStacks; ++unit)
            {
                irpPMUs[s][unit] = UncorePMU(
                    std::make_shared<MSRRegister>(handle, IRP_UNIT_CTL[unit]),
                    std::make_shared<MSRRegister>(handle, IRP_UNIT_CTL[unit] + IRP_CTL_REG_OFFSET + 0),
                    std::make_shared<MSRRegister>(handle, IRP_UNIT_CTL[unit] + IRP_CTL_REG_OFFSET + 1),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>(),
                    std::make_shared<MSRRegister>(handle, IRP_UNIT_CTL[unit] + IRP_CTR_REG_OFFSET + 0),
                    std::make_shared<MSRRegister>(handle, IRP_UNIT_CTL[unit] + IRP_CTR_REG_OFFSET + 1),
                    std::shared_ptr<MSRRegister>(),
                    std::shared_ptr<MSRRegister>()
                );
            }
        }
    }
#if 0
    auto findPCICFGPMU = [](const uint32 did,
                            const int s,
                            const uint32 CtlOffset,
                            const std::vector<uint32> & CounterControlOffsets,
                            const std::vector<uint32> & CounterValueOffsets)
    {
        int found = 0;
        UncorePMU out;
        forAllIntelDevices([&](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
        {
            if (device_id == did)
            {
                if (s == found)
                {
                    auto handle = std::make_shared<PciHandleType>(group, bus, device, function);
                    const size_t n_regs = 4;
                    std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs;
                    for (size_t r = 0; r < n_regs; ++r)
                    {
                        CounterControlRegs.push_back(std::make_shared<PCICFGRegister32>(handle, CounterControlOffsets[r]));
                        CounterValueRegs.push_back(std::make_shared<PCICFGRegister64>(handle, CounterValueOffsets[r]));
                    }
                    auto boxCtlRegister = std::make_shared<PCICFGRegister32>(handle, CtlOffset);
                    // std::cerr << "socket " << std::hex <<  s <<  " device " << device_id << " " << group << ":" << bus << ":" << device << "@" << function << "\n" << std::dec;
                    out = UncorePMU(boxCtlRegister, CounterControlRegs, CounterValueRegs);
                }
                ++found;
            }
        });
        return out;
    };
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        switch (cpu_family_model)
        {
            case BDX:
                irpPMUs[s][0] = findPCICFGPMU(0x6f39, s, 0xF4, {0xD8, 0xDC, 0xE0, 0xE4}, {0xA0, 0xB0, 0xB8, 0xC0});
                iioPMUs[s][0] = findPCICFGPMU(0x6f34, s, 0xF4, {0xD8, 0xDC, 0xE0, 0xE4}, {0xA0, 0xA8, 0xB0, 0xB8});
                break;
        }
    }
#endif

    if (hasPCICFGUncore() && MSR.size())
    {
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            uncorePMUs[s].resize(1);
            auto & handle = MSR[socketRefCore[s]];
            for (uint32 cbo = 0; cbo < getMaxNumOfCBoxesInternal(); ++cbo)
            {
                assert(CX_MSR_PMON_BOX_CTL(cbo));
                const auto filter1MSR = CX_MSR_PMON_BOX_FILTER1(cbo);
                std::shared_ptr<HWRegister> filter1MSRHandle = filter1MSR ? std::make_shared<MSRRegister>(handle, filter1MSR) : std::shared_ptr<HWRegister>();
                uncorePMUs[s][0][CBO_PMU_ID].push_back(std::make_shared<UncorePMU>(
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

    if (1)
    {
        cxlPMUs.resize(num_sockets);
        for (uint32 s = 0; s < (uint32)num_sockets; ++s)
        {
            if (uncorePMUDiscovery.get())
            {
                auto createCXLPMU = [this](const uint32 s, const unsigned BoxType, const size_t pos) -> UncorePMU
                {
                    std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs;
                    const auto n_regs = uncorePMUDiscovery->getBoxNumRegs(BoxType, s, pos);
                    const auto unitControlAddr = uncorePMUDiscovery->getBoxCtlAddr(BoxType, s, pos);
                    const auto unitControlAddrAligned = unitControlAddr & ~4095ULL;
                    auto handle = std::make_shared<MMIORange>(unitControlAddrAligned, CXL_PMON_SIZE, false);
                    for (size_t r = 0; r < n_regs; ++r)
                    {
                        CounterControlRegs.push_back(std::make_shared<MMIORegister64>(handle, uncorePMUDiscovery->getBoxCtlAddr(BoxType, s, pos, r) - unitControlAddrAligned));
                        CounterValueRegs.push_back(std::make_shared<MMIORegister64>(handle, uncorePMUDiscovery->getBoxCtrAddr(BoxType, s, pos, r) - unitControlAddrAligned));
                    }
                    return UncorePMU(std::make_shared<MMIORegister64>(handle, unitControlAddr - unitControlAddrAligned), CounterControlRegs, CounterValueRegs);
                };

                switch (getCPUFamilyModel())
                {
                    case PCM::SPR:
                    case PCM::EMR:
                    case PCM::GNR:
                    case PCM::SRF:
                    {
                        const auto n_units = (std::min)(uncorePMUDiscovery->getNumBoxes(SPR_CXLCM_BOX_TYPE, s),
                            uncorePMUDiscovery->getNumBoxes(SPR_CXLDP_BOX_TYPE, s));
                        for (size_t pos = 0; pos < n_units; ++pos)
                        {
                            try
                            {
                                cxlPMUs[s].push_back(std::make_pair(createCXLPMU(s, SPR_CXLCM_BOX_TYPE, pos), createCXLPMU(s, SPR_CXLDP_BOX_TYPE, pos)));
                            }
                            catch (const std::exception& e)
                            {
                                std::cerr << "CXL PMU initialization for socket " << s << " at position " << pos << " failed: " << e.what() << std::endl;
                            }
                        }
                    }
                    break;
                }
            }
        }
    }
}

#ifdef PCM_USE_PERF
std::vector<int> enumeratePerfPMUs(const std::string & type, int max_id);
void populatePerfPMUs(unsigned socket_, const std::vector<int> & ids, std::vector<UncorePMU> & pmus, bool fixed, bool filter0 = false, bool filter1 = false);
void populatePerfPMUs(unsigned socket_, const std::vector<int>& ids, std::vector<UncorePMURef>& pmus, bool fixed, bool filter0 = false, bool filter1 = false);

std::vector<std::pair<int, uint32> > enumerateIDXPerfPMUs(const std::string & type, int max_id);
void populateIDXPerfPMUs(unsigned socket_, const std::vector<std::pair<int, uint32> > & ids, std::vector<IDX_PMU> & pmus);
#endif

void PCM::initUncorePMUsPerf()
{
#ifdef PCM_USE_PERF
    uncorePMUs.resize(num_sockets);
    iioPMUs.resize(num_sockets);
    irpPMUs.resize(num_sockets);
    for (uint32 s = 0; s < (uint32)num_sockets; ++s)
    {
        uncorePMUs[s].resize(1);
        populatePerfPMUs(s, enumeratePerfPMUs("pcu", 100), uncorePMUs[s][0][PCU_PMU_ID], false, true);
        populatePerfPMUs(s, enumeratePerfPMUs("ubox", 100), uncorePMUs[s][0][UBOX_PMU_ID], true);
        populatePerfPMUs(s, enumeratePerfPMUs("cbox", 100), uncorePMUs[s][0][CBO_PMU_ID], false, true, true);
        populatePerfPMUs(s, enumeratePerfPMUs("cha", 200), uncorePMUs[s][0][CBO_PMU_ID], false, true, true);
        populatePerfPMUs(s, enumeratePerfPMUs("mdf", 200), uncorePMUs[s][0][MDF_PMU_ID], false, true, true);
        auto populateMapPMUs = [&s](const std::string& type, std::vector<std::map<int32, UncorePMU> > & out)
        {
            std::vector<UncorePMU> PMUVector;
            populatePerfPMUs(s, enumeratePerfPMUs(type, 100), PMUVector, false);
            for (size_t i = 0; i < PMUVector.size(); ++i)
            {
                out[s][i] = PMUVector[i];
            }
        };
        populateMapPMUs("iio", iioPMUs);
        populateMapPMUs("irp", irpPMUs);
    }

    if (supportIDXAccelDev() == true)
    {
        idxPMUs.resize(IDX_MAX);
        idxPMUs[IDX_IAA].clear();
        idxPMUs[IDX_DSA].clear();
        idxPMUs[IDX_QAT].clear(); //QAT NOT support perf driver mode.
        populateIDXPerfPMUs(0, enumerateIDXPerfPMUs("iax", 100), idxPMUs[IDX_IAA]);
        populateIDXPerfPMUs(0, enumerateIDXPerfPMUs("dsa", 100), idxPMUs[IDX_DSA]);
#ifndef PCM_SILENT
        std::cerr << "Info: IDX - Detected " << idxPMUs[IDX_IAA].size() << " IAA devices, " << idxPMUs[IDX_DSA].size() << " DSA devices.\n";
        std::cerr << "Warning: IDX - QAT device NOT support perf driver mode.\n";
#endif
    }
#endif
}

#ifdef __linux__

const char * keepNMIWatchdogEnabledEnvStr = "PCM_KEEP_NMI_WATCHDOG";

bool keepNMIWatchdogEnabled()
{
    static int keep = -1;
    if (keep < 0)
    {
        keep = (safe_getenv(keepNMIWatchdogEnabledEnvStr) == std::string("1")) ? 1 : 0;
    }
    return keep == 1;
}

#define PCM_NMI_WATCHDOG_PATH "/proc/sys/kernel/nmi_watchdog"

bool isNMIWatchdogEnabled(const bool silent)
{
    const auto watchdog = readSysFS(PCM_NMI_WATCHDOG_PATH, silent);
    if (watchdog.length() == 0)
    {
        return false;
    }

    return (std::atoi(watchdog.c_str()) == 1);
}

void disableNMIWatchdog(const bool silent)
{
    if (!silent)
    {
        std::cerr << " Disabling NMI watchdog since it consumes one hw-PMU counter. To keep NMI watchdog set environment variable "
                  << keepNMIWatchdogEnabledEnvStr << "=1 (this reduces the core metrics set)\n";
    }
    writeSysFS(PCM_NMI_WATCHDOG_PATH, "0");
}

void enableNMIWatchdog(const bool silent)
{
    if (!silent) std::cerr << " Re-enabling NMI watchdog.\n";
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
    CoreTaskQueue & operator = (CoreTaskQueue &) = delete;
public:
    CoreTaskQueue(int32 core) :
        worker([=]() {
        try {
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
        }
        catch (const std::exception & e)
        {
            std::cerr << "PCM Error. Exception in CoreTaskQueue worker function: " << e.what() << "\n";
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

std::ofstream* PCM::outfile = nullptr;       // output file stream
std::streambuf* PCM::backup_ofile = nullptr; // backup of original output = cout
std::streambuf* PCM::backup_ofile_cerr = nullptr; // backup of original output = cerr

#ifdef __linux__
void increaseULimit()
{
    rlimit lim{};
    if (getrlimit(RLIMIT_NOFILE, &lim) == 0)
    {
        const rlim_t recommendedLimit = 1000000;
        // std::cout << "file open limit: " << lim.rlim_cur << "," << lim.rlim_max << "\n";
        if (lim.rlim_cur < recommendedLimit || lim.rlim_max < recommendedLimit)
        {
            lim.rlim_cur = lim.rlim_max = recommendedLimit;
            if (setrlimit(RLIMIT_NOFILE, &lim) != 0)
            {
                std::cerr << "PCM Info: setrlimit for file limit " << recommendedLimit << " failed with error " << strerror(errno) << "\n";
            }
        }
    }
    else
    {
       std::cerr << "PCM Info: getrlimit for file limit failed with error " << strerror(errno) << "\n";
    }
}
#endif

PCM::PCM() :
    cpu_family(-1),
    cpu_model_private(-1),
    cpu_family_model(-1),
    cpu_stepping(-1),
    cpu_microcode_level(-1),
    max_cpuid(0),
    threads_per_core(0),
    num_cores(0),
    num_sockets(0),
    num_phys_cores_per_socket(0),
    num_online_cores(0),
    num_online_sockets(0),
    accel(0),
    accel_counters_num_max(0),
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
    canUsePerf(false),
    run_state(1),
    needToRestoreNMIWatchdog(false)
{
#ifdef __linux__
    increaseULimit();
#endif
#ifdef _MSC_VER
    // WARNING: This driver code (msr.sys) is only for testing purposes, not for production use
    Driver drv(Driver::msrLocalPath());
    // drv.stop();     // restart driver (usually not needed)
    if (!drv.start())
    {
        tcerr << "Cannot access CPU counters\n";
        tcerr << "You must have a signed  driver at " << drv.driverPath() << " and have administrator rights to run this program\n";
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

#ifndef PCM_DEBUG_TOPOLOGY
    if (safe_getenv("PCM_PRINT_TOPOLOGY") == "1")
#endif
    {
        printDetailedSystemTopology(1);
    }

    initEnergyMonitoring();

#ifndef PCM_SILENT
    std::cerr << "\n";
#endif

    uncorePMUDiscovery = std::make_shared<UncorePMUDiscovery>();

    initUncoreObjects();

    initRDT();

    readCPUMicrocodeLevel();

#ifdef PCM_USE_PERF
    canUsePerf = true;
    perfEventHandle.resize(num_cores, std::vector<int>(PERF_MAX_COUNTERS, -1));
    std::fill(perfTopDownPos.begin(), perfTopDownPos.end(), 0);
#endif

    for (int32 i = 0; i < num_cores; ++i)
    {
        coreTaskQueues.push_back(std::make_shared<CoreTaskQueue>(i));
    }

#ifndef PCM_SILENT
    std::cerr << "\n";
#endif
}

void PCM::printDetailedSystemTopology(const int detailLevel)
{
    // produce debug output similar to Intel MPI cpuinfo
    if (true)
    {
        std::cerr << "\n=====  Processor topology  =====\n";
        std::cerr << "OS_Processor    Thread_Id       Core_Id         ";
        if (detailLevel > 0) std::cerr << "Module_Id       ";
        std::cerr << "Tile_Id         ";
        if (detailLevel > 0) std::cerr << "Die_Id          Die_Group_Id    ";
        std::cerr << "Package_Id      Core_Type       Native_CPU_Model\n";
        std::map<uint32, std::vector<uint32> > os_id_by_core, os_id_by_tile, core_id_by_socket;
        size_t counter = 0;
        for (auto it = topology.begin(); it != topology.end(); ++it)
        {
            std::cerr << std::left << std::setfill(' ')
                << std::setw(16) << ((it->os_id >= 0) ? it->os_id : counter)
                << std::setw(16) << it->thread_id
                << std::setw(16) << it->core_id;
            if (detailLevel > 0) std::cerr << std::setw(16) << it->module_id;
            std::cerr << std::setw(16) << it->tile_id;
            if (detailLevel > 0) std::cerr << std::setw(16) << it->die_id << std::setw(16) << it->die_grp_id;
            std::cerr << std::setw(16) << it->socket_id
                << std::setw(16) << it->getCoreTypeStr()
                << std::setw(16) << it->native_cpu_model
                << "\n";
            if (std::find(core_id_by_socket[it->socket_id].begin(), core_id_by_socket[it->socket_id].end(), it->core_id)
                == core_id_by_socket[it->socket_id].end())
                core_id_by_socket[it->socket_id].push_back(it->core_id);
            // add socket offset to distinguish cores and tiles from different sockets
            os_id_by_core[(it->socket_id << 15) + it->core_id].push_back(it->os_id);
            os_id_by_tile[(it->socket_id << 15) + it->tile_id].push_back(it->os_id);

            ++counter;
        }
        std::cerr << "=====  Placement on packages  =====\n";
        std::cerr << "Package Id.    Core Id.     Processors\n";
        for (auto pkg = core_id_by_socket.begin(); pkg != core_id_by_socket.end(); ++pkg)
        {
            auto core_id = pkg->second.begin();
            std::cerr << std::left << std::setfill(' ') << std::setw(15) << pkg->first << *core_id;
            for (++core_id; core_id != pkg->second.end(); ++core_id)
            {
                std::cerr << "," << *core_id;
            }
            std::cerr << "\n";
        }
        std::cerr << "\n=====  Core/Tile sharing  =====\n";
        std::cerr << "Level      Processors\nCore       ";
        for (auto core = os_id_by_core.begin(); core != os_id_by_core.end(); ++core)
        {
            auto os_id = core->second.begin();
            std::cerr << "(" << *os_id;
            for (++os_id; os_id != core->second.end(); ++os_id) {
                std::cerr << "," << *os_id;
            }
            std::cerr << ")";
        }
        std::cerr << "\nTile / L2$ ";
        for (auto core = os_id_by_tile.begin(); core != os_id_by_tile.end(); ++core)
        {
            auto os_id = core->second.begin();
            std::cerr << "(" << *os_id;
            for (++os_id; os_id != core->second.end(); ++os_id) {
                std::cerr << "," << *os_id;
            }
            std::cerr << ")";
        }
        std::cerr << "\n";
        std::cerr << "\n";
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
    for (size_t i = 0; i < (size_t)serverUncorePMUs.size(); ++i)
    {
            if(serverUncorePMUs[i].get()) serverUncorePMUs[i]->enableJKTWorkaround(enable);
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
    return (topology[os_core_id].os_id != -1) && (topology[os_core_id].core_id != -1) && (topology[os_core_id].socket_id != -1);
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
            || model_ == ELKHART_LAKE
            || model_ == JASPER_LAKE
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
            || model_ == ADL
            || model_ == RPL
            || model_ == MTL
            || model_ == LNL
            || model_ == ARL
            || model_ == SKX
            || model_ == ICX
            || model_ == SPR
            || model_ == EMR
            || model_ == GNR
            || model_ == GRR
            || model_ == SRF
           );
}

bool PCM::checkModel()
{
    switch (cpu_family_model)
    {
        case NEHALEM:
            cpu_family_model = NEHALEM_EP;
            break;
        case ATOM_2:
            cpu_family_model = ATOM;
            break;
        case HASWELL_ULT:
        case HASWELL_2:
            cpu_family_model = HASWELL;
            break;
        case BROADWELL_XEON_E3:
            cpu_family_model = BROADWELL;
            break;
        case ICX_D:
            cpu_family_model = ICX;
            break;
        case CML_1:
            cpu_family_model = CML;
            break;
        case ARL_1:
            cpu_family_model = ARL;
            break;
        case ICL_1:
            cpu_family_model = ICL;
            break;
        case TGL_1:
            cpu_family_model = TGL;
            break;
        case ADL_1:
            cpu_family_model = ADL;
            break;
        case RPL_1:
        case RPL_2:
        case RPL_3:
            cpu_family_model = RPL;
            break;
        case GNR_D:
            cpu_family_model = GNR;
            break;
    }

    if(!isCPUModelSupported((int)cpu_family_model))
    {
        std::cerr << getUnsupportedMessage() << " CPU family " << cpu_family << " model number " << cpu_model_private << " Brand: \"" << getCPUBrandString().c_str() << "\"\n";
/* FOR TESTING PURPOSES ONLY */
#ifdef PCM_TEST_FALLBACK_TO_ATOM
        std::cerr << "Fall back to ATOM functionality.\n";
        cpu_family_model = ATOM;
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
    if (instance)
    {
        destroyMSR();
        instance = NULL;
        deleteAndNullify(systemTopology);
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
    e.pinned = 0;
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

PCM::ErrorCode PCM::program(const PCM::ProgramMode mode_, const void * parameter_, const bool silent, const int pid)
{
#ifdef __linux__
    if (isNMIWatchdogEnabled(silent) && (keepNMIWatchdogEnabled() == false))
    {
        disableNMIWatchdog(silent);
        needToRestoreNMIWatchdog = true;
    }
#endif

    if (MSR.empty()) return PCM::MSRAccessDenied;

    ExtendedCustomCoreEventDescription * pExtDesc = (ExtendedCustomCoreEventDescription *)parameter_;

#ifdef PCM_USE_PERF
    closePerfHandles(silent);
    if (!silent) std::cerr << "Trying to use Linux perf events...\n";
    const char * no_perf_env = std::getenv("PCM_NO_PERF");
    if (no_perf_env != NULL && std::string(no_perf_env) == std::string("1"))
    {
        canUsePerf = false;
        if (!silent) std::cerr << "Usage of Linux perf events is disabled through PCM_NO_PERF environment variable. Using direct PMU programming...\n";
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
        if (!silent) std::cerr << "Can not use Linux perf because your Linux kernel does not support PERF_COUNT_HW_REF_CPU_CYCLES event. Falling-back to direct PMU programming.\n";
    }
    else if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->fixedCfg && (pExtDesc->fixedCfg->value & 0x444))
    {
        canUsePerf = false;
        if (!silent)
        {
             std::cerr << "Can not use Linux perf because \"any_thread\" fixed counter configuration requested (0x" << std::hex << pExtDesc->fixedCfg->value
                       << std::dec << ") =\n" << *(pExtDesc->fixedCfg) << "\nFalling-back to direct PMU programming.\n\n";
        }
    }
    else if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && (pExtDesc->OffcoreResponseMsrValue[0] || pExtDesc->OffcoreResponseMsrValue[1]))
    {
        const std::string offcore_rsp_format = readSysFS("/sys/bus/event_source/devices/cpu/format/offcore_rsp");
        if (offcore_rsp_format != "config1:0-63\n")
        {
            canUsePerf = false;
            if (!silent) std::cerr << "Can not use Linux perf because OffcoreResponse usage is not supported. Falling-back to direct PMU programming.\n";
        }
    }
    if (isHWTMAL1Supported() == true && perfSupportsTopDown() == false && pid == -1)
    {
        canUsePerf = false;
        if (!silent) std::cerr << "Installed Linux kernel perf does not support hardware top-down level-1 counters. Using direct PMU programming instead.\n";
    }
    if (canUsePerf &&    (cpu_family_model == ADL
                       || cpu_family_model == RPL
                       || cpu_family_model == MTL
                       || cpu_family_model == LNL
                       || cpu_family_model == ARL
                         ))
    {
        canUsePerf = false;
        if (!silent) std::cerr << "Linux kernel perf rejects an architectural event on your platform. Using direct PMU programming instead.\n";
    }

    if (canUsePerf == false && noMSRMode())
    {
        std::cerr << "ERROR: can not use perf driver and no-MSR mode is enabled\n" ;
        return PCM::UnknownError;
    }
#endif

    if (programmed_core_pmu == false)
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
        if (isAtom() == false && cpu_family_model != KNL)
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
        auto LLCArchEventInit = [](CustomCoreEventDescription * evt)
        {
            evt[0].event_number = ARCH_LLC_MISS_EVTNR;
            evt[0].umask_value = ARCH_LLC_MISS_UMASK;
            evt[1].event_number = ARCH_LLC_REFERENCE_EVTNR;
            evt[1].umask_value = ARCH_LLC_REFERENCE_UMASK;
        };
        if (isAtom() || cpu_family_model == KNL)
        {
            LLCArchEventInit(coreEventDesc);
            L2CacheHitRatioAvailable = true;
            L2CacheMissesAvailable = true;
            L2CacheHitsAvailable = true;
            core_gen_counter_num_used = 2;
        }
        else if (memoryEventErrata())
        {
            LLCArchEventInit(coreEventDesc);
            L3CacheHitRatioAvailable = true;
            L3CacheMissesAvailable = true;
            L2CacheMissesAvailable = true;
            L3CacheHitsAvailable = true;
            core_gen_counter_num_used = 2;
            if (HASWELLX == cpu_family_model || HASWELL == cpu_family_model)
            {
                coreEventDesc[BasicCounterState::HSXL2MissPos].event_number = HSX_L2_RQSTS_MISS_EVTNR;
                coreEventDesc[BasicCounterState::HSXL2MissPos].umask_value = HSX_L2_RQSTS_MISS_UMASK;
                coreEventDesc[BasicCounterState::HSXL2RefPos].event_number = HSX_L2_RQSTS_REFERENCES_EVTNR;
                coreEventDesc[BasicCounterState::HSXL2RefPos].umask_value = HSX_L2_RQSTS_REFERENCES_UMASK;
                L2CacheHitRatioAvailable = true;
                L2CacheHitsAvailable = true;
                core_gen_counter_num_used = 4;
            }
        }
        else
        switch (cpu_family_model) {
            case ADL:
            case RPL:
            case MTL:
            case LNL:
            case ARL:
                LLCArchEventInit(hybridAtomEventDesc);
                hybridAtomEventDesc[2].event_number = SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR;
                hybridAtomEventDesc[2].umask_value = SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK;
                hybridAtomEventDesc[3].event_number = SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                hybridAtomEventDesc[3].umask_value = SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK;
                LLCArchEventInit(coreEventDesc);
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
            case SNOWRIDGE:
            case ELKHART_LAKE:
            case JASPER_LAKE:
                LLCArchEventInit(coreEventDesc);
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
            case GRR:
            case SRF:
                LLCArchEventInit(coreEventDesc);
                coreEventDesc[2].event_number = CMT_MEM_LOAD_RETIRED_L2_MISS_EVTNR;
                coreEventDesc[2].umask_value = CMT_MEM_LOAD_RETIRED_L2_MISS_UMASK;
                coreEventDesc[3].event_number = CMT_MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = CMT_MEM_LOAD_RETIRED_L2_HIT_UMASK;
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
            case SPR:
            case EMR:
            case GNR:
                assert(useSkylakeEvents());
                coreEventDesc[0].event_number = SKL_MEM_LOAD_RETIRED_L3_MISS_EVTNR;
                coreEventDesc[0].umask_value = SKL_MEM_LOAD_RETIRED_L3_MISS_UMASK;
                coreEventDesc[1].event_number = SKL_MEM_LOAD_RETIRED_L3_HIT_EVTNR;
                coreEventDesc[1].umask_value = SKL_MEM_LOAD_RETIRED_L3_HIT_UMASK;
                coreEventDesc[2].event_number = SKL_MEM_LOAD_RETIRED_L2_MISS_EVTNR;
                coreEventDesc[2].umask_value = SKL_MEM_LOAD_RETIRED_L2_MISS_UMASK;
                coreEventDesc[3].event_number = SKL_MEM_LOAD_RETIRED_L2_HIT_EVTNR;
                coreEventDesc[3].umask_value = SKL_MEM_LOAD_RETIRED_L2_HIT_UMASK;
                if (core_gen_counter_num_max == 2)
                {
                    L3CacheHitRatioAvailable = true;
                    L3CacheMissesAvailable = true;
                    L3CacheHitsSnoopAvailable = true;
                    L3CacheHitsAvailable = true;
                    core_gen_counter_num_used = 2;
                    break;
                }
                else if (core_gen_counter_num_max == 3)
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

    if(EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && (pExtDesc->gpCounterCfg || pExtDesc->gpCounterHybridAtomCfg))
    {
        core_gen_counter_num_used = pExtDesc->nGPCounters;
    }

    if(cpu_family_model == JAKETOWN)
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
    if (pid != -1 && canUsePerf == false)
    {
        std::cerr << "PCM ERROR: pid monitoring is only supported with Linux perf_event driver\n";
        return PCM::UnknownError;
    }
#ifdef __linux__
    if (isNMIWatchdogEnabled(silent) && (canUsePerf == false))
    {
        std::cerr << "PCM ERROR: Unsupported mode. NMI watchdog is enabled and Linux perf_event driver is not used\n";
        return PCM::UnknownError;
    }
#endif

    std::vector<int> tids{};
    #ifdef PCM_USE_PERF
    if (pid != -1)
    {
        const auto strDir = std::string("/proc/") +  std::to_string(pid) + "/task/";
        DIR * tidDir = opendir(strDir.c_str());
        if (tidDir)
        {
            struct dirent * entry{nullptr};
            while ((entry = readdir(tidDir)) != nullptr)
            {
                assert(entry->d_name);
                const auto tid = atoi(entry->d_name);
                if (tid)
                {
                    tids.push_back(tid);
                    // std::cerr << "Detected task " << tids.back() << "\n";
                }
            }
            closedir(tidDir);
        }
        else
        {
            std::cerr << "ERROR: Can't open " << strDir << "\n";
            return PCM::UnknownError;
        }
    }
    if (tids.empty() == false)
    {
        if (isHWTMAL1Supported())
        {
            if (!silent) std::cerr << "INFO: TMA L1 metrics are not supported in PID collection mode\n";
        }
        if (!silent) std::cerr << "INFO: collecting core metrics for " << tids.size() << " threads in process " << pid << "\n";
        PerfEventHandleContainer _1(num_cores, std::vector<int>(PERF_MAX_COUNTERS, -1));
        perfEventTaskHandle.resize(tids.size(), _1);
    }
    #endif

    lastProgrammedCustomCounters.clear();
    lastProgrammedCustomCounters.resize(num_cores);
    core_global_ctrl_value = 0ULL;
    isHWTMAL1Supported(); // ínit value to prevent MT races

    std::vector<std::future<void> > asyncCoreResults;
    std::vector<PCM::ErrorCode> programmingStatuses(num_cores, PCM::Success);

    for (int i = 0; i < (int)num_cores; ++i)
    {
        if (isCoreOnline(i) == false) continue;

        std::packaged_task<void()> task([this, i, mode_, pExtDesc, &programmingStatuses, &tids]() -> void
            {
                TemporalThreadAffinity tempThreadAffinity(i, false); // speedup trick for Linux

                programmingStatuses[i] = programCoreCounters(i, mode_, pExtDesc, lastProgrammedCustomCounters[i], tids);
            });
        asyncCoreResults.push_back(task.get_future());
        coreTaskQueues[i]->push(task);
    }

    for (auto& ar : asyncCoreResults)
        ar.wait();

    for (const auto& status : programmingStatuses)
    {
        if (status != PCM::Success)
        {
            return status;
        }
    }

    programmed_core_pmu = true;

    if (canUsePerf && !silent)
    {
        std::cerr << "Successfully programmed on-core PMU using Linux perf\n";
    }

    if (EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->defaultUncoreProgramming == false)
    {
        return PCM::Success;
    }

    if (hasPCICFGUncore()) // program uncore counters
    {
        std::vector<std::future<uint64>> qpi_speeds;
        for (size_t i = 0; i < (size_t)serverUncorePMUs.size(); ++i)
        {
            serverUncorePMUs[i]->program();
            qpi_speeds.push_back(std::async(std::launch::async,
                &ServerUncorePMUs::computeQPISpeed, serverUncorePMUs[i].get(), socketRefCore[i], cpu_family_model));
        }
        for (size_t i = 0; i < (size_t)serverUncorePMUs.size(); ++i)
        {
            max_qpi_speed = (std::max)(qpi_speeds[i].get(), max_qpi_speed);
        }

        programCbo();

    } // program uncore counters on old CPU arch
    else if (cpu_family_model == NEHALEM_EP || cpu_family_model == WESTMERE_EP || cpu_family_model == CLARKDALE)
    {
        for (int i = 0; i < (int)num_cores; ++i)
        {
            if (isCoreOnline(i) == false) continue;
            TemporalThreadAffinity tempThreadAffinity(i, false); // speedup trick for Linux
            programNehalemEPUncore(i);
        }
    }
    else if (hasBecktonUncore())
    {
        for (int i = 0; i < (int)num_cores; ++i)
        {
            if (isCoreOnline(i) == false) continue;
            TemporalThreadAffinity tempThreadAffinity(i, false); // speedup trick for Linux
            programBecktonUncore(i);
        }
    }

    if (!silent) reportQPISpeed();

    return PCM::Success;
}

void PCM::checkStatus(const PCM::ErrorCode status)
{
    switch (status)
    {
        case pcm::PCM::Success:
        {
            break;
        }
        case pcm::PCM::MSRAccessDenied:
            throw std::system_error(pcm::PCM::MSRAccessDenied, std::generic_category(),
                "Access to Intel(r) Performance Counter Monitor has denied (no MSR or PCI CFG space access).");
        case pcm::PCM::PMUBusy:
            throw std::system_error(pcm::PCM::PMUBusy, std::generic_category(),
                "Access to Intel(r) Performance Counter Monitor has denied (Performance Monitoring Unit"
                " is occupied by other application). Try to stop the application that uses PMU,"
                " or reset PMU configuration from PCM application itself");
        default:
            throw std::system_error(pcm::PCM::UnknownError, std::generic_category(),
                "Access to Intel(r) Performance Counter Monitor has denied (Unknown error).");
    }
}

void PCM::checkError(const PCM::ErrorCode code)
{
    try
    {
        checkStatus(code);
    }
    catch (const std::system_error &e)
    {
        switch (e.code().value())
        {
           case PCM::PMUBusy:
               std::cerr << e.what() << "\n"
                               << "You can try to reset PMU configuration now. Try to reset? (y/n)" << std::endl;
               char yn;
               std::cin >> yn;
               if ('y' == yn)
               {
                   resetPMU();
                   std::cerr << "PMU configuration has been reset. Try to rerun the program again." << std::endl;
               }
               exit(EXIT_FAILURE);
           case PCM::MSRAccessDenied:
           default:
               std::cerr << e.what() << std::endl;
               exit(EXIT_FAILURE);
        }
    }
}

std::mutex printErrorMutex;

PCM::ErrorCode PCM::programCoreCounters(const int i /* core */,
    const PCM::ProgramMode mode_,
    const ExtendedCustomCoreEventDescription * pExtDesc,
    std::vector<EventSelectRegister> & result,
    const std::vector<int> & tids)
{
    (void) tids; // to silence uused param warning on non Linux OS
    // program core counters

    result.clear();
    FixedEventControlRegister ctrl_reg;
    auto initFixedCtrl = [&](const bool & enableCtr3)
    {
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

             if (enableCtr3 && isFixedCounterSupported(3))
             {
                  ctrl_reg.fields.os3 = 1;
                  ctrl_reg.fields.usr3 = 1;
             }
        }
    };
#ifdef PCM_USE_PERF
    int leader_counter = -1;
    auto programPerfEvent = [this, &leader_counter, &i, &tids](perf_event_attr e, const int eventPos, const std::string & eventName) -> bool
    {
        auto programPerfEventHelper = [&i]( PerfEventHandleContainer & perfEventHandle,
                                            perf_event_attr & e,
                                            const int eventPos,
                                            const std::string & eventName,
                                            const int leader_counter,
                                            const int tid) -> bool
        {
            // if (i == 0) std::cerr << "DEBUG: programming event "<< std::hex << e.config << std::dec << "\n";
            if ((perfEventHandle[i][eventPos] = syscall(SYS_perf_event_open, &e, tid,
                i /* core id */, leader_counter /* group leader */, 0)) <= 0)
            {
                std::lock_guard<std::mutex> _(printErrorMutex);
                std::cerr << "Linux Perf: Error when programming " << eventName << ", error: " << strerror(errno) <<
                " with config 0x" << std::hex << e.config <<
                " config1 0x" << e.config1 << std::dec << " for tid " << tid << " leader " << leader_counter << "\n";
                if (24 == errno)
                {
                    std::cerr << PCM_ULIMIT_RECOMMENDATION;
                }
                else
                {
                    std::cerr << "try running with environment variable PCM_NO_PERF=1\n";
                }
                return false;
            }
            return true;
        };
        if (tids.empty() == false)
        {
            e.inherit = 1;
            e.exclude_kernel = 1;
            e.exclude_hv = 1;
            e.read_format = 0; // 'inherit' does not work for combinations of read format (e.g. PERF_FORMAT_GROUP)
            auto handleIt = perfEventTaskHandle.begin();
            for (const auto & tid: tids)
            {
                if (handleIt == perfEventTaskHandle.end())
                {
                    break;
                }
                if (programPerfEventHelper(*handleIt, e, eventPos, eventName, -1, tid) == false)
                {
                    return false;
                }
                ++handleIt;
            }
            return true;
        }
        return programPerfEventHelper(perfEventHandle, e, eventPos, eventName, leader_counter, -1);
    };
    if (canUsePerf)
    {
        initFixedCtrl(false);
        perf_event_attr e = PCM_init_perf_event_attr();
        e.type = PERF_TYPE_HARDWARE;
        e.config = PERF_COUNT_HW_INSTRUCTIONS;
        e.exclude_kernel = 1 - ctrl_reg.fields.os0;
        e.exclude_hv = e.exclude_kernel;
        e.exclude_user = 1 - ctrl_reg.fields.usr0;
        if (programPerfEvent(e, PERF_INST_RETIRED_POS, "INST_RETIRED") == false)
        {
            return PCM::UnknownError;
        }
        leader_counter = perfEventHandle[i][PERF_INST_RETIRED_POS];
        e.config = PERF_COUNT_HW_CPU_CYCLES;
        e.exclude_kernel = 1 - ctrl_reg.fields.os1;
        e.exclude_hv = e.exclude_kernel;
        e.exclude_user = 1 - ctrl_reg.fields.usr1;
        if (programPerfEvent(e, PERF_CPU_CLK_UNHALTED_THREAD_POS, "CPU_CLK_UNHALTED_THREAD") == false)
        {
            return PCM::UnknownError;
        }
        e.config = PCM_PERF_COUNT_HW_REF_CPU_CYCLES;
        e.exclude_kernel = 1 - ctrl_reg.fields.os2;
        e.exclude_hv = e.exclude_kernel;
        e.exclude_user = 1 - ctrl_reg.fields.usr2;
        if (programPerfEvent(e, PERF_CPU_CLK_UNHALTED_REF_POS, "CPU_CLK_UNHALTED_REF") == false)
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

        initFixedCtrl(true);

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

        if (pExtDesc->LoadLatencyMsrValue != ExtendedCustomCoreEventDescription::invalidMsrValue())
        {
            MSR[i]->write(MSR_LOAD_LATENCY, pExtDesc->LoadLatencyMsrValue);
        }
        if (pExtDesc->FrontendMsrValue != ExtendedCustomCoreEventDescription::invalidMsrValue())
        {
            MSR[i]->write(MSR_FRONTEND, pExtDesc->FrontendMsrValue);
        }
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
    uint64 PEBSEnable = 0ULL;
    for (uint32 j = 0; j < core_gen_counter_num_used; ++j)
    {
        if (hybrid == false || (hybrid == true && topology[i].core_type == TopologyEntry::Core))
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
        }
        else if (hybrid == true && topology[i].core_type == TopologyEntry::Atom)
        {
            if (EXT_CUSTOM_CORE_EVENTS == mode_ && pExtDesc && pExtDesc->gpCounterHybridAtomCfg)
            {
                event_select_reg = pExtDesc->gpCounterHybridAtomCfg[j];
                event_select_reg.fields.enable = 1;
            }
            else
            {
                MSR[i]->read(IA32_PERFEVTSEL0_ADDR + j, &event_select_reg.value); // read-only also safe for perf
                setEvent(event_select_reg, hybridAtomEventDesc[j].event_number, hybridAtomEventDesc[j].umask_value);
            }
        }

        result.push_back(event_select_reg);
        if (pExtDesc != nullptr && event_select_reg.fields.event_select == LOAD_LATENCY_EVTNR && event_select_reg.fields.umask == LOAD_LATENCY_UMASK)
        {
            PEBSEnable |= (1ULL << j);
        }
#ifdef PCM_USE_PERF
        if (canUsePerf)
        {
            perf_event_attr e = PCM_init_perf_event_attr();
            e.type = PERF_TYPE_RAW;
            e.config = (1ULL << 63ULL) + event_select_reg.value;
            if (pExtDesc != nullptr)
            {
                if (event_select_reg.fields.event_select == getOCREventNr(0, i).first && event_select_reg.fields.umask == getOCREventNr(0, i).second)
                    e.config1 = pExtDesc->OffcoreResponseMsrValue[0];
                if (event_select_reg.fields.event_select == getOCREventNr(1, i).first && event_select_reg.fields.umask == getOCREventNr(1, i).second)
                    e.config1 = pExtDesc->OffcoreResponseMsrValue[1];

                if (event_select_reg.fields.event_select == LOAD_LATENCY_EVTNR && event_select_reg.fields.umask == LOAD_LATENCY_UMASK)
                {
                    e.config1 = pExtDesc->LoadLatencyMsrValue;
                }
                if (event_select_reg.fields.event_select == FRONTEND_EVTNR && event_select_reg.fields.umask == FRONTEND_UMASK)
                {
                    e.config1 = pExtDesc->FrontendMsrValue;
                }
            }

            if (programPerfEvent(e, PERF_GEN_EVENT_0_POS + j, std::string("generic event #") + std::to_string(j) + std::string(" on core #") + std::to_string(i)) == false)
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

        if (isAtom() || cpu_family_model == KNL)       // KNL and Atom have 3 fixed + only 2 programmable counters
            value = (1ULL << 0) + (1ULL << 1) + (1ULL << 32) + (1ULL << 33) + (1ULL << 34);

        for (uint32 j = 0; j < core_gen_counter_num_used; ++j)
        {
            value |= (1ULL << j); // enable all custom counters (if > 4)
        }

        if (core_global_ctrl_value)
        {
            assert(core_global_ctrl_value == value);
        }
        else
        {
            core_global_ctrl_value = value;
        }

        MSR[i]->write(IA32_PERF_GLOBAL_OVF_CTRL, value);
        MSR[i]->write(IA32_CR_PERF_GLOBAL_CTRL, value);
    }
#ifdef PCM_USE_PERF
    else
    {
	    if (isFixedCounterSupported(3) && isHWTMAL1Supported() && perfSupportsTopDown())
        {
            std::vector<std::pair<const char*, int> > topDownEvents = {  std::make_pair(perfSlotsPath, PERF_TOPDOWN_SLOTS_POS),
                                          std::make_pair(perfBadSpecPath, PERF_TOPDOWN_BADSPEC_POS),
                                          std::make_pair(perfBackEndPath, PERF_TOPDOWN_BACKEND_POS),
                                          std::make_pair(perfFrontEndPath, PERF_TOPDOWN_FRONTEND_POS),
                                          std::make_pair(perfRetiringPath, PERF_TOPDOWN_RETIRING_POS)};
            if (isHWTMAL2Supported())
            {
                topDownEvents.push_back(std::make_pair(perfMemBound, PERF_TOPDOWN_MEM_BOUND_POS));
                topDownEvents.push_back(std::make_pair(perfFetchLat, PERF_TOPDOWN_FETCH_LAT_POS));
                topDownEvents.push_back(std::make_pair(perfBrMispred, PERF_TOPDOWN_BR_MISPRED_POS));
                topDownEvents.push_back(std::make_pair(perfHeavyOps, PERF_TOPDOWN_HEAVY_OPS_POS));
            }
            int readPos = core_fixed_counter_num_used + core_gen_counter_num_used;
            leader_counter = -1;
            for (const auto & event : topDownEvents)
            {
                uint64 eventSel = 0, umask = 0;
                const auto eventDesc = readSysFS(event.first);
                const auto tokens = split(eventDesc, ',');
                for (const auto & token : tokens)
                {
                    if (match(token, "event=", &eventSel))
                    {
                        // found and matched event, wrote value to 'eventSel'
                    }
                    else if (match(token, "umask=", &umask))
                    {
                        // found and matched umask, wrote value to 'umask'
                    }
                    else
                    {
                        std::lock_guard<std::mutex> _(printErrorMutex);
                        std::cerr << "ERROR: unknown token " << token << " in event description \"" << eventDesc << "\" from " << event.first << "\n";
                        return PCM::UnknownError;
                    }
                }
                EventSelectRegister reg;
                reg.fields.event_select = eventSel;
                reg.fields.umask = umask;
                perf_event_attr e = PCM_init_perf_event_attr();
                e.type = PERF_TYPE_RAW;
                e.config = reg.value;
                // std::cerr << "Programming perf event " << std::hex << e.config << "\n" << std::dec;
                if (programPerfEvent(e, event.second, std::string("event ") + event.first + " " + eventDesc) == false)
                {
                    return PCM::UnknownError;
                }
                leader_counter = perfEventHandle[i][PERF_TOPDOWN_SLOTS_POS];
                perfTopDownPos[event.second] = readPos++;
            }
        }
    }
#endif
    if (PEBSEnable)
    {
        cleanupPEBS = true;
        MSR[i]->write(IA32_PEBS_ENABLE_ADDR, PEBSEnable);
    }
    return PCM::Success;
}

void PCM::reportQPISpeed() const
{
    if (!max_qpi_speed) return;

    if (hasPCICFGUncore()) {
        for (size_t i = 0; i < (size_t)serverUncorePMUs.size(); ++i)
        {
            std::cerr << "Socket " << i << "\n";
            if(serverUncorePMUs[i].get()) serverUncorePMUs[i]->reportQPISpeed();
        }
    } else {
        std::cerr << "Max " << xPI() << " speed: " << max_qpi_speed / (1e9) << " GBytes/second (" << max_qpi_speed / (1e9*getBytesPerLinkTransfer()) << " GT/second)\n";
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
    if (cpu_family_model == NEHALEM_EX)
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
    const uint64 before = getInvariantTSC_Fast(ref_core);
    MySleepMs(100);
    const uint64 after = getInvariantTSC_Fast(ref_core);
    nominal_frequency = 10ULL*(after-before);
    std::cerr << "WARNING: Core nominal frequency has to be estimated\n";
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
    return getCPUFamilyModelString(cpu_family, cpu_model_private, cpu_stepping);
}

std::string PCM::getCPUFamilyModelString(const uint32 cpu_family_, const uint32 internal_cpu_model_, const uint32 cpu_stepping_)
{
    char buffer[sizeof(int)*4*3+6];
    std::fill(buffer, buffer + sizeof(buffer), 0);
    std::snprintf(buffer,sizeof(buffer),"GenuineIntel-%d-%2X-%X", cpu_family_, internal_cpu_model_, cpu_stepping_);
    std::string result(buffer);
    return result;
}

void PCM::enableForceRTMAbortMode(const bool silent)
{
    // std::cout << "enableForceRTMAbortMode(): forceRTMAbortMode=" << forceRTMAbortMode << "\n";
    if (!forceRTMAbortMode)
    {
        if (isForceRTMAbortModeAvailable() && (core_gen_counter_num_max < 4))
        {
            for (auto& m : MSR)
            {
                const auto res = m->write(MSR_TSX_FORCE_ABORT, 1);
                if (res != sizeof(uint64))
                {
                    std::cerr << "Warning: writing 1 to MSR_TSX_FORCE_ABORT failed with error "
                        << res << " on core " << m->getCoreId() << "\n";
                }
            }
            readCoreCounterConfig(true); // re-read core_gen_counter_num_max from CPUID
            if (!silent) std::cerr << "The number of custom counters is now " << core_gen_counter_num_max << "\n";
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

void PCM::disableForceRTMAbortMode(const bool silent)
{
    // std::cout << "disableForceRTMAbortMode(): forceRTMAbortMode=" << forceRTMAbortMode << "\n";
    if (forceRTMAbortMode)
    {
        for (auto& m : MSR)
        {
            const auto res = m->write(MSR_TSX_FORCE_ABORT, 0);
            if (res != sizeof(uint64))
            {
                std::cerr << "Warning: writing 0 to MSR_TSX_FORCE_ABORT failed with error "
                    << res << " on core " << m->getCoreId() << "\n";
            }
        }
        readCoreCounterConfig(true); // re-read core_gen_counter_num_max from CPUID
        if (!silent) std::cerr << "The number of custom counters is now " << core_gen_counter_num_max << "\n";
        if (core_gen_counter_num_max != 3)
        {
            std::cerr << "PCM Warning: the number of custom counters is not 3 (" << core_gen_counter_num_max << ")\n";
        }
        forceRTMAbortMode = false;
    }
}

bool PCM::isForceRTMAbortModeAvailable()
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
        // checking 'canUsePerf'because corruption detection currently works
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
            const auto count = MSR[i]->read(IA32_PERFEVTSEL0_ADDR + j, &event_select_reg.value);

            if (count && (event_select_reg.fields.event_select != 0 || event_select_reg.fields.apic_int != 0))
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

        const auto count = MSR[i]->read(IA32_CR_FIXED_CTR_CTRL, &ctrl_reg.value);

        // Check if someone has installed pmi handler on counter overflow.
        // If so, that agent might potentially need to change counter value
        // for the "sample after"-mode messing up PCM measurements
        if (count && (ctrl_reg.fields.enable_pmi0 || ctrl_reg.fields.enable_pmi1 || ctrl_reg.fields.enable_pmi2))
        {
            std::cerr << "WARNING: Core " << i << " fixed ctrl:" << ctrl_reg.value << "\n";
            if (needToRestoreNMIWatchdog == false) // if NMI watchdog did not clear the fields, ignore it
            {
                return true;
            }
        }
#if 0
        // either os=0,usr=0 (not running) or os=1,usr=1 (fits PCM modus) are ok, other combinations are not
        if(ctrl_reg.fields.os0 != ctrl_reg.fields.usr0 ||
           ctrl_reg.fields.os1 != ctrl_reg.fields.usr1 ||
           ctrl_reg.fields.os2 != ctrl_reg.fields.usr2)
        {
           std::cerr << "WARNING: Core " << i << " fixed ctrl:" << ctrl_reg.value << "\n";
           return true;
        }
#endif
    }
#ifdef _MSC_VER
    // try to check if PMU is reserved using MSR driver
    auto hDriver = openMSRDriver();
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        DWORD reslength = 0;
        uint64 result = 0;
        BOOL status = DeviceIoControl(hDriver, IO_CTL_PMU_ALLOC_SUPPORT, NULL, 0, &result, sizeof(uint64), &reslength, NULL);
        if (status == TRUE && reslength == sizeof(uint64) && result == 1)
        {
            status = DeviceIoControl(hDriver, IO_CTL_PMU_ALLOC, NULL, 0, &result, sizeof(uint64), &reslength, NULL);
            if (status == FALSE)
            {
                std::cerr << "PMU can not be allocated with msr.sys driver. Error code is " << ((reslength == sizeof(uint64)) ? std::to_string(result) : "unknown") << " \n";
                CloseHandle(hDriver);
                return true;
            }
            else
            {
                // std::cerr << "Successfully allocated PMU through msr.sys" << " \n";
            }
        }
        CloseHandle(hDriver);
    }
#endif
    //std::cout << std::flush
    return false;
}

const char * PCM::getUArchCodename(const int32 cpu_family_model_param) const
{
    auto cpu_family_model_ = cpu_family_model_param;
    if(cpu_family_model_ < 0)
        cpu_family_model_ = this->cpu_family_model;

    switch(cpu_family_model_)
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
        case GEMINI_LAKE:
            return "Gemini Lake";
        case DENVERTON:
            return "Denverton";
        case SNOWRIDGE:
            return "Snowridge";
        case ELKHART_LAKE:
            return "Elkhart Lake";
        case JASPER_LAKE:
            return "Jasper Lake";
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
        case ADL:
            return "Alder Lake";
        case RPL:
            return "Raptor Lake";
        case MTL:
            return "Meteor Lake";
        case LNL:
            return "Lunar Lake";
        case ARL:
            return "Arrow Lake";
        case SKX:
            if (cpu_family_model_param >= 0)
            {
                // query for specified cpu_family_model_param, stepping not provided
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
        case SPR:
            return "Sapphire Rapids-SP";
        case EMR:
            return "Emerald Rapids-SP";
        case GNR:
            return "Granite Rapids-SP";
        case GRR:
            return "Grand Ridge";
        case SRF:
            return "Sierra Forest";
    }
    return "unknown";
}

#ifdef PCM_USE_PERF
void PCM::closePerfHandles(const bool silent)
{
    if (canUsePerf)
    {
        auto cleanOne = [this](PerfEventHandleContainer & cont)
        {
            for (int i = 0; i < num_cores; ++i)
            {
                for(int c = 0; c < PERF_MAX_COUNTERS; ++c)
                {
                    auto & h = cont[i][c];
                    if (h != -1) ::close(h);
                    h = -1;
                }
            }
        };
        cleanOne(perfEventHandle);
        for (auto & cont : perfEventTaskHandle)
        {
            cleanOne(cont);
        }
        perfEventTaskHandle.clear();

        if (!silent) std::cerr << " Closed perf event handles\n";
    }
}
#endif

void PCM::cleanupPMU(const bool silent)
{
    programmed_core_pmu = false;
#ifdef PCM_USE_PERF
    closePerfHandles(silent);
    if (canUsePerf)
    {
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
        if (cleanupPEBS)
        {
            MSR[i]->write(IA32_PEBS_ENABLE_ADDR, 0ULL);
        }
    }
    cleanupPEBS = false;

    if(cpu_family_model == JAKETOWN)
        enableJKTWorkaround(false);

#ifndef PCM_SILENT
    if (!silent) std::cerr << " Zeroed PMU registers\n";
#endif
}
#ifdef PCM_SILENT
    #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
void PCM::cleanupUncorePMUs(const bool silent)
{
    for (auto & sPMUs : iioPMUs)
    {
        for (auto & pmu : sPMUs)
        {
            pmu.second.cleanup();
        }
    }

    for (auto & sPMUs : idxPMUs)
    {
        for (auto & pmu : sPMUs)
        {
            pmu.cleanup();
        }
    }
    
    for (auto& sPMUs : irpPMUs)
    {
        for (auto& pmu : sPMUs)
        {
            pmu.second.cleanup();
        }
    }

    forAllUncorePMUs([](UncorePMU & p) { p.cleanup(); });

    for (auto& sPMUs : cxlPMUs)
    {
        for (auto& pmus : sPMUs)
        {
            pmus.first.cleanup();
            pmus.second.cleanup();
        }
    }
    for (auto & uncore : serverUncorePMUs)
    {
        uncore->cleanupPMUs();
    }
#ifndef PCM_SILENT
    if (!silent) std::cerr << " Zeroed uncore PMU registers\n";
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
void PCM::cleanupRDT(const bool silent)
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


    if (!silent) std::cerr << " Freeing up all RMIDs\n";
}

void PCM::setOutput(const std::string filename, const bool cerrToo)
{
     const auto pos = filename.find_last_of("/");
     if (pos != std::string::npos) {
         const std::string dir_name = filename.substr(0, pos);
         struct stat info;
         if (stat(dir_name.c_str(), &info) != 0)
         {
             std::cerr << "Output directory: " << dir_name << " doesn't exist\n";
             exit(EXIT_FAILURE);
         }
     }

     outfile = new std::ofstream(filename.c_str());
     backup_ofile = std::cout.rdbuf();
     std::cout.rdbuf(outfile->rdbuf());
     if (cerrToo)
     {
         backup_ofile_cerr = std::cerr.rdbuf();
         std::cerr.rdbuf(outfile->rdbuf());
     }
}

void PCM::restoreOutput()
{
    // restore cout back to what it was originally
    if(backup_ofile)
        std::cout.rdbuf(backup_ofile);

    if (backup_ofile_cerr)
        std::cerr.rdbuf(backup_ofile_cerr);

// close output file
    if(outfile)
        outfile->close();
}

void PCM::cleanup(const bool silent)
{
    if (MSR.empty()) return;

    if (!silent) std::cerr << "Cleaning up\n";

    cleanupPMU(silent);

    disableForceRTMAbortMode(silent);

    cleanupUncorePMUs(silent);
    cleanupRDT(silent);
#ifdef __linux__
    if (needToRestoreNMIWatchdog)
    {
        enableNMIWatchdog(silent);
        needToRestoreNMIWatchdog = false;
    }
#endif
#ifdef _MSC_VER
    // free PMU using MSR driver
    auto hDriver = openMSRDriver();
    if (hDriver != INVALID_HANDLE_VALUE)
    {
        DWORD reslength = 0;
        uint64 result = 0;
        BOOL status = DeviceIoControl(hDriver, IO_CTL_PMU_ALLOC_SUPPORT, NULL, 0, &result, sizeof(uint64), &reslength, NULL);
        if (status == TRUE && reslength == sizeof(uint64) && result == 1)
        {
            status = DeviceIoControl(hDriver, IO_CTL_PMU_FREE, NULL, 0, &result, sizeof(uint64), &reslength, NULL);
            if (status == FALSE)
            {
                std::cerr << "PMU can not be freed with msr.sys driver. Error code is " << ((reslength == sizeof(uint64)) ? std::to_string(result) : "unknown") << " \n";
            }
        }
        CloseHandle(hDriver);
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

bool PCM::supportsRDTSCP() const
{
    static int supports = -1;
    if (supports < 0)
    {
        PCM_CPUID_INFO info;
        pcm_cpuid(0x80000001, info);
        supports = (info.reg.edx & (0x1 << 27)) ? 1 : 0;
    }
    return 1 == supports;
}

#ifdef __APPLE__

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


uint64 PCM::getTickCount(uint64 multiplier, uint32 core)
{
    return (multiplier * getInvariantTSC_Fast(core)) / getNominalFrequency();
}

uint64 PCM::getInvariantTSC_Fast(uint32 core)
{
    if (supportsRDTSCP())
    {
        TemporalThreadAffinity aff(core);
        return RDTSCP();
    }
    else if (core < MSR.size())
    {
        uint64 cInvariantTSC = 0;
        MSR[core]->read(IA32_TIME_STAMP_COUNTER, &cInvariantTSC);
        if (cInvariantTSC) return cInvariantTSC;
    }
    std::cerr << "ERROR:  cannot read time stamp counter\n";
    return 0ULL;
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
    if (perfEventTaskHandle.empty() == false)
    {
        std::fill(outData.begin(), outData.end(), 0);
        for (const auto & handleArray : perfEventTaskHandle)
        {
            for (size_t ctr = 0; ctr < PERF_MAX_COUNTERS; ++ctr)
            {
                const int fd = handleArray[core][ctr];
                if (fd != -1)
                {
                    uint64 result{0ULL};
                    const int status = ::read(fd, &result, sizeof(result));
                    if (status != sizeof(result))
                    {
                        std::cerr << "PCM Error: failed to read from Linux perf handle " << fd <<  "\n";
                    }
                    else
                    {
                        outData[ctr] += result;
                    }
                }
            }
        }
        return;
    }
    auto readPerfDataHelper = [this](const uint32 core, std::vector<uint64>& outData, const uint32 leader, const uint32 num_counters)
    {
        if (perfEventHandle[core][leader] < 0)
        {
            std::fill(outData.begin(), outData.end(), 0);
            return;
        }
        uint64 data[1 + PERF_MAX_COUNTERS];
        const int32 bytes2read = sizeof(uint64) * (1 + num_counters);
        assert(num_counters <= PERF_MAX_COUNTERS);
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
        {
            /*
            if (core == 0)
            {
                std::unique_lock<std::mutex> _(instanceCreationMutex);
                std::cerr << "DEBUG: perf raw: " << std::dec;
                for (uint32 p=0; p < (1 + num_counters) ; ++p) std::cerr << data[p] << " ";
                std::cerr << "\n";
            }
            */
            // copy all counters, they start from position 1 in data
            std::copy((data + 1), (data + 1) + data[0], outData.begin());
        }
    };
    readPerfDataHelper(core, outData, PERF_GROUP_LEADER_COUNTER, core_fixed_counter_num_used + core_gen_counter_num_used);
    if (isHWTMAL1Supported() && perfSupportsTopDown())
    {
        std::vector<uint64> outTopDownData(outData.size(), 0);
        const auto topdownCtrNum = isHWTMAL2Supported() ? PERF_TOPDOWN_COUNTERS : PERF_TOPDOWN_COUNTERS_L1;
        readPerfDataHelper(core, outTopDownData, PERF_TOPDOWN_GROUP_LEADER_COUNTER, topdownCtrNum);
        std::copy(outTopDownData.begin(), outTopDownData.begin() + topdownCtrNum, outData.begin() + core_fixed_counter_num_used + core_gen_counter_num_used);
    }
}
#endif

void BasicCounterState::readAndAggregateTSC(std::shared_ptr<SafeMsrHandle> msr)
{
    uint64 cInvariantTSC = 0;
    PCM * m = PCM::getInstance();
    const auto cpu_family_model = m->getCPUFamilyModel();
    if (m->isAtom() == false || cpu_family_model == PCM::AVOTON)
    {
        cInvariantTSC = m->getInvariantTSC_Fast(msr->getCoreId());
        MSRValues[IA32_TIME_STAMP_COUNTER] = cInvariantTSC;
    }
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
    assert(msr.get());
    uint64 cInstRetiredAny = 0, cCpuClkUnhaltedThread = 0, cCpuClkUnhaltedRef = 0;
    uint64 cL3Occupancy = 0;
    uint64 cCustomEvents[PERF_MAX_CUSTOM_COUNTERS] = {0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL, 0ULL };
    uint64 cCStateResidency[PCM::MAX_C_STATE + 1];
    std::fill(cCStateResidency, cCStateResidency + PCM::MAX_C_STATE + 1, 0);
    uint64 thermStatus = 0;
    uint64 cSMICount = 0;
    uint64 cFrontendBoundSlots = 0;
    uint64 cBadSpeculationSlots = 0;
    uint64 cBackendBoundSlots = 0;
    uint64 cRetiringSlots = 0;
    uint64 cAllSlotsRaw = 0;
    uint64 cMemBoundSlots = 0;
    uint64 cFetchLatSlots = 0;
    uint64 cBrMispredSlots = 0;
    uint64 cHeavyOpsSlots = 0;
    const int32 core_id = msr->getCoreId();
    TemporalThreadAffinity tempThreadAffinity(core_id); // speedup trick for Linux

    PCM * m = PCM::getInstance();
    assert(m);
    const auto core_global_ctrl_value = m->core_global_ctrl_value;
    const bool freezeUnfreeze = m->canUsePerf == false && core_global_ctrl_value != 0ULL;
    if (freezeUnfreeze)
    {
        msr->write(IA32_CR_PERF_GLOBAL_CTRL, 0ULL); // freeze
    }

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
        if (m->isHWTMAL1Supported() && m->perfSupportsTopDown())
        {
            cFrontendBoundSlots =   perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_FRONTEND_POS]];
            cBadSpeculationSlots =  perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_BADSPEC_POS]];
            cBackendBoundSlots =    perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_BACKEND_POS]];
            cRetiringSlots =        perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_RETIRING_POS]];
            cAllSlotsRaw =          perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_SLOTS_POS]];
//          if (core_id == 0) std::cout << "DEBUG: All: "<< cAllSlotsRaw << " FE: " << cFrontendBoundSlots << " BAD-SP: " << cBadSpeculationSlots << " BE: " << cBackendBoundSlots << " RET: " << cRetiringSlots << std::endl;
            if (m->isHWTMAL2Supported())
            {
                cMemBoundSlots = perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_MEM_BOUND_POS]];
                cFetchLatSlots = perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_FETCH_LAT_POS]];
                cBrMispredSlots = perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_BR_MISPRED_POS]];;
                cHeavyOpsSlots = perfData[m->perfTopDownPos[PCM::PERF_TOPDOWN_HEAVY_OPS_POS]];
            }
        }
    }
    else
#endif
    {
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
        }

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
            if (m->isHWTMAL2Supported())
            {
                cMemBoundSlots = extract_bits(perfMetrics,  32 + 3*8, 32 + 3*8 + 7);
                cFetchLatSlots = extract_bits(perfMetrics,  32 + 2*8, 32 + 2*8 + 7);
                cBrMispredSlots = extract_bits(perfMetrics, 32 + 1*8, 32 + 1*8 + 7);
                cHeavyOpsSlots = extract_bits(perfMetrics,    32 + 0*8, 32 + 0*8 + 7);
            }
            const double total = double(cFrontendBoundSlots + cBadSpeculationSlots + cBackendBoundSlots + cRetiringSlots);
            if (total != 0)
            {
                cFrontendBoundSlots = m->FrontendBoundSlots[core_id] += uint64((double(cFrontendBoundSlots) / total) * double(slots));
                cBadSpeculationSlots = m->BadSpeculationSlots[core_id] += uint64((double(cBadSpeculationSlots) / total) * double(slots));
                cBackendBoundSlots = m->BackendBoundSlots[core_id] += uint64((double(cBackendBoundSlots) / total) * double(slots));
                cRetiringSlots = m->RetiringSlots[core_id] += uint64((double(cRetiringSlots) / total) * double(slots));
                if (m->isHWTMAL2Supported())
                {
                    cMemBoundSlots = m->MemBoundSlots[core_id] += uint64((double(cMemBoundSlots) / total) * double(slots));
                    cFetchLatSlots = m->FetchLatSlots[core_id] += uint64((double(cFetchLatSlots) / total) * double(slots));
                    cBrMispredSlots = m->BrMispredSlots[core_id] += uint64((double(cBrMispredSlots) / total) * double(slots));
                    cHeavyOpsSlots = m->HeavyOpsSlots[core_id] += uint64((double(cHeavyOpsSlots) / total) * double(slots));
                }
            }
            cAllSlotsRaw = m->AllSlotsRaw[core_id] += slots;
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
    for (int i = 0; i <= (int)(PCM::MAX_C_STATE); ++i)
    {
        if (m->coreCStateMsr && m->coreCStateMsr[i])
        {
            const auto index = m->coreCStateMsr[i];
            msr->read(index, &(cCStateResidency[i]));
            MSRValues[index] = cCStateResidency[i];
        }
    }

    // reading temperature
    msr->read(MSR_IA32_THERM_STATUS, &thermStatus);
    MSRValues[MSR_IA32_THERM_STATUS] = thermStatus;

    msr->read(MSR_SMI_COUNT, &cSMICount);
    MSRValues[MSR_SMI_COUNT] = cSMICount;

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
    AllSlotsRaw         += cAllSlotsRaw;
    MemBoundSlots       += cMemBoundSlots;
    FetchLatSlots       += cFetchLatSlots;
    BrMispredSlots      += cBrMispredSlots;
    HeavyOpsSlots       += cHeavyOpsSlots;

    if (freezeUnfreeze)
    {
        msr->write(IA32_CR_PERF_GLOBAL_CTRL, core_global_ctrl_value); // unfreeze
    }
}

PCM::ErrorCode PCM::programServerUncoreLatencyMetrics(bool enable_pmm)
{
    uint32 DDRConfig[4] = {0,0,0,0};

    if (enable_pmm == false)
    {   //DDR is false
        if (ICX == cpu_family_model || SPR == cpu_family_model || EMR == cpu_family_model)
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
        for (size_t i = 0; i < (size_t)serverUncorePMUs.size(); ++i)
        {
            serverUncorePMUs[i]->programIMC(DDRConfig);
        }
    }
    return PCM::Success;
}

PCM::ErrorCode PCM::programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, int rankA, int rankB)
{
    if (MSR.empty() || serverUncorePMUs.empty())  return PCM::MSRAccessDenied;

    for (int i = 0; (i < (int)serverUncorePMUs.size()) && MSR.size(); ++i)
    {
         serverUncorePMUs[i]->programServerUncoreMemoryMetrics(metrics, rankA, rankB);
    }
    programCXLCM();
    programCXLDP();

    return PCM::Success;
}

PCM::ErrorCode PCM::programServerUncorePowerMetrics(int mc_profile, int pcu_profile, int * freq_bands)
{
    if(MSR.empty() || serverUncorePMUs.empty())  return PCM::MSRAccessDenied;

    uint32 PCUCntConf[4] = {0,0,0,0};

    auto printError = [this](const char * eventCategory)
    {
        assert(eventCategory);
        std::cerr << "ERROR: no " << eventCategory << " events defined for CPU family " << cpu_family << " model " << cpu_model_private << "\n";
    };

    switch (cpu_family_model)
    {
        case SPR:
        case EMR:
        case SRF:
        case GNR:
        case GNR_D:
            PCUCntConf[0] = PCU_MSR_PMON_CTL_EVENT(1); // clock ticks
            break;
        default:
            PCUCntConf[0] = PCU_MSR_PMON_CTL_EVENT(0); // clock ticks
    }

    switch(pcu_profile)
    {
    case 0:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0xB); // FREQ_BAND0_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0xC); // FREQ_BAND1_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0xD); // FREQ_BAND2_CYCLES
         break;
    case 1:
         switch (cpu_family_model)
         {
             case SPR:
             case EMR:
             case SRF:
             case GNR:
             case GNR_D:
                 PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x35); // POWER_STATE_OCCUPANCY.C0
                 PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x36); // POWER_STATE_OCCUPANCY.C3
                 PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x37); // POWER_STATE_OCCUPANCY.C6
                 break;
             default:
                 PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(1); // POWER_STATE_OCCUPANCY.C0 using CLOCKTICKS + 8th-bit
                 PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(2); // POWER_STATE_OCCUPANCY.C3 using CLOCKTICKS + 8th-bit
                 PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x80) + PCU_MSR_PMON_CTL_OCC_SEL(3); // POWER_STATE_OCCUPANCY.C6 using CLOCKTICKS + 8th-bit
         }
         break;
    case 2:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x09); // PROCHOT_INTERNAL_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x0A); // PROCHOT_EXTERNAL_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x04); // Thermal frequency limit cycles: FREQ_MAX_LIMIT_THERMAL_CYCLES
         break;
    case 3:
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x04); // Thermal frequency limit cycles: FREQ_MAX_LIMIT_THERMAL_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x05); // Power frequency limit cycles: FREQ_MAX_POWER_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x07); // Clipped frequency limit cycles: FREQ_MAX_CURRENT_CYCLES (not supported on SKX,ICX,SNOWRIDGE,SPR,EMR,SRF,GNR)
         break;
    case 4: // not supported on SKX, ICX, SNOWRIDGE, SPR, EMR
         PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x06); // OS frequency limit cycles: FREQ_MAX_OS_CYCLES
         PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x05); // Power frequency limit cycles: FREQ_MAX_POWER_CYCLES
         PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x07); // Clipped frequency limit cycles: FREQ_MAX_CURRENT_CYCLES (not supported on SKX,ICX,SNOWRIDGE,SPR,EMR,SRF,GNR)
         break;
    case 5:
         if (JAKETOWN == cpu_family_model)
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0) + PCU_MSR_PMON_CTL_EXTRA_SEL + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0) + PCU_MSR_PMON_CTL_EXTRA_SEL ; // cycles spent changing frequency
         } else if (IVYTOWN == cpu_family_model)
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x60) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x60) ; // cycles spent changing frequency: FREQ_TRANS_CYCLES
         } else if (
               HASWELLX == cpu_family_model
            || BDX_DE == cpu_family_model
            || BDX == cpu_family_model
            || SKX == cpu_family_model
            || ICX == cpu_family_model
            || SNOWRIDGE == cpu_family_model
            || SPR == cpu_family_model
            || EMR == cpu_family_model
            || SRF == cpu_family_model
            || GNR == cpu_family_model
            || GNR_D == cpu_family_model
            )
         {
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x74) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of frequency transitions
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x74) ; // cycles spent changing frequency: FREQ_TRANS_CYCLES
             if(HASWELLX == cpu_family_model)
             {
                 PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x79) + PCU_MSR_PMON_CTL_EDGE_DET ; // number of UFS transitions
                 PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x79)                             ; // UFS transition cycles
             }
         } else
         {
             printError("frequency transition");
         }
         break;
    case 6:
         if (IVYTOWN == cpu_family_model)
         {
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x2B) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC2 transitions
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x2D) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC6 transitions
         } else if (
               HASWELLX == cpu_family_model
            || BDX_DE == cpu_family_model
            || BDX == cpu_family_model
            || SKX == cpu_family_model
            || ICX == cpu_family_model
            || SNOWRIDGE == cpu_family_model
            || SPR == cpu_family_model
            || EMR == cpu_family_model
            || SRF == cpu_family_model
            || GNR == cpu_family_model
            || GNR_D == cpu_family_model
            )
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x4E)                             ; // PC1e residenicies (not supported on SKX,ICX,SNOWRIDGE,SPR,EMR,SRF,GNR)
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x4E) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC1 transitions (not supported on SKX,ICX,SNOWRIDGE,SPR,EMR,SRF,GNR)
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x2B) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC2e transitions
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x2D) + PCU_MSR_PMON_CTL_EDGE_DET ; // PC6 transitions
         } else
         {
             printError("package C-state transition");
         }
         break;
     case 7:
         if (HASWELLX == cpu_family_model || BDX_DE == cpu_family_model || BDX == cpu_family_model)
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x7E) ; // UFS_TRANSITIONS_PERF_P_LIMIT
             PCUCntConf[1] =  PCU_MSR_PMON_CTL_EVENT(0x7D) ; // UFS_TRANSITIONS_IO_P_LIMIT
             PCUCntConf[2] =  PCU_MSR_PMON_CTL_EVENT(0x7A) ; // UFS_TRANSITIONS_UP_RING_TRAFFIC
             PCUCntConf[3] =  PCU_MSR_PMON_CTL_EVENT(0x7B) ; // UFS_TRANSITIONS_UP_STALL_CYCLES
         } else
         {
             printError("UFS transition");
         }
         break;
    case 8:
         if (HASWELLX == cpu_family_model || BDX_DE == cpu_family_model || BDX == cpu_family_model)
         {
             PCUCntConf[0] =  PCU_MSR_PMON_CTL_EVENT(0x7C) ; // UFS_TRANSITIONS_DOWN
         } else
         {
             printError("UFS transition");
         }
         break;
    default:
         std::cerr << "ERROR: unsupported PCU profile " << pcu_profile << "\n";
    }

    for (auto& u : serverUncorePMUs)
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
    programUncorePMUs(PCU_PMU_ID, [&PCUCntConf, &filter](UncorePMU& pmu)
    {
        pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        if (pmu.filter[0].get())
        {
            *pmu.filter[0] = filter;
        }

        program(pmu, &PCUCntConf[0], &PCUCntConf[4], UNC_PMON_UNIT_CTL_FRZ_EN);
    });
}

PCM::ErrorCode PCM::program(const RawPMUConfigs& curPMUConfigs_, const bool silent, const int pid)
{
    if (MSR.empty())  return PCM::MSRAccessDenied;
    threadMSRConfig = RawPMUConfig{};
    packageMSRConfig = RawPMUConfig{};
    pcicfgConfig = RawPMUConfig{};
    mmioConfig = RawPMUConfig{};
    pmtConfig = RawPMUConfig{};
    RawPMUConfigs curPMUConfigs = curPMUConfigs_;
    constexpr auto globalRegPos = 0ULL;
    PCM::ExtendedCustomCoreEventDescription conf;
    auto updateRegs = [this, &conf](const RawPMUConfig& corePMUConfig, EventSelectRegister* regs) -> bool
    {
        if (corePMUConfig.programmable.size() > (size_t)getMaxCustomCoreEvents())
        {
            std::cerr << "ERROR: trying to program " << corePMUConfig.programmable.size() << " core PMU counters, which exceeds the max num possible (" << getMaxCustomCoreEvents() << ").\n";
            for (const auto& e : corePMUConfig.programmable)
            {
                std::cerr << "      Event: " << e.second << "\n";
            }
            return false;
        }
        size_t c = 0;
        for (; c < corePMUConfig.programmable.size() && c < (size_t)getMaxCustomCoreEvents() && c < PERF_MAX_CUSTOM_COUNTERS; ++c)
        {
            regs[c].value = corePMUConfig.programmable[c].first[0];
        }
        conf.nGPCounters = (std::max)((uint32)c, conf.nGPCounters);
        return true;
    };
    FixedEventControlRegister fixedReg;
    auto setOtherConf = [&conf, &fixedReg
#ifdef _MSC_VER
        , &globalRegPos
#endif
            ](const RawPMUConfig& corePMUConfig)
    {
        if ((size_t)globalRegPos < corePMUConfig.programmable.size())
        {
            conf.OffcoreResponseMsrValue[0] = corePMUConfig.programmable[globalRegPos].first[OCR0Pos];
            conf.OffcoreResponseMsrValue[1] = corePMUConfig.programmable[globalRegPos].first[OCR1Pos];
            conf.LoadLatencyMsrValue = corePMUConfig.programmable[globalRegPos].first[LoadLatencyPos];
            conf.FrontendMsrValue = corePMUConfig.programmable[globalRegPos].first[FrontendPos];
        }
        if (corePMUConfig.fixed.empty())
        {
            conf.fixedCfg = NULL; // default
        }
        else
        {
            fixedReg.value = 0;
            for (const auto& cfg : corePMUConfig.fixed)
            {
                fixedReg.value |= uint64(cfg.first[0]);
            }
            conf.fixedCfg = &fixedReg;
        }
    };
    EventSelectRegister regs[PERF_MAX_CUSTOM_COUNTERS];
    EventSelectRegister atomRegs[PERF_MAX_CUSTOM_COUNTERS];
    conf.OffcoreResponseMsrValue[0] = 0;
    conf.OffcoreResponseMsrValue[1] = 0;
    if (curPMUConfigs.count("core") > 0)
    {
        // need to program core PMU first
        const auto & corePMUConfig = curPMUConfigs["core"];
        if (updateRegs(corePMUConfig, regs) == false)
        {
            return PCM::UnknownError;
        }
        conf.gpCounterCfg = regs;
        setOtherConf(corePMUConfig);
        conf.defaultUncoreProgramming = false;
        curPMUConfigs.erase("core");

        if (curPMUConfigs.count("atom"))
        {
            const auto & atomPMUConfig = curPMUConfigs["atom"];
            if (updateRegs(atomPMUConfig, atomRegs) == false)
            {
                return PCM::UnknownError;
            }
            conf.gpCounterHybridAtomCfg = atomRegs;
            curPMUConfigs.erase("atom");
        }
        const auto status = program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf, silent, pid);
        if (status != PCM::Success)
        {
            return status;
        }
    }
    else if (curPMUConfigs.count("atom") > 0) // no core, only atom
    {
        const auto& atomPMUConfig = curPMUConfigs["atom"];
        if (updateRegs(atomPMUConfig, atomRegs) == false)
        {
            return PCM::UnknownError;
        }
        conf.gpCounterHybridAtomCfg = atomRegs;
        setOtherConf(atomPMUConfig);
        conf.defaultUncoreProgramming = false;
        curPMUConfigs.erase("atom");

        const auto status = program(PCM::EXT_CUSTOM_CORE_EVENTS, &conf, silent, pid);
        if (status != PCM::Success)
        {
            return status;
        }
    }
    for (auto& pmuConfig : curPMUConfigs)
    {
        const auto & type = pmuConfig.first;
        const auto & events = pmuConfig.second;
        if (events.programmable.empty() && events.fixed.empty())
        {
            continue;
        }
        if (events.programmable.size() > ServerUncoreCounterState::maxCounters && isRegisterEvent(type) == false)
        {
            std::cerr << "ERROR: trying to program " << events.programmable.size() << " uncore PMU counters, which exceeds the max num possible (" << ServerUncoreCounterState::maxCounters << ").";
            return PCM::UnknownError;
        }
        uint32 events32[ServerUncoreCounterState::maxCounters] = { 0,0,0,0,0,0,0,0 };
        uint64 events64[ServerUncoreCounterState::maxCounters] = { 0,0,0,0,0,0,0,0 };
        for (size_t c = 0; c < events.programmable.size() && c < ServerUncoreCounterState::maxCounters; ++c)
        {
            events32[c] = (uint32)events.programmable[c].first[0];
            events64[c] = events.programmable[c].first[0];
        }
        if (type == "m3upi")
        {
            for (auto& uncore : serverUncorePMUs)
            {
                uncore->programM3UPI(events32);
            }
        }
        else if (type == "xpi" || type == "upi" || type == "qpi")
        {
            for (auto& uncore : serverUncorePMUs)
            {
                uncore->programXPI(events32);
            }
        }
        else if (type == "imc")
        {
            for (auto& uncore : serverUncorePMUs)
            {
                uncore->programIMC(events32);
            }
        }
        else if (type == "ha")
        {
            for (auto& uncore : serverUncorePMUs)
            {
                uncore->programHA(events32);
            }
        }
        else if (type == "m2m")
        {
            for (auto& uncore : serverUncorePMUs)
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
        else if (type == "mdf")
        {
            programMDF(events64);
        }
        else if (type == "irp")
        {
            programIRPCounters(events64);
        }
        else if (type == "iio")
        {
            programIIOCounters(events64);
        }
        else if (type == "package_msr")
        {
            packageMSRConfig = pmuConfig.second;
        }
        else if (type == "thread_msr")
        {
            threadMSRConfig = pmuConfig.second;
        }
        else if (type == "pcicfg")
        {
            pcicfgConfig = pmuConfig.second;
            auto addLocations = [this](const std::vector<RawEventConfig>& configs) {
                for (const auto& c : configs)
                {
                    if (PCICFGRegisterLocations.find(c.first) == PCICFGRegisterLocations.end())
                    {
                        // add locations
                        std::vector<PCICFGRegisterEncoding> locations;
                        const auto deviceID = c.first[PCICFGEventPosition::deviceID];
                        forAllIntelDevices([&locations, &deviceID, &c](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
                            {
                                if (deviceID == device_id && PciHandleType::exists(group, bus, device, function))
                                {
                                    // PciHandleType shared ptr, offset
                                    locations.push_back(PCICFGRegisterEncoding{ std::make_shared<PciHandleType>(group, bus, device, function), (uint32)c.first[PCICFGEventPosition::offset] });
                                }
                            });
                        PCICFGRegisterLocations[c.first] = locations;
                    }
                }
            };
            addLocations(pcicfgConfig.programmable);
            addLocations(pcicfgConfig.fixed);
        }
        else if (type == "mmio")
        {
            mmioConfig = pmuConfig.second;
            auto addLocations = [this](const std::vector<RawEventConfig>& configs) {
                for (const auto& c : configs)
                {
                    if (MMIORegisterLocations.find(c.first) == MMIORegisterLocations.end())
                    {
                        // add locations
                        std::vector<MMIORegisterEncoding> locations;
                        const auto deviceID = c.first[MMIOEventPosition::deviceID];
                        forAllIntelDevices([&locations, &deviceID, &c](const uint32 group, const uint32 bus, const uint32 device, const uint32 function, const uint32 device_id)
                            {
                                if (deviceID == device_id && PciHandleType::exists(group, bus, device, function))
                                {
                                    PciHandleType pciHandle(group, bus, device, function);
                                    auto computeBarOffset = [&pciHandle](uint64 membarBits) -> size_t
                                    {
                                        if (membarBits)
                                        {
                                            const auto destPos = extract_bits(membarBits, 32, 39);
                                            const auto numBits = extract_bits(membarBits, 24, 31);
                                            const auto srcPos = extract_bits(membarBits, 16, 23);
                                            const auto pcicfgOffset = extract_bits(membarBits, 0, 15);
                                            uint32 memBarOffset = 0;
                                            pciHandle.read32(pcicfgOffset, &memBarOffset);
                                            return size_t(extract_bits_ui(memBarOffset, srcPos, srcPos + numBits - 1)) << destPos;
                                        }
                                        return 0;
                                    };

                                    size_t memBar = computeBarOffset(c.first[MMIOEventPosition::membar_bits1])
                                        | computeBarOffset(c.first[MMIOEventPosition::membar_bits2]);

                                    assert(memBar);

                                    const size_t addr = memBar + c.first[MMIOEventPosition::offset];
                                    // MMIORange shared ptr (handle), offset
                                    locations.push_back(MMIORegisterEncoding{ std::make_shared<MMIORange>(addr & ~4095ULL, 4096), (uint32) (addr & 4095ULL) });
                                }
                            });
                        MMIORegisterLocations[c.first] = locations;
                    }
                }
            };
            addLocations(mmioConfig.programmable);
            addLocations(mmioConfig.fixed);
        }
        else if (type == "pmt")
        {
            pmtConfig = pmuConfig.second;
            auto addLocations = [this](const std::vector<RawEventConfig>& configs) {
                for (const auto& c : configs)
                {
                    if (PMTRegisterLocations.find(c.first) == PMTRegisterLocations.end())
                    {
                        // add locations
                        std::vector<PMTRegisterEncoding> locations;
                        const auto UID = c.first[PMTEventPosition::UID];
                        for (size_t inst = 0; inst < TelemetryArray::numInstances(UID); ++inst)
                        {
                            locations.push_back(std::make_shared<TelemetryArray>(UID, inst));
                            // std::cout << "PMTRegisterLocations: UID: 0x" << std::hex << UID << " inst: " << std::dec << inst << std::endl;
                        }
                        PMTRegisterLocations[c.first] = locations;
                    }
                }
            };
            addLocations(pmtConfig.programmable);
            addLocations(pmtConfig.fixed);
        }
        else if (type == "cxlcm")
        {
            programCXLCM(events64);
        }
        else if (type == "cxldp")
        {
            programCXLDP(events64);
        }
        else if (strToUncorePMUID(type) != INVALID_PMU_ID)
        {
            const auto pmu_id = strToUncorePMUID(type);
            programUncorePMUs(pmu_id, [&events64, &events, &pmu_id](UncorePMU& pmu)
            {
                uint64 * eventsIter = (uint64 *)events64;
                if (pmu_id != PCIE_GEN5x16_PMU_ID && pmu_id != PCIE_GEN5x8_PMU_ID)
                {
                    pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
                }
                PCM::program(pmu, eventsIter, eventsIter + (std::min)(events.programmable.size(), (size_t)ServerUncoreCounterState::maxCounters), UNC_PMON_UNIT_CTL_FRZ_EN);
            });
        }
        else
        {
            std::cerr << "ERROR: unrecognized PMU type \"" << type << "\" when trying to program PMUs.\n";
            return PCM::UnknownError;
        }
    }
    return PCM::Success;
}

void PCM::freezeServerUncoreCounters()
{
    for (int i = 0; (i < (int)serverUncorePMUs.size()) && MSR.size(); ++i)
    {
        serverUncorePMUs[i]->freezeCounters();

        const auto refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        forAllUncorePMUs(i, PCU_PMU_ID, [](UncorePMU& pmu) { pmu.freeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

        if (IIOEventsAvailable())
        {
            for (auto & pmu : iioPMUs[i])
            {
                pmu.second.freeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        if (size_t(i) < irpPMUs.size())
        {
            for (auto& pmu : irpPMUs[i])
            {
                pmu.second.freeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        forAllUncorePMUs(i, CBO_PMU_ID, [](UncorePMU& pmu) { pmu.freeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

        forAllUncorePMUs(i, MDF_PMU_ID, [](UncorePMU& pmu) { pmu.freeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

    }
    for (auto& sPMUs : cxlPMUs)
    {
        for (auto& pmus : sPMUs)
        {
            pmus.first.freeze(UNC_PMON_UNIT_CTL_FRZ_EN);
            pmus.second.freeze(UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}
void PCM::unfreezeServerUncoreCounters()
{
    for (int i = 0; (i < (int)serverUncorePMUs.size()) && MSR.size(); ++i)
    {
        serverUncorePMUs[i]->unfreezeCounters();

        const auto refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        forAllUncorePMUs(i, PCU_PMU_ID, [](UncorePMU& pmu) { pmu.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

        if (IIOEventsAvailable())
        {
            for (auto & pmu : iioPMUs[i])
            {
                pmu.second.unfreeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        if (size_t(i) < irpPMUs.size())
        {
            for (auto& pmu : irpPMUs[i])
            {
                pmu.second.unfreeze(UNC_PMON_UNIT_CTL_RSV);
            }
        }

        forAllUncorePMUs(i, CBO_PMU_ID, [](UncorePMU& pmu) { pmu.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

        forAllUncorePMUs(i, MDF_PMU_ID, [](UncorePMU& pmu) { pmu.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN); });

    }
    for (auto& sPMUs : cxlPMUs)
    {
        for (auto& pmus : sPMUs)
        {
            pmus.first.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
            pmus.second.unfreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
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

        readAndAggregateCXLCMCounters(result);
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
void PCM::readAndAggregateCXLCMCounters( CounterStateType & result)
{

    for (size_t socket = 0; socket < getNumSockets(); ++socket)
    {
        uint64 CXLWriteMem = 0;
        uint64 CXLWriteCache = 0;
        for (size_t p = 0; p < getNumCXLPorts(socket); ++p)
        {
            CXLWriteMem += *cxlPMUs[socket][p].first.counterValue[0];
            CXLWriteCache += *cxlPMUs[socket][p].first.counterValue[1];
        }
        result.CXLWriteMem[socket] = CXLWriteMem;
        result.CXLWriteCache[socket] = CXLWriteCache;
    }
}


template <class CounterStateType>
void PCM::readAndAggregateUncoreMCCounters(const uint32 socket, CounterStateType & result)
{
    if (LLCReadMissLatencyMetricsAvailable())
    {
        result.TOROccupancyIAMiss += getUncoreCounterState(CBO_PMU_ID, socket, EventPosition::TOR_OCCUPANCY);
        result.TORInsertsIAMiss += getUncoreCounterState(CBO_PMU_ID, socket, EventPosition::TOR_INSERTS);
    }

    if (LLCReadMissLatencyMetricsAvailable() || uncoreFrequencyMetricAvailable())
    {
        result.UncClocks += getUncoreClocks(socket);
    }

    if (socket < UFSStatus.size())
    {
        result.UFSStatus.clear();
        for (size_t die = 0; die < UFSStatus[socket].size(); ++die)
        {
            auto & handle = UFSStatus[socket][die];
            if (handle.get() && die < handle->getNumEntries())
            {
                const auto value = handle->read64(die);
                // std::cerr << "DEBUG: " << std::hex << value << std::dec << " ";
                result.UFSStatus.push_back(value);
            }
        }
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
        if (serverUncorePMUs.size() && serverUncorePMUs[socket].get())
        {
            serverUncorePMUs[socket]->freezeCounters();
	    if (ReadMCStatsFromServerBW == false)
            {
                result.UncMCNormalReads += serverUncorePMUs[socket]->getImcReads();
                result.UncMCFullWrites += serverUncorePMUs[socket]->getImcWrites();
                if(nearMemoryMetricsAvailable()){
                    result.UncNMHit += serverUncorePMUs[socket]->getNMHits();
                    result.UncNMMiss += serverUncorePMUs[socket]->getNMMisses();
                }

            }
            if (localMemoryRequestRatioMetricAvailable())
            {
                if (hasCHA())
                {
                    result.UncHARequests += getUncoreCounterState(CBO_PMU_ID, socket, EventPosition::REQUESTS_ALL);
                    result.UncHALocalRequests += getUncoreCounterState(CBO_PMU_ID, socket, EventPosition::REQUESTS_LOCAL);
                }
                else
                {
                    result.UncHARequests += serverUncorePMUs[socket]->getHARequests();
                    result.UncHALocalRequests += serverUncorePMUs[socket]->getHALocalRequests();
                }
            }
            if (PMMTrafficMetricsAvailable() && (ReadMCStatsFromServerBW == false))
            {
                result.UncPMMReads += serverUncorePMUs[socket]->getPMMReads();
                result.UncPMMWrites += serverUncorePMUs[socket]->getPMMWrites();
            }
            if (HBMmemoryTrafficMetricsAvailable())
            {
                result.UncEDCNormalReads += serverUncorePMUs[socket]->getEdcReads();
                result.UncEDCFullWrites += serverUncorePMUs[socket]->getEdcWrites();
            }
            serverUncorePMUs[socket]->unfreezeCounters();
        }
    }
    else if(clientBW.get() && socket == 0)
    {
        result.UncMCNormalReads += clientImcReads->read();
        result.UncMCFullWrites += clientImcWrites->read();
        result.UncMCGTRequests += clientGtRequests->read();
        result.UncMCIARequests += clientIaRequests->read();
        result.UncMCIORequests += clientIoRequests->read();
    }
    else
    {
        std::shared_ptr<SafeMsrHandle> msr = MSR[socketRefCore[socket]];
        TemporalThreadAffinity tempThreadAffinity(socketRefCore[socket]); // speedup trick for Linux
        switch (cpu_family_model)
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

    if (socket == 0)
    {
        for (size_t pp = 0; pp < pp_energy_status.size(); ++pp)
        {
            result.PPEnergyStatus[pp] += pp_energy_status[pp]->read();
        }
    }
}

template <class CounterStateType>
void PCM::readMSRs(std::shared_ptr<SafeMsrHandle> msr, const PCM::RawPMUConfig& msrConfig, CounterStateType& result)
{
    auto read = [&msr, &result](const RawEventConfig & cfg) {
        const auto index = cfg.first[MSREventPosition::index];
        if (result.MSRValues.find(index) == result.MSRValues.end())
        {
            uint64 val{ 0 };
            msr->read(index, &val);
            result.MSRValues[index] = val;
        }
    };
    for (const auto& cfg : msrConfig.programmable)
    {
        read(cfg);
    }
    for (const auto& cfg : msrConfig.fixed)
    {
        read(cfg);
    }
}

template <class CounterStateType>
void PCM::readAndAggregatePackageCStateResidencies(std::shared_ptr<SafeMsrHandle> msr, CounterStateType & result)
{
    // reading package C state counters
    uint64 cCStateResidency[PCM::MAX_C_STATE + 1];
    std::fill(cCStateResidency, cCStateResidency + PCM::MAX_C_STATE + 1, 0);

    for(int i=0; i <= int(PCM::MAX_C_STATE) ;++i)
        if(pkgCStateMsr && pkgCStateMsr[i])
                msr->read(pkgCStateMsr[i], &(cCStateResidency[i]));

    for (int i = 0; i <= int(PCM::MAX_C_STATE); ++i)
    {
        if (cCStateResidency[i])
        {
            atomic_fetch_add((std::atomic<uint64> *)(result.CStateResidency + i), cCStateResidency[i]);
        }
    }
}

void PCM::readPCICFGRegisters(SystemCounterState& systemState)
{
    auto read = [this, &systemState](const RawEventConfig& cfg) {
        const RawEventEncoding& reEnc = cfg.first;
        systemState.PCICFGValues[reEnc].clear();
        for (auto& reg : PCICFGRegisterLocations[reEnc])
        {
            const auto width = reEnc[PCICFGEventPosition::width];
            auto& h = reg.first;
            const auto& offset = reg.second;
            if (h.get())
            {
                uint64 value = ~0ULL;
                uint32 value32 = 0;
                switch (width)
                {
                case 16:
                    h->read32(offset, &value32);
                    value = (uint64)extract_bits_ui(value32, 0, 15);
                    break;
                case 32:
                    h->read32(offset, &value32);
                    value = (uint64)value32;
                    break;
                case 64:
                    h->read64(offset, &value);
                    break;
                default:
                    std::cerr << "ERROR: Unsupported width " << width << " for pcicfg register " << cfg.second << "\n";
                }
                systemState.PCICFGValues[reEnc].push_back(value);
            }
        }
    };
    for (const auto& cfg : pcicfgConfig.programmable)
    {
        read(cfg);
    }
    for (const auto& cfg : pcicfgConfig.fixed)
    {
        read(cfg);
    }
}

void PCM::readMMIORegisters(SystemCounterState& systemState)
{
    auto read = [this, &systemState](const RawEventConfig& cfg) {
        const RawEventEncoding& reEnc = cfg.first;
        systemState.MMIOValues[reEnc].clear();
        for (auto& reg : MMIORegisterLocations[reEnc])
        {
            const auto width = reEnc[MMIOEventPosition::width];
            auto& h = reg.first;
            const auto& offset = reg.second;
            if (h.get())
            {
                uint64 value = ~0ULL;
                uint32 value32 = 0;
                switch (width)
                {
                case 16:
                    value32 = h->read32(offset);
                    value = (uint64)extract_bits_ui(value32, 0, 15);
                    break;
                case 32:
                    value32 = h->read32(offset);
                    value = (uint64)value32;
                    break;
                case 64:
                    value = h->read64(offset);
                    break;
                default:
                    std::cerr << "ERROR: Unsupported width " << width << " for mmio register " << cfg.second << "\n";
                }
                systemState.MMIOValues[reEnc].push_back(value);
            }
        }
    };
    for (const auto& cfg : mmioConfig.programmable)
    {
        read(cfg);
    }
    for (const auto& cfg : mmioConfig.fixed)
    {
        read(cfg);
    }
}

void PCM::readPMTRegisters(SystemCounterState& systemState)
{
    for (auto & p: PMTRegisterLocations)
    {
        for (auto & t: p.second)
        {
            if (t.get())
            {
                t->load();
            }
        }
    }
    auto read = [this, &systemState](const RawEventConfig& cfg) {
        const RawEventEncoding& reEnc = cfg.first;
        systemState.PMTValues[reEnc].clear();
        const auto lsb = reEnc[PMTEventPosition::lsb];
        const auto msb = reEnc[PMTEventPosition::msb];
        const auto offset = reEnc[PMTEventPosition::offset];
        // std::cout << "PMTValues: " << std::hex << reEnc[PMTEventPosition::UID] << std::dec << std::endl;
        for (auto& reg : PMTRegisterLocations[reEnc])
        {
            if (reg.get())
            {
                systemState.PMTValues[reEnc].push_back(reg->get(offset, lsb, msb));
                // std::cout << "PMTValues: " << std::hex << reEnc[PMTEventPosition::UID] << " " << std::dec << reg->get(offset, lsb, msb) << std::endl;
            }
        }
    };
    for (const auto& cfg : pmtConfig.programmable)
    {
        read(cfg);
    }
    for (const auto& cfg : pmtConfig.fixed)
    {
        read(cfg);
    }
}

void PCM::readQPICounters(SystemCounterState & result)
{
        // read QPI counters
        std::vector<bool> SocketProcessed(num_sockets, false);
        if (cpu_family_model == PCM::NEHALEM_EX || cpu_family_model == PCM::WESTMERE_EX)
        {
            for (int32 core = 0; core < num_cores; ++core)
            {
                if(isCoreOnline(core) == false) continue;

                if(core == socketRefCore[0]) MSR[core]->read(W_MSR_PMON_FIXED_CTR, &(result.uncoreTSC));

                uint32 s = topology[core].socket_id;

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
        else if ((cpu_family_model == PCM::NEHALEM_EP || cpu_family_model == PCM::WESTMERE_EP))
        {
            if (num_sockets == 2)
            {
                uint32 SCore[2] = { (uint32)socketRefCore[0], (uint32)socketRefCore[1] };
                uint64 Total_Reads[2] = { 0, 0 };
                uint64 Total_Writes[2] = { 0, 0 };
                uint64 IOH_Reads[2] = { 0, 0 };
                uint64 IOH_Writes[2] = { 0, 0 };
                uint64 Remote_Reads[2] = { 0, 0 };
                uint64 Remote_Writes[2] = { 0, 0 };
                uint64 Local_Reads[2] = { 0, 0 };
                uint64 Local_Writes[2] = { 0, 0 };

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
                for (int32 s = 0; (s < (int32)serverUncorePMUs.size()); ++s)
                {
                    serverUncorePMUs[s]->freezeCounters();
                    for (uint32 port = 0; port < (uint32)getQPILinksPerSocket(); ++port)
                    {
                        result.incomingQPIPackets[s][port] = uint64(double(serverUncorePMUs[s]->getIncomingDataFlits(port)) / (64./getDataBytesPerFlit()));
                        result.outgoingQPIFlits[s][port] = serverUncorePMUs[s]->getOutgoingFlits(port);
                        result.TxL0Cycles[s][port] = serverUncorePMUs[s]->getUPIL0TxCycles(port);
                    }
                    serverUncorePMUs[s]->unfreezeCounters();
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
        result.MSRValues[MSR_PACKAGE_THERM_STATUS] = val;
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
template void PCM::readAndAggregateCXLCMCounters<SystemCounterState>(SystemCounterState &);

SocketCounterState PCM::getSocketCounterState(uint32 socket)
{
    SocketCounterState result;
    if (MSR.size())
    {
        // reading core and uncore counter states
        for (int32 core = 0; core < num_cores; ++core)
            if (isCoreOnline(core) && (topology[core].socket_id == int32(socket)))
                result.readAndAggregate(MSR[core]);

        readAndAggregateUncoreMCCounters(socket, result);

        readAndAggregateEnergyCounters(socket, result);

        readPackageThermalHeadroom(socket, result);

    }
    return result;
}

void PCM::getAllCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates, std::vector<CoreCounterState> & coreStates, const bool readAndAggregateSocketUncoreCounters)
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
            std::packaged_task<void()> task([this,&coreStates,&socketStates,core,readAndAggregateSocketUncoreCounters]() -> void
                {
                    coreStates[core].readAndAggregate(MSR[core]);
                    if (readAndAggregateSocketUncoreCounters)
                    {
                        socketStates[topology[core].socket_id].UncoreCounterState::readAndAggregate(MSR[core]); // read package C state counters
                    }
                    readMSRs(MSR[core], threadMSRConfig, coreStates[core]);
                }
            );
            asyncCoreResults.push_back(task.get_future());
            coreTaskQueues[core]->push(task);
        }
        // std::cout << "DEBUG2: " << core << " " << coreStates[core].InstRetiredAny << " \n";
    }
    // std::cout << std::flush;
    for (uint32 s = 0; s < (uint32)num_sockets && readAndAggregateSocketUncoreCounters; ++s)
    {
        int32 refCore = socketRefCore[s];
        if (refCore<0) refCore = 0;
        std::packaged_task<void()> task([this, s, &socketStates, refCore]() -> void
            {
                readAndAggregateUncoreMCCounters(s, socketStates[s]);
                readAndAggregateEnergyCounters(s, socketStates[s]);
                readPackageThermalHeadroom(s, socketStates[s]);
                readMSRs(MSR[refCore], packageMSRConfig, socketStates[s]);
            } );
        asyncCoreResults.push_back(task.get_future());
        coreTaskQueues[refCore]->push(task);
    }

    if (readAndAggregateSocketUncoreCounters)
    {
        readQPICounters(systemState);
        readPCICFGRegisters(systemState);
        readMMIORegisters(systemState);
        readPMTRegisters(systemState);
    }

    for (auto & ar : asyncCoreResults)
        ar.wait();

    for (int32 core = 0; core < num_cores; ++core)
    {   // aggregate core counters into sockets
        if(isCoreOnline(core))
          socketStates[topology[core].socket_id] += coreStates[core];
    }

    for (int32 s = 0; s < num_sockets; ++s)
    {   // aggregate core counters from sockets into system state and
        // aggregate socket uncore iMC, energy and package C state counters into system
        systemState += socketStates[s];
    }

    if (systemEnergyMetricAvailable() && system_energy_status.get() != nullptr)
    {
        systemState.systemEnergyStatus = system_energy_status->read();
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
                if(topology[core].socket_id == s && isCoreOnline(core))
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

uint32 PCM::getAccel() const
{
    return accel;
}

void PCM::setAccel(uint32 input)
{
    accel = input;
}

uint32 PCM::getNumberofAccelCounters() const
{
    return accel_counters_num_max;
}

void PCM::setNumberofAccelCounters(uint32 input)
{
    accel_counters_num_max = input;
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
    if(serverUncorePMUs.size() && serverUncorePMUs[socket].get())
    {
        serverUncorePMUs[socket]->freezeCounters();
        for(uint32 port=0;port < (uint32)serverUncorePMUs[socket]->getNumQPIPorts();++port)
        {
            assert(port < result.xPICounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.xPICounter[port][cnt] = serverUncorePMUs[socket]->getQPILLCounter(port, cnt);
            assert(port < result.M3UPICounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.M3UPICounter[port][cnt] = serverUncorePMUs[socket]->getM3UPICounter(port, cnt);
        }
        for (uint32 channel = 0; channel < (uint32)serverUncorePMUs[socket]->getNumMCChannels(); ++channel)
        {
            assert(channel < result.DRAMClocks.size());
            result.DRAMClocks[channel] = serverUncorePMUs[socket]->getDRAMClocks(channel);
            assert(channel < result.MCCounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.MCCounter[channel][cnt] = serverUncorePMUs[socket]->getMCCounter(channel, cnt);
        }
        for (uint32 channel = 0; channel < (uint32)serverUncorePMUs[socket]->getNumEDCChannels(); ++channel)
        {
            assert(channel < result.HBMClocks.size());
            result.HBMClocks[channel] = serverUncorePMUs[socket]->getHBMClocks(channel);
            assert(channel < result.EDCCounter.size());
            for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
                result.EDCCounter[channel][cnt] = serverUncorePMUs[socket]->getEDCCounter(channel, cnt);
        }
    for (uint32 controller = 0; controller < (uint32)serverUncorePMUs[socket]->getNumMC(); ++controller)
    {
      assert(controller < result.M2MCounter.size());
      for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
          result.M2MCounter[controller][cnt] = serverUncorePMUs[socket]->getM2MCounter(controller, cnt);
      assert(controller < result.HACounter.size());
      for (uint32 cnt = 0; cnt < ServerUncoreCounterState::maxCounters; ++cnt)
          result.HACounter[controller][cnt] = serverUncorePMUs[socket]->getHACounter(controller, cnt);
    }
        serverUncorePMUs[socket]->unfreezeCounters();
    }
    if (MSR.size())
    {
        uint32 refCore = socketRefCore[socket];
        TemporalThreadAffinity tempThreadAffinity(refCore);

        readUncoreCounterValues(result, socket);

        for (uint32 stack = 0; socket < iioPMUs.size() && stack < iioPMUs[socket].size() && stack < ServerUncoreCounterState::maxIIOStacks; ++stack)
        {
            for (int i = 0; i < ServerUncoreCounterState::maxCounters && size_t(i) < iioPMUs[socket][stack].size(); ++i)
            {
                result.IIOCounter[stack][i] = *(iioPMUs[socket][stack].counterValue[i]);
            }
        }
        for (uint32 stack = 0; socket < irpPMUs.size() && stack < irpPMUs[socket].size() && stack < ServerUncoreCounterState::maxIIOStacks; ++stack)
        {
            for (int i = 0; i < ServerUncoreCounterState::maxCounters && size_t(i) < irpPMUs[socket][stack].size(); ++i)
            {
                if (irpPMUs[socket][stack].counterValue[i].get())
                {
                    result.IRPCounter[stack][i] = *(irpPMUs[socket][stack].counterValue[i]);
                }
            }
        }

        result.UncClocks = getUncoreClocks(socket);

        for (size_t p = 0; p < getNumCXLPorts(socket); ++p)
        {
            for (int i = 0; i < ServerUncoreCounterState::maxCounters && socket < cxlPMUs.size() && size_t(i) < cxlPMUs[socket][p].first.size(); ++i)
            {
                result.CXLCMCounter[p][i] = *cxlPMUs[socket][p].first.counterValue[i];
            }
            for (int i = 0; i < ServerUncoreCounterState::maxCounters && socket < cxlPMUs.size() && size_t(i) < cxlPMUs[socket][p].second.size(); ++i)
            {
                result.CXLDPCounter[p][i] = *cxlPMUs[socket][p].second.counterValue[i];
            }
        }
        uint64 val=0;
        //MSR[refCore]->read(MSR_PKG_ENERGY_STATUS,&val);
        //std::cout << "Energy status: " << val << "\n";
        MSR[refCore]->read(MSR_PACKAGE_THERM_STATUS,&val);
        result.PackageThermalHeadroom = extractThermalHeadroom(val);
        result.InvariantTSC = getInvariantTSC_Fast(refCore);
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
        ::close(mcfg_handle);
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
              ::close(mcfg_handle);
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
    0x3441,
    0x3241,
};

static const uint32 M2M_DEV_IDS[] = {
    0x2066,
    0x344A,
    0x324A
};

Mutex socket2busMutex;
std::vector<std::pair<uint32,uint32> > ServerUncorePMUs::socket2iMCbus{};
std::vector<std::pair<uint32,uint32> > ServerUncorePMUs::socket2UPIbus{};
std::vector<std::pair<uint32,uint32> > ServerUncorePMUs::socket2M2Mbus{};

void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize)
{
    if (device == PCM_INVALID_DEV_ADDR || function == PCM_INVALID_FUNC_ADDR)
    {
        return;
    }
    Mutex::Scope _(socket2busMutex);
    if(!socket2bus.empty()) return;

    forAllIntelDevices(
        [&devIdsSize,&DEV_IDS, &socket2bus](const uint32 group, const uint32 bus, const uint32 /* device */, const uint32 /* function */, const uint32 device_id)
        {
            for (uint32 i = 0; i < devIdsSize; ++i)
            {
                // match
                if (DEV_IDS[i] == device_id)
                {
                    // std::cout << "DEBUG: found bus " << std::hex << bus << " with device ID " << device_id << std::dec << "\n";
                    socket2bus.push_back(std::make_pair(group, bus));
                    break;
                }
            }
        }, device, function);
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

PciHandleType * ServerUncorePMUs::createIntelPerfMonDevice(uint32 groupnr_, int32 bus_, uint32 dev_, uint32 func_, bool checkVendor)
{
    if (PciHandleType::exists(groupnr_, (uint32)bus_, dev_, func_))
    {
        PciHandleType * handle = new PciHandleType(groupnr_, bus_, dev_, func_);

        if(!checkVendor) return handle;

        uint32 vendor_id = 0;
        handle->read32(PCM_PCI_VENDOR_ID_OFFSET,&vendor_id);
        vendor_id &= 0x0ffff;

        if(vendor_id == PCM_INTEL_PCI_VENDOR_ID) return handle;

        deleteAndNullify(handle);
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
    std::cerr << "INFO: Linux perf interface to program uncore PMUs is " << (imcIDs.empty()?"NOT ":"") << "present\n";
    if (imcIDs.empty())
    {
        use = 0;
        return 1 == use;
    }
    const char * perf_env = std::getenv("PCM_USE_UNCORE_PERF");
    if (perf_env != NULL && std::string(perf_env) == std::string("1"))
    {
        std::cerr << "INFO: using Linux perf interface to program uncore PMUs because env variable PCM_USE_UNCORE_PERF=1\n";
        use = 1;
    }
    if (secureBoot)
    {
        std::cerr << "INFO: Secure Boot detected. Using Linux perf for uncore PMU programming.\n";
        use = 1;
    }
#else
    if (1)
    {
        if (secureBoot)
        {
            std::cerr << "ERROR: Secure Boot detected. Recompile PCM with -DPCM_USE_PERF or disable Secure Boot.\n";
        }
    }
#endif
    return 1 == use;
}

template <class F>
void PCM::getPCICFGPMUsFromDiscovery(const unsigned int BoxType, const size_t s, F f) const
{
    if (uncorePMUDiscovery.get())
    {
        const auto numBoxes = uncorePMUDiscovery->getNumBoxes(BoxType, s);
        for (size_t pos = 0; pos < numBoxes; ++pos)
        {
            if (uncorePMUDiscovery->getBoxAccessType(BoxType, s, pos) == UncorePMUDiscovery::accessTypeEnum::PCICFG)
            {
                std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs;
                const auto n_regs = uncorePMUDiscovery->getBoxNumRegs(BoxType, s, pos);
                auto makeRegister = [](const uint64 rawAddr)
                {
#ifndef PCI_ENABLE
                    constexpr auto PCI_ENABLE = 0x80000000ULL;
#endif
                    UncorePMUDiscovery::PCICFGAddress Addr;
                    Addr.raw = rawAddr;
                    assert(Addr.raw & PCI_ENABLE);
                    try {
                        auto handle = std::make_shared<PciHandleType>(0, (uint32)Addr.fields.bus,
                                                                        (uint32)Addr.fields.device,
                                                                        (uint32)Addr.fields.function);
                        assert(handle.get());
                        // std::cerr << "DEBUG: opened bdf "<< Addr.getStr() << "\n";
                        return std::make_shared<PCICFGRegister64>(handle, (size_t)Addr.fields.offset);
                    }
                    catch (...)
                    {
                        // std::cerr << "DEBUG: error opening bdf "<< Addr.getStr() << "\n";
                    }
                    return std::shared_ptr<PCICFGRegister64>();
                };
                auto boxCtlRegister = makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, s, pos));
                if (boxCtlRegister.get())
                {
                    for (size_t r = 0; r < n_regs; ++r)
                    {
                        CounterControlRegs.push_back(makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, s, pos, r)));
                        CounterValueRegs.push_back(makeRegister(uncorePMUDiscovery->getBoxCtrAddr(BoxType, s, pos, r)));
                    }
                    f(UncorePMU(boxCtlRegister, CounterControlRegs, CounterValueRegs));
                }
            }
        }
    }
};

ServerUncorePMUs::ServerUncorePMUs(uint32 socket_, const PCM * pcm) :
     iMCbus(-1)
   , UPIbus(-1)
   , M2Mbus(-1)
   , groupnr(0)
   , cpu_family_model(pcm->getCPUFamilyModel())
   , qpi_speed(0)
{
    if (pcm->useLinuxPerfForUncore())
    {
        initPerf(socket_, pcm);
    }
    else
    {
        initRegisterLocations(pcm);
        initBuses(socket_, pcm);
        initDirect(socket_, pcm);
    }

    std::cerr << "Socket " << socket_ << ": " <<
        getNumMC() << " memory controllers detected with total number of " << getNumMCChannels() << " channels. " <<
        getNumQPIPorts() << " " << pcm->xPI() << " ports detected." <<
        " " << m2mPMUs.size() << " M2M (mesh to memory)/B2CMI blocks detected."
        " " << hbm_m2mPMUs.size() << " HBM M2M blocks detected."
        " " << edcPMUs.size() << " EDC/HBM channels detected."
        " " << haPMUs.size()  << " Home Agents detected."
        " " << m3upiPMUs.size() << " M3UPI/B2UPI blocks detected."
        "\n";
}

void ServerUncorePMUs::initRegisterLocations(const PCM * pcm)
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

#define PCM_PCICFG_HBM_M2M_INIT(x, arch) \
    HBM_M2MRegisterLocation.resize(x + 1); \
    HBM_M2MRegisterLocation[x] = std::make_pair(arch##_HBM_M2M_##x##_REGISTER_DEV_ADDR, arch##_HBM_M2M_##x##_REGISTER_FUNC_ADDR);

#define PCM_PCICFG_HA_INIT(x, arch) \
    HARegisterLocation.resize(x + 1); \
    HARegisterLocation[x] = std::make_pair(arch##_HA##x##_REGISTER_DEV_ADDR, arch##_HA##x##_REGISTER_FUNC_ADDR);

    switch (cpu_family_model)
    {
    case PCM::JAKETOWN:
    case PCM::IVYTOWN:
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
    break;
    case PCM::HASWELLX:
    case PCM::BDX_DE:
    case PCM::BDX:
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
    break;
    case PCM::SKX:
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
    break;
    case PCM::ICX:
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
    break;
    case PCM::SPR:
    case PCM::EMR:
    {
        PCM_PCICFG_QPI_INIT(0, SPR);
        PCM_PCICFG_QPI_INIT(1, SPR);
        PCM_PCICFG_QPI_INIT(2, SPR);
        PCM_PCICFG_QPI_INIT(3, SPR);

        PCM_PCICFG_M3UPI_INIT(0, SPR);
        PCM_PCICFG_M3UPI_INIT(1, SPR);
        PCM_PCICFG_M3UPI_INIT(2, SPR);
        PCM_PCICFG_M3UPI_INIT(3, SPR);

        PCM_PCICFG_M2M_INIT(0, SERVER)
        PCM_PCICFG_M2M_INIT(1, SERVER)
        PCM_PCICFG_M2M_INIT(2, SERVER)
        PCM_PCICFG_M2M_INIT(3, SERVER)

        PCM_PCICFG_HBM_M2M_INIT(0, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(1, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(2, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(3, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(4, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(5, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(6, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(7, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(8, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(9, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(10, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(11, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(12, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(13, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(14, SERVER)
        PCM_PCICFG_HBM_M2M_INIT(15, SERVER)
    }
    break;
    case PCM::KNL:
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
    break;
    case PCM::SRF:
    case PCM::GNR:
    {
        PCM_PCICFG_QPI_INIT(0, BHS);
        PCM_PCICFG_QPI_INIT(1, BHS);
        PCM_PCICFG_QPI_INIT(2, BHS);
        PCM_PCICFG_QPI_INIT(3, BHS);
        PCM_PCICFG_QPI_INIT(4, BHS);
        PCM_PCICFG_QPI_INIT(5, BHS);

        // B2CMI (M2M)
        PCM_PCICFG_M2M_INIT(0, BHS)
        PCM_PCICFG_M2M_INIT(1, BHS)
        PCM_PCICFG_M2M_INIT(2, BHS)
        PCM_PCICFG_M2M_INIT(3, BHS)
        PCM_PCICFG_M2M_INIT(4, BHS)
        PCM_PCICFG_M2M_INIT(5, BHS)
        PCM_PCICFG_M2M_INIT(6, BHS)
        PCM_PCICFG_M2M_INIT(7, BHS)
        PCM_PCICFG_M2M_INIT(8, BHS)
        PCM_PCICFG_M2M_INIT(9, BHS)
        PCM_PCICFG_M2M_INIT(10, BHS)
        PCM_PCICFG_M2M_INIT(11, BHS)

        // B2UPI (M3UPI)
        PCM_PCICFG_M3UPI_INIT(0, BHS);
        PCM_PCICFG_M3UPI_INIT(1, BHS);
        PCM_PCICFG_M3UPI_INIT(2, BHS);
        PCM_PCICFG_M3UPI_INIT(3, BHS);
        PCM_PCICFG_M3UPI_INIT(4, BHS);
        PCM_PCICFG_M3UPI_INIT(5, BHS);
    }
    break;
    case PCM::SNOWRIDGE:
    {
        PCM_PCICFG_M2M_INIT(0, SERVER)
        PCM_PCICFG_M2M_INIT(1, SERVER)
        PCM_PCICFG_M2M_INIT(2, SERVER)
        PCM_PCICFG_M2M_INIT(3, SERVER)
    }
    break;
    case PCM::GRR:
    {
        // placeholder to init GRR PCICFG
    }
    break;
    default:
        std::cerr << "Error: Uncore PMU for processor with id 0x" << std::hex << cpu_family_model << std::dec << " is not supported.\n";
        throw std::exception();
    }

#undef PCM_PCICFG_MC_INIT
#undef PCM_PCICFG_QPI_INIT
#undef PCM_PCICFG_M3UPI_INIT
#undef PCM_PCICFG_EDC_INIT
#undef PCM_PCICFG_M2M_INIT
#undef PCM_PCICFG_HA_INIT
}

void ServerUncorePMUs::initBuses(uint32 socket_, const PCM * pcm)
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

    if (PCM::hasUPI(cpu_family_model) && XPIRegisterLocation.size() > 0)
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
    // std::cerr << "DEBUG: UPIbus: " << UPIbus << "\n";
}

void ServerUncorePMUs::initDirect(uint32 socket_, const PCM * pcm)
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
            if (cpu_family_model == PCM::KNL) {
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

    auto populateM2MPMUs = [](uint32 groupnr, int32 M2Mbus, int32 cpu_family_model, const std::vector<std::pair<uint32, uint32> > & M2MRegisterLocation, UncorePMUVector & m2mPMUs)
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
            switch (cpu_family_model)
            {
            case PCM::ICX:
            case PCM::SNOWRIDGE:
            case PCM::SPR:
            case PCM::EMR:
            case PCM::GNR: // B2CMI PMUs
            case PCM::SRF:
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
                break;
            default:
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
    };
    populateM2MPMUs(groupnr, M2Mbus, cpu_family_model, M2MRegisterLocation, m2mPMUs);
    populateM2MPMUs(groupnr, M2Mbus, cpu_family_model, HBM_M2MRegisterLocation, hbm_m2mPMUs);

    int numChannels = 0;
    if (safe_getenv("PCM_NO_IMC_DISCOVERY") == std::string("1"))
    {
        if (cpu_family_model == PCM::SPR || cpu_family_model == PCM::EMR)
        {
            numChannels = 3;
        }
    }
    if (cpu_family_model == PCM::SNOWRIDGE || cpu_family_model == PCM::ICX)
    {
        numChannels = 2;
        if (PCM::getCPUFamilyModelFromCPUID() == PCM::ICX_D)
        {
            numChannels = 3;
        }
    }

    auto createIMCPMU = [](const size_t addr, const size_t mapSize) -> UncorePMU
    {
        const auto alignedAddr = addr & ~4095ULL;
        const auto alignDelta = addr & 4095ULL;
        auto handle = std::make_shared<MMIORange>(alignedAddr, mapSize, false);
        return UncorePMU(
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_BOX_CTL_OFFSET + alignDelta),
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL0_OFFSET + alignDelta),
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL1_OFFSET + alignDelta),
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL2_OFFSET + alignDelta),
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_CTL3_OFFSET + alignDelta),
            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR0_OFFSET + alignDelta),
            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR1_OFFSET + alignDelta),
            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR2_OFFSET + alignDelta),
            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_CTR3_OFFSET + alignDelta),
            std::make_shared<MMIORegister32>(handle, SERVER_MC_CH_PMON_FIXED_CTL_OFFSET + alignDelta),
            std::make_shared<MMIORegister64>(handle, SERVER_MC_CH_PMON_FIXED_CTR_OFFSET + alignDelta)
        );
    };

    auto initAndCheckSocket2Ubox0Bus = [&socket_]() -> bool
    {
        initSocket2Ubox0Bus();
        if (socket_ >= socket2UBOX0bus.size())
        {
            std::cerr << "ERROR: socket " << socket_ << " is not found in socket2UBOX0bus. socket2UBOX0bus.size =" << socket2UBOX0bus.size() << std::endl;
            return false;
        }
        return true;
    };

    if (numChannels > 0)
    {
        if (initAndCheckSocket2Ubox0Bus())
        {
            auto memBars = getServerMemBars((uint32)m2mPMUs.size(), socket2UBOX0bus[socket_].first, socket2UBOX0bus[socket_].second);
            for (auto & memBar : memBars)
            {
                for (int channel = 0; channel < numChannels; ++channel)
                {
                    imcPMUs.push_back(createIMCPMU(memBar + SERVER_MC_CH_PMON_BASE_ADDR + channel * SERVER_MC_CH_PMON_STEP, SERVER_MC_CH_PMON_SIZE));
                }
                num_imc_channels.push_back(numChannels);
            }
        }
    }
    else
    {
        switch (cpu_family_model)
        {
            case PCM::SPR:
            case PCM::EMR:
                {
                    auto & uncorePMUDiscovery = pcm->uncorePMUDiscovery;
                    const auto BoxType = SPR_IMC_BOX_TYPE;
                    if (uncorePMUDiscovery.get())
                    {
                        const auto numBoxes = uncorePMUDiscovery->getNumBoxes(BoxType, socket_);
                        for (size_t pos = 0; pos < numBoxes; ++pos)
                        {
                            if (uncorePMUDiscovery->getBoxAccessType(BoxType, socket_, pos) == UncorePMUDiscovery::accessTypeEnum::MMIO)
                            {
                                std::vector<std::shared_ptr<HWRegister> > CounterControlRegs, CounterValueRegs;
                                const auto n_regs = uncorePMUDiscovery->getBoxNumRegs(BoxType, socket_, pos);
                                auto makeRegister = [](const uint64 rawAddr, const uint32 bits) -> std::shared_ptr<HWRegister>
                                {
                                    const auto mapSize = SERVER_MC_CH_PMON_SIZE;
                                    const auto alignedAddr = rawAddr & ~4095ULL;
                                    const auto alignDelta = rawAddr & 4095ULL;
                                    try {
                                        auto handle = std::make_shared<MMIORange>(alignedAddr, mapSize, false);
                                        assert(handle.get());
                                        switch (bits)
                                        {
                                            case 32:
                                                return std::make_shared<MMIORegister32>(handle, (size_t)alignDelta);
                                            case 64:
                                                return std::make_shared<MMIORegister64>(handle, (size_t)alignDelta);
                                        }
                                    }
                                    catch (...)
                                    {
                                    }
                                    return std::shared_ptr<HWRegister>();
                                };

                                auto boxCtlRegister = makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, socket_, pos), 32);
                                if (boxCtlRegister.get())
                                {
                                    for (size_t r = 0; r < n_regs; ++r)
                                    {
                                        CounterControlRegs.push_back(makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, socket_, pos, r), 32));
                                        CounterValueRegs.push_back(makeRegister(uncorePMUDiscovery->getBoxCtrAddr(BoxType, socket_, pos, r), 64));
                                    }
                                    imcPMUs.push_back(UncorePMU(boxCtlRegister,
                                        CounterControlRegs,
                                        CounterValueRegs,
                                        makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, socket_, pos) + SERVER_MC_CH_PMON_FIXED_CTL_OFFSET, 32),
                                        makeRegister(uncorePMUDiscovery->getBoxCtlAddr(BoxType, socket_, pos) + SERVER_MC_CH_PMON_FIXED_CTR_OFFSET, 64)));
                                }
                            }
                        }
                    }
                    if (imcPMUs.empty() == false)
                    {
                        numChannels = 2;
                        for (size_t c = 0; c < imcPMUs.size(); c += numChannels)
                        {
                            num_imc_channels.push_back(numChannels);
                        }
                    }
                }
                break;
        }
    }

    auto initBHSiMCPMUs = [&](const size_t numChannelsParam)
    {
        numChannels = (std::min)(numChannelsParam, m2mPMUs.size());
        if (initAndCheckSocket2Ubox0Bus())
        {
            auto memBar = getServerSCFBar(socket2UBOX0bus[socket_].first, socket2UBOX0bus[socket_].second);
            for (int channel = 0; channel < numChannels; ++channel)
            {
                imcPMUs.push_back(createIMCPMU(memBar + BHS_MC_CH_PMON_BASE_ADDR + channel * SERVER_MC_CH_PMON_STEP, SERVER_MC_CH_PMON_SIZE));
                num_imc_channels.push_back(1);
            }
        }
    };

    switch (cpu_family_model)
    {
        case PCM::GRR:
            initBHSiMCPMUs(2);
            break;
        case PCM::GNR:
        case PCM::SRF:
            initBHSiMCPMUs(12);
            break;
    }

    if (imcPMUs.empty())
    {
        std::cerr << "PCM error: no memory controllers found.\n";
        throw std::exception();
    }

    if (cpu_family_model == PCM::KNL)
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

    if (hbm_m2mPMUs.empty() == false)
    {
        // HBM
        if (initAndCheckSocket2Ubox0Bus())
        {
            const auto bar = getServerSCFBar(socket2UBOX0bus[socket_].first, socket2UBOX0bus[socket_].second);
            for (size_t box = 0; box < hbm_m2mPMUs.size(); ++box)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    edcPMUs.push_back(createIMCPMU(bar + SERVER_HBM_CH_PMON_BASE_ADDR + box * SERVER_HBM_BOX_PMON_STEP + channel * SERVER_HBM_CH_PMON_STEP, SERVER_HBM_CH_PMON_SIZE));
                }
            }
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
        switch (cpu_family_model)
        {
        case PCM::ICX:
        case PCM::SPR:
        case PCM::EMR:
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
            break;
        case PCM::GNR:
        case PCM::SRF:
            m3upiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTL0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTL1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTL2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTL3_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTR0_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTR1_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTR2_ADDR),
                    std::make_shared<PCICFGRegister64>(handle, BHS_M3UPI_PCI_PMON_CTR3_ADDR)
                )
            );
            break;

        default:
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

    if (pcm->getNumSockets() <= 4 && safe_getenv("PCM_NO_UPILL_DISCOVERY") != std::string("1"))
    {
        switch (cpu_family_model)
        {
            // don't use the discovery on SPR to work-around the issue
	    // mentioned in https://lore.kernel.org/lkml/20221129191023.936738-1-kan.liang@linux.intel.com/T/
            case PCM::EMR:
                {
                    std::cerr << "INFO: Trying to detect UPILL PMU through uncore PMU discovery..\n";
                    pcm->getPCICFGPMUsFromDiscovery(SPR_UPILL_BOX_TYPE, socket_, [this](const UncorePMU & pmu)
                    {
                        xpiPMUs.push_back(pmu);
                    });
                }
                break;
        }
    }

    std::vector<std::shared_ptr<PciHandleType> > qpiLLHandles;
    auto xPI = pcm->xPI();
    try
    {
        if (xpiPMUs.empty()) for (size_t i = 0; i < XPIRegisterLocation.size(); ++i)
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

    if (xpiPMUs.empty()) for (auto & handle : qpiLLHandles)
    {
        switch (cpu_family_model)
        {
        case PCM::SKX:
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
            break;
        case PCM::ICX:
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
            break;
       case PCM::SPR:
       case PCM::EMR:
       case PCM::GNR:
       case PCM::SRF:
            xpiPMUs.push_back(
                UncorePMU(
                    std::make_shared<PCICFGRegister32>(handle, SPR_UPI_PCI_PMON_BOX_CTL_ADDR),
                    std::make_shared<PCICFGRegister32>(handle, SPR_UPI_PCI_PMON_CTL0_ADDR + 8*0),
                    std::make_shared<PCICFGRegister32>(handle, SPR_UPI_PCI_PMON_CTL0_ADDR + 8*1),
                    std::make_shared<PCICFGRegister32>(handle, SPR_UPI_PCI_PMON_CTL0_ADDR + 8*2),
                    std::make_shared<PCICFGRegister32>(handle, SPR_UPI_PCI_PMON_CTL0_ADDR + 8*3),
                    std::make_shared<PCICFGRegister64>(handle, SPR_UPI_PCI_PMON_CTR0_ADDR + 8*0),
                    std::make_shared<PCICFGRegister64>(handle, SPR_UPI_PCI_PMON_CTR0_ADDR + 8*1),
                    std::make_shared<PCICFGRegister64>(handle, SPR_UPI_PCI_PMON_CTR0_ADDR + 8*2),
                    std::make_shared<PCICFGRegister64>(handle, SPR_UPI_PCI_PMON_CTR0_ADDR + 8*3)
                )
            );
            break;
        default:
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

bool ServerUncorePMUs::HBMAvailable() const
{
    return edcPMUs.empty() == false;
}


#ifdef PCM_USE_PERF
class PerfVirtualFilterRegister;

class PerfVirtualControlRegister : public HWRegister
{
    friend class PerfVirtualCounterRegister;
    friend class PerfVirtualFilterRegister;
    friend class IDXPerfVirtualFilterRegister;
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
    PerfVirtualControlRegister(const PerfVirtualControlRegister &) = delete;
    PerfVirtualControlRegister & operator = (const PerfVirtualControlRegister &) = delete;
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
            if (errno == 24) std::cerr << PCM_ULIMIT_RECOMMENDATION;
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

class IDXPerfVirtualFilterRegister : public HWRegister
{
    uint64 lastValue;
    std::shared_ptr<PerfVirtualControlRegister> controlReg;
    int filterNr;
public:
    IDXPerfVirtualFilterRegister(std::shared_ptr<PerfVirtualControlRegister> controlReg_, int filterNr_) :
            lastValue(0),
            controlReg(controlReg_),
            filterNr(filterNr_)
    {
    }
    void operator = (uint64 val) override
    {
        lastValue = val;

        /*
        struct {
            u64 wq:32;
            u64 tc:8;
            u64 pg_sz:4;
            u64 xfer_sz:8;
            u64 eng:8;
        } filter_cfg;
        */
        
        switch (filterNr)
        {
            case 0: //FLT_WQ
                controlReg->event.config1 = ((controlReg->event.config1 & 0xFFFFFFF00000000) | (val & 0xFFFFFFFF));
                break;

            case 1: //FLT_TC
                controlReg->event.config1 = ((controlReg->event.config1 & 0xFFFFF00FFFFFFFF) | ((val & 0xFF) << 32));
                break;
                
            case 2: //FLT_PG_SZ
                controlReg->event.config1 = ((controlReg->event.config1 & 0xFFFF0FFFFFFFFFF) | ((val & 0xF) << 40));
                break;
                
            case 3: //FLT_XFER_SZ
                controlReg->event.config1 = ((controlReg->event.config1 & 0xFF00FFFFFFFFFFF) | ((val & 0xFF) << 44));
                break;

            case 4: //FLT_ENG
                controlReg->event.config1 = ((controlReg->event.config1 & 0x00FFFFFFFFFFFFF) | ((val & 0xFF) << 52));
                break;

            default:
                break;
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
            //std::cout << "DEBUG: " << type << " pmu id " << pmuID << " found\n";
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
                std::make_shared<VirtualDummyRegister>(),
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

void populatePerfPMUs(unsigned socket_, const std::vector<int>& ids, std::vector<UncorePMURef>& pmus, bool fixed, bool filter0, bool filter1)
{
    for (const auto& id : ids)
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
            std::make_shared<UncorePMU>(
                std::make_shared<VirtualDummyRegister>(),
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

std::vector<std::pair<int, uint32> > enumerateIDXPerfPMUs(const std::string & type, int max_id)
{
    uint32 numaNode=0xff;
    auto getPerfPMUID = [](const std::string & type, int num)
    {
        int id = -1;
        std::ostringstream pmuIDPath(std::ostringstream::out);
        pmuIDPath << std::string("/sys/bus/event_source/devices/") << type;
        if (num != -1)
        {
            pmuIDPath << num;
        }
        pmuIDPath << "/type";
        const std::string pmuIDStr = readSysFS(pmuIDPath.str().c_str(), true);
        if (pmuIDStr.size())
        {
            id = std::atoi(pmuIDStr.c_str());
        }
        return id;
    };

    //Enumurate IDX devices by linux sysfs scan
    std::vector<std::pair<int, uint32> > ids;
    for (int i = -1; i < max_id; ++i)
    {
        int pmuID = getPerfPMUID(type, i);
        if (pmuID > 0)
        {
            numaNode = 0xff;
            std::ostringstream devNumaNodePath(std::ostringstream::out);
            devNumaNodePath << std::string("/sys/bus/dsa/devices/") << type << i << "/numa_node";
            const std::string devNumaNodeStr = readSysFS(devNumaNodePath.str().c_str(), true);
            if (devNumaNodeStr.size())
            {
                numaNode = std::atoi(devNumaNodeStr.c_str());
                if (numaNode == (std::numeric_limits<uint32>::max)())
                {
                    numaNode = 0xff; //translate to special value for numa disable case.
                }
            }
            //std::cout << "IDX DEBUG: " << type << " pmu id " << pmuID << " found\n";
            //std::cout << "IDX DEBUG: numa node file path=" << devNumaNodePath.str().c_str()  << ", value=" << numaNode << std::endl;
            ids.push_back(std::make_pair(pmuID, numaNode));
        }
    }
    
    return ids;
}

void populateIDXPerfPMUs(unsigned socket_, const std::vector<std::pair<int, uint32> > & ids, std::vector<IDX_PMU> & pmus)
{
    for (const auto & id : ids)
    {
        uint32 n_regs = SPR_IDX_ACCEL_COUNTER_MAX_NUM;

        std::vector<std::shared_ptr<HWRegister> > CounterControlRegs;
        std::vector<std::shared_ptr<HWRegister> > CounterValueRegs;
        std::vector<std::shared_ptr<HWRegister> > CounterFilterWQRegs, CounterFilterENGRegs, CounterFilterTCRegs, CounterFilterPGSZRegs, CounterFilterXFERSZRegs;

        for (size_t r = 0; r < n_regs; ++r)
        {
            auto CounterControlReg = std::make_shared<PerfVirtualControlRegister>(socket_, id.first);

            CounterControlRegs.push_back(CounterControlReg);
            CounterValueRegs.push_back(std::make_shared<PerfVirtualCounterRegister>(CounterControlReg));
            CounterFilterWQRegs.push_back(std::make_shared<IDXPerfVirtualFilterRegister>(CounterControlReg, 0));
            CounterFilterTCRegs.push_back(std::make_shared<IDXPerfVirtualFilterRegister>(CounterControlReg, 1));
            CounterFilterPGSZRegs.push_back(std::make_shared<IDXPerfVirtualFilterRegister>(CounterControlReg, 2));
            CounterFilterXFERSZRegs.push_back(std::make_shared<IDXPerfVirtualFilterRegister>(CounterControlReg, 3));
            CounterFilterENGRegs.push_back(std::make_shared<IDXPerfVirtualFilterRegister>(CounterControlReg, 4));
        }

        pmus.push_back(
            IDX_PMU(
                true,
                id.second,
                0xff,//No support of socket location in perf driver mode.
                std::make_shared<VirtualDummyRegister>(),
                std::make_shared<VirtualDummyRegister>(),
                std::make_shared<VirtualDummyRegister>(),
                CounterControlRegs,
                CounterValueRegs,
                CounterFilterWQRegs,
                CounterFilterENGRegs,
                CounterFilterTCRegs,
                CounterFilterPGSZRegs,
                CounterFilterXFERSZRegs
            ));
    }
}
#endif

void ServerUncorePMUs::initPerf(uint32 socket_, const PCM * /*pcm*/)
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

size_t ServerUncorePMUs::getNumMCChannels(const uint32 controller) const
{
    if (controller < num_imc_channels.size())
    {
        return num_imc_channels[controller];
    }
    return 0;
}

ServerUncorePMUs::~ServerUncorePMUs()
{
}


void ServerUncorePMUs::programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, const int rankA, const int rankB)
{
    switch (metrics)
    {
        case PartialWrites:
        case Pmem:
        case PmemMemoryMode:
        case PmemMixedMode:
            break;
        default:
            std::cerr << "PCM Error: unknown memory metrics: " << metrics << "\n";
            return;
    }

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
                    MCCntConfig[EventPosition::MM_MISS_CLEAN] = MC_CH_PCI_PMON_CTL_EVENT(0xd3) + MC_CH_PCI_PMON_CTL_UMASK(2); // monitor TAGCHK.MISS_CLEAN on counter 2
                    MCCntConfig[EventPosition::MM_MISS_DIRTY] = MC_CH_PCI_PMON_CTL_EVENT(0xd3) + MC_CH_PCI_PMON_CTL_UMASK(4); // monitor TAGCHK.MISS_DIRTY on counter 3
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
        switch(cpu_family_model)
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
        case PCM::SPR:
        case PCM::EMR:
            {
                EDCCntConfig[EventPosition::READ] = MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 0: CAS_COUNT.RD
                EDCCntConfig[EventPosition::WRITE] = MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 1: CAS_COUNT.WR
            }
            if (setEvents2_3(MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xcc)) == false) // monitor partial writes on counter 2: CAS_COUNT.RD_UNDERFILL
            {
                return;
            }
            break;
        case PCM::GNR:
        case PCM::GRR:
        case PCM::SRF:
            if (metrics == PmemMemoryMode)
            {
                std::cerr << "PCM Error: PMM/Pmem metrics are not available on your platform\n";
                return;
            }
            else
            {
                MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 0: CAS_COUNT_SCH0.RD
                MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 1: CAS_COUNT_SCH0.WR
                MCCntConfig[EventPosition::READ2] = MC_CH_PCI_PMON_CTL_EVENT(0x06) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 2: CAS_COUNT_SCH1.RD
                MCCntConfig[EventPosition::WRITE2] = MC_CH_PCI_PMON_CTL_EVENT(0x06) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 3: CAS_COUNT_SCH1.WR
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
        if (rankA < 0 || rankA > 7)
        {
            std::cerr << "PCM Error: invalid rankA value: " << rankA << "\n";
            return;
        }
        switch(cpu_family_model)
        {
        case PCM::IVYTOWN:
            MCCntConfig[EventPosition::READ_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // RD_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::WRITE_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // WR_CAS_RANK(rankA) all banks
            if (rankB >= 0 && rankB <= 7)
            {
                MCCntConfig[EventPosition::READ_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // RD_CAS_RANK(rankB) all banks
                MCCntConfig[EventPosition::WRITE_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(0xff); // WR_CAS_RANK(rankB) all banks
            }
            break;
        case PCM::HASWELLX:
        case PCM::BDX_DE:
        case PCM::BDX:
        case PCM::SKX:
            MCCntConfig[EventPosition::READ_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(16); // RD_CAS_RANK(rankA) all banks
            MCCntConfig[EventPosition::WRITE_RANK_A] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankA)) + MC_CH_PCI_PMON_CTL_UMASK(16); // WR_CAS_RANK(rankA) all banks
            if (rankB >= 0 && rankB <= 7)
            {
                MCCntConfig[EventPosition::READ_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb0 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(16); // RD_CAS_RANK(rankB) all banks
                MCCntConfig[EventPosition::WRITE_RANK_B] = MC_CH_PCI_PMON_CTL_EVENT((0xb8 + rankB)) + MC_CH_PCI_PMON_CTL_UMASK(16); // WR_CAS_RANK(rankB) all banks
            }
            break;
        case PCM::KNL:
            MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: CAS.RD
            MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x03) + MC_CH_PCI_PMON_CTL_UMASK(2);  // monitor reads on counter 1: CAS.WR
            EDCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x01) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 0: RPQ
            EDCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x02) + MC_CH_PCI_PMON_CTL_UMASK(1);  // monitor reads on counter 1: WPQ
            break;
        default:
            std::cerr << "PCM Error: your processor " << pcm->getCPUBrandString() << " ID 0x" << std::hex << cpu_family_model << std::dec << " does not support the required performance events \n";
            return;
        }
    }
    programIMC(MCCntConfig);
    if (pcm->HBMmemoryTrafficMetricsAvailable()) programEDC(EDCCntConfig);

    programM2M();

    xpiPMUs.clear(); // no QPI events used
    return;
}

void ServerUncorePMUs::program()
{
    PCM * pcm = PCM::getInstance();
    uint32 MCCntConfig[4] = {0, 0, 0, 0};
    uint32 EDCCntConfig[4] = {0, 0, 0, 0};
    switch(cpu_family_model)
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
    case PCM::SPR:
    case PCM::EMR:
        EDCCntConfig[EventPosition::READ] = MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 0: CAS_COUNT.RD
        EDCCntConfig[EventPosition::WRITE] = MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 1: CAS_COUNT.WR
        break;
    case PCM::GNR:
    case PCM::GRR:
    case PCM::SRF:
        MCCntConfig[EventPosition::READ] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 0: CAS_COUNT_SCH0.RD
        MCCntConfig[EventPosition::WRITE] = MC_CH_PCI_PMON_CTL_EVENT(0x05) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 1: CAS_COUNT_SCH0.WR
        MCCntConfig[EventPosition::READ2] = MC_CH_PCI_PMON_CTL_EVENT(0x06) + MC_CH_PCI_PMON_CTL_UMASK(0xcf);  // monitor reads on counter 2: CAS_COUNT_SCH1.RD
        MCCntConfig[EventPosition::WRITE2] = MC_CH_PCI_PMON_CTL_EVENT(0x06) + MC_CH_PCI_PMON_CTL_UMASK(0xf0); // monitor writes on counter 3: CAS_COUNT_SCH1.WR
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
    if (pcm->HBMmemoryTrafficMetricsAvailable()) programEDC(EDCCntConfig);

    programM2M();

    uint32 event[4];
    if (PCM::hasUPI(cpu_family_model))
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

void ServerUncorePMUs::programXPI(const uint32 * event)
{
    const uint32 extra = PCM::hasUPI(cpu_family_model) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN;
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

void ServerUncorePMUs::cleanupQPIHandles()
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

void ServerUncorePMUs::cleanupPMUs()
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

uint64 ServerUncorePMUs::getImcReads()
{
    return getImcReadsForChannels((uint32)0, (uint32)imcPMUs.size());
}

uint64 ServerUncorePMUs::getImcReadsForController(uint32 controller)
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

uint64 ServerUncorePMUs::getImcReadsForChannels(uint32 beginChannel, uint32 endChannel)
{
    uint64 result = 0;
    for (uint32 i = beginChannel; i < endChannel && i < imcPMUs.size(); ++i)
    {
        result += getMCCounter(i, EventPosition::READ);
        switch (cpu_family_model)
        {
            case PCM::GNR:
            case PCM::GRR:
            case PCM::SRF:
                result += getMCCounter(i, EventPosition::READ2);
                break;
        }
    }
    return result;
}

uint64 ServerUncorePMUs::getImcWrites()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)imcPMUs.size(); ++i)
    {
        result += getMCCounter(i, EventPosition::WRITE);
        switch (cpu_family_model)
        {
            case PCM::GNR:
            case PCM::GRR:
            case PCM::SRF:
                result += getMCCounter(i, EventPosition::WRITE2);
                break;
        }
    }

    return result;
}


uint64 ServerUncorePMUs::getNMHits()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::NM_HIT);
    }

    return result;
}

uint64 ServerUncorePMUs::getNMMisses()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::MM_MISS_CLEAN) + getM2MCounter(i, EventPosition::MM_MISS_DIRTY);        
    }

    return result;
}


uint64 ServerUncorePMUs::getPMMReads()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::PMM_READ);
    }
    return result;
}

uint64 ServerUncorePMUs::getPMMWrites()
{
    uint64 result = 0;
    for (uint32 i = 0; i < (uint32)m2mPMUs.size(); ++i)
    {
        result += getM2MCounter(i, EventPosition::PMM_WRITE);
    }
    return result;
}

uint64 ServerUncorePMUs::getEdcReads()
{
    uint64 result = 0;

    for (auto & pmu: edcPMUs)
    {
        result += *pmu.counterValue[EventPosition::READ];
    }

    return result;
}

uint64 ServerUncorePMUs::getEdcWrites()
{
    uint64 result = 0;

    for (auto & pmu : edcPMUs)
    {
        result += *pmu.counterValue[EventPosition::WRITE];
    }

    return result;
}

uint64 ServerUncorePMUs::getIncomingDataFlits(uint32 port)
{
    uint64 drs = 0, ncb = 0;

    if (port >= (uint32)xpiPMUs.size())
        return 0;

    if (PCM::hasUPI(cpu_family_model) == false)
    {
        drs = *xpiPMUs[port].counterValue[0];
    }
    ncb = *xpiPMUs[port].counterValue[1];

    return drs + ncb;
}

uint64 ServerUncorePMUs::getOutgoingFlits(uint32 port)
{
    return getQPILLCounter(port,2);
}

uint64 ServerUncorePMUs::getUPIL0TxCycles(uint32 port)
{
    if (PCM::hasUPI(cpu_family_model))
        return getQPILLCounter(port,0);
    return 0;
}

void ServerUncorePMUs::program_power_metrics(int mc_profile)
{
    uint32 xPIEvents[4] = { 0,0,0,0 };
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_TxL0P_POWER_CYCLES] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_family_model) ? 0x27 : 0x0D)); // L0p Tx Cycles (TxL0P_POWER_CYCLES)
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_L1_POWER_CYCLES] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_family_model) ? 0x21 : 0x12)); // L1 Cycles (L1_POWER_CYCLES)
    xPIEvents[ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS] = (uint32)Q_P_PCI_PMON_CTL_EVENT((PCM::hasUPI(cpu_family_model) ? 0x01 : 0x14)); // QPI/UPI clocks (CLOCKTICKS)

    programXPI(xPIEvents);

    uint32 MCCntConfig[4] = {0,0,0,0};
    unsigned int UNC_M_POWER_CKE_CYCLES = 0x83;
    switch (cpu_family_model)
    {
        case PCM::ICX:
        case PCM::SNOWRIDGE:
        case PCM::SPR:
        case PCM::EMR:
        case PCM::SRF:
        case PCM::GNR:
        case PCM::GNR_D:
            UNC_M_POWER_CKE_CYCLES = 0x47;
            break;
    }
    unsigned int UNC_M_POWER_CHANNEL_PPD_CYCLES = 0x85;
    switch (cpu_family_model)
    {
        case PCM::SRF:
        case PCM::GNR:
        case PCM::GNR_D:
            UNC_M_POWER_CHANNEL_PPD_CYCLES = 0x88;
            break;
    }
    unsigned int UNC_M_SELF_REFRESH_ENTER_SUCCESS_CYCLES_UMASK = 0;
    switch (cpu_family_model)
    {
        case PCM::SRF:
        case PCM::GNR:
        case PCM::GNR_D:
            UNC_M_SELF_REFRESH_ENTER_SUCCESS_CYCLES_UMASK = 0x01;
            break;
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
            MCCntConfig[0] = MC_CH_PCI_PMON_CTL_EVENT(0x43) + MC_CH_PCI_PMON_CTL_UMASK(UNC_M_SELF_REFRESH_ENTER_SUCCESS_CYCLES_UMASK);
            MCCntConfig[1] = MC_CH_PCI_PMON_CTL_EVENT(0x43) + MC_CH_PCI_PMON_CTL_UMASK(UNC_M_SELF_REFRESH_ENTER_SUCCESS_CYCLES_UMASK) + MC_CH_PCI_PMON_CTL_THRESH(1) + MC_CH_PCI_PMON_CTL_EDGE_DET;
            MCCntConfig[2] = MC_CH_PCI_PMON_CTL_EVENT(UNC_M_POWER_CHANNEL_PPD_CYCLES);
            break;
    }

    programIMC(MCCntConfig);
}

void enableAndResetMCFixedCounter(UncorePMU& pmu)
{
    // enable fixed counter (DRAM clocks)
    *pmu.fixedCounterControl = MC_CH_PCI_PMON_FIXED_CTL_EN;

    // reset it
    *pmu.fixedCounterControl = MC_CH_PCI_PMON_FIXED_CTL_EN + MC_CH_PCI_PMON_FIXED_CTL_RST;
}

void ServerUncorePMUs::programIMC(const uint32 * MCCntConfig)
{
    const uint32 extraIMC = (cpu_family_model == PCM::SKX)?UNC_PMON_UNIT_CTL_RSV:UNC_PMON_UNIT_CTL_FRZ_EN;

    for (uint32 i = 0; i < (uint32)imcPMUs.size(); ++i)
    {
        // imc PMU
        imcPMUs[i].initFreeze(extraIMC);

        enableAndResetMCFixedCounter(imcPMUs[i]);

        PCM::program(imcPMUs[i], MCCntConfig, MCCntConfig + 4, extraIMC);
    }
}

void ServerUncorePMUs::programEDC(const uint32 * EDCCntConfig)
{
    for (uint32 i = 0; i < (uint32)edcPMUs.size(); ++i)
    {
        edcPMUs[i].initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        // HBM clocks enabled by default
        if (cpu_family_model == PCM::KNL)
        {
            *edcPMUs[i].fixedCounterControl = EDC_CH_PCI_PMON_FIXED_CTL_EN;
        }
        else
        {
            enableAndResetMCFixedCounter(edcPMUs[i]);
        }

        PCM::program(edcPMUs[i], EDCCntConfig, EDCCntConfig + 4, UNC_PMON_UNIT_CTL_FRZ_EN);
    }
}

void ServerUncorePMUs::programM2M()
{
    uint64 cfg[4] = {0, 0, 0, 0};
    switch (cpu_family_model)
    {
    case PCM::SPR:
    case PCM::EMR:
        cfg[EventPosition::M2M_CLOCKTICKS] = M2M_PCI_PMON_CTL_EVENT(0x01);                         // CLOCKTICKS
        cfg[EventPosition::PMM_READ] = M2M_PCI_PMON_CTL_EVENT(0x24) + M2M_PCI_PMON_CTL_UMASK(0x20) + UNC_PMON_CTL_UMASK_EXT(0x03);  // UNC_M2M_IMC_READS.TO_PMM
        cfg[EventPosition::PMM_WRITE] = M2M_PCI_PMON_CTL_EVENT(0x25) + M2M_PCI_PMON_CTL_UMASK(0x80) + UNC_PMON_CTL_UMASK_EXT(0x18); // UNC_M2M_IMC_WRITES.TO_PMM
        break;
    case PCM::ICX:
        cfg[EventPosition::NM_HIT] = M2M_PCI_PMON_CTL_EVENT(0x2c) + M2M_PCI_PMON_CTL_UMASK(3);    // UNC_M2M_TAG_HIT.NM_DRD_HIT_* events (CLEAN | DIRTY)
        cfg[EventPosition::M2M_CLOCKTICKS] = 0;                                                      // CLOCKTICKS
        cfg[EventPosition::PMM_READ] = M2M_PCI_PMON_CTL_EVENT(0x37) + M2M_PCI_PMON_CTL_UMASK(0x20) + UNC_PMON_CTL_UMASK_EXT(0x07);  // UNC_M2M_IMC_READS.TO_PMM
        cfg[EventPosition::PMM_WRITE] = M2M_PCI_PMON_CTL_EVENT(0x38) + M2M_PCI_PMON_CTL_UMASK(0x80) + UNC_PMON_CTL_UMASK_EXT(0x1C); // UNC_M2M_IMC_WRITES.TO_PMM
        break;
    case PCM::GNR:
    case PCM::SRF:
        cfg[EventPosition::NM_HIT] = M2M_PCI_PMON_CTL_EVENT(0x1F) + M2M_PCI_PMON_CTL_UMASK(0x0F);    // UNC_B2CMI_TAG_HIT.ALL
        cfg[EventPosition::M2M_CLOCKTICKS] = 0;                                                      // CLOCKTICKS
        cfg[EventPosition::MM_MISS_CLEAN] = M2M_PCI_PMON_CTL_EVENT(0x4B) + M2M_PCI_PMON_CTL_UMASK(0x05);  // UNC_B2CMI_TAG_MISS.CLEAN
        cfg[EventPosition::MM_MISS_DIRTY] = M2M_PCI_PMON_CTL_EVENT(0x4B) + M2M_PCI_PMON_CTL_UMASK(0x0A);  // UNC_B2CMI_TAG_MISS.DIRTY
        break;
    default:
        cfg[EventPosition::NM_HIT] = M2M_PCI_PMON_CTL_EVENT(0x2c) + M2M_PCI_PMON_CTL_UMASK(3);    // UNC_M2M_TAG_HIT.NM_DRD_HIT_* events (CLEAN | DIRTY)
        cfg[EventPosition::M2M_CLOCKTICKS] = 0;                                                      // CLOCKTICKS
        cfg[EventPosition::PMM_READ] = M2M_PCI_PMON_CTL_EVENT(0x37) + M2M_PCI_PMON_CTL_UMASK(0x8);  // UNC_M2M_IMC_READS.TO_PMM
        cfg[EventPosition::PMM_WRITE] = M2M_PCI_PMON_CTL_EVENT(0x38) + M2M_PCI_PMON_CTL_UMASK(0x20); // UNC_M2M_IMC_WRITES.TO_PMM
    }
    programM2M(cfg);
}

void ServerUncorePMUs::programM2M(const uint64* M2MCntConfig)
{
    {
        for (auto & pmu : m2mPMUs)
        {
            // std::cout << "programming m2m pmu "<< i++ << std::endl;
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
            PCM::program(pmu, M2MCntConfig, M2MCntConfig + 4, UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void ServerUncorePMUs::programM3UPI(const uint32* M3UPICntConfig)
{
    {
        for (auto& pmu : m3upiPMUs)
        {
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
            PCM::program(pmu, M3UPICntConfig, M3UPICntConfig + 4, UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void ServerUncorePMUs::programHA(const uint32 * config)
{
    for (auto & pmu : haPMUs)
    {
        pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);
        PCM::program(pmu, config, config + 4, UNC_PMON_UNIT_CTL_RSV);
    }
}

uint64 ServerUncorePMUs::getHARequests()
{
    uint64 result = 0;
    for (auto & pmu: haPMUs)
    {
        result += *pmu.counterValue[PCM::EventPosition::REQUESTS_ALL];
    }
    return result;
}

uint64 ServerUncorePMUs::getHALocalRequests()
{
    uint64 result = 0;
    for (auto & pmu: haPMUs)
    {
        result += *pmu.counterValue[PCM::EventPosition::REQUESTS_LOCAL];
    }
    return result;
}

void ServerUncorePMUs::programHA()
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

void ServerUncorePMUs::freezeCounters()
{
    for (auto& pmuVector : allPMUs)
    {
        for (auto& pmu : *pmuVector)
        {
            pmu.freeze((cpu_family_model == PCM::SKX) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}

void ServerUncorePMUs::unfreezeCounters()
{
    for (auto& pmuVector : allPMUs)
    {
        for (auto& pmu : *pmuVector)
        {
            pmu.unfreeze((cpu_family_model == PCM::SKX) ? UNC_PMON_UNIT_CTL_RSV : UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}

uint64 ServerUncorePMUs::getQPIClocks(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS);
}

uint64 ServerUncorePMUs::getQPIL0pTxCycles(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_TxL0P_POWER_CYCLES);
}

uint64 ServerUncorePMUs::getQPIL1Cycles(uint32 port)
{
    return getQPILLCounter(port, ServerUncoreCounterState::EventPosition::xPI_L1_POWER_CYCLES);
}

uint64 ServerUncorePMUs::getDRAMClocks(uint32 channel)
{
    uint64 result = 0;

    if (channel < (uint32)imcPMUs.size())
        result = *(imcPMUs[channel].fixedCounterValue);

    // std::cout << "DEBUG: DRAMClocks on channel " << channel << " = " << result << "\n";
    return result;
}

uint64 ServerUncorePMUs::getHBMClocks(uint32 channel)
{
    uint64 result = 0;

    if (channel < (uint32)edcPMUs.size())
        result = *edcPMUs[channel].fixedCounterValue;

    // std::cout << "DEBUG: HBMClocks on EDC" << channel << " = " << result << "\n";
    return result;
}

uint64 ServerUncorePMUs::getPMUCounter(std::vector<UncorePMU> & pmu, const uint32 id, const uint32 counter)
{
    uint64 result = 0;

    if (id < (uint32)pmu.size() && counter < 4 && pmu[id].counterValue[counter].get() != nullptr)
    {
        result = *(pmu[id].counterValue[counter]);
    }
    else
    {
        //std::cout << "DEBUG: Invalid ServerUncorePMUs::getPMUCounter(" << id << ", " << counter << ") \n";
    }
    // std::cout << "DEBUG: ServerUncorePMUs::getPMUCounter(" << id << ", " << counter << ") = " << result << "\n";
    return result;
}

uint64 ServerUncorePMUs::getHACounter(uint32 id, uint32 counter)
{
    return getPMUCounter(haPMUs, id, counter);
}

uint64 ServerUncorePMUs::getMCCounter(uint32 channel, uint32 counter)
{
    return getPMUCounter(imcPMUs, channel, counter);
}

uint64 ServerUncorePMUs::getEDCCounter(uint32 channel, uint32 counter)
{
    return getPMUCounter(edcPMUs, channel, counter);
}

uint64 ServerUncorePMUs::getM2MCounter(uint32 box, uint32 counter)
{
    return getPMUCounter(m2mPMUs, box, counter);
}

uint64 ServerUncorePMUs::getQPILLCounter(uint32 port, uint32 counter)
{
    return getPMUCounter(xpiPMUs, port, counter);
}

uint64 ServerUncorePMUs::getM3UPICounter(uint32 port, uint32 counter)
{
    // std::cout << "DEBUG: ServerUncorePMUs::getM3UPICounter(" << port << ", " << counter << ") = " << getPMUCounter(m3upiPMUs, port, counter) << "\n";
    return getPMUCounter(m3upiPMUs, port, counter);
}

void ServerUncorePMUs::enableJKTWorkaround(bool enable)
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

void ServerUncorePMUs::initMemTest(ServerUncorePMUs::MemTestParam & param)
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
    const int64 onlineNodes = (int64)readMaxFromSysFS("/sys/devices/system/node/online");
    unsigned long long maxNode = (unsigned long long)(onlineNodes + 1);
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
            for (auto& b : memBuffers)
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
    for (auto& b : memBuffers)
        std::fill(b, b + (memBufferBlockSize / sizeof(uint64)), 0ULL);
}

void ServerUncorePMUs::doMemTest(const ServerUncorePMUs::MemTestParam & param)
{
    const auto & memBufferBlockSize = param.first;
    const auto & memBuffers = param.second;
    // read and write each cache line once
    for (auto& b : memBuffers)
        for (unsigned int i = 0; i < memBufferBlockSize / sizeof(uint64); i += 64 / sizeof(uint64))
        {
            (b[i])++;
        }
}

void ServerUncorePMUs::cleanupMemTest(const ServerUncorePMUs::MemTestParam & param)
{
    const auto & memBufferBlockSize = param.first;
    const auto & memBuffers = param.second;
    for (auto& b : memBuffers)
    {
#if defined(__linux__)
        munmap(b, memBufferBlockSize);
#elif defined(_MSC_VER)
        VirtualFree(b, memBufferBlockSize, MEM_RELEASE);
#elif defined(__FreeBSD__) || defined(__APPLE__)
        (void) b;                  // avoid the unused variable warning
        (void) memBufferBlockSize; // avoid the unused variable warning
#else
#endif
    }
}

uint64 ServerUncorePMUs::computeQPISpeed(const uint32 core_nr, const int cpufamilymodel)
{
    if(qpi_speed.empty())
    {
        PCM * pcm = PCM::getInstance();
        TemporalThreadAffinity aff(core_nr);
        qpi_speed.resize(getNumQPIPorts());

        auto getSpeed = [&] (size_t i) {
           if (PCM::hasUPI(cpufamilymodel) == false && i == 1) return 0ULL; // link 1 should have the same speed as link 0, skip it
           uint64 result = 0;
           if (PCM::hasUPI(cpufamilymodel) == false && i < XPIRegisterLocation.size())
           {
               PciHandleType reg(groupnr,UPIbus, XPIRegisterLocation[i].first, QPI_PORT0_MISC_REGISTER_FUNC_ADDR);
               uint32 value = 0;
               reg.read32(QPI_RATE_STATUS_ADDR, &value);
               value &= 7; // extract lower 3 bits
               if(value) result = static_cast<uint64>((4000000000ULL + ((uint64)value)*800000000ULL)*2ULL);
           }
           std::unordered_map<uint32, size_t> UPISpeedMap{};
           std::pair<uint32, uint32> regBits{};
           switch (cpufamilymodel)
           {
           case PCM::GNR:
           case PCM::SRF:
               UPISpeedMap = {
                   { 0,  2500},
                   { 1, 12800},
                   { 2, 14400},
                   { 3, 16000},
                   { 8, 20000},
                   { 9, 24000}
               };
               regBits = std::make_pair(5, 8);
               break;
           case PCM::SPR:
               UPISpeedMap = {
                   {0,  2500},
                   {1, 12800},
                   {2, 14400},
                   {3, 16000},
                   {4, 20000}
               };
               regBits = std::make_pair(0, 2);
               break;
           }
           if (UPISpeedMap.empty() == false && i < XPIRegisterLocation.size())
           {
               const auto UPI_SPEED_REGISTER_FUNC_ADDR = 2;
               const auto UPI_SPEED_REGISTER_OFFSET = 0x2e0;
               PciHandleType reg(groupnr, UPIbus, XPIRegisterLocation[i].first, UPI_SPEED_REGISTER_FUNC_ADDR);
               uint32 value = 0;
               if (reg.read32(UPI_SPEED_REGISTER_OFFSET, &value) == sizeof(uint32))
               {
                   const size_t speedMT = UPISpeedMap[extract_bits_ui(value, regBits.first, regBits.second)];
                   if (false)
                   {
                       std::cerr << "speedMT: " << speedMT << "\n";
                   }
                   result = speedMT * 1000000ULL * pcm->getBytesPerLinkTransfer();
               }
           }
           if(result == 0ULL)
           {
               if (PCM::hasUPI(cpufamilymodel) == false)
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

               result = (uint64(double(endClocks - startClocks) * PCM::getBytesPerLinkCycle(cpufamilymodel) * double(timerGranularity) / double(endTSC - startTSC)));
               if(cpufamilymodel == PCM::HASWELLX || cpufamilymodel == PCM::BDX) /* BDX_DE does not have QPI. */{
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
             qpi_speed[i] = (PCM::hasUPI(cpufamilymodel) == false && i==1)? qpi_speed[0] : getSpeedsAsync[i].get(); // link 1 does not have own speed register, it runs with the speed of link 0
         }
         if (PCM::hasUPI(cpufamilymodel))
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

void ServerUncorePMUs::reportQPISpeed() const
{
    PCM * m = PCM::getInstance();
    std::cerr.precision(1);
    std::cerr << std::fixed;
    for (uint32 i = 0; i < (uint32)qpi_speed.size(); ++i)
        std::cerr << "Max " << m->xPI() << " link " << i << " speed: " << qpi_speed[i] / (1e9) << " GBytes/second (" << qpi_speed[i] / (1e9 * m->getBytesPerLinkTransfer()) << " GT/second)\n";
}

uint64 PCM::CX_MSR_PMON_CTRY(uint32 Cbo, uint32 Ctr) const
{
    switch (cpu_family_model)
    {
    case JAKETOWN:
    case IVYTOWN:
        return JKT_C0_MSR_PMON_CTR0 + (JKTIVT_CBO_MSR_STEP * Cbo) + Ctr;

    case HASWELLX:
    case BDX_DE:
    case BDX:
    case SKX:
        return HSX_C0_MSR_PMON_CTR0 + (HSX_CBO_MSR_STEP * Cbo) + Ctr;

    case ICX:
    case SNOWRIDGE:
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_CTR0_OFFSET + Ctr;

    case SPR:
    case EMR:
    case GNR:
    case GRR:
    case SRF:
        return SPR_CHA0_MSR_PMON_CTR0 + SPR_CHA_MSR_STEP * Cbo + Ctr;

    default:
        return 0;
    }
}

uint64 PCM::CX_MSR_PMON_BOX_FILTER(uint32 Cbo) const
{
    switch (cpu_family_model)
    {
    case JAKETOWN:
    case IVYTOWN:
        return JKT_C0_MSR_PMON_BOX_FILTER + (JKTIVT_CBO_MSR_STEP * Cbo);

    case HASWELLX:
    case BDX_DE:
    case BDX:
    case SKX:
        return HSX_C0_MSR_PMON_BOX_FILTER + (HSX_CBO_MSR_STEP * Cbo);

    case KNL:
        return KNL_CHA0_MSR_PMON_BOX_CTL + (KNL_CHA_MSR_STEP * Cbo);

    case ICX:
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_BOX_FILTER_OFFSET;

    case SPR:
    case EMR:
    case GNR:
    case GRR:
    case SRF:
        return SPR_CHA0_MSR_PMON_BOX_FILTER + SPR_CHA_MSR_STEP * Cbo;

    default:
        return 0;
    }
}

uint64 PCM::CX_MSR_PMON_BOX_FILTER1(uint32 Cbo) const
{
    switch (cpu_family_model) {
    case IVYTOWN:
        return IVT_C0_MSR_PMON_BOX_FILTER1 + (JKTIVT_CBO_MSR_STEP * Cbo);

    case HASWELLX:
    case BDX_DE:
    case BDX:
    case SKX:
        return HSX_C0_MSR_PMON_BOX_FILTER1 + (HSX_CBO_MSR_STEP * Cbo);

    default:
        return 0;
    }
}
uint64 PCM::CX_MSR_PMON_CTLY(uint32 Cbo, uint32 Ctl) const
{
    switch (cpu_family_model) {
    case JAKETOWN:
    case IVYTOWN:
        return JKT_C0_MSR_PMON_CTL0 + (JKTIVT_CBO_MSR_STEP * Cbo) + Ctl;

    case HASWELLX:
    case BDX_DE:
    case BDX:
    case SKX:
        return HSX_C0_MSR_PMON_CTL0 + (HSX_CBO_MSR_STEP * Cbo) + Ctl;

    case ICX:
    case SNOWRIDGE:
        return CX_MSR_PMON_BOX_CTL(Cbo) + SERVER_CHA_MSR_PMON_CTL0_OFFSET + Ctl;

    case SPR:
    case EMR:
    case GNR:
    case GRR:
    case SRF:
        return SPR_CHA0_MSR_PMON_CTL0 + SPR_CHA_MSR_STEP * Cbo + Ctl;

    default:
        return 0;
    }
}

uint64 PCM::CX_MSR_PMON_BOX_CTL(uint32 Cbo) const
{
    switch (cpu_family_model) {
    case JAKETOWN:
    case IVYTOWN:
        return JKT_C0_MSR_PMON_BOX_CTL + (JKTIVT_CBO_MSR_STEP * Cbo);

    case HASWELLX:
    case BDX_DE:
    case BDX:
    case SKX:
        return HSX_C0_MSR_PMON_BOX_CTL + (HSX_CBO_MSR_STEP * Cbo);

    case KNL:
        return KNL_CHA0_MSR_PMON_BOX_CTRL + (KNL_CHA_MSR_STEP * Cbo);

    case ICX:
        return ICX_CHA_MSR_PMON_BOX_CTL[Cbo];

    case SPR:
    case EMR:
    case GNR:
    case GRR:
    case SRF:
        return SPR_CHA0_MSR_PMON_BOX_CTRL + SPR_CHA_MSR_STEP * Cbo;

    case SNOWRIDGE:
        return SNR_CHA_MSR_PMON_BOX_CTL[Cbo];

    default:
        return 0;
    }
}


// Return the first device found with specific vendor/device IDs
PciHandleType * getDeviceHandle(uint32 vendorId, uint32 deviceId)
{
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
    {
        for (uint32 bus = (uint32)mcfg[s].startBusNumber; bus <= (uint32)mcfg[s].endBusNumber; ++bus)
        {
            for (uint32 device = 0; device < 0x20; ++device)
            {
                for (uint32 function = 0; function < 0x8; ++function)
                {
                    if (PciHandleType::exists(mcfg[s].PCISegmentGroupNumber, bus, device, function))
                    {
                        PciHandleType * h = new PciHandleType(mcfg[s].PCISegmentGroupNumber, bus, device, function);
                        uint32 value;
                        h->read32(0, &value);
                        const uint32 vid = value & 0xffff;
                        const uint32 did = (value >> 16) & 0xffff;
                        if (vid == vendorId && did == deviceId)
                            return h;
                        deleteAndNullify(h);
                    }
                }
            }
        }
    }
    return NULL;
}

inline uint32 weight32(uint32 n)
{
    uint32 count = 0;
    while (n)
    {
        n &= (n - 1);
        count++;
    }

    return count;
}

uint32 PCM::getMaxNumOfCBoxesInternal() const
{
    static int num = -1;
    if (num >= 0)
    {
        return (uint32)num;
    }
    const auto refCore = socketRefCore[0];
    uint64 val = 0;
    switch (cpu_family_model)
    {
    case GRR:
    case GNR:
    case SRF:
        {
            const auto MSR_PMON_NUMBER_CBOS = 0x3fed;
            MSR[refCore]->read(MSR_PMON_NUMBER_CBOS, &val);
            num = (uint32)(val & 511);
        }
        break;
    case SPR:
    case EMR:
        try {
            PciHandleType * h = getDeviceHandle(PCM_INTEL_PCI_VENDOR_ID, 0x325b);
            if (h)
            {
                uint32 value;
                h->read32(0x9c, &value);
                num = (uint32)weight32(value);
                h->read32(0xa0, &value);
                num += (uint32)weight32(value);
                deleteAndNullify(h);
            }
            else
            {
                num = 0;
            }
        }
        catch (std::exception& e)
        {
            std::cerr << "Warning: reading the number of CHA from PCICFG register has failed: " << e.what() << "\n";
        }
        break;
    case KNL:
    case SKX:
    case ICX:
        {
            /*
             *  on KNL two physical cores share CHA.
             *  The number of CHAs in the processor is stored in bits 5:0
             *  of NCUPMONConfig [0x702] MSR.
             */
            const auto NCUPMONConfig = 0x702;
            MSR[refCore]->read(NCUPMONConfig, &val);
        }
        num = (uint32)(val & 63);
        break;
    case SNOWRIDGE:
        num = (uint32)num_phys_cores_per_socket / 4;
        break;
    default:
        /*
         *  on other supported CPUs there is one CBox per physical core.  This calculation will get us
         *  the number of physical cores per socket which is the expected
         *  value to be returned.
         */
        num = (uint32)num_phys_cores_per_socket;
    }
#ifdef PCM_USE_PERF
    if (num <= 0)
    {
        num = (uint32)enumeratePerfPMUs("cbox", 100).size();
    }
    if (num <= 0)
    {
        num = (uint32)enumeratePerfPMUs("cha", 100).size();
    }
#endif
    assert(num >= 0);
    return (uint32)num;
}

uint32 PCM::getMaxNumOfIIOStacks() const
{
    if (iioPMUs.size() > 0)
    {
        assert(irpPMUs.size());
        assert(iioPMUs[0].size() == irpPMUs[0].size());
        return (uint32)iioPMUs[0].size();
    }
    return 0;
}

void PCM::programCboOpcodeFilter(const uint32 opc0, UncorePMU & pmu, const uint32 nc_, const uint32 opc1, const uint32 loc, const uint32 rem)
{
    if (JAKETOWN == cpu_family_model)
    {
        *pmu.filter[0] = JKT_CBO_MSR_PMON_BOX_FILTER_OPC(opc0);

    } else if (IVYTOWN == cpu_family_model || HASWELLX == cpu_family_model || BDX_DE == cpu_family_model || BDX == cpu_family_model)
    {
        *pmu.filter[1] = IVTHSX_CBO_MSR_PMON_BOX_FILTER1_OPC(opc0);
    } else if (SKX == cpu_family_model)
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
        std::cerr << "ERROR: programCboOpcodeFilter function is not implemented for cpu family " << cpu_family << " model " << cpu_model_private << std::endl;
        throw std::exception();
    }
}

void PCM::programIIOCounters(uint64 rawEvents[4], int IIOStack)
{
    std::vector<int32> IIO_units;
    if (IIOStack == -1)
    {
        int stacks_count;
        switch (getCPUFamilyModel())
        {
        case PCM::GRR:
            stacks_count = GRR_M2IOSF_NUM;
            break;
        case PCM::GNR:
        case PCM::SRF:
            stacks_count = BHS_M2IOSF_NUM;
            break;
        case PCM::SPR:
        case PCM::EMR:
            stacks_count = SPR_M2IOSF_NUM;
            break;
        case PCM::ICX:
            stacks_count = ICX_IIO_STACK_COUNT;
            break;
        case PCM::SNOWRIDGE:
            stacks_count = SNR_IIO_STACK_COUNT;
            break;
        case PCM::BDX:
            stacks_count = BDX_IIO_STACK_COUNT;
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

void PCM::programIRPCounters(uint64 rawEvents[4], int IIOStack)
{
    // std::cerr << "PCM::programIRPCounters IRP PMU unit (stack) " << IIOStack << " getMaxNumOfIIOStacks(): " << getMaxNumOfIIOStacks()<< "\n";
    std::vector<int32> IIO_units;
    if (IIOStack == -1)
    {
        for (uint32 stack = 0; stack < getMaxNumOfIIOStacks(); ++stack)
        {
            IIO_units.push_back(stack);
        }
    }
    else
    {
        IIO_units.push_back(IIOStack);
    }

    for (int32 i = 0; (i < num_sockets) && MSR.size() && irpPMUs.size(); ++i)
    {
        uint32 refCore = socketRefCore[i];
        TemporalThreadAffinity tempThreadAffinity(refCore); // speedup trick for Linux

        for (const auto& unit : IIO_units)
        {
            if (irpPMUs[i].count(unit) == 0)
            {
                std::cerr << "IRP PMU unit (stack) " << unit << " is not found \n";
                continue;
            }
            // std::cerr << "Programming IRP PMU unit (stack) " << unit << " on socket " << i << " \n";
            auto& pmu = irpPMUs[i][unit];
            pmu.initFreeze(UNC_PMON_UNIT_CTL_RSV);

            program(pmu, &rawEvents[0], &rawEvents[2], UNC_PMON_UNIT_CTL_RSV);
        }
    }
}

void PCM::programPCIeEventGroup(eventGroup_t &eventGroup)
{
    assert(eventGroup.size() > 0);
    uint64 events[4] = {0};
    uint64 umask[4] = {0};

    switch (cpu_family_model)
    {
        case PCM::GNR:
        case PCM::GRR:
        case PCM::SRF:
        case PCM::SPR:
        case PCM::EMR:
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
    programUncorePMUs(CBO_PMU_ID, [&](UncorePMU & pmu)
        {
            pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

            if (    ICX != cpu_family_model
                &&  SNOWRIDGE != cpu_family_model
                &&  SPR != cpu_family_model
                &&  EMR != cpu_family_model
                &&  GNR != cpu_family_model
                &&  SRF != cpu_family_model
                &&  GRR != cpu_family_model
                )
            {
                programCboOpcodeFilter(opCode, pmu, nc_, 0, loc, rem);
            }

            if ((HASWELLX == cpu_family_model || BDX_DE == cpu_family_model || BDX == cpu_family_model || SKX == cpu_family_model) && llc_lookup_tid_filter != 0)
                *pmu.filter[0] = llc_lookup_tid_filter;

            PCM::program(pmu, events, events + ServerUncoreCounterState::maxCounters, UNC_PMON_UNIT_CTL_FRZ_EN);

            for (int c = 0; c < ServerUncoreCounterState::maxCounters && size_t(c) < pmu.size(); ++c)
            {
                *pmu.counterValue[c] = 0;
            }
        }
    );
}

void PCM::programCboRaw(const uint64* events, const uint64 filter0, const uint64 filter1)
{
    programUncorePMUs(CBO_PMU_ID, [&](UncorePMU& pmu)
        {
            pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

            if (pmu.filter[0].get())
            {
                *pmu.filter[0] = filter0;
            }

            if (pmu.filter[1].get())
            {
                *pmu.filter[1] = filter1;
            }

            PCM::program(pmu, events, events + 4, UNC_PMON_UNIT_CTL_FRZ_EN);

            for (int c = 0; c < ServerUncoreCounterState::maxCounters && size_t(c) < pmu.size(); ++c)
            {
                *pmu.counterValue[c] = 0;
            }
        }
    );
}

void PCM::programMDF(const uint64* events)
{
    programUncorePMUs(MDF_PMU_ID, [&](UncorePMU& pmu)
    {
        pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        PCM::program(pmu, events, events + 4, UNC_PMON_UNIT_CTL_FRZ_EN);
    });
}

void PCM::programUBOX(const uint64* events)
{
    programUncorePMUs(UBOX_PMU_ID, [&events](UncorePMU& pmu)
    {
        pmu.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);

        *pmu.fixedCounterControl = UCLK_FIXED_CTL_EN;

        if (events)
        {
            PCM::program(pmu, events, events + 2, 0);
        }
    });
}

void PCM::controlQATTelemetry(uint32 dev, uint32 operation)
{
    if (getNumOfIDXAccelDevs(IDX_QAT) == 0 || dev >= getNumOfIDXAccelDevs(IDX_QAT) || operation >= PCM::QAT_TLM_MAX)
        return;

    auto &gControl_reg = idxPMUs[IDX_QAT][dev].generalControl;
    switch (operation)
    {
        case PCM::QAT_TLM_START:
        case PCM::QAT_TLM_STOP:
        case PCM::QAT_TLM_REFRESH:
            *gControl_reg = operation;
            break;
        default:
            break;
    }
}

void PCM::programCXLCM(const uint64* events)
{
    for (auto & sPMUs : cxlPMUs)
    {
        for (auto& pmus : sPMUs)
        {
            pmus.first.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
            assert(pmus.first.size() == 8);
            PCM::program(pmus.first, events, events + 8, UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}

void PCM::programCXLDP(const uint64* events)
{
    for (auto& sPMUs : cxlPMUs)
    {
        for (auto& pmus : sPMUs)
        {
            pmus.second.initFreeze(UNC_PMON_UNIT_CTL_FRZ_EN);
            assert(pmus.second.size() == 4);
            PCM::program(pmus.second, events, events + 4, UNC_PMON_UNIT_CTL_FRZ_EN);
        }
    }
}
void PCM::programCXLCM()
{
    uint64 CXLCMevents[8] = { 0,0,0,0,0,0,0,0 };

    CXLCMevents[EventPosition::CXL_RxC_MEM] = UNC_PMON_CTL_EVENT(0x41) + UNC_PMON_CTL_UMASK(0x10); // CXLCM_RxC_PACK_BUF_INSERTS.MEM_DATA
    CXLCMevents[EventPosition::CXL_TxC_MEM] = UNC_PMON_CTL_EVENT(0x02) + UNC_PMON_CTL_UMASK(0x10); // CXLCM_TxC_PACK_BUF_INSERTS.MEM_DATA
    CXLCMevents[EventPosition::CXL_RxC_CACHE] = UNC_PMON_CTL_EVENT(0x41) + UNC_PMON_CTL_UMASK(0x04);// CXLCM_RxC_PACK_BUF_INSERTS.CACHE_DATA
    CXLCMevents[EventPosition::CXL_TxC_CACHE] = UNC_PMON_CTL_EVENT(0x02) + UNC_PMON_CTL_UMASK(0x04);// CXLCM_TxC_PACK_BUF_INSERTS.CACHE_DATA

    programCXLCM(CXLCMevents);
}


void PCM::programCXLDP()
{
    uint64 events[4] = { 0,0,0,0 };

    events[EventPosition::CXL_TxC_MEM] = UNC_PMON_CTL_EVENT(0x02) + UNC_PMON_CTL_UMASK(0x20); // UNC_CXLDP_TxC_AGF_INSERTS.M2S_DATA

    programCXLDP(events);
}

void PCM::programIDXAccelCounters(uint32 accel, std::vector<uint64_t> &events, std::vector<uint32_t> &filters_wq, std::vector<uint32_t> &filters_eng, std::vector<uint32_t> &filters_tc, std::vector<uint32_t> &filters_pgsz, std::vector<uint32_t> &filters_xfersz)
{
    uint32 maxCTR = getMaxNumOfIDXAccelCtrs(accel); //limit the number of physical counter to use

    if (events.size() == 0 || accel >= IDX_MAX || getNumOfIDXAccelDevs(accel) == 0)
        return; //invalid input parameter or IDX accel dev NOT exist

    if (events.size() < maxCTR)
        maxCTR = events.size();

    for (auto & pmu : idxPMUs[accel])
    {
        pmu.initFreeze();

        for (uint32 i = 0; i < maxCTR; i++)
        {
            auto &ctrl_reg = pmu.counterControl[i];
            auto &filter_wq_reg = pmu.counterFilterWQ[i];
            auto &filter_eng_reg = pmu.counterFilterENG[i];
            auto &filter_tc_reg = pmu.counterFilterTC[i];
            auto &filter_pgsz_reg = pmu.counterFilterPGSZ[i];
            auto &filter_xfersz_reg = pmu.counterFilterXFERSZ[i];

            if (pmu.getPERFMode() == false)
            {
                //disable the counter before raw program in PMU direct mode.
                *ctrl_reg = 0x0;
            }

            *filter_wq_reg = extract_bits_ui(filters_wq.at(i), 0, 15);            
            *filter_eng_reg = extract_bits_ui(filters_eng.at(i), 0, 15);
            *filter_tc_reg = extract_bits_ui(filters_tc.at(i), 0, 7);
            *filter_pgsz_reg = extract_bits_ui(filters_pgsz.at(i), 0, 7);
            *filter_xfersz_reg = extract_bits_ui(filters_xfersz.at(i), 0, 7);

            if (pmu.getPERFMode() == false)
            {
                *ctrl_reg = events.at(i);
            }
            else{
                switch (accel)
                {
                    case IDX_IAA:
                    case IDX_DSA:
                        //translate the event config from raw to perf format in Linux perf mode.
                        //please reference the bitmap from DSA EAS spec and linux idxd driver perfmon interface.
                        *ctrl_reg = ((extract_bits(events.at(i), 8, 11)) | ((extract_bits(events.at(i), 32, 59)) << 4));
                        break;
                    case IDX_QAT://QAT NOT support perf mode
                        break;
                    default:
                        break;
                }
            }
        }

        pmu.resetUnfreeze();
    }
}

IDXCounterState PCM::getIDXAccelCounterState(uint32 accel, uint32 dev, uint32 counter_id)
{
    IDXCounterState result;

    if (accel >= IDX_MAX || dev >= getNumOfIDXAccelDevs(accel) || counter_id >= getMaxNumOfIDXAccelCtrs(accel))
        return result;

    result.data = *idxPMUs[accel][dev].counterValue[counter_id];
    return result;
}

uint32 PCM::getNumOfIDXAccelDevs(int accel) const
{
    if (accel >= IDX_MAX)
        return 0;

    return idxPMUs[accel].size();
}

uint32 PCM::getMaxNumOfIDXAccelCtrs(int accel) const
{
    uint32 retval = 0;

    if (supportIDXAccelDev() == true)
    {
        if (accel == IDX_IAA || accel == IDX_DSA)
        {
            retval = SPR_IDX_ACCEL_COUNTER_MAX_NUM;
        }
        else if(accel == IDX_QAT)
        {
            retval = SPR_QAT_ACCEL_COUNTER_MAX_NUM;
        }
    }

    return retval;
}

uint32 PCM::getNumaNodeOfIDXAccelDev(uint32 accel, uint32 dev) const
{
    uint32 numa_node = 0xff;

    if (accel >= IDX_MAX || dev >= getNumOfIDXAccelDevs(accel))
        return numa_node;

    numa_node = idxPMUs[accel][dev].getNumaNode();
    return numa_node;
}

uint32 PCM::getCPUSocketIdOfIDXAccelDev(uint32 accel, uint32 dev) const
{
    uint32 socketid = 0xff;

    if (accel >= IDX_MAX || dev >= getNumOfIDXAccelDevs(accel))
        return socketid;

    socketid = idxPMUs[accel][dev].getSocketId();
    return socketid;
}

bool PCM::supportIDXAccelDev() const
{
    bool retval = false;

    switch (this->getCPUFamilyModel())
    {
        case PCM::SPR:
        case PCM::EMR:
        case PCM::GNR:
        case PCM::SRF:
        case PCM::GNR_D:
            retval = true;
            break;

        default:
            retval = false;
            break;
    }

    return retval;
}

uint64 PCM::getUncoreCounterState(const int pmu_id, const size_t socket, const uint32 ctr) const
{
    uint64 result = 0;

    if (socket < uncorePMUs.size() && ctr < ServerUncoreCounterState::maxCounters)
    {
        for (size_t die = 0; die < uncorePMUs[socket].size(); ++die)
        {
            TemporalThreadAffinity tempThreadAffinity(socketRefCore[socket]); // speedup trick for Linux
            const auto pmusIter = uncorePMUs[socket][die].find(pmu_id);
            if (pmusIter != uncorePMUs[socket][die].end())
            {
                for (const auto& pmu : pmusIter->second)
                {
                    if (pmu.get())
                    {
                        result += *(pmu->counterValue[ctr]);
                    }
                }
            }
        }
    }
    return result;
}

uint64 PCM::getUncoreClocks(const uint32 socket_id)
{
    uint64 result = 0;
    if (socket_id < uncorePMUs.size())
    {
        for (auto& d : uncorePMUs[socket_id])
        {
            const auto iter = d.find(UBOX_PMU_ID);
            if (iter != d.end())
            {
                for (auto& pmu : iter->second)
                {
                    if (pmu.get())
                    {
                        result += *pmu->fixedCounterValue;
                    }
                }
            }
        }
    }
    return result;
}

PCIeCounterState PCM::getPCIeCounterState(const uint32 socket_, const uint32 ctr_)
{
    PCIeCounterState result;
    result.data = getUncoreCounterState(CBO_PMU_ID, socket_, ctr_);
    return result;
}

uint64 PCM::getPCIeCounterData(const uint32 socket_, const uint32 ctr_)
{
    return getUncoreCounterState(CBO_PMU_ID, socket_, ctr_);
}

void PCM::initLLCReadMissLatencyEvents(uint64 * events, uint32 & opCode)
{
    if (LLCReadMissLatencyMetricsAvailable() == false)
    {
        return;
    }
    uint64 umask = 3ULL; // MISS_OPCODE
    switch (cpu_family_model)
    {
        case ICX:
        case SPR:
        case SNOWRIDGE:
            umask = 1ULL;
            break;
        case SKX:
            umask = (uint64)(SKX_CHA_TOR_INSERTS_UMASK_IRQ(1)) + (uint64)(SKX_CHA_TOR_INSERTS_UMASK_MISS(1));
            break;
    }

    uint64 umask_ext = 0;
    switch (cpu_family_model)
    {
        case ICX:
            umask_ext = 0xC817FE;
            break;
        case SPR:
            umask_ext = 0x00C817FE;
            break;
        case SNOWRIDGE:
            umask_ext = 0xC827FE;
            break;
    }

    const uint64 all_umasks = CBO_MSR_PMON_CTL_UMASK(umask) + UNC_PMON_CTL_UMASK_EXT(umask_ext);
    events[EventPosition::TOR_OCCUPANCY] = CBO_MSR_PMON_CTL_EVENT(0x36) + all_umasks; // TOR_OCCUPANCY (must be on counter 0)
    events[EventPosition::TOR_INSERTS] = CBO_MSR_PMON_CTL_EVENT(0x35) + all_umasks; // TOR_INSERTS

    opCode = (SKX == cpu_family_model) ? 0x202 : 0x182;
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
    deleteAndNullify(UpdateThread);
    deleteAndNullify(raw_counter);
}


UncorePMU::UncorePMU(const HWRegisterPtr& unitControl_,
    const HWRegisterPtr& counterControl0,
    const HWRegisterPtr& counterControl1,
    const HWRegisterPtr& counterControl2,
    const HWRegisterPtr& counterControl3,
    const HWRegisterPtr& counterValue0,
    const HWRegisterPtr& counterValue1,
    const HWRegisterPtr& counterValue2,
    const HWRegisterPtr& counterValue3,
    const HWRegisterPtr& fixedCounterControl_,
    const HWRegisterPtr& fixedCounterValue_,
    const HWRegisterPtr& filter0,
    const HWRegisterPtr& filter1
) :
    cpu_family_model_(0),
    unitControl(unitControl_),
    counterControl{ counterControl0, counterControl1, counterControl2, counterControl3 },
    counterValue{ counterValue0, counterValue1, counterValue2, counterValue3 },
    fixedCounterControl(fixedCounterControl_),
    fixedCounterValue(fixedCounterValue_),
    filter{ filter0 , filter1 }
{
    assert(counterControl.size() == counterValue.size());
}

UncorePMU::UncorePMU(const HWRegisterPtr& unitControl_,
    const std::vector<HWRegisterPtr>& counterControl_,
    const std::vector<HWRegisterPtr>& counterValue_,
    const HWRegisterPtr& fixedCounterControl_,
    const HWRegisterPtr& fixedCounterValue_,
    const HWRegisterPtr& filter0,
    const HWRegisterPtr& filter1
):
    cpu_family_model_(0),
    unitControl(unitControl_),
    counterControl{counterControl_},
    counterValue{counterValue_},
    fixedCounterControl(fixedCounterControl_),
    fixedCounterValue(fixedCounterValue_),
    filter{ filter0 , filter1 }
{
    assert(counterControl.size() == counterValue.size());
}

uint32 UncorePMU::getCPUFamilyModel()
{
    if (cpu_family_model_ == 0)
    {
        cpu_family_model_ = PCM::getInstance()->getCPUFamilyModel();
    }
    return cpu_family_model_;
}

void UncorePMU::cleanup()
{
    for (auto& cc: counterControl)
    {
        if (cc.get()) *cc = 0;
    }
    if (unitControl.get()) *unitControl = 0;
    if (fixedCounterControl.get()) *fixedCounterControl = 0;
}

void UncorePMU::freeze(const uint32 extra)
{
    switch (getCPUFamilyModel())
    {
    case PCM::SPR:
    case PCM::EMR:
    case PCM::GNR:
    case PCM::GRR:
    case PCM::SRF:
        *unitControl = SPR_UNC_PMON_UNIT_CTL_FRZ;
        break;
    default:
        *unitControl = extra + UNC_PMON_UNIT_CTL_FRZ;
    }
}

void UncorePMU::unfreeze(const uint32 extra)
{
    switch (getCPUFamilyModel())
    {
    case PCM::SPR:
    case PCM::EMR:
    case PCM::GNR:
    case PCM::GRR:
    case PCM::SRF:
        *unitControl = 0;
        break;
    default:
        *unitControl = extra;
    }
}

bool UncorePMU::initFreeze(const uint32 extra, const char* xPICheckMsg)
{
    if (unitControl.get() == nullptr)
    {
        return true; // this PMU does not have unit control register => no op
    }

    switch (getCPUFamilyModel())
    {
        case PCM::SPR:
        case PCM::EMR:
        case PCM::GNR:
        case PCM::GRR:
        case PCM::SRF:
            *unitControl = SPR_UNC_PMON_UNIT_CTL_FRZ; // freeze
            *unitControl = SPR_UNC_PMON_UNIT_CTL_FRZ + SPR_UNC_PMON_UNIT_CTL_RST_CONTROL; // freeze and reset control registers
            return true;
    }
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
        std::cerr << "ERROR: PMU counter programming seems not to work. PMON_BOX_CTL=0x" << std::hex << val << " needs to be =0x" << (UNC_PMON_UNIT_CTL_FRZ_EN + UNC_PMON_UNIT_CTL_FRZ) << std::dec << "\n";
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
    switch (getCPUFamilyModel())
    {
    case PCM::SPR:
    case PCM::EMR:
    case PCM::GNR:
    case PCM::GRR:
    case PCM::SRF:
        *unitControl = SPR_UNC_PMON_UNIT_CTL_FRZ + SPR_UNC_PMON_UNIT_CTL_RST_COUNTERS; // freeze and reset counter registers
        *unitControl = 0; // unfreeze
        return;
    }
    // reset counter values
    *unitControl = extra + UNC_PMON_UNIT_CTL_FRZ + UNC_PMON_UNIT_CTL_RST_COUNTERS;

    // unfreeze counters
    *unitControl = extra;
}

IDX_PMU::IDX_PMU(const bool perfMode_,
        const uint32 numaNode_,
        const uint32 socketId_,
        const HWRegisterPtr& resetControl_,
        const HWRegisterPtr& freezeControl_,
        const HWRegisterPtr& generalControl_,
        const std::vector<HWRegisterPtr> & counterControl,
        const std::vector<HWRegisterPtr> & counterValue,
        const std::vector<HWRegisterPtr> & counterFilterWQ,
        const std::vector<HWRegisterPtr> & counterFilterENG,
        const std::vector<HWRegisterPtr> & counterFilterTC,
        const std::vector<HWRegisterPtr> & counterFilterPGSZ,
        const std::vector<HWRegisterPtr> & counterFilterXFERSZ
    ) : 
    cpu_family_model_(0),
    perf_mode_(perfMode_),
    numa_node_(numaNode_),
    socket_id_(socketId_),
    resetControl(resetControl_),
    freezeControl(freezeControl_),
    generalControl(generalControl_),
    counterControl{counterControl},
    counterValue{counterValue},
    counterFilterWQ{counterFilterWQ},
    counterFilterENG{counterFilterENG},
    counterFilterTC{counterFilterTC},
    counterFilterPGSZ{counterFilterPGSZ},
    counterFilterXFERSZ{counterFilterXFERSZ}
{
    assert(counterControl.size() == counterValue.size());
}

uint32 IDX_PMU::getCPUFamilyModel()
{
    if (cpu_family_model_ == 0)
    {
        cpu_family_model_ = PCM::getInstance()->getCPUFamilyModel();
    }

    return cpu_family_model_;
}

void IDX_PMU::cleanup()
{
    for (auto& cc: counterControl)
    {
        if (cc.get())
        {
            *cc = 0;
        }
    }

    if (resetControl.get())
    {
        *resetControl = 0x3;
    }

    if (generalControl.get())
    {
        *generalControl = 0x0;
    }
    //std::cout << "IDX_PMU::cleanup \n";
}

void IDX_PMU::freeze()
{
    *freezeControl = 0xFFFFFFFF;
}

void IDX_PMU::unfreeze()
{
    *freezeControl = 0x0;
}

bool IDX_PMU::initFreeze()
{
    if (resetControl.get() == nullptr || freezeControl.get() == nullptr)
    {
        return true; // does not have reset/freeze control register => no op
    }

    *resetControl = 0x2; // reset counter
    freeze(); // freeze counter
    return true;
}

void IDX_PMU::resetUnfreeze()
{
    unfreeze(); // unfreeze counter
}

bool IDX_PMU::getPERFMode()
{
    return perf_mode_;
}

uint32 IDX_PMU::getNumaNode() const
{
    return numa_node_;
}

uint32 IDX_PMU::getSocketId() const
{
    return socket_id_;
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
    switch (this->getCPUFamilyModel())
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
        std::cerr << "INFO: Monitored accesses include demand + L2 cache prefetcher, code read and RFO.\n";
        // OCR.READS_TO_CORE.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x0104000477;
        // OCR.READS_TO_CORE.REMOTE_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x0730000477;
        break;
    case PCM::SPR:
    case PCM::EMR:
    case PCM::GNR:
        std::cout << "INFO: Monitored accesses include demand + L2 cache prefetcher, code read and RFO.\n";
         // OCR.READS_TO_CORE.LOCAL_DRAM
        conf.OffcoreResponseMsrValue[0] = 0x104004477;
         // OCR.READS_TO_CORE.REMOTE_DRAM and OCR.READS_TO_CORE.SNC_DRAM
        conf.OffcoreResponseMsrValue[1] = 0x730004477 | 0x708004477;
       break;
    default:
        throw UnsupportedProcessorException();
    }
}

} // namespace pcm
