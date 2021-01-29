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
//            Thomas Willhalm

#ifndef CPUCOUNTERS_HEADER
#define CPUCOUNTERS_HEADER

/*!     \file cpucounters.h
        \brief Main CPU counters header

        Include this header file if you want to access CPU counters (core and uncore - including memory controller chips and QPI)
*/

#define PCM_VERSION " ($Format:%ci ID=%h$)"

#ifndef PCM_API
#define PCM_API
#endif

#undef PCM_HA_REQUESTS_READS_ONLY

#include "types.h"
#include "msr.h"
#include "pci.h"
#include "client_bw.h"
#include "width_extender.h"
#include "exceptions/unsupported_processor_exception.hpp"

#include <vector>
#include <array>
#include <limits>
#include <string>
#include <memory>
#include <map>
#include <string.h>

#ifdef PCM_USE_PERF
#include <linux/perf_event.h>
#include <errno.h>
#define PCM_PERF_COUNT_HW_REF_CPU_CYCLES (9)
#endif

#ifndef _MSC_VER
#define NOMINMAX
#include <semaphore.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#ifdef _MSC_VER
#if _MSC_VER>= 1600
#include <intrin.h>
#endif
#endif

#include "resctrl.h"

namespace pcm {

#ifdef _MSC_VER
void PCM_API restrictDriverAccess(LPCWSTR path);
#endif

class SystemCounterState;
class SocketCounterState;
class CoreCounterState;
class BasicCounterState;
class ServerUncoreCounterState;
class PCM;
class CoreTaskQueue;
class SystemRoot;

/*
        CPU performance monitoring routines

        A set of performance monitoring routines for recent Intel CPUs
*/

struct PCM_API TopologyEntry // decribes a core
{
    int32 os_id;
    int32 thread_id;
    int32 core_id;
    int32 tile_id; // tile is a constalation of 1 or more cores sharing salem L2 cache. Unique for entire system
    int32 socket;

    TopologyEntry() : os_id(-1), thread_id (-1), core_id(-1), tile_id(-1), socket(-1) { }
};

class HWRegister
{
public:
    virtual void operator = (uint64 val) = 0; // write operation
    virtual operator uint64 () = 0; //read operation
    virtual ~HWRegister() {}
};

class PCICFGRegister64 : public HWRegister
{
    std::shared_ptr<PciHandleType> handle;
    size_t offset;
public:
    PCICFGRegister64(const std::shared_ptr<PciHandleType> & handle_, size_t offset_) :
        handle(handle_),
        offset(offset_)
    {
    }
    void operator = (uint64 val) override
    {
        cvt_ds cvt;
        cvt.ui64 = val;
        handle->write32(offset, cvt.ui32.low);
        handle->write32(offset + sizeof(uint32), cvt.ui32.high);
    }
    operator uint64 ()  override
    {
        uint64 result = 0;
        handle->read64(offset, &result);
        return result;
    }
};

class PCICFGRegister32 : public HWRegister
{
    std::shared_ptr<PciHandleType> handle;
    size_t offset;
public:
    PCICFGRegister32(const std::shared_ptr<PciHandleType> & handle_, size_t offset_) :
        handle(handle_),
        offset(offset_)
    {
    }
    void operator = (uint64 val) override
    {
        handle->write32(offset, (uint32)val);
    }
    operator uint64 () override
    {
        uint32 result = 0;
        handle->read32(offset, &result);
        return result;
    }
};

class MMIORegister64 : public HWRegister
{
    std::shared_ptr<MMIORange> handle;
    size_t offset;
public:
    MMIORegister64(const std::shared_ptr<MMIORange> & handle_, size_t offset_) :
        handle(handle_),
        offset(offset_)
    {
    }
    void operator = (uint64 val) override
    {
        handle->write64(offset, val);
    }
    operator uint64 () override
    {
        return handle->read64(offset);
    }
};

class MMIORegister32 : public HWRegister
{
    std::shared_ptr<MMIORange> handle;
    size_t offset;
public:
    MMIORegister32(const std::shared_ptr<MMIORange> & handle_, size_t offset_) :
        handle(handle_),
        offset(offset_)
    {
    }
    void operator = (uint64 val) override
    {
        handle->write32(offset, (uint32)val);
    }
    operator uint64 () override
    {
        return (uint64)handle->read32(offset);
    }
};

class MSRRegister : public HWRegister
{
    std::shared_ptr<SafeMsrHandle> handle;
    size_t offset;
public:
    MSRRegister(const std::shared_ptr<SafeMsrHandle> & handle_, size_t offset_) :
        handle(handle_),
        offset(offset_)
    {
    }
    void operator = (uint64 val) override
    {
        handle->write(offset, val);
    }
    operator uint64 () override
    {
        uint64 value = 0;
        handle->read(offset, &value);
        return value;
    }
};

class CounterWidthExtenderRegister : public HWRegister
{
    std::shared_ptr<CounterWidthExtender> handle;
public:
    CounterWidthExtenderRegister(const std::shared_ptr<CounterWidthExtender> & handle_) :
        handle(handle_)
    {
    }
    void operator = (uint64 val) override
    {
        if (val == 0)
        {
            handle->reset();
        }
        else
        {
            std::cerr << "ERROR: writing non-zero values to CounterWidthExtenderRegister is not supported\n";
            throw std::exception();
        }
    }
    operator uint64 () override
    {
        return handle->read();;
    }
};

#undef PCM_UNCORE_PMON_BOX_CHECK_STATUS // debug only

class UncorePMU
{
    typedef std::shared_ptr<HWRegister> HWRegisterPtr;
    HWRegisterPtr unitControl;
public:
    HWRegisterPtr counterControl[4];
    HWRegisterPtr counterValue[4];
    HWRegisterPtr fixedCounterControl;
    HWRegisterPtr fixedCounterValue;
    HWRegisterPtr filter[2];

    UncorePMU(const HWRegisterPtr & unitControl_,
        const HWRegisterPtr & counterControl0,
        const HWRegisterPtr & counterControl1,
        const HWRegisterPtr & counterControl2,
        const HWRegisterPtr & counterControl3,
        const HWRegisterPtr & counterValue0,
        const HWRegisterPtr & counterValue1,
        const HWRegisterPtr & counterValue2,
        const HWRegisterPtr & counterValue3,
        const HWRegisterPtr & fixedCounterControl_ = HWRegisterPtr(),
        const HWRegisterPtr & fixedCounterValue_ = HWRegisterPtr(),
        const HWRegisterPtr & filter0 = HWRegisterPtr(),
        const HWRegisterPtr & filter1 = HWRegisterPtr()
    ) :
        unitControl(unitControl_),
        counterControl{ counterControl0, counterControl1, counterControl2, counterControl3 },
        counterValue{ counterValue0, counterValue1, counterValue2, counterValue3 },
        fixedCounterControl(fixedCounterControl_),
        fixedCounterValue(fixedCounterValue_),
        filter{ filter0 , filter1 }
    {
    }
    UncorePMU() {}
    virtual ~UncorePMU() {}
    bool valid() const
    {
        return unitControl.get() != nullptr;
    }
    void writeUnitControl(const uint32 value)
    {
        *unitControl = value;
    }
    void cleanup();
    void freeze(const uint32 extra);
    bool initFreeze(const uint32 extra, const char* xPICheckMsg = nullptr);
    void unfreeze(const uint32 extra);
    void resetUnfreeze(const uint32 extra);
};

//! Object to access uncore counters in a socket/processor with microarchitecture codename SandyBridge-EP (Jaketown) or Ivytown-EP or Ivytown-EX
class ServerPCICFGUncore
{
    friend class PCM;
    int32 iMCbus,UPIbus,M2Mbus;
    uint32 groupnr;
    int32 cpu_model;
    typedef std::vector<UncorePMU> UncorePMUVector;
    UncorePMUVector imcPMUs;
    UncorePMUVector edcPMUs;
    UncorePMUVector xpiPMUs;
    UncorePMUVector m3upiPMUs;
    UncorePMUVector m2mPMUs;
    UncorePMUVector haPMUs;
    std::vector<UncorePMUVector*> allPMUs{ &imcPMUs, &edcPMUs, &xpiPMUs, &m3upiPMUs , &m2mPMUs, &haPMUs };
    std::vector<uint64> qpi_speed;
    std::vector<uint32> num_imc_channels; // number of memory channels in each memory controller
    std::vector<std::pair<uint32, uint32> > XPIRegisterLocation; // (device, function)
    std::vector<std::pair<uint32, uint32> > M3UPIRegisterLocation; // (device, function)
    std::vector<std::vector< std::pair<uint32, uint32> > > MCRegisterLocation; // MCRegisterLocation[controller]: (device, function)
    std::vector<std::pair<uint32, uint32> > EDCRegisterLocation; // EDCRegisterLocation: (device, function)
    std::vector<std::pair<uint32, uint32> > M2MRegisterLocation; // M2MRegisterLocation: (device, function)
    std::vector<std::pair<uint32, uint32> > HARegisterLocation;  // HARegisterLocation: (device, function)

    static Mutex socket2busMutex;
    static std::vector<std::pair<uint32, uint32> > socket2iMCbus;
    static std::vector<std::pair<uint32, uint32> > socket2UPIbus;
    static std::vector<std::pair<uint32, uint32> > socket2M2Mbus;
    void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize);

    ServerPCICFGUncore();                                         // forbidden
    ServerPCICFGUncore(ServerPCICFGUncore &);                     // forbidden
    ServerPCICFGUncore & operator = (const ServerPCICFGUncore &); // forbidden
    PciHandleType * createIntelPerfMonDevice(uint32 groupnr, int32 bus, uint32 dev, uint32 func, bool checkVendor = false);
    void programIMC(const uint32 * MCCntConfig);
    void programEDC(const uint32 * EDCCntConfig);
    void programM2M(const uint64 * M2MCntConfig);
    void programM2M();
    void programHA(const uint32 * config);
    void programHA();
    void programXPI(const uint32 * XPICntConfig);
    void programM3UPI(const uint32* M3UPICntConfig);
    typedef std::pair<size_t, std::vector<uint64 *> > MemTestParam;
    void initMemTest(MemTestParam & param);
    void doMemTest(const MemTestParam & param);
    void cleanupMemTest(const MemTestParam & param);
    void cleanupQPIHandles();
    void cleanupPMUs();
    void writeAllUnitControl(const uint32 value);
    void initDirect(uint32 socket_, const PCM * pcm);
    void initPerf(uint32 socket_, const PCM * pcm);
    void initBuses(uint32 socket_, const PCM * pcm);
    void initRegisterLocations(const PCM * pcm);
    uint64 getPMUCounter(std::vector<UncorePMU> & pmu, const uint32 id, const uint32 counter);

public:
    enum EventPosition {
        READ=0,
        WRITE=1,
        READ_RANK_A=0,
        WRITE_RANK_A=1,
        READ_RANK_B=2,
        WRITE_RANK_B=3,
        PARTIAL=2,
        PMM_READ=2,
        PMM_WRITE=3,
        PMM_MM_MISS_CLEAN=2,
        PMM_MM_MISS_DIRTY=3,
        NM_HIT=0,  // NM :  Near Memory (DRAM cache) in Memory Mode
        M2M_CLOCKTICKS=1
    };
    //! \brief Initialize access data structures
    //! \param socket_ socket id
    //! \param pcm pointer to PCM instance
    ServerPCICFGUncore(uint32 socket_, const PCM * pcm);
    //! \brief Program performance counters (disables programming power counters)
    void program();
    //! \brief Get the number of integrated controller reads (in cache lines)
    uint64 getImcReads();
    //! \brief Get the number of integrated controller reads for given controller (in cache lines)
    //! \param controller controller ID/number
    uint64 getImcReadsForController(uint32 controller);
    //! \brief Get the number of integrated controller reads for given channels (in cache lines)
    //! \param beginChannel first channel in the range
    //! \param endChannel last channel + 1: the range is [beginChannel, endChannel). endChannel is not included.
    uint64 getImcReadsForChannels(uint32 beginChannel, uint32 endChannel);
    //! \brief Get the number of integrated controller writes (in cache lines)
    uint64 getImcWrites();
    //! \brief Get the number of requests to home agent (BDX/HSX only)
    uint64 getHALocalRequests();
    //! \brief Get the number of local requests to home agent (BDX/HSX only)
    uint64 getHARequests();

    //! \brief Get the number of PMM memory reads (in cache lines)
    uint64 getPMMReads();
    //! \brief Get the number of PMM memory writes (in cache lines)
    uint64 getPMMWrites();

    //! \brief Get the number of cache lines read by EDC (embedded DRAM controller)
    uint64 getEdcReads();
    //! \brief Get the number of cache lines written by EDC (embedded DRAM controller)
    uint64 getEdcWrites();

    //! \brief Get the number of incoming data flits to the socket through a port
    //! \param port QPI port id
    uint64 getIncomingDataFlits(uint32 port);

    //! \brief Get the number of outgoing data and non-data or idle flits (depending on the architecture) from the socket through a port
    //! \param port QPI port id
    uint64 getOutgoingFlits(uint32 port);

    ~ServerPCICFGUncore();

    //! \brief Program power counters (disables programming performance counters)
    //! \param mc_profile memory controller measurement profile. See description of profiles in pcm-power.cpp
    void program_power_metrics(int mc_profile);

    //! \brief Program memory counters (disables programming performance counters)
    //! \param rankA count DIMM rank1 statistics (disables memory channel monitoring)
    //! \param rankB count DIMM rank2 statistics (disables memory channel monitoring)
    //! \param PMM monitor PMM bandwidth instead of partial writes
    //! \param Program events for PMM mixed mode (AppDirect + MemoryMode)
    void programServerUncoreMemoryMetrics(const int rankA = -1, const int rankB = -1, const bool PMM = false, const bool PMMMixedMode = false);

    //! \brief Get number of QPI LL clocks on a QPI port
    //! \param port QPI port number
    uint64 getQPIClocks(uint32 port);

    //! \brief Get number cycles on a QPI port when the link was in a power saving half-lane mode
    //! \param port QPI port number
    uint64 getQPIL0pTxCycles(uint32 port);
    //! \brief Get number cycles on a UPI port when the link was in a L0 mode (fully active)
    //! \param port UPI port number
    uint64 getUPIL0TxCycles(uint32 port);
    //! \brief Get number cycles on a QPI port when the link was in a power saving shutdown mode
    //! \param port QPI port number
    uint64 getQPIL1Cycles(uint32 port);
    //! \brief Get number DRAM channel cycles
    //! \param channel channel number
    uint64 getDRAMClocks(uint32 channel);
    //! \brief Get number MCDRAM channel cycles
    //! \param channel channel number
    uint64 getMCDRAMClocks(uint32 channel);
    //! \brief Direct read of memory controller PMU counter (counter meaning depends on the programming: power/performance/etc)
    //! \param channel channel number
    //! \param counter counter number
    uint64 getMCCounter(uint32 channel, uint32 counter);
    //! \brief Direct read of embedded DRAM memory controller PMU counter (counter meaning depends on the programming: power/performance/etc)
    //! \param channel channel number
    //! \param counter counter number
    uint64 getEDCCounter(uint32 channel, uint32 counter);
    //! \brief Direct read of QPI LL PMU counter (counter meaning depends on the programming: power/performance/etc)
    //! \param port port number
    //! \param counter counter number
    uint64 getQPILLCounter(uint32 port, uint32 counter);
    //! \brief Direct read of M3UPI PMU counter (counter meaning depends on the programming: power/performance/etc)
    //! \param port port number
    //! \param counter counter number
    uint64 getM3UPICounter(uint32 port, uint32 counter);
    //! \brief Direct read of M2M counter
    //! \param box box ID/number
    //! \param counter counter number
    uint64 getM2MCounter(uint32 box, uint32 counter);

    //! \brief Freezes event counting
    void freezeCounters();
    //! \brief Unfreezes event counting
    void unfreezeCounters();

    //! \brief Measures/computes the maximum theoretical QPI link bandwidth speed in GByte/seconds
    uint64 computeQPISpeed(const uint32 ref_core, const int cpumodel);

    //! \brief Enable correct counting of various LLC events (with memory access perf penalty)
    void enableJKTWorkaround(bool enable);

    //! \brief Returns the number of detected QPI ports
    size_t getNumQPIPorts() const { return xpiPMUs.size(); }

    //! \brief Returns the speed of the QPI link
    uint64 getQPILinkSpeed(const uint32 linkNr) const
    {
        return qpi_speed.empty() ? 0 : qpi_speed[linkNr];
    }

    //! \brief Print QPI Speeds
    void reportQPISpeed() const;

    //! \brief Returns the number of detected integrated memory controllers
    uint32 getNumMC() const { return (uint32)num_imc_channels.size(); }

    //! \brief Returns the total number of detected memory channels on all integrated memory controllers
    size_t getNumMCChannels() const { return (size_t)imcPMUs.size(); }

    //! \brief Returns the total number of detected memory channels on given integrated memory controller
    //! \param controller controller number
    size_t getNumMCChannels(const uint32 controller) const;

    //! \brief Returns the total number of detected memory channels on all embedded DRAM controllers (EDC)
    size_t getNumEDCChannels() const { return edcPMUs.size(); }
};

class SimpleCounterState
{
    template <class T>
    friend uint64 getNumberOfEvents(const T & before, const T & after);
    friend class PCM;
    uint64 data;

public:
    SimpleCounterState() : data(0)
    { }
    virtual ~SimpleCounterState() { }
};

typedef SimpleCounterState PCIeCounterState;
typedef SimpleCounterState IIOCounterState;
typedef std::vector<uint64> eventGroup_t;

class PerfVirtualControlRegister;

/*!
        \brief CPU Performance Monitor

        This singleton object needs to be instantiated for each process
        before accessing counting and measuring routines
*/
class PCM_API PCM
{
    friend class BasicCounterState;
    friend class UncoreCounterState;
    friend class Socket;
    friend class ServerUncore;
    friend class PerfVirtualControlRegister;
    friend class Aggregator;
    friend class ServerPCICFGUncore;
    PCM();     // forbidden to call directly because it is a singleton
    PCM(const PCM &) = delete;
    PCM & operator = (const PCM &) = delete;

    int32 cpu_family;
    int32 cpu_model;
    int32 cpu_stepping;
    int64 cpu_microcode_level;
    int32 max_cpuid;
    int32 threads_per_core;
    int32 num_cores;
    int32 num_sockets;
    int32 num_phys_cores_per_socket;
    int32 num_online_cores;
    int32 num_online_sockets;
    uint32 core_gen_counter_num_max;
    uint32 core_gen_counter_num_used;
    uint32 core_gen_counter_width;
    uint32 core_fixed_counter_num_max;
    uint32 core_fixed_counter_num_used;
    uint32 core_fixed_counter_width;
    uint32 uncore_gen_counter_num_max;
    uint32 uncore_gen_counter_num_used;
    uint32 uncore_gen_counter_width;
    uint32 uncore_fixed_counter_num_max;
    uint32 uncore_fixed_counter_num_used;
    uint32 uncore_fixed_counter_width;
    uint32 perfmon_version;
    int32 perfmon_config_anythread;
    uint64 nominal_frequency;
    uint64 max_qpi_speed; // in GBytes/second
    uint32 L3ScalingFactor;
    int32 pkgThermalSpecPower, pkgMinimumPower, pkgMaximumPower;

    std::vector<TopologyEntry> topology;
    SystemRoot* systemTopology;
    std::string errorMessage;

    static PCM * instance;
    bool allow_multiple_instances;
    bool programmed_pmu;
    std::vector<std::shared_ptr<SafeMsrHandle> > MSR;
    std::vector<std::shared_ptr<ServerPCICFGUncore> > server_pcicfg_uncore;
    std::vector<UncorePMU> pcuPMUs;
    std::vector<std::map<int32, UncorePMU> > iioPMUs;
    std::vector<UncorePMU> uboxPMUs;
    double joulesPerEnergyUnit;
    std::vector<std::shared_ptr<CounterWidthExtender> > energy_status;
    std::vector<std::shared_ptr<CounterWidthExtender> > dram_energy_status;
    std::vector<std::vector<UncorePMU> > cboPMUs;

    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_local;
    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_total;
#ifdef __linux__
    Resctrl resctrl;
#endif
    bool useResctrl;

    std::shared_ptr<ClientBW> clientBW;
    std::shared_ptr<CounterWidthExtender> clientImcReads;
    std::shared_ptr<CounterWidthExtender> clientImcWrites;
    std::shared_ptr<CounterWidthExtender> clientIoRequests;

    bool disable_JKT_workaround;
    bool blocked;              // track if time-driven counter update is running or not: PCM is blocked

    uint64 * coreCStateMsr;    // MSR addresses of core C-state free-running counters
    uint64 * pkgCStateMsr;     // MSR addresses of package C-state free-running counters

    std::vector<std::shared_ptr<CoreTaskQueue> > coreTaskQueues;

    bool L2CacheHitRatioAvailable;
    bool L3CacheHitRatioAvailable;
    bool L3CacheMissesAvailable;
    bool L2CacheMissesAvailable;
    bool L2CacheHitsAvailable;
    bool L3CacheHitsNoSnoopAvailable;
    bool L3CacheHitsSnoopAvailable;
    bool L3CacheHitsAvailable;

    bool forceRTMAbortMode;

    bool vm = false;
    bool linux_arch_perfmon = false;

public:
    enum { MAX_C_STATE = 10 }; // max C-state on Intel architecture

    //! \brief Returns true if the specified core C-state residency metric is supported
    bool isCoreCStateResidencySupported(int state)
    {
        if (state == 0 || state == 1)
            return true;

        return (coreCStateMsr != NULL && state <= ((int)MAX_C_STATE) && coreCStateMsr[state] != 0);
    }

    //! \brief Returns true if the specified package C-state residency metric is supported
    bool isPackageCStateResidencySupported(int state)
    {
        if (state == 0)
        {
            return true;
        }
        return (pkgCStateMsr != NULL && state <= ((int)MAX_C_STATE) && pkgCStateMsr[state] != 0);
    }

    //! \brief Redirects output destination to provided file, instead of std::cout
    void setOutput(const std::string filename);

    //! \brief Restores output, closes output file if opened
    void restoreOutput();

    //! \brief Set Run State.
    // Arguments:
    //  -- 1 - program is running
    //  -- 0 -pgram is sleeping
    void setRunState(int new_state) { run_state = new_state; }

    //! \brief Returns program's Run State.
    // Results:
    //  -- 1 - program is running
    //  -- 0 -pgram is sleeping
    int getRunState(void) { return run_state; }

    bool isBlocked(void) { return blocked; }
    void setBlocked(const bool new_blocked) { blocked = new_blocked; }

    //! \brief Call it before program() to allow multiple running instances of PCM on the same system
    void allowMultipleInstances()
    {
        allow_multiple_instances = true;
    }

    //! Mode of programming (parameter in the program() method)
    enum ProgramMode {
        DEFAULT_EVENTS = 0,         /*!< Default choice of events, the additional parameter is not needed and ignored */
        CUSTOM_CORE_EVENTS = 1,     /*!< Custom set of core events specified in the parameter to the program method. The parameter must be a pointer to array of four \c CustomCoreEventDescription values */
        EXT_CUSTOM_CORE_EVENTS = 2, /*!< Custom set of core events specified in the parameter to the program method. The parameter must be a pointer to a \c ExtendedCustomCoreEventDescription  data structure */
        INVALID_MODE                /*!< Non-programmed mode */
    };

    //! Return codes (e.g. for program(..) method)
    enum ErrorCode {
        Success = 0,
        MSRAccessDenied = 1,
        PMUBusy = 2,
        UnknownError
    };

    enum PerfmonField {
        INVALID, /* Use to parse invalid field */
        OPCODE,
        EVENT_SELECT,
        UMASK,
        RESET,
        EDGE_DET,
        IGNORED,
        OVERFLOW_ENABLE,
        ENABLE,
        INVERT,
        THRESH,
        CH_MASK,
        FC_MASK,
        /* Below are not part of perfmon definition */
        H_EVENT_NAME,
        V_EVENT_NAME,
        MULTIPLIER,
        DIVIDER,
        COUNTER_INDEX
    };

    enum PCIeWidthMode {
        X1,
        X4,
        X8,
        X16,
        XFF
    };

    enum { // offsets/enumeration of IIO stacks
        IIO_CBDMA = 0, // shared with DMI
        IIO_PCIe0 = 1,
        IIO_PCIe1 = 2,
        IIO_PCIe2 = 3,
        IIO_MCP0 = 4,
        IIO_MCP1 = 5,
        IIO_STACK_COUNT = 6
    };

    struct SimplePCIeDevInfo
    {
        enum PCIeWidthMode width;
        std::string pciDevName;
        std::string busNumber;

        SimplePCIeDevInfo() : width(XFF) { }
    };

    /*! \brief Custom Core event description

        See "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2" for the concrete values of the data structure fields,
        e.g. Appendix A.2 "Performance Monitoring Events for Intel(r) Core(tm) Processor Family
        and Xeon Processor Family"
    */
    struct CustomCoreEventDescription
    {
        int32 event_number, umask_value;
    };

    /*! \brief Extended custom core event description

        In contrast to CustomCoreEventDescription supports configuration of all fields.

        See "Intel 64 and IA-32 Architectures Software Developers Manual Volume 3B:
        System Programming Guide, Part 2" for the concrete values of the data structure fields,
        e.g. Appendix A.2 "Performance Monitoring Events for Intel(r) Core(tm) Processor Family
        and Xeon Processor Family"
    */
    struct ExtendedCustomCoreEventDescription
    {
        FixedEventControlRegister * fixedCfg; // if NULL, then default configuration performed for fixed counters
        uint32 nGPCounters;                   // number of general purpose counters
        EventSelectRegister * gpCounterCfg;   // general purpose counters, if NULL, then default configuration performed for GP counters
        uint64 OffcoreResponseMsrValue[2];
        ExtendedCustomCoreEventDescription() : fixedCfg(NULL), nGPCounters(0), gpCounterCfg(NULL)
        {
            OffcoreResponseMsrValue[0] = 0;
            OffcoreResponseMsrValue[1] = 0;
        }
    };

    struct CustomIIOEventDescription
    {
        /* We program the same counters to every IIO Stacks */
        std::string eventNames[4];
        IIOPMUCNTCTLRegister eventOpcodes[4];
        int multiplier[4]; //Some IIO event requires transformation to get meaningful output (i.e. DWord to bytes)
        int divider[4]; //We usually like to have some kind of divider (i.e. /10e6 )
    };

private:
    ProgramMode mode;
    CustomCoreEventDescription coreEventDesc[PERF_MAX_CUSTOM_COUNTERS];

#ifdef _MSC_VER
    HANDLE numInstancesSemaphore;     // global semaphore that counts the number of PCM instances on the system
#else
    // global semaphore that counts the number of PCM instances on the system
    sem_t * numInstancesSemaphore;
#endif

    std::vector<int32> socketRefCore;

    bool canUsePerf;
#ifdef PCM_USE_PERF
    std::vector<std::vector<int> > perfEventHandle;
    void readPerfData(uint32 core, std::vector<uint64> & data);

    enum {
        PERF_INST_RETIRED_ANY_POS = 0,
        PERF_CPU_CLK_UNHALTED_THREAD_POS = 1,
        PERF_CPU_CLK_UNHALTED_REF_POS = 2,
        PERF_GEN_EVENT_0_POS = 3,
        PERF_GEN_EVENT_1_POS = 4,
        PERF_GEN_EVENT_2_POS = 5,
        PERF_GEN_EVENT_3_POS = 6
    };

    enum {
        PERF_GROUP_LEADER_COUNTER = PERF_INST_RETIRED_ANY_POS
    };
#endif
    std::ofstream * outfile;       // output file stream
    std::streambuf * backup_ofile; // backup of original output = cout
    int run_state;                 // either running (1) or sleeping (0)

    bool needToRestoreNMIWatchdog;

    std::vector<std::vector<EventSelectRegister> > lastProgrammedCustomCounters;
    uint32 checkCustomCoreProgramming(std::shared_ptr<SafeMsrHandle> msr);
    ErrorCode programCoreCounters(int core, const PCM::ProgramMode mode, const ExtendedCustomCoreEventDescription * pExtDesc,
        std::vector<EventSelectRegister> & programmedCustomCounters);

    bool PMUinUse();
    void cleanupPMU();
    void cleanupRDT();
    bool decrementInstanceSemaphore(); // returns true if it was the last instance

#ifdef __APPLE__
    // OSX does not have sem_getvalue, so we must get the number of instances by a different method
    uint32 getNumInstances();
    uint32 decrementNumInstances();
    uint32 incrementNumInstances();
#endif


    void computeQPISpeedBeckton(int core_nr);
    void destroyMSR();
    void computeNominalFrequency();
    static bool isCPUModelSupported(const int model_);
    std::string getSupportedUarchCodenames() const;
    std::string getUnsupportedMessage() const;
    bool detectModel();
    bool checkModel();

    void initCStateSupportTables();
    bool discoverSystemTopology();
    void printSystemTopology() const;
    bool initMSR();
    bool detectNominalFrequency();
    void showSpecControlMSRs();
    void initEnergyMonitoring();
    void initUncoreObjects();
    /*!
    *       \brief initializes each core with an RMID
    *
    *       \returns nothing
    */
    void initRDT();
    /*!
     *      \brief Initializes RDT
     *
     *      Initializes RDT infrastructure through resctrl Linux driver or direct MSR programming.
     *      For the latter: initializes each core event MSR with an RMID for QOS event (L3 cache monitoring or memory bandwidth monitoring)
     *      \returns nothing
    */
    void initQOSevent(const uint64 event, const int32 core);
    void programBecktonUncore(int core);
    void programNehalemEPUncore(int core);
    void enableJKTWorkaround(bool enable);
    template <class CounterStateType>
    void readAndAggregateMemoryBWCounters(const uint32 core, CounterStateType & counterState);
    template <class CounterStateType>
    void readAndAggregateUncoreMCCounters(const uint32 socket, CounterStateType & counterState);
    template <class CounterStateType>
    void readAndAggregateEnergyCounters(const uint32 socket, CounterStateType & counterState);
    template <class CounterStateType>
    void readPackageThermalHeadroom(const uint32 socket, CounterStateType & counterState);
    template <class CounterStateType>
    void readAndAggregatePackageCStateResidencies(std::shared_ptr<SafeMsrHandle> msr, CounterStateType & result);
    void readQPICounters(SystemCounterState & counterState);
    void reportQPISpeed() const;
    void readCoreCounterConfig(const bool complainAboutMSR = false);
    void readCPUMicrocodeLevel();

    uint64 CX_MSR_PMON_CTRY(uint32 Cbo, uint32 Ctr) const;
    uint64 CX_MSR_PMON_BOX_FILTER(uint32 Cbo) const;
    uint64 CX_MSR_PMON_BOX_FILTER1(uint32 Cbo) const;
    uint64 CX_MSR_PMON_CTLY(uint32 Cbo, uint32 Ctl) const;
    uint64 CX_MSR_PMON_BOX_CTL(uint32 Cbo) const;
    void programCboOpcodeFilter(const uint32 opc0, UncorePMU & pmu, const uint32 nc_, const uint32 opc1, const uint32 loc, const uint32 rem);
    void initLLCReadMissLatencyEvents(uint64 * events, uint32 & opCode);
    void initCHARequestEvents(uint64 * events);
    void programCbo();
    uint64 getCBOCounterState(const uint32 socket, const uint32 ctr_);
    template <class Iterator>
    static void program(UncorePMU& pmu, const Iterator& eventsBegin, const Iterator& eventsEnd, const uint32 extra)
    {
        if (!eventsBegin) return;
        Iterator curEvent = eventsBegin;
        for (int c = 0; curEvent != eventsEnd; ++c, ++curEvent)
        {
            *pmu.counterControl[c] = MC_CH_PCI_PMON_CTL_EN;
            *pmu.counterControl[c] = MC_CH_PCI_PMON_CTL_EN | *curEvent;
        }
        if (extra)
        {
            pmu.resetUnfreeze(extra);
        }
    }
    void programPCU(uint32 * events, const uint64 filter);
    void programUBOX(const uint64* events);

    void cleanupUncorePMUs();

    bool isCLX() const // Cascade Lake-SP
    {
        return (PCM::SKX == cpu_model) && (cpu_stepping > 4 && cpu_stepping < 8);
    }

    static bool isCPX(int cpu_model_, int cpu_stepping_) // Cooper Lake
    {
        return (PCM::SKX == cpu_model_) && (cpu_stepping_ >= 10);
    }

    bool isCPX() const
    {
        return isCPX(cpu_model, cpu_stepping);
    }

    void initUncorePMUsDirect();
    void initUncorePMUsPerf();
    bool isRDTDisabled() const;

public:
    enum EventPosition
    {
        TOR_OCCUPANCY = 0,
        TOR_INSERTS = 1,
        REQUESTS_ALL = 2,
        REQUESTS_LOCAL = 3
    };
    //! check if in secure boot mode
    bool isSecureBoot() const;

    //! true if Linux perf for uncore PMU programming should AND can be used internally
    bool useLinuxPerfForUncore() const;

    /*!
             \brief The system, sockets, uncores, cores and threads are structured like a tree

             \returns a reference to a const System object representing the root of the tree
     */
    SystemRoot const & getSystemTopology() const {
        return *systemTopology;
    }

    /*!
             \brief checks if QOS monitoring support present

             \returns true or false
     */
    bool QOSMetricAvailable() const;
    /*!
             \brief checks L3 cache support for QOS present

             \returns true or false
     */
    bool L3QOSMetricAvailable() const;
    /*!
             \brief checks if L3 cache monitoring present

             \returns true or false
     */
    bool L3CacheOccupancyMetricAvailable() const;
    /*!
            \brief checks if local memory bandwidth monitoring present

            \returns true or false
    */
    bool CoreLocalMemoryBWMetricAvailable() const;
    /*!
    \brief checks if total memory bandwidth monitoring present

    \returns true or false
    */
    bool CoreRemoteMemoryBWMetricAvailable() const;
    /*!
     *      \brief returns the max number of RMID supported by socket
     *
     *      \returns maximum number of RMID supported by socket
     */
    unsigned getMaxRMID() const;

    //! \brief Returns the number of CBO or CHA units per socket
    uint32 getMaxNumOfCBoxes() const;

    //! \brief Returns the number of IIO stacks per socket
    uint32 getMaxNumOfIIOStacks() const;

    /*!
            \brief Returns PCM object

            Returns PCM object. If the PCM has not been created before than
            an instance is created. PCM is a singleton.

            \return Pointer to PCM object
    */
    static PCM * getInstance();        // the only way to get access

    /*!
            \brief Checks the status of PCM object

            Call this method to check if PCM gained access to model specific registers. The method is deprecated, see program error code instead.

            \return true iff access to model specific registers works without problems
    */
    bool good();                       // true if access to CPU counters works

    /*! \brief Returns the error message

                Call this when good() returns false, otherwise return an empty string
    */
    const std::string & getErrorMessage() const
    {
        return errorMessage;
    }

    /*! \brief Programs performance counters
        \param mode_ mode of programming, see ProgramMode definition
        \param parameter_ optional parameter for some of programming modes

                Call this method before you start using the performance counting routines.

        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode program(const ProgramMode mode_ = DEFAULT_EVENTS, const void * parameter_ = NULL); // program counters and start counting

    /*! \brief Programs uncore latency counters on microarchitectures codename SandyBridge-EP and later Xeon uarch
        \param enable_pmm enables DDR/PMM. See possible profile values in pcm-latency.cpp example

        Call this method before you start using the latency counter routines on microarchitecture codename SandyBridge-EP and later Xeon uarch

        \warning After this call the memory and QPI bandwidth counters on microarchitecture codename SandyBridge-EP and later Xeon uarch will not work.
        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode programServerUncoreLatencyMetrics(bool enable_pmm);

    /*! \brief Programs uncore power/energy counters on microarchitectures codename SandyBridge-EP and later Xeon uarch
        \param mc_profile profile for integrated memory controller PMU. See possible profile values in pcm-power.cpp example
        \param pcu_profile profile for power control unit PMU. See possible profile values in pcm-power.cpp example
        \param freq_bands array of three integer values for core frequency band monitoring. See usage in pcm-power.cpp example

        Call this method before you start using the power counter routines on microarchitecture codename SandyBridge-EP and later Xeon uarch

        \warning After this call the memory and QPI bandwidth counters on microarchitecture codename SandyBridge-EP and later Xeon uarch will not work.
        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode programServerUncorePowerMetrics(int mc_profile, int pcu_profile, int * freq_bands = NULL);

    /*! \brief Programs uncore memory counters on microarchitectures codename SandyBridge-EP and later Xeon uarch
        \param rankA count DIMM rank1 statistics (disables memory channel monitoring)
        \param rankB count DIMM rank2 statistics (disables memory channel monitoring)
        \param PMM monitor PMM bandwidth instead of partial writes
        \param Program events for PMM mixed mode (AppDirect + MemoryMode)

        Call this method before you start using the memory counter routines on microarchitecture codename SandyBridge-EP and later Xeon uarch

        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode programServerUncoreMemoryMetrics(int rankA = -1, int rankB = -1, bool PMM = false, bool PMMMixedMode = false);

    // vector of IDs. E.g. for core {raw event} or {raw event, offcore response1 msr value, } or {raw event, offcore response1 msr value, offcore response2}
    // or for cha/cbo {raw event, filter value}, etc
    // + user-supplied name
    typedef std::pair<std::array<uint64, 3>, std::string> RawEventConfig;
    struct RawPMUConfig
    {
        std::vector<RawEventConfig> programmable;
        std::vector<RawEventConfig> fixed;
    };
    typedef std::map<std::string, RawPMUConfig> RawPMUConfigs;
    ErrorCode program(const RawPMUConfigs& allPMUConfigs);

    //! \brief Freezes uncore event counting (works only on microarchitecture codename SandyBridge-EP and IvyTown)
    void freezeServerUncoreCounters();

    //! \brief Unfreezes uncore event counting (works only on microarchitecture codename SandyBridge-EP and IvyTown)
    void unfreezeServerUncoreCounters();

    /*! \brief Reads the power/energy counter state of a socket (works only on microarchitecture codename SandyBridge-EP)
        \param socket socket id
        \return State of power counters in the socket
    */
    ServerUncoreCounterState getServerUncoreCounterState(uint32 socket);

    /*! \brief Cleanups resources and stops performance counting

            One needs to call this method when your program finishes or/and you are not going to use the
            performance counting routines anymore.
*/
    void cleanup();

    /*! \brief Forces PMU reset

                If there is no chance to free up PMU from other applications you might try to call this method at your own risk.
    */
    void resetPMU();

    /*! \brief Reads all counter states (including system, sockets and cores)

        \param systemState system counter state (return parameter)
        \param socketStates socket counter states (return parameter)
        \param coreStates core counter states (return parameter)

    */
    void getAllCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates, std::vector<CoreCounterState> & coreStates);

    /*! \brief Reads uncore counter states (including system and sockets) but no core counters

    \param systemState system counter state (return parameter)
    \param socketStates socket counter states (return parameter)

    */
    void getUncoreCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates);

    /*! \brief Return true if the core in online

        \param os_core_id OS core id
    */
    bool isCoreOnline(int32 os_core_id) const;

    /*! \brief Return true if the socket in online

        \param socket_id OS socket id
    */
    bool isSocketOnline(int32 socket_id) const;

    /*! \brief Reads the counter state of the system

            System consists of several sockets (CPUs).
            Socket has a CPU in it. Socket (CPU) consists of several (logical) cores.

            \return State of counters in the entire system
    */
    SystemCounterState getSystemCounterState();

    /*! \brief Reads the counter state of a socket
            \param socket socket id
            \return State of counters in the socket
    */
    SocketCounterState getSocketCounterState(uint32 socket);

    /*! \brief Reads the counter state of a (logical) core

        Be aware that during the measurement other threads may be scheduled on the same core by the operating system (this is called context-switching). The performance events caused by these threads will be counted as well.


            \param core core id
            \return State of counters in the core
    */
    CoreCounterState getCoreCounterState(uint32 core);

    /*! \brief Reads number of logical cores in the system
            \return Number of logical cores in the system
    */
    uint32 getNumCores() const;

    /*! \brief Reads number of online logical cores in the system
            \return Number of online logical cores in the system
    */
    uint32 getNumOnlineCores() const;

    /*! \brief Reads number of sockets (CPUs) in the system
            \return Number of sockets in the system
    */
    uint32 getNumSockets() const;

    /*! \brief Reads number of online sockets (CPUs) in the system
            \return Number of online sockets in the system
    */
    uint32 getNumOnlineSockets() const;

    /*! \brief Reads how many hardware threads has a physical core
            "Hardware thread" is a logical core in a different terminology.
            If Intel(r) Hyperthreading(tm) is enabled then this function returns 2.
            \return Number of hardware threads per physical core
    */
    uint32 getThreadsPerCore() const;

    /*! \brief Checks if SMT (HyperThreading) is enabled.
            \return true iff SMT (HyperThreading) is enabled.
    */
    bool getSMT() const; // returns true iff SMT ("Hyperthreading") is on

    /*! \brief Reads the nominal core frequency
            \return Nominal frequency in Hz
    */
    uint64 getNominalFrequency() const; // in Hz

    /*! \brief runs CPUID.0xF.0x01 to get the L3 up scaling factor to calculate L3 Occupancy
     *  Scaling factor is returned in EBX register after running the CPU instruction
     * \return L3 up scaling factor
     */
    uint32 getL3ScalingFactor() const;

    /*! \brief runs CPUID.0xB.0x01 to get maximum logical cores (including SMT) per socket.
     *  max_lcores_per_socket is returned in EBX[15:0]. Compare this value with number of cores per socket
     *  detected in the system to see if some cores are offlined
     * \return true iff max_lcores_per_socket == number of cores per socket detected
     */
    bool isSomeCoreOfflined();

    /*! \brief Returns the maximum number of custom (general-purpose) core events supported by CPU
    */
    int32 getMaxCustomCoreEvents();

    //! \brief Identifiers of supported CPU models
    enum SupportedCPUModels
    {
        NEHALEM_EP = 26,
        NEHALEM = 30,
        ATOM = 28,
        ATOM_2 = 53,
        CENTERTON = 54,
        BAYTRAIL = 55,
        AVOTON = 77,
        CHERRYTRAIL = 76,
        APOLLO_LAKE = 92,
        DENVERTON = 95,
        CLARKDALE = 37,
        WESTMERE_EP = 44,
        NEHALEM_EX = 46,
        WESTMERE_EX = 47,
        SANDY_BRIDGE = 42,
        JAKETOWN = 45,
        IVY_BRIDGE = 58,
        HASWELL = 60,
        HASWELL_ULT = 69,
        HASWELL_2 = 70,
        IVYTOWN = 62,
        HASWELLX = 63,
        BROADWELL = 61,
        BROADWELL_XEON_E3 = 71,
        BDX_DE = 86,
        SKL_UY = 78,
        KBL = 158,
        KBL_1 = 142,
        CML = 166,
        CML_1 = 165,
        ICL = 126,
        ICL_1 = 125,
        TGL = 140,
        TGL_1 = 141,
        BDX = 79,
        KNL = 87,
        SKL = 94,
        SKX = 85,
        END_OF_MODEL_LIST = 0x0ffff
    };

#define PCM_SKL_PATH_CASES \
        case PCM::SKL_UY:  \
        case PCM::KBL:     \
        case PCM::KBL_1:   \
        case PCM::CML:     \
        case PCM::ICL:     \
        case PCM::TGL:     \
        case PCM::SKL:

private:
    bool useSKLPath() const
    {
        switch (cpu_model)
        {
            PCM_SKL_PATH_CASES
                return true;
        }
        return false;
    }
public:

    //! \brief Reads CPU model id
    //! \return CPU model ID
    uint32 getCPUModel() const { return (uint32)cpu_model; }

    //! \brief Reads CPU stepping id
    //! \return CPU stepping ID
    uint32 getCPUStepping() const { return (uint32)cpu_stepping; }

    //! \brief Determines physical thread of given processor ID within a core
    //! \param os_id processor identifier
    //! \return physical thread identifier
    int32 getThreadId(uint32 os_id) const { return (int32)topology[os_id].thread_id; }

    //! \brief Determines physical core of given processor ID within a socket
    //! \param os_id processor identifier
    //! \return physical core identifier
    int32 getCoreId(uint32 os_id) const { return (int32)topology[os_id].core_id; }

    //! \brief Determines physical tile (cores sharing L2 cache) of given processor ID
    //! \param os_id processor identifier
    //! \return physical tile identifier
    int32 getTileId(uint32 os_id) const { return (int32)topology[os_id].tile_id; }

    //! \brief Determines socket of given core
    //! \param core_id core identifier
    //! \return socket identifier
    int32 getSocketId(uint32 core_id) const { return (int32)topology[core_id].socket; }

    //! \brief Returns the number of Intel(r) Quick Path Interconnect(tm) links per socket
    //! \return number of QPI links per socket
    uint64 getQPILinksPerSocket() const
    {
        switch (cpu_model)
        {
        case NEHALEM_EP:
        case WESTMERE_EP:
        case CLARKDALE:
            if (num_sockets == 2)
                return 2;
            else
                return 1;
        case NEHALEM_EX:
        case WESTMERE_EX:
            return 4;
        case JAKETOWN:
        case IVYTOWN:
        case HASWELLX:
        case BDX_DE:
        case BDX:
        case SKX:
            return (server_pcicfg_uncore.size() && server_pcicfg_uncore[0].get()) ? (server_pcicfg_uncore[0]->getNumQPIPorts()) : 0;
        }
        return 0;
    }

    //! \brief Returns the number of detected integrated memory controllers per socket
    uint32 getMCPerSocket() const
    {
        switch (cpu_model)
        {
        case NEHALEM_EP:
        case WESTMERE_EP:
        case CLARKDALE:
            return 1;
        case NEHALEM_EX:
        case WESTMERE_EX:
            return 2;
        case JAKETOWN:
        case IVYTOWN:
        case HASWELLX:
        case BDX_DE:
        case SKX:
        case BDX:
        case KNL:
            return (server_pcicfg_uncore.size() && server_pcicfg_uncore[0].get()) ? (server_pcicfg_uncore[0]->getNumMC()) : 0;
        }
        return 0;
    }

    //! \brief Returns the total number of detected memory channels on all integrated memory controllers per socket
    size_t getMCChannelsPerSocket() const
    {
        switch (cpu_model)
        {
        case NEHALEM_EP:
        case WESTMERE_EP:
        case CLARKDALE:
            return 3;
        case NEHALEM_EX:
        case WESTMERE_EX:
            return 4;
        case JAKETOWN:
        case IVYTOWN:
        case HASWELLX:
        case BDX_DE:
        case SKX:
        case BDX:
        case KNL:
            return (server_pcicfg_uncore.size() && server_pcicfg_uncore[0].get()) ? (server_pcicfg_uncore[0]->getNumMCChannels()) : 0;
        }
        return 0;
    }

    //! \brief Returns the number of detected memory channels on given integrated memory controllers
    //! \param socket socket
    //! \param controller controller
    size_t getMCChannels(uint32 socket, uint32 controller) const
    {
        switch (cpu_model)
        {
        case NEHALEM_EP:
        case WESTMERE_EP:
        case CLARKDALE:
            return 3;
        case NEHALEM_EX:
        case WESTMERE_EX:
            return 4;
        case JAKETOWN:
        case IVYTOWN:
        case HASWELLX:
        case BDX_DE:
        case SKX:
        case BDX:
        case KNL:
            return (socket < server_pcicfg_uncore.size() && server_pcicfg_uncore[socket].get()) ? (server_pcicfg_uncore[socket]->getNumMCChannels(controller)) : 0;
        }
        return 0;
    }


    //! \brief Returns the total number of detected memory channels on all integrated memory controllers per socket
    size_t getEDCChannelsPerSocket() const
    {
        switch (cpu_model)
        {
        case KNL:
            return (server_pcicfg_uncore.size() && server_pcicfg_uncore[0].get()) ? (server_pcicfg_uncore[0]->getNumEDCChannels()) : 0;
        }
        return 0;
    }


    //! \brief Returns the max number of instructions per cycle
    //! \return max number of instructions per cycle
    uint32 getMaxIPC() const
    {
        if (ICL == cpu_model || TGL == cpu_model) return 5;
        switch (cpu_model)
        {
        case DENVERTON:
            return 3;
        case NEHALEM_EP:
        case WESTMERE_EP:
        case NEHALEM_EX:
        case WESTMERE_EX:
        case CLARKDALE:
        case SANDY_BRIDGE:
        case JAKETOWN:
        case IVYTOWN:
        case IVY_BRIDGE:
        case HASWELL:
        case HASWELLX:
        case BROADWELL:
        case BDX_DE:
        case BDX:
        PCM_SKL_PATH_CASES
        case SKX:
            return 4;
        case KNL:
            return 2;
        }
        if (isAtom())
        {
            return 2;
        }
        return 0;
    }

    //! \brief Returns the frequency of Power Control Unit
    uint64 getPCUFrequency() const
    {
        switch (cpu_model)
        {
        case JAKETOWN:
        case IVYTOWN:
            return 800000000ULL;  // 800 MHz
        case HASWELLX:
        case BDX_DE:
        case BDX:
        case KNL:
            return 1000000000ULL; // 1 GHz
        case SKX:
            return 1100000000ULL; // 1.1 GHz
        }
        return 0;
    }

    //! \brief Returns whether it is a server part
    bool isServerCPU() const
    {
        switch (cpu_model)
        {
        case NEHALEM_EP:
        case NEHALEM_EX:
        case WESTMERE_EP:
        case WESTMERE_EX:
        case JAKETOWN:
        case IVYTOWN:
        case HASWELLX:
        case BDX:
        case BDX_DE:
        case SKX:
        case KNL:
            return true;
        default:
            return false;
        };
    }

    //! \brief Returns whether it is a client part
    bool isClientCPU() const
    {
        return !isServerCPU();
    }
    //! \brief Return TSC timer value in time units
    //! \param multiplier use 1 for seconds, 1000 for ms, 1000000 for mks, etc (default is 1000: ms)
    //! \param core core to read on-chip TSC value (default is 0)
    //! \return time counter value
    uint64 getTickCount(uint64 multiplier = 1000 /* ms */, uint32 core = 0);

    //! \brief Return TSC timer value in time units using rdtscp instruction from current core
    //! \param multiplier use 1 for seconds, 1000 for ms, 1000000 for mks, etc (default is 1000: ms)
    //! \warning Processor support is required  bit 27 of cpuid EDX must be set, for Windows, Visual Studio 2010 is required
    //! \return time counter value
    uint64 getTickCountRDTSCP(uint64 multiplier = 1000 /* ms */);

    //! \brief Returns uncore clock ticks on specified socket
    uint64 getUncoreClocks(const uint32 socket_);

    //! \brief Return QPI Link Speed in GBytes/second
    //! \warning Works only for Nehalem-EX (Xeon 7500) and Xeon E7 and E5 processors
    //! \return QPI Link Speed in GBytes/second
    uint64 getQPILinkSpeed(uint32 socketNr, uint32 linkNr) const
    {
        return hasPCICFGUncore() ? server_pcicfg_uncore[socketNr]->getQPILinkSpeed(linkNr) : max_qpi_speed;
    }

    //! \brief Returns how many joules are in an internal processor energy unit
    double getJoulesPerEnergyUnit() const { return joulesPerEnergyUnit; }

    //! \brief Returns thermal specification power of the package domain in Watt
    int32 getPackageThermalSpecPower() const { return pkgThermalSpecPower; }

    //! \brief Returns minimum power derived from electrical spec of the package domain in Watt
    int32 getPackageMinimumPower() const { return pkgMinimumPower; }

    //! \brief Returns maximum power derived from electrical spec of the package domain in Watt
    int32 getPackageMaximumPower() const { return pkgMaximumPower; }

    #ifndef NO_WINRING // In cases where loading the WinRing0 driver is not desirable as a fallback to MSR.sys, add -DNO_WINRING to compile command to remove ability to load driver
    //! \brief Loads and initializes Winring0 third party library for access to processor model specific and PCI configuration registers
    //! \return returns true in case of success
    static bool initWinRing0Lib();
    #endif // NO_WINRING

    inline void disableJKTWorkaround() { disable_JKT_workaround = true; }

    enum PCIeEventCode
    {
        // PCIe read events (PCI devices reading from memory - application writes to disk/network/PCIe device)
        PCIeRdCur = 0x19E, // PCIe read current (full cache line)
        PCIeNSRd = 0x1E4,  // PCIe non-snoop read (full cache line)
        // PCIe write events (PCI devices writing to memory - application reads from disk/network/PCIe device)
        PCIeWiLF = 0x194,  // PCIe Write (non-allocating) (full cache line)
        PCIeItoM = 0x19C,  // PCIe Write (allocating) (full cache line)
        PCIeNSWr = 0x1E5,  // PCIe Non-snoop write (partial cache line)
        PCIeNSWrF = 0x1E6, // PCIe Non-snoop write (full cache line)
        // events shared by CPU and IO
        RFO = 0x180,       // Demand Data RFO; share the same code for CPU, use tid to filter PCIe only traffic
        CRd = 0x181,       // Demand Code Read
        DRd = 0x182,       // Demand Data Read
        PRd = 0x187,       // Partial Reads (UC) (MMIO Read)
        WiL = 0x18F,       // Write Invalidate Line - partial (MMIO write), PL: Not documented in HSX/IVT
        ItoM = 0x1C8,      // Request Invalidate Line; share the same code for CPU, use tid to filter PCIe only traffic

        SKX_RFO = 0x200,
        SKX_CRd = 0x201,
        SKX_DRd = 0x202,
        SKX_PRd = 0x207,
        SKX_WiL = 0x20F,
        SKX_RdCur = 0x21E,
        SKX_ItoM = 0x248,
    };

    enum ChaPipelineQueue
    {
        None,
        IRQ,
        PRQ,
    };

    enum CBoEventTid
    {
        RFOtid = 0x3E,
        ItoMtid = 0x3E,
    };

    //! \brief Program uncore PCIe monitoring event(s)
    //! \param eventGroup - events to programm for the same run
    void programPCIeEventGroup(eventGroup_t &eventGroup);
    uint64 getPCIeCounterData(const uint32 socket_, const uint32 ctr_);

    //! \brief Program CBO (or CHA on SKX+) counters
    //! \param events array with four raw event values
    //! \param opCode opcode match filter
    //! \param nc_ match non-coherent requests
    //! \param llc_lookup_tid_filter filter for LLC lookup event filter and TID filter (core and thread ID)
    //! \param loc match on local node target
    //! \param rem match on remote node target
    void programCbo(const uint64 * events, const uint32 opCode, const uint32 nc_ = 0, const uint32 llc_lookup_tid_filter = 0, const uint32 loc = 1, const uint32 rem = 1);

    //! \brief Program CBO (or CHA on SKX+) counters
    //! \param events array with four raw event values
    //! \param filter0 raw filter value
    //! \param filter1 raw filter1 value
    void programCboRaw(const uint64* events, const uint64 filter0, const uint64 filter1);

    //! \brief Get the state of PCIe counter(s)
    //! \param socket_ socket of the PCIe controller
    //! \return State of PCIe counter(s)
    PCIeCounterState getPCIeCounterState(const uint32 socket_);

    //! \brief Program uncore IIO events
    //! \param rawEvents events to program (raw format)
    //! \param IIOStack id of the IIO stack to program (-1 for all, if parameter omitted)
    void programIIOCounters(IIOPMUCNTCTLRegister rawEvents[4], int IIOStack = -1);

    //! \brief Get the state of IIO counter
    //! \param socket socket of the IIO stack
    //! \param IIOStack id of the IIO stack
    //! \return State of IIO counter
    IIOCounterState getIIOCounterState(int socket, int IIOStack, int counter);

    //! \brief Get the states of the four IIO counters in bulk (faster than four single reads)
    //! \param socket socket of the IIO stack
    //! \param IIOStack id of the IIO stack
    //! \param result states of IIO counters (array of four IIOCounterState elements)
    void getIIOCounterStates(int socket, int IIOStack, IIOCounterState * result);

    uint64 extractCoreGenCounterValue(uint64 val);
    uint64 extractCoreFixedCounterValue(uint64 val);
    uint64 extractUncoreGenCounterValue(uint64 val);
    uint64 extractUncoreFixedCounterValue(uint64 val);
    uint64 extractQOSMonitoring(uint64 val);

    //! \brief Get a string describing the codename of the processor microarchitecture
    //! \param cpu_model_ cpu model (if no parameter provided the codename of the detected CPU is returned)
    const char * getUArchCodename(const int32 cpu_model_ = -1) const;

    //! \brief Get Brand string of processor
    static std::string getCPUBrandString();
    std::string getCPUFamilyModelString();


    //! \brief Enables "force all RTM transaction abort" mode also enabling 4+ programmable counters on Skylake generation processors
    void enableForceRTMAbortMode();

    //! \brief queries status of "force all RTM transaction abort" mode
    bool isForceRTMAbortModeEnabled() const;

    //! \brief Disables "force all RTM transaction abort" mode restricting the number of programmable counters on Skylake generation processors to 3
    void disableForceRTMAbortMode();

    //! \brief queries availability of "force all RTM transaction abort" mode
    bool isForceRTMAbortModeAvailable() const;

    //! \brief Get microcode level (returns -1 if retrieval not supported due to some restrictions)
    int64 getCPUMicrocodeLevel() const { return cpu_microcode_level; }

    //! \brief returns true if CPU model is Atom-based
    static bool isAtom(const int32 cpu_model_)
    {
        return cpu_model_ == ATOM
            || cpu_model_ == ATOM_2
            || cpu_model_ == CENTERTON
            || cpu_model_ == BAYTRAIL
            || cpu_model_ == AVOTON
            || cpu_model_ == CHERRYTRAIL
            || cpu_model_ == APOLLO_LAKE
            || cpu_model_ == DENVERTON
            ;
    }

    //! \brief returns true if CPU is Atom-based
    bool isAtom() const
    {
        return isAtom(cpu_model);
    }

    bool packageEnergyMetricsAvailable() const
    {
        return (
                    cpu_model == PCM::JAKETOWN
                 || cpu_model == PCM::IVYTOWN
                 || cpu_model == PCM::SANDY_BRIDGE
                 || cpu_model == PCM::IVY_BRIDGE
                 || cpu_model == PCM::HASWELL
                 || cpu_model == PCM::AVOTON
                 || cpu_model == PCM::CHERRYTRAIL
                 || cpu_model == PCM::BAYTRAIL
                 || cpu_model == PCM::APOLLO_LAKE
                 || cpu_model == PCM::DENVERTON
                 || cpu_model == PCM::HASWELLX
                 || cpu_model == PCM::BROADWELL
                 || cpu_model == PCM::BDX_DE
                 || cpu_model == PCM::BDX
                 || cpu_model == PCM::KNL
                 || useSKLPath()
                 || cpu_model == PCM::SKX
               );
    }

    bool dramEnergyMetricsAvailable() const
    {
        return (
             cpu_model == PCM::JAKETOWN
          || cpu_model == PCM::IVYTOWN
          || cpu_model == PCM::HASWELLX
          || cpu_model == PCM::BDX_DE
          || cpu_model == PCM::BDX
          || cpu_model == PCM::KNL
          || cpu_model == PCM::SKX
          );
    }

    bool packageThermalMetricsAvailable() const
    {
        return packageEnergyMetricsAvailable();
    }

    bool outgoingQPITrafficMetricsAvailable() const
    {
        return getQPILinksPerSocket() > 0 &&
            (
                cpu_model == PCM::NEHALEM_EX
            ||  cpu_model == PCM::WESTMERE_EX
            ||  cpu_model == PCM::JAKETOWN
            ||  cpu_model == PCM::IVYTOWN
            ||  cpu_model == PCM::HASWELLX
            ||  cpu_model == PCM::BDX
            ||  cpu_model == PCM::SKX
            );
    }

    bool incomingQPITrafficMetricsAvailable() const
    {
        return getQPILinksPerSocket() > 0 &&
            (
                cpu_model == PCM::NEHALEM_EX
            ||  cpu_model == PCM::WESTMERE_EX
            ||  cpu_model == PCM::JAKETOWN
            ||  cpu_model == PCM::IVYTOWN
            || (cpu_model == PCM::SKX && cpu_stepping > 1)
               );
    }

    bool localMemoryRequestRatioMetricAvailable() const
    {
        return cpu_model == PCM::HASWELLX
            || cpu_model == PCM::BDX
            || cpu_model == PCM::SKX
            ;
    }

    bool qpiUtilizationMetricsAvailable() const
    {
        return outgoingQPITrafficMetricsAvailable();
    }

    bool memoryTrafficMetricsAvailable() const
    {
        return !(
            isAtom()
            || cpu_model == PCM::CLARKDALE
            );
    }

    bool MCDRAMmemoryTrafficMetricsAvailable() const
    {
        return (cpu_model == PCM::KNL);
    }

    bool memoryIOTrafficMetricAvailable() const
    {
        return (
            cpu_model == PCM::SANDY_BRIDGE
            || cpu_model == PCM::IVY_BRIDGE
            || cpu_model == PCM::HASWELL
            || cpu_model == PCM::BROADWELL
            || useSKLPath()
            );
    }

    bool IIOEventsAvailable() const
    {
        return (
            cpu_model == PCM::SKX
        );
    }

    bool LatencyMetricsAvailable() const
    {
        return (
            cpu_model == PCM::HASWELLX
            || cpu_model == PCM::BDX
            || cpu_model == PCM::SKX
            || useSKLPath()
            );
    }

    bool DDRLatencyMetricsAvailable() const
    {
        return (
            cpu_model == PCM::SKX
            );
    }

    bool PMMTrafficMetricsAvailable() const
    {
        return (
            isCLX()
                    ||  isCPX()
        );
    }

    bool LLCReadMissLatencyMetricsAvailable() const
    {
        return (
               HASWELLX == cpu_model
            || BDX_DE == cpu_model
            || BDX == cpu_model
            || isCLX()
            || isCPX()
#ifdef PCM_ENABLE_LLCRDLAT_SKX_MP
            || SKX == cpu_model
#else
            || ((SKX == cpu_model) && (num_sockets == 1))
#endif
               );
    }

    bool hasBecktonUncore() const
    {
        return (
            cpu_model == PCM::NEHALEM_EX
            || cpu_model == PCM::WESTMERE_EX
            );
    }
    bool hasPCICFGUncore() const // has PCICFG uncore PMON
    {
        return (
            cpu_model == PCM::JAKETOWN
            || cpu_model == PCM::IVYTOWN
            || cpu_model == PCM::HASWELLX
            || cpu_model == PCM::BDX_DE
          ||  cpu_model == PCM::SKX
            || cpu_model == PCM::BDX
            || cpu_model == PCM::KNL
            );
    }

    bool isSkxCompatible() const
    {
        return (
            cpu_model == PCM::SKX
               );
    }

    bool hasUPI() const // Intel(r) Ultra Path Interconnect
    {
        return (
            cpu_model == PCM::SKX
               );
    }

    const char * xPI() const
    {
        if (hasUPI())
            return "UPI";

        return "QPI";
    }

    bool hasCHA() const
    {
        return (
            cpu_model == PCM::SKX
               );
    }

    bool supportsHLE() const;
    bool supportsRTM() const;

    bool useSkylakeEvents() const
    {
        return    useSKLPath()
               || PCM::SKX == cpu_model
               ;
    }

    bool hasClientMCCounters() const
    {
        return  cpu_model == SANDY_BRIDGE
            || cpu_model == IVY_BRIDGE
            || cpu_model == HASWELL
            || cpu_model == BROADWELL
            || useSKLPath()
            ;
    }

    static double getBytesPerFlit(int32 cpu_model_)
    {
        if(cpu_model_ == PCM::SKX)
        {
            // 172 bits per UPI flit
            return 172./8.;
        }
        // 8 bytes per QPI flit
        return 8.;
    }

    double getBytesPerFlit() const
    {
        return getBytesPerFlit(cpu_model);
    }

    static double getDataBytesPerFlit(int32 cpu_model_)
    {
        if(cpu_model_ == PCM::SKX)
        {
            // 9 UPI flits to transfer 64 bytes
            return 64./9.;
        }
        // 8 bytes per QPI flit
        return 8.;
    }

    double getDataBytesPerFlit() const
    {
        return getDataBytesPerFlit(cpu_model);
    }

    static double getFlitsPerLinkCycle(int32 cpu_model_)
    {
        if(cpu_model_ == PCM::SKX)
        {
            // 5 UPI flits sent every 6 link cycles
            return 5./6.;
        }
        return 2.;
    }

    static double getBytesPerLinkCycle(int32 cpu_model_)
    {
        return getBytesPerFlit(cpu_model_) * getFlitsPerLinkCycle(cpu_model_);
    }

    double getBytesPerLinkCycle() const
    {
        return getBytesPerLinkCycle(cpu_model);
    }

    static double getLinkTransfersPerLinkCycle()
    {
        return 8.;
    }

    double getBytesPerLinkTransfer() const
    {
        return getBytesPerLinkCycle() / getLinkTransfersPerLinkCycle();
    }

    //! \brief Setup ExtendedCustomCoreEventDescription object to read offcore (numa) counters for each processor type
    //! \param conf conf object to setup offcore MSR values
    void setupCustomCoreEventsForNuma(PCM::ExtendedCustomCoreEventDescription& conf) const;

    #define PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(m) bool is##m() const { return m; }

    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L2CacheHitRatioAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L3CacheHitRatioAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L3CacheMissesAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L2CacheMissesAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L2CacheHitsAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L3CacheHitsNoSnoopAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L3CacheHitsSnoopAvailable)
    PCM_GENERATE_METRIC_AVAILABLE_FUNCTION(L3CacheHitsAvailable)

    #undef PCM_GEN_METRIC_AVAILABLE_FUNCTION

    bool isActiveRelativeFrequencyAvailable() const
    {
        return !isAtom();
    }

    ~PCM();
};

//! \brief Basic core counter state
//!
//! Intended only for derivation, but not for the direct use
class BasicCounterState
{
    friend class PCM;
    friend class JSONPrinter;
    template <class CounterStateType>
    friend double getExecUsage(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getIPC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getAverageFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getActiveAverageFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getRelativeFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getActiveRelativeFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getL2CacheHitRatio(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getL3CacheHitRatio(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheMisses(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL2CacheMisses(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL2CacheHits(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheHitsNoSnoop(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheHitsSnoop(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheHits(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheOccupancy(const CounterStateType & now);
    template <class CounterStateType>
    friend uint64 getLocalMemoryBW(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getRemoteMemoryBW(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getCycles(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getInstructionsRetired(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getCycles(const CounterStateType & now);
    template <class CounterStateType>
    friend uint64 getInstructionsRetired(const CounterStateType & now);
    template <class CounterStateType>
    friend uint64 getNumberOfCustomEvents(int32 eventCounterNr, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getInvariantTSC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getRefCycles(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getCoreCStateResidency(int state, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getCoreCStateResidency(int state, const CounterStateType& now);
    template <class CounterStateType>
    friend uint64 getSMICount(const CounterStateType & before, const CounterStateType & after);

protected:
    checked_uint64 InstRetiredAny;
    checked_uint64 CpuClkUnhaltedThread;
    checked_uint64 CpuClkUnhaltedRef;
    checked_uint64 Event[PERF_MAX_CUSTOM_COUNTERS];
    enum
    {
               L3MissPos = 0,
          ArchLLCMissPos = 0,
        L3UnsharedHitPos = 1,
           ArchLLCRefPos = 1,
             SKLL3HitPos = 1,
               L2HitMPos = 2,
            SKLL2MissPos = 2,
                L2HitPos = 3
    };
    uint64 InvariantTSC; // invariant time stamp counter
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    int32 ThermalHeadroom;
    uint64 L3Occupancy;
    uint64 MemoryBWLocal;
    uint64 MemoryBWTotal;
    uint64 SMICount;

public:
    BasicCounterState() :
        InvariantTSC(0),
        ThermalHeadroom(PCM_INVALID_THERMAL_HEADROOM),
        L3Occupancy(0),
        MemoryBWLocal(0),
        MemoryBWTotal(0),
        SMICount(0)
    {
        memset(CStateResidency, 0, sizeof(CStateResidency));
    }
    virtual ~BasicCounterState() { }

    BasicCounterState( const BasicCounterState& ) = default;
    BasicCounterState( BasicCounterState&& ) = default;
    BasicCounterState & operator = ( BasicCounterState&& ) = default;

    BasicCounterState & operator += (const BasicCounterState & o)
    {
        InstRetiredAny += o.InstRetiredAny;
        CpuClkUnhaltedThread += o.CpuClkUnhaltedThread;
        CpuClkUnhaltedRef += o.CpuClkUnhaltedRef;
        for (int i = 0; i < PERF_MAX_CUSTOM_COUNTERS; ++i)
        {
            Event[i] += o.Event[i];
        }
        InvariantTSC += o.InvariantTSC;
        for (int i = 0; i <= (int)PCM::MAX_C_STATE; ++i)
            CStateResidency[i] += o.CStateResidency[i];
        // ThermalHeadroom is not accumulative
        L3Occupancy += o.L3Occupancy;
        MemoryBWLocal += o.MemoryBWLocal;
        MemoryBWTotal += o.MemoryBWTotal;
        SMICount += o.SMICount;
        return *this;
    }

    void readAndAggregate(std::shared_ptr<SafeMsrHandle>);
    void readAndAggregateTSC(std::shared_ptr<SafeMsrHandle>);

    //! Returns current thermal headroom below TjMax
    int32 getThermalHeadroom() const { return ThermalHeadroom; }
};

inline uint64 RDTSC()
{
        uint64 result = 0;
#ifdef _MSC_VER
        // Windows
        #if _MSC_VER>= 1600
        result = static_cast<uint64>(__rdtsc());
        #endif
#else
        // Linux
        uint32 high = 0, low = 0;
        asm volatile("rdtsc" : "=a" (low), "=d" (high));
        result = low + (uint64(high)<<32ULL);
#endif
        return result;

}

inline uint64 RDTSCP()
{
    uint64 result = 0;
#ifdef _MSC_VER
    // Windows
    #if _MSC_VER>= 1600
    unsigned int Aux;
    result = __rdtscp(&Aux);
    #endif
#else
    // Linux and OS X
    uint32 high = 0, low = 0;
    asm volatile (
       "rdtscp\n\t"
       "mov %%edx, %0\n\t"
       "mov %%eax, %1\n\t":
       "=r" (high), "=r" (low) :: "%rax", "%rcx", "%rdx");
    result = low + (uint64(high)<<32ULL);
#endif
    return result;
}

template <class CounterStateType>
int32 getThermalHeadroom(const CounterStateType & /* before */, const CounterStateType & after)
{
    return after.getThermalHeadroom();
}

/*! \brief Returns the ratio of QPI cycles in power saving half-lane mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return 0..1 - ratio of QPI cycles in power saving half-lane mode
*/
template <class CounterStateType>
double getNormalizedQPIL0pTxCycles(uint32 port, const CounterStateType & before, const CounterStateType & after)
{
    return double(getQPIL0pTxCycles(port, before, after)) / double(getQPIClocks(port, before, after));
}

/*! \brief Returns the ratio of QPI cycles in power saving shutdown mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return 0..1 - ratio of QPI cycles in power saving shutdown mode
*/
template <class CounterStateType>
double getNormalizedQPIL1Cycles(uint32 port, const CounterStateType & before, const CounterStateType & after)
{
    return double(getQPIL1Cycles(port, before, after)) / double(getQPIClocks(port, before, after));
}

/*! \brief Returns DRAM clock ticks
    \param channel DRAM channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after)
{
    return after.DRAMClocks[channel] - before.DRAMClocks[channel];
}

/*! \brief Returns MCDRAM clock ticks
    \param channel MCDRAM channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getMCDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after)
{
    return after.MCDRAMClocks[channel] - before.MCDRAMClocks[channel];
}


/*! \brief Direct read of memory controller PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param channel channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getMCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    return after.MCCounter[channel][counter] - before.MCCounter[channel][counter];
}

/*! \brief Direct read of M3UPI PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param port UPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getM3UPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.M3UPICounter[port][counter] - before.M3UPICounter[port][counter];
}

/*! \brief Direct read of CHA or CBO PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param cbo cbo or cha number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getCBOCounter(uint32 cbo, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.CBOCounter[cbo][counter] - before.CBOCounter[cbo][counter];
}

/*! \brief Direct read of UBOX PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param cbo cbo or cha number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getUBOXCounter(uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.UBOXCounter[counter] - before.UBOXCounter[counter];
}

/*! \brief Direct read of IIO PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param cbo IIO stack number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getIIOCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.IIOCounter[stack][counter] - before.IIOCounter[stack][counter];
}

/*! \brief Direct read of UPI or QPI PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param port UPI/QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getXPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.xPICounter[port][counter] - before.xPICounter[port][counter];
}

/*! \brief Direct read of Memory2Mesh controller PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param controller controller number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getM2MCounter(uint32 controller, uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    return after.M2MCounter[controller][counter] - before.M2MCounter[controller][counter];
}


/*! \brief Direct read of embedded DRAM memory controller counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param channel channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getEDCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->MCDRAMmemoryTrafficMetricsAvailable())
        return after.EDCCounter[channel][counter] - before.EDCCounter[channel][counter];
    return 0ULL;
}

/*! \brief Direct read of power control unit PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getPCUCounter(uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    return after.PCUCounter[counter] - before.PCUCounter[counter];
}

/*!  \brief Returns clock ticks of power control unit
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getPCUClocks(const CounterStateType & before, const CounterStateType & after)
{
    return getPCUCounter(0, before, after);
}

/*!  \brief Returns energy consumed by processor, excluding DRAM (measured in internal units)
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getConsumedEnergy(const CounterStateType & before, const CounterStateType & after)
{
    return after.PackageEnergyStatus - before.PackageEnergyStatus;
}

/*!  \brief Returns energy consumed by DRAM (measured in internal units)
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getDRAMConsumedEnergy(const CounterStateType & before, const CounterStateType & after)
{
    return after.DRAMEnergyStatus - before.DRAMEnergyStatus;
}

/*!  \brief Returns uncore clock ticks
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getUncoreClocks(const CounterStateType& before, const CounterStateType& after)
{
    return after.UncClocks - before.UncClocks;
}

/*!  \brief Returns Joules consumed by processor (excluding DRAM)
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
double getConsumedJoules(const CounterStateType & before, const CounterStateType & after)
{
    PCM * m = PCM::getInstance();
    if (!m) return -1.;

    return double(getConsumedEnergy(before, after)) * m->getJoulesPerEnergyUnit();
}

/*!  \brief Returns Joules consumed by DRAM
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
double getDRAMConsumedJoules(const CounterStateType & before, const CounterStateType & after)
{
    PCM * m = PCM::getInstance();
    if (!m) return -1.;
    double dram_joules_per_energy_unit;

    if (PCM::HASWELLX == m->getCPUModel()
        || PCM::BDX_DE == m->getCPUModel()
        || PCM::BDX == m->getCPUModel()
        || PCM::SKX == m->getCPUModel()
        || PCM::KNL == m->getCPUModel()
        ) {
/* as described in sections 5.3.2 (DRAM_POWER_INFO) and 5.3.3 (DRAM_ENERGY_STATUS) of
 * Volume 2 (Registers) of
 * Intel Xeon E5-1600 v3 and Intel Xeon E5-2600 v3 (Haswell-EP) Datasheet (Ref 330784-001, Sept.2014)
 * ENERGY_UNIT for DRAM domain is fixed to 15.3 uJ for server HSX, BDW and KNL processors.
 */
        dram_joules_per_energy_unit = 0.0000153;
    } else {
/* for all other processors (including Haswell client/mobile SKUs) the ENERGY_UNIT for DRAM domain
 * should be read from PACKAGE_POWER_SKU register (usually value around ~61uJ)
 */
        dram_joules_per_energy_unit = m->getJoulesPerEnergyUnit();
    }
    return double(getDRAMConsumedEnergy(before, after)) * dram_joules_per_energy_unit;
}

//! \brief Basic uncore counter state
//!
//! Intended only for derivation, but not for the direct use
class UncoreCounterState
{
    friend class PCM;
    friend class JSONPrinter;
    template <class CounterStateType>
    friend uint64 getBytesReadFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesReadFromPMM(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToPMM(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesReadFromEDC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToEDC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getIORequestBytesFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getDRAMConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getUncoreClocks(const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend double getPackageCStateResidency(int state, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getPackageCStateResidency(int state, const CounterStateType& now);
    template <class CounterStateType>
    friend double getLLCReadMissLatency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getLocalMemoryRequestRatio(const CounterStateType & before, const CounterStateType & after);

protected:
    uint64 UncMCFullWrites;
    uint64 UncMCNormalReads;
    uint64 UncHARequests;
    uint64 UncHALocalRequests;
    uint64 UncPMMWrites;
    uint64 UncPMMReads;
    uint64 UncEDCFullWrites;
    uint64 UncEDCNormalReads;
    uint64 UncMCIORequests;
    uint64 PackageEnergyStatus;
    uint64 DRAMEnergyStatus;
    uint64 TOROccupancyIAMiss;
    uint64 TORInsertsIAMiss;
    uint64 UncClocks;
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    void readAndAggregate(std::shared_ptr<SafeMsrHandle>);

public:
    UncoreCounterState() :
        UncMCFullWrites(0),
        UncMCNormalReads(0),
        UncHARequests(0),
        UncHALocalRequests(0),
        UncPMMWrites(0),
        UncPMMReads(0),
        UncEDCFullWrites(0),
        UncEDCNormalReads(0),
        UncMCIORequests(0),
        PackageEnergyStatus(0),
        DRAMEnergyStatus(0),
        TOROccupancyIAMiss(0),
        TORInsertsIAMiss(0),
        UncClocks(0)
    {
        memset(CStateResidency, 0, sizeof(CStateResidency));
    }
    virtual ~UncoreCounterState() { }

    UncoreCounterState( const UncoreCounterState& ) = default;
    UncoreCounterState( UncoreCounterState&& ) = default;
    UncoreCounterState & operator = ( UncoreCounterState&& ) = default;

    UncoreCounterState & operator += (const UncoreCounterState & o)
    {
        UncMCFullWrites += o.UncMCFullWrites;
        UncMCNormalReads += o.UncMCNormalReads;
        UncHARequests += o.UncHARequests;
        UncHALocalRequests += o.UncHALocalRequests;
        UncPMMReads += o.UncPMMReads;
        UncPMMWrites += o.UncPMMWrites;
        UncEDCFullWrites += o.UncEDCFullWrites;
        UncEDCNormalReads += o.UncEDCNormalReads;
        UncMCIORequests += o.UncMCIORequests;
        PackageEnergyStatus += o.PackageEnergyStatus;
        DRAMEnergyStatus += o.DRAMEnergyStatus;
        TOROccupancyIAMiss += o.TOROccupancyIAMiss;
        TORInsertsIAMiss += o.TORInsertsIAMiss;
        UncClocks += o.UncClocks;
        for (int i = 0; i <= (int)PCM::MAX_C_STATE; ++i)
            CStateResidency[i] += o.CStateResidency[i];
        return *this;
    }
};


//! \brief Server uncore power counter state
//!
class ServerUncoreCounterState : public UncoreCounterState
{
public:
    enum {
        maxControllers = 2,
        maxChannels = 8,
        maxXPILinks = 6,
        maxCBOs = 128,
        maxIIOStacks = 16,
        maxCounters = 4
    };
    enum EventPosition
    {
        xPI_TxL0P_POWER_CYCLES = 0,
        xPI_L1_POWER_CYCLES = 2,
        xPI_CLOCKTICKS = 3
    };
private:
    std::array<std::array<uint64, maxCounters>, maxXPILinks> xPICounter;
    std::array<std::array<uint64, maxCounters>, maxXPILinks> M3UPICounter;
    std::array<std::array<uint64, maxCounters>, maxCBOs> CBOCounter;
    std::array<std::array<uint64, maxCounters>, maxIIOStacks> IIOCounter;
    std::array<uint64, maxCounters> UBOXCounter;
    std::array<uint64, maxChannels> DRAMClocks;
    std::array<uint64, maxChannels> MCDRAMClocks;
    std::array<std::array<uint64, maxCounters>, maxChannels> MCCounter; // channel X counter
    std::array<std::array<uint64, maxCounters>, maxControllers> M2MCounter; // M2M/iMC boxes x counter
    std::array<std::array<uint64, maxCounters>, maxChannels> EDCCounter; // EDC controller X counter
    std::array<uint64, maxCounters> PCUCounter;
    int32 PackageThermalHeadroom;
    uint64 InvariantTSC;    // invariant time stamp counter
    friend class PCM;
    template <class CounterStateType>
    friend uint64 getDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMCDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getM3UPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getCBOCounter(uint32 cbo, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getUBOXCounter(uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getIIOCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getXPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getM2MCounter(uint32 controller, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getEDCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getPCUCounter(uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getDRAMConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getInvariantTSC(const CounterStateType & before, const CounterStateType & after);

public:
    //! Returns current thermal headroom below TjMax
    int32 getPackageThermalHeadroom() const { return PackageThermalHeadroom; }
    ServerUncoreCounterState() :
        xPICounter{},
        M3UPICounter{},
        CBOCounter{},
        IIOCounter{},
        UBOXCounter{},
        DRAMClocks{},
        MCDRAMClocks{},
        MCCounter{},
        M2MCounter{},
        EDCCounter{},
        PCUCounter{},
        PackageThermalHeadroom(0),
        InvariantTSC(0)
    {
    }
};

/*! \brief Returns QPI LL clock ticks
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIClocks(uint32 port, const CounterStateType& before, const CounterStateType& after)
{
    return getXPICounter(port, ServerUncoreCounterState::EventPosition::xPI_CLOCKTICKS, before, after);
}

/*! \brief Returns the number of QPI cycles in power saving half-lane mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIL0pTxCycles(uint32 port, const CounterStateType& before, const CounterStateType& after)
{
    return getXPICounter(port, ServerUncoreCounterState::EventPosition::xPI_TxL0P_POWER_CYCLES, before, after);
}

/*! \brief Returns the number of QPI cycles in power saving shutdown mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIL1Cycles(uint32 port, const CounterStateType& before, const CounterStateType& after)
{
    return getXPICounter(port, ServerUncoreCounterState::EventPosition::xPI_L1_POWER_CYCLES, before, after);
}

//! \brief (Logical) core-wide counter state
class CoreCounterState : public BasicCounterState
{
    friend class PCM;

public:
    CoreCounterState() = default;
    CoreCounterState( const CoreCounterState& ) = default;
    CoreCounterState( CoreCounterState&& ) = default;
    CoreCounterState & operator= ( CoreCounterState&& ) = default;
};

//! \brief Socket-wide counter state
class SocketCounterState : public BasicCounterState, public UncoreCounterState
{
    friend class PCM;

protected:
    void readAndAggregate(std::shared_ptr<SafeMsrHandle> handle)
    {
        BasicCounterState::readAndAggregate(handle);
        UncoreCounterState::readAndAggregate(handle);
    }

public:
    SocketCounterState& operator += ( const BasicCounterState& ccs )
    {
        BasicCounterState::operator += ( ccs );

        return *this;
    }

    SocketCounterState& operator += ( const UncoreCounterState& ucs )
    {
        UncoreCounterState::operator += ( ucs );

        return *this;
    }

    SocketCounterState() = default;
    SocketCounterState( const SocketCounterState& ) = default;
    SocketCounterState( SocketCounterState&& ) = default;
    SocketCounterState & operator = ( SocketCounterState&& ) = default;

    SocketCounterState & operator = ( UncoreCounterState&& ucs ) {
        UncoreCounterState::operator = ( std::move(ucs) );
        return *this;
    }
};

//! \brief System-wide counter state
class SystemCounterState : public SocketCounterState
{
    friend class PCM;

    std::vector<std::vector<uint64> > incomingQPIPackets; // each 64 byte
    std::vector<std::vector<uint64> > outgoingQPIFlits; // idle or data/non-data flits depending on the architecture
    std::vector<std::vector<uint64> > TxL0Cycles;
    uint64 uncoreTSC;

protected:
    void readAndAggregate(std::shared_ptr<SafeMsrHandle> handle)
    {
        BasicCounterState::readAndAggregate(handle);
        UncoreCounterState::readAndAggregate(handle);
    }

public:
    friend uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & now);
    friend double getOutgoingQPILinkUtilization(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getOutgoingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getOutgoingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & now);

    SystemCounterState() :
        uncoreTSC(0)
    {
        PCM * m = PCM::getInstance();
        incomingQPIPackets.resize(m->getNumSockets(),
                                  std::vector<uint64>((uint32)m->getQPILinksPerSocket(), 0));
        outgoingQPIFlits.resize(m->getNumSockets(),
                                    std::vector<uint64>((uint32)m->getQPILinksPerSocket(), 0));
        TxL0Cycles.resize(m->getNumSockets(),
                                    std::vector<uint64>((uint32)m->getQPILinksPerSocket(), 0));
    }

    SystemCounterState( const SystemCounterState& ) = default;
    SystemCounterState( SystemCounterState&& ) = default;
    SystemCounterState & operator = ( SystemCounterState&& ) = default;

    SystemCounterState & operator += ( const SocketCounterState& scs )
    {
        BasicCounterState::operator += ( scs );
        UncoreCounterState::operator += ( scs );

        return *this;
    }

    SystemCounterState & operator += ( const UncoreCounterState& ucs )
    {
        UncoreCounterState::operator += ( ucs );

        return *this;
    }
};

/*! \brief Reads the counter state of the system

        Helper function. Uses PCM object to access counters.

        System consists of several sockets (CPUs).
        Socket has a CPU in it. Socket (CPU) consists of several (logical) cores.

        \return State of counters in the entire system
*/
PCM_API SystemCounterState getSystemCounterState();

/*! \brief Reads the counter state of a socket

        Helper function. Uses PCM object to access counters.

        \param socket socket id
        \return State of counters in the socket
*/
PCM_API SocketCounterState getSocketCounterState(uint32 socket);

/*! \brief Reads the counter state of a (logical) core

    Helper function. Uses PCM object to access counters.

    \param core core id
    \return State of counters in the core
*/
PCM_API CoreCounterState getCoreCounterState(uint32 core);


/*! \brief Computes average number of retired instructions per core cycle (IPC)

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return IPC
*/
template <class CounterStateType>
double getIPC(const CounterStateType & before, const CounterStateType & after) // instructions per cycle
{
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    if (clocks != 0)
        return double(after.InstRetiredAny - before.InstRetiredAny) / double(clocks);
    return -1;
}


/*! \brief Computes the number of retired instructions

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return number of retired instructions
*/
template <class CounterStateType>
uint64 getInstructionsRetired(const CounterStateType & before, const CounterStateType & after) // instructions
{
    return after.InstRetiredAny - before.InstRetiredAny;
}

/*! \brief Computes average number of retired instructions per time intervall

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return usage
*/
template <class CounterStateType>
double getExecUsage(const CounterStateType & before, const CounterStateType & after) // usage
{
    int64 timer_clocks = after.InvariantTSC - before.InvariantTSC;
    if (timer_clocks != 0)
        return double(after.InstRetiredAny - before.InstRetiredAny) / double(timer_clocks);
    return -1;
}

/*! \brief Computes the number of retired instructions

    \param now Current CPU counter state
    \return number of retired instructions
*/
template <class CounterStateType>
uint64 getInstructionsRetired(const CounterStateType & now) // instructions
{
    return now.InstRetiredAny.getRawData_NoOverflowProtection();
}

/*! \brief Computes the number core clock cycles when signal on a specific core is running (not halted)

    Returns number of used cycles (halted cyles are not counted).
    The counter does not advance in the following conditions:
    - an ACPI C-state is other than C0 for normal operation
    - HLT
    - STPCLK+ pin is asserted
    - being throttled by TM1
    - during the frequency switching phase of a performance state transition

    The performance counter for this event counts across performance state
    transitions using different core clock frequencies

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return number core clock cycles
*/
template <class CounterStateType>
uint64 getCycles(const CounterStateType & before, const CounterStateType & after) // clocks
{
    return after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
}

/*! \brief Computes the number of reference clock cycles while clock signal on the core is running

    The reference clock operates at a fixed frequency, irrespective of core
    frequency changes due to performance state transitions. See Intel(r) Software
    Developer's Manual for more details

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return number core clock cycles
*/
template <class CounterStateType>
uint64 getRefCycles(const CounterStateType & before, const CounterStateType & after) // clocks
{
    return after.CpuClkUnhaltedRef - before.CpuClkUnhaltedRef;
}

/*! \brief Computes the number executed core clock cycles

    Returns number of used cycles (halted cyles are not counted).

    \param now Current CPU counter state
    \return number core clock cycles
*/
template <class CounterStateType>
uint64 getCycles(const CounterStateType & now) // clocks
{
    return now.CpuClkUnhaltedThread.getRawData_NoOverflowProtection();
}

/*! \brief Computes average number of retired instructions per core cycle for the entire system combining instruction counts from logical cores to corresponding physical cores

        Use this metric to evaluate IPC improvement between SMT(Hyperthreading) on and SMT off.

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return IPC
*/
inline double getCoreIPC(const SystemCounterState & before, const SystemCounterState & after) // instructions per cycle
{
    double ipc = getIPC(before, after);
    PCM * m = PCM::getInstance();
    if (ipc >= 0. && m && (m->getNumCores() == m->getNumOnlineCores()))
        return ipc * double(m->getThreadsPerCore());
    return -1;
}


/*! \brief Computes average number of retired instructions per time intervall for the entire system combining instruction counts from logical cores to corresponding physical cores

        Use this metric to evaluate cores utilization improvement between SMT(Hyperthreading) on and SMT off.

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return usage
*/
inline double getTotalExecUsage(const SystemCounterState & before, const SystemCounterState & after) // usage
{
    double usage = getExecUsage(before, after);
    PCM * m = PCM::getInstance();
    if (usage >= 0. && m && (m->getNumCores() == m->getNumOnlineCores()))
        return usage * double(m->getThreadsPerCore());
    return -1;
}

/*! \brief Computes average core frequency also taking Intel Turbo Boost technology into account

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return frequency in Hz
*/
template <class CounterStateType>
double getAverageFrequency(const CounterStateType & before, const CounterStateType & after) // in Hz
{
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    int64 timer_clocks = after.InvariantTSC - before.InvariantTSC;
    PCM * m = PCM::getInstance();
    if (timer_clocks != 0 && m)
        return double(m->getNominalFrequency()) * double(clocks) / double(timer_clocks);
    return -1;
}

/*! \brief Computes average core frequency when not in powersaving C0-state (also taking Intel Turbo Boost technology into account)

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return frequency in Hz
*/
template <class CounterStateType>
double getActiveAverageFrequency(const CounterStateType & before, const CounterStateType & after) // in Hz
{
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    int64 ref_clocks = after.CpuClkUnhaltedRef - before.CpuClkUnhaltedRef;
    PCM * m = PCM::getInstance();
    if (ref_clocks != 0 && m)
        return double(m->getNominalFrequency()) * double(clocks) / double(ref_clocks);
    return -1;
}

/*! \brief Computes average core frequency also taking Intel Turbo Boost technology into account

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Fraction of nominal frequency
*/
template <class CounterStateType>
double getRelativeFrequency(const CounterStateType & before, const CounterStateType & after) // fraction of nominal frequency
{
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    int64 timer_clocks = after.InvariantTSC - before.InvariantTSC;
    if (timer_clocks != 0)
        return double(clocks) / double(timer_clocks);
    return -1;
}

/*! \brief Computes average core frequency when not in powersaving C0-state (also taking Intel Turbo Boost technology into account)

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Fraction of nominal frequency (if >1.0 then Turbo was working during the measurement)
*/
template <class CounterStateType>
double getActiveRelativeFrequency(const CounterStateType & before, const CounterStateType & after) // fraction of nominal frequency
{
    if (!PCM::getInstance()->isActiveRelativeFrequencyAvailable()) return -1.;
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    int64 ref_clocks = after.CpuClkUnhaltedRef - before.CpuClkUnhaltedRef;
    if (ref_clocks != 0)
        return double(clocks) / double(ref_clocks);
    return -1;
}

/*! \brief Computes L2 cache hit ratio

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return value between 0 and 1
*/
template <class CounterStateType>
double getL2CacheHitRatio(const CounterStateType& before, const CounterStateType& after) // 0.0 - 1.0
{
    if (!PCM::getInstance()->isL2CacheHitRatioAvailable()) return 0;
    const auto hits = getL2CacheHits(before, after);
    const auto misses = getL2CacheMisses(before, after);
    return double(hits) / double(hits + misses);
}

/*! \brief Computes L3 cache hit ratio

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return value between 0 and 1
*/
template <class CounterStateType>
double getL3CacheHitRatio(const CounterStateType& before, const CounterStateType& after) // 0.0 - 1.0
{
    if (!PCM::getInstance()->isL3CacheHitRatioAvailable()) return 0;
    const auto hits = getL3CacheHits(before, after);
    const auto misses = getL3CacheMisses(before, after);
    return double(hits) / double(hits + misses);
}

/*! \brief Computes number of L3 cache misses

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of misses
*/
template <class CounterStateType>
uint64 getL3CacheMisses(const CounterStateType & before, const CounterStateType & after)
{
    if (!PCM::getInstance()->isL3CacheMissesAvailable()) return 0;
    return after.Event[BasicCounterState::L3MissPos] - before.Event[BasicCounterState::L3MissPos];
}

/*! \brief Computes number of L2 cache misses

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of misses
*/
template <class CounterStateType>
uint64 getL2CacheMisses(const CounterStateType & before, const CounterStateType & after)
{
    auto pcm = PCM::getInstance();
    if (pcm->isL2CacheMissesAvailable() == false) return 0ULL;
    if (pcm->useSkylakeEvents()) {
        return after.Event[BasicCounterState::SKLL2MissPos] - before.Event[BasicCounterState::SKLL2MissPos];
    }
    if (pcm->isAtom() || pcm->getCPUModel() == PCM::KNL)
    {
        return after.Event[BasicCounterState::ArchLLCMissPos] - before.Event[BasicCounterState::ArchLLCMissPos];
    }
    uint64 L3Miss = after.Event[BasicCounterState::L3MissPos] - before.Event[BasicCounterState::L3MissPos];
    uint64 L3UnsharedHit = after.Event[BasicCounterState::L3UnsharedHitPos] - before.Event[BasicCounterState::L3UnsharedHitPos];
    uint64 L2HitM = after.Event[BasicCounterState::L2HitMPos] - before.Event[BasicCounterState::L2HitMPos];
    return L2HitM + L3UnsharedHit + L3Miss;
}

/*! \brief Computes number of L2 cache hits

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of hits
*/
template <class CounterStateType>
uint64 getL2CacheHits(const CounterStateType & before, const CounterStateType & after)
{
    auto pcm = PCM::getInstance();
    if (pcm->isL2CacheHitsAvailable() == false) return 0ULL;
    if (pcm->isAtom() || pcm->getCPUModel() == PCM::KNL)
    {
        uint64 L2Miss = after.Event[BasicCounterState::ArchLLCMissPos] - before.Event[BasicCounterState::ArchLLCMissPos];
        uint64 L2Ref = after.Event[BasicCounterState::ArchLLCRefPos] - before.Event[BasicCounterState::ArchLLCRefPos];
        return L2Ref - L2Miss;
    }
    return after.Event[BasicCounterState::L2HitPos] - before.Event[BasicCounterState::L2HitPos];
}

/*! \brief Computes L3 Cache Occupancy

*/
template <class CounterStateType>
uint64 getL3CacheOccupancy(const CounterStateType & now)
{
    if (PCM::getInstance()->L3CacheOccupancyMetricAvailable() == false) return 0ULL;
    return now.L3Occupancy;
}
/*! \brief Computes Local Memory Bandwidth

 */
template <class CounterStateType>
uint64 getLocalMemoryBW(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->CoreLocalMemoryBWMetricAvailable() == false) return 0ULL;
    return after.MemoryBWLocal - before.MemoryBWLocal;
}

/*! \brief Computes Remote Memory Bandwidth

 */
template <class CounterStateType>
uint64 getRemoteMemoryBW(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->CoreRemoteMemoryBWMetricAvailable() == false) return 0ULL;
    const uint64 total = after.MemoryBWTotal - before.MemoryBWTotal;
    const uint64 local = getLocalMemoryBW(before, after);
    if (total > local)
        return total - local;

    return 0;
}

/*! \brief Computes number of L3 cache hits where no snooping in sibling L2 caches had to be done

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of hits
*/
template <class CounterStateType>
uint64 getL3CacheHitsNoSnoop(const CounterStateType & before, const CounterStateType & after)
{
    if (!PCM::getInstance()->isL3CacheHitsNoSnoopAvailable()) return 0;
    return after.Event[BasicCounterState::L3UnsharedHitPos] - before.Event[BasicCounterState::L3UnsharedHitPos];
}

/*! \brief Computes number of L3 cache hits where snooping in sibling L2 caches had to be done

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of hits
*/
template <class CounterStateType>
uint64 getL3CacheHitsSnoop(const CounterStateType & before, const CounterStateType & after)
{
    if (!PCM::getInstance()->isL3CacheHitsSnoopAvailable()) return 0;
    if (PCM::getInstance()->useSkylakeEvents()) {
        return after.Event[BasicCounterState::SKLL3HitPos] - before.Event[BasicCounterState::SKLL3HitPos];
    }
    return after.Event[BasicCounterState::L2HitMPos] - before.Event[BasicCounterState::L2HitMPos];
}


/*! \brief Computes total number of L3 cache hits

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return number of hits
*/
template <class CounterStateType>
uint64 getL3CacheHits(const CounterStateType & before, const CounterStateType & after)
{
    if (!PCM::getInstance()->isL3CacheHitsAvailable()) return 0;
    return getL3CacheHitsSnoop(before, after) + getL3CacheHitsNoSnoop(before, after);
}

/*! \brief Computes number of invariant time stamp counter ticks

    This counter counts irrespectively of C-, P- or T-states

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return number of time stamp counter ticks
*/
template <class CounterStateType>
uint64 getInvariantTSC(const CounterStateType & before, const CounterStateType & after)
{
    return after.InvariantTSC - before.InvariantTSC;
}

/*! \brief Computes residency in the core C-state

    \param state C-state
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return residence ratio (0..1): 0 - 0%, 1.0 - 100%
*/
template <class CounterStateType>
inline double getCoreCStateResidency(int state, const CounterStateType & before, const CounterStateType & after)
{
    const double tsc = double(getInvariantTSC(before, after));

    if (state == 0) return double(getRefCycles(before, after)) / tsc;

    if (state == 1)
    {
        PCM * m = PCM::getInstance();
        double result = 1.0 - double(getRefCycles(before, after)) / tsc; // 1.0 - cC0
        for (int i = 2; i <= PCM::MAX_C_STATE; ++i)
            if (m->isCoreCStateResidencySupported(state))
                result -= (after.BasicCounterState::CStateResidency[i] - before.BasicCounterState::CStateResidency[i]) / tsc;

        if (result < 0.) result = 0.;       // fix counter dissynchronization
        else if (result > 1.) result = 1.;  // fix counter dissynchronization

        return result;
    }
    return (after.BasicCounterState::CStateResidency[state] - before.BasicCounterState::CStateResidency[state]) / tsc;
}

/*! \brief Reads raw residency counter for the core C-state

    \param state C-state #
    \param now CPU counter state
    \return raw residency value
*/
template <class CounterStateType>
inline uint64 getCoreCStateResidency(int state, const CounterStateType& now)
{
    if (state == 0) return now.CpuClkUnhaltedRef.getRawData_NoOverflowProtection();

    return now.BasicCounterState::CStateResidency[state];
}

/*! \brief Computes residency in the package C-state

    \param state C-state
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return residence ratio (0..1): 0 - 0%, 1.0 - 100%
*/
template <class CounterStateType>
inline double getPackageCStateResidency(int state, const CounterStateType & before, const CounterStateType & after)
{
    const double tsc = double(getInvariantTSC(before, after));
    if (state == 0)
    {
        PCM * m = PCM::getInstance();
        double result = 1.0;
        for (int i = 1; i <= PCM::MAX_C_STATE; ++i)
            if (m->isPackageCStateResidencySupported(state))
                result -= (after.UncoreCounterState::CStateResidency[i] - before.UncoreCounterState::CStateResidency[i]) / tsc;

        if (result < 0.) result = 0.;       // fix counter dissynchronization
        else if (result > 1.) result = 1.;  // fix counter dissynchronization

        return result;
    }
    return double(after.UncoreCounterState::CStateResidency[state] - before.UncoreCounterState::CStateResidency[state]) / tsc;
}

/*! \brief Reads raw residency counter for the package C-state

    \param state C-state #
    \param now CPU counter state
    \return raw residency value
*/
template <class CounterStateType>
inline uint64 getPackageCStateResidency(int state, const CounterStateType& now)
{
    return now.UncoreCounterState::CStateResidency[state];
}

/*! \brief Computes number of bytes read from DRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromMC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->memoryTrafficMetricsAvailable())
        return (after.UncMCNormalReads - before.UncMCNormalReads) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes written to DRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToMC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->memoryTrafficMetricsAvailable())
        return (after.UncMCFullWrites - before.UncMCFullWrites) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes read from PMM memory

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromPMM(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->PMMTrafficMetricsAvailable())
        return (after.UncPMMReads - before.UncPMMReads) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes written to PMM memory

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToPMM(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->PMMTrafficMetricsAvailable())
        return (after.UncPMMWrites - before.UncPMMWrites) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes read from MCDRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromEDC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->MCDRAMmemoryTrafficMetricsAvailable())
        return (after.UncEDCNormalReads - before.UncEDCNormalReads) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes written to MCDRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToEDC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->MCDRAMmemoryTrafficMetricsAvailable())
        return (after.UncEDCFullWrites - before.UncEDCFullWrites) * 64;
    return 0ULL;
}


/*! \brief Computes number of bytes of read/write requests from all IO sources

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getIORequestBytesFromMC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->memoryIOTrafficMetricAvailable())
        return (after.UncMCIORequests - before.UncMCIORequests) * 64;
    return 0ULL;
}

/*! \brief Returns the number of occured system management interrupts

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of SMIs (system manegement interrupts)
*/
template <class CounterStateType>
uint64 getSMICount(const CounterStateType & before, const CounterStateType & after)
{
    return after.SMICount - before.SMICount;
}

/*! \brief Returns the number of occured custom core events

    Read number of events programmed with the \c CUSTOM_CORE_EVENTS

    \param eventCounterNr Event/counter number (value from 0 to 3)
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getNumberOfCustomEvents(int32 eventCounterNr, const CounterStateType & before, const CounterStateType & after)
{
    return after.Event[eventCounterNr] - before.Event[eventCounterNr];
}

/*! \brief Get estimation of QPI data traffic per incoming QPI link

    Returns an estimation of number of data bytes transferred to a socket over Intel(r) Quick Path Interconnect

    \param socketNr socket identifier
    \param linkNr linkNr
    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Number of bytes
*/
inline uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after)
{
    if (!PCM::getInstance()->incomingQPITrafficMetricsAvailable()) return 0ULL;
    uint64 b = before.incomingQPIPackets[socketNr][linkNr];
    uint64 a = after.incomingQPIPackets[socketNr][linkNr];
    // prevent overflows due to counter dissynchronisation
    return (a > b) ? (64 * (a - b)) : 0;
}

/*! \brief Get data utilization of incoming QPI link (0..1)

    Returns an estimation of utilization of QPI link by data traffic transferred to a socket over Intel(r) Quick Path Interconnect

    \param socketNr socket identifier
    \param linkNr linkNr
    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return utilization (0..1)
*/
inline double getIncomingQPILinkUtilization(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after)
{
    PCM * m = PCM::getInstance();
    if (!(m->qpiUtilizationMetricsAvailable())) return 0.;

    const double bytes = (double)getIncomingQPILinkBytes(socketNr, linkNr, before, after);
    const uint64 max_speed = m->getQPILinkSpeed(socketNr, linkNr);
    const double max_bytes = (double)(double(max_speed) * double(getInvariantTSC(before, after) / double(m->getNumCores())) / double(m->getNominalFrequency()));
    return bytes / max_bytes;
}

/*! \brief Get utilization of outgoing QPI link (0..1)

    Returns an estimation of utilization of QPI link by (data+nondata) traffic transferred from a socket over Intel(r) Quick Path Interconnect

    \param socketNr socket identifier
    \param linkNr linkNr
    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return utilization (0..1)
*/
inline double getOutgoingQPILinkUtilization(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after)
{
    PCM * m = PCM::getInstance();

    if (m->outgoingQPITrafficMetricsAvailable() == false) return 0.;

    if (m->hasBecktonUncore())
    {
        const uint64 b = before.outgoingQPIFlits[socketNr][linkNr]; // idle flits
        const uint64 a = after.outgoingQPIFlits[socketNr][linkNr];  // idle flits
        // prevent overflows due to counter dissynchronisation
        const double idle_flits = (double)((a > b) ? (a - b) : 0);
        const uint64 bTSC = before.uncoreTSC;
        const uint64 aTSC = after.uncoreTSC;
        const double tsc = (double)((aTSC > bTSC) ? (aTSC - bTSC) : 0);
        if (idle_flits >= tsc) return 0.; // prevent oveflows due to potential counter dissynchronization

        return (1. - (idle_flits / tsc));
    } else if (m->hasPCICFGUncore())
    {
        const uint64 b = before.outgoingQPIFlits[socketNr][linkNr]; // data + non-data flits or idle (null) flits
        const uint64 a = after.outgoingQPIFlits[socketNr][linkNr]; // data + non-data flits or idle (null) flits
        // prevent overflows due to counter dissynchronisation
        double flits = (double)((a > b) ? (a - b) : 0);
        const double max_flits = ((double(getInvariantTSC(before, after)) * double(m->getQPILinkSpeed(socketNr, linkNr)) / m->getBytesPerFlit()) / double(m->getNominalFrequency())) / double(m->getNumCores());
        if(m->hasUPI())
        {
            flits = flits/3.;
        }
        if (flits > max_flits) return 1.; // prevent oveflows due to potential counter dissynchronization
        return (flits / max_flits);
    }

    return 0;
}

/*! \brief Get estimation of QPI (data+nondata) traffic per outgoing QPI link

    Returns an estimation of number of data bytes transferred from a socket over Intel(r) Quick Path Interconnect

    \param socketNr socket identifier
    \param linkNr linkNr
    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Number of bytes
*/
inline uint64 getOutgoingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after)
{
    PCM * m = PCM::getInstance();
    if (!(m->outgoingQPITrafficMetricsAvailable())) return 0ULL;

    const double util = getOutgoingQPILinkUtilization(socketNr, linkNr, before, after);
    const double max_bytes = (double(m->getQPILinkSpeed(socketNr, linkNr)) * double(getInvariantTSC(before, after) / double(m->getNumCores())) / double(m->getNominalFrequency()));

    return (uint64)(max_bytes * util);
}


/*! \brief Get estimation of total QPI data traffic

    Returns an estimation of number of data bytes transferred to all sockets over all Intel(r) Quick Path Interconnect links

    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Number of bytes
*/
inline uint64 getAllIncomingQPILinkBytes(const SystemCounterState & before, const SystemCounterState & after)
{
    PCM * m = PCM::getInstance();
    const uint32 ns = m->getNumSockets();
    const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
    uint64 sum = 0;

    for (uint32 s = 0; s < ns; ++s)
        for (uint32 q = 0; q < qpiLinks; ++q)
            sum += getIncomingQPILinkBytes(s, q, before, after);

    return sum;
}

/*! \brief Get estimation of total QPI data+nondata traffic

    Returns an estimation of number of data and non-data bytes transferred from all sockets over all Intel(r) Quick Path Interconnect links

    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Number of bytes
*/
inline uint64 getAllOutgoingQPILinkBytes(const SystemCounterState & before, const SystemCounterState & after)
{
    PCM * m = PCM::getInstance();
    const uint32 ns = m->getNumSockets();
    const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
    uint64 sum = 0;

    for (uint32 s = 0; s < ns; ++s)
        for (uint32 q = 0; q < qpiLinks; ++q)
            sum += getOutgoingQPILinkBytes(s, q, before, after);

    return sum;
}


/*! \brief Return current value of the counter of QPI data traffic per incoming QPI link

    Returns the number of incoming data bytes to a socket over Intel(r) Quick Path Interconnect

    \param socketNr socket identifier
    \param linkNr linkNr
    \param now Current System CPU counter state
    \return Number of bytes
*/
inline uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & now)
{
    if (PCM::getInstance()->incomingQPITrafficMetricsAvailable())
        return 64 * now.incomingQPIPackets[socketNr][linkNr];
    return 0ULL;
}

/*! \brief Get estimation of total QPI data traffic for this socket

    Returns an estimation of number of bytes transferred to this sockets over all Intel(r) Quick Path Interconnect links on this socket

    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Number of bytes
*/
inline uint64 getSocketIncomingQPILinkBytes(uint32 socketNr, const SystemCounterState & now)
{
    PCM * m = PCM::getInstance();
    const uint32 qpiLinks = (uint32)m->getQPILinksPerSocket();
    uint64 sum = 0;

    for (uint32 q = 0; q < qpiLinks; ++q)
        sum += getIncomingQPILinkBytes(socketNr, q, now);

    return sum;
}

/*! \brief Get estimation of Socket QPI data traffic

    Returns an estimation of number of data bytes transferred to all sockets over all Intel(r) Quick Path Interconnect links

    \param now System CPU counter state
    \return Number of bytes
*/
inline uint64 getAllIncomingQPILinkBytes(const SystemCounterState & now)
{
    PCM * m = PCM::getInstance();
    const uint32 ns = m->getNumSockets();
    uint64 sum = 0;

    for (uint32 s = 0; s < ns; ++s)
        sum += getSocketIncomingQPILinkBytes(s, now);
    return sum;
}


/*! \brief Get QPI data to Memory Controller traffic ratio

    Ideally for NUMA-optmized programs the ratio should be close to 0.

    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Ratio
*/

inline double getQPItoMCTrafficRatio(const SystemCounterState & before, const SystemCounterState & after)
{
    const uint64 totalQPI = getAllIncomingQPILinkBytes(before, after);
    uint64 memTraffic = getBytesReadFromMC(before, after) + getBytesWrittenToMC(before, after);
    if (PCM::getInstance()->PMMTrafficMetricsAvailable())
    {
        memTraffic += getBytesReadFromPMM(before, after) + getBytesWrittenToPMM(before, after);
    }
    return double(totalQPI) / double(memTraffic);
}

/*! \brief Get local memory access ration measured in home agent

    \param before System CPU counter state before the experiment
    \param after System CPU counter state after the experiment
    \return Ratio
*/
template <class CounterStateType>
inline double getLocalMemoryRequestRatio(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->localMemoryRequestRatioMetricAvailable() == false) return -1.;
    const auto all = after.UncHARequests - before.UncHARequests;
    const auto local = after.UncHALocalRequests - before.UncHALocalRequests;
    // std::cout << "DEBUG "<< 64*all/1e6 << " " << 64*local/1e6 << "\n";
    return double(local)/double(all);
}

//! \brief Returns the raw count of events
//! \param before counter state before the experiment
//! \param after counter state after the experiment
template <class CounterType>
inline uint64 getNumberOfEvents(const CounterType & before, const CounterType & after)
{
    return after.data - before.data;
}
//! \brief Returns average last level cache read+prefetch miss latency in ns

template <class CounterStateType>
inline double getLLCReadMissLatency(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->LLCReadMissLatencyMetricsAvailable() == false) return -1.;
    const double occupancy = double(after.TOROccupancyIAMiss) - double(before.TOROccupancyIAMiss);
    const double inserts = double(after.TORInsertsIAMiss) - double(before.TORInsertsIAMiss);
    const double unc_clocks = double(after.UncClocks) - double(before.UncClocks);
    auto * m = PCM::getInstance();
    const double seconds = double(getInvariantTSC(before, after)) / double(m->getNumCores()/m->getNumSockets()) / double(m->getNominalFrequency());
    return 1e9*seconds*(occupancy/inserts)/unc_clocks;
}

} // namespace pcm

#endif
