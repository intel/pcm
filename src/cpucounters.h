// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2024, Intel Corporation
// written by Roman Dementiev
//            Thomas Willhalm
//            and others

#ifndef CPUCOUNTERS_HEADER
#define CPUCOUNTERS_HEADER

/*!     \file cpucounters.h
        \brief Main CPU counters header

        Include this header file if you want to access CPU counters (core and uncore - including memory controller chips and QPI)
*/

#include "version.h"

#ifndef PCM_API
#define PCM_API
#endif

#undef PCM_HA_REQUESTS_READS_ONLY
#undef PCM_DEBUG_TOPOLOGY // debug of topology enumeration routine
#undef PCM_UNCORE_PMON_BOX_CHECK_STATUS // debug only

#include "types.h"
#include "topologyentry.h"
#include "msr.h"
#include "pci.h"
#include "tpmi.h"
#include "pmt.h"
#include "bw.h"
#include "width_extender.h"
#include "exceptions/unsupported_processor_exception.hpp"
#include "uncore_pmu_discovery.h"

#include <vector>
#include <array>
#include <limits>
#include <string>
#include <memory>
#include <map>
#include <unordered_map>
#include <string.h>
#include <assert.h>

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

#ifdef __linux__
#include "resctrl.h"
#endif

namespace pcm {

#ifdef _MSC_VER
void PCM_API restrictDriverAccess(LPCTSTR path);
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
        // std::cout << std::hex << "MMIORegister64 writing " << val << " at offset " << offset << std::dec << std::endl;
        handle->write64(offset, val);
    }
    operator uint64 () override
    {
        const uint64 val = handle->read64(offset);
        // std::cout << std::hex << "MMIORegister64 read " << val << " from offset " << offset << std::dec << std::endl;
        return val;
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
        // std::cout << std::hex << "MMIORegister32 writing " << val << " at offset " << offset << std::dec << std::endl;
        handle->write32(offset, (uint32)val);
    }
    operator uint64 () override
    {
        const uint64 val = (uint64)handle->read32(offset);
        // std::cout << std::hex << "MMIORegister32 read " << val << " from offset " << offset << std::dec << std::endl;
        return val;
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
        // std::cout << "reading MSR " << offset << " returning " << value << std::endl;
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

class UncorePMU
{
    typedef std::shared_ptr<HWRegister> HWRegisterPtr;
    uint32 cpu_family_model_;
    uint32 getCPUFamilyModel();
    HWRegisterPtr unitControl;
public:
    std::vector<HWRegisterPtr> counterControl;
    std::vector<HWRegisterPtr> counterValue;
    HWRegisterPtr fixedCounterControl;
    HWRegisterPtr fixedCounterValue;
    HWRegisterPtr filter[2];
    enum {
        maxCounters = 8
    };

    UncorePMU(const HWRegisterPtr& unitControl_,
        const HWRegisterPtr& counterControl0,
        const HWRegisterPtr& counterControl1,
        const HWRegisterPtr& counterControl2,
        const HWRegisterPtr& counterControl3,
        const HWRegisterPtr& counterValue0,
        const HWRegisterPtr& counterValue1,
        const HWRegisterPtr& counterValue2,
        const HWRegisterPtr& counterValue3,
        const HWRegisterPtr& fixedCounterControl_ = HWRegisterPtr(),
        const HWRegisterPtr& fixedCounterValue_ = HWRegisterPtr(),
        const HWRegisterPtr& filter0 = HWRegisterPtr(),
        const HWRegisterPtr& filter1 = HWRegisterPtr()
    );
    UncorePMU(const HWRegisterPtr& unitControl_,
        const std::vector<HWRegisterPtr> & counterControl_,
        const std::vector<HWRegisterPtr> & counterValue_,
        const HWRegisterPtr& fixedCounterControl_ = HWRegisterPtr(),
        const HWRegisterPtr& fixedCounterValue_ = HWRegisterPtr(),
        const HWRegisterPtr& filter0 = HWRegisterPtr(),
        const HWRegisterPtr& filter1 = HWRegisterPtr()
    );
    UncorePMU() : cpu_family_model_(0U) {}
    size_t size() const { return counterControl.size(); }
    virtual ~UncorePMU() {}
    bool valid() const
    {
        return unitControl.get() != nullptr;
    }
    void cleanup();
    void freeze(const uint32 extra);
    bool initFreeze(const uint32 extra, const char* xPICheckMsg = nullptr);
    void unfreeze(const uint32 extra);
    void resetUnfreeze(const uint32 extra);
};

typedef std::shared_ptr<UncorePMU> UncorePMURef;

class IDX_PMU
{
    typedef std::shared_ptr<HWRegister> HWRegisterPtr;
    uint32 cpu_family_model_;
    uint32 getCPUFamilyModel();
    bool perf_mode_;
    uint32 numa_node_;
    uint32 socket_id_;
    HWRegisterPtr resetControl;
    HWRegisterPtr freezeControl;
public:
    HWRegisterPtr generalControl;
    std::vector<HWRegisterPtr> counterControl;
    std::vector<HWRegisterPtr> counterValue;
    std::vector<HWRegisterPtr> counterFilterWQ;
    std::vector<HWRegisterPtr> counterFilterENG;
    std::vector<HWRegisterPtr> counterFilterTC;
    std::vector<HWRegisterPtr> counterFilterPGSZ;
    std::vector<HWRegisterPtr> counterFilterXFERSZ; 

    IDX_PMU(const bool perfMode_,
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
    );

    IDX_PMU() : cpu_family_model_(0U), perf_mode_(false), numa_node_(0), socket_id_(0) {}
    size_t size() const { return counterControl.size(); }
    virtual ~IDX_PMU() {}
    bool valid() const
    {
        return resetControl.get() != nullptr;
    }
    void cleanup();
    void freeze();
    bool initFreeze();
    void unfreeze();
    void resetUnfreeze();
    bool getPERFMode();
    uint32 getNumaNode() const;
    uint32 getSocketId() const;
};

enum ServerUncoreMemoryMetrics
{
    PartialWrites,
    Pmem,
    PmemMemoryMode,
    PmemMixedMode
};

//! Object to access uncore counters in a socket/processor with microarchitecture codename SandyBridge-EP (Jaketown) or Ivytown-EP or Ivytown-EX
class ServerUncorePMUs
{
    friend class PCM;
    int32 iMCbus,UPIbus,M2Mbus;
    uint32 groupnr;
    int32 cpu_family_model;
    typedef std::vector<UncorePMU> UncorePMUVector;
    UncorePMUVector imcPMUs;
    UncorePMUVector edcPMUs;
    UncorePMUVector xpiPMUs;
    UncorePMUVector m3upiPMUs;
    UncorePMUVector m2mPMUs;
    UncorePMUVector haPMUs;
    UncorePMUVector hbm_m2mPMUs;
    std::vector<UncorePMUVector*> allPMUs{ &imcPMUs, &edcPMUs, &xpiPMUs, &m3upiPMUs , &m2mPMUs, &haPMUs, &hbm_m2mPMUs };
    std::vector<uint64> qpi_speed;
    std::vector<uint32> num_imc_channels; // number of memory channels in each memory controller
    std::vector<std::pair<uint32, uint32> > XPIRegisterLocation; // (device, function)
    std::vector<std::pair<uint32, uint32> > M3UPIRegisterLocation; // (device, function)
    std::vector<std::vector< std::pair<uint32, uint32> > > MCRegisterLocation; // MCRegisterLocation[controller]: (device, function)
    std::vector<std::pair<uint32, uint32> > EDCRegisterLocation; // EDCRegisterLocation: (device, function)
    std::vector<std::pair<uint32, uint32> > M2MRegisterLocation; // M2MRegisterLocation: (device, function)
    std::vector<std::pair<uint32, uint32> > HARegisterLocation;  // HARegisterLocation: (device, function)
    std::vector<std::pair<uint32, uint32> > HBM_M2MRegisterLocation; // HBM_M2MRegisterLocation: (device, function)

    static std::vector<std::pair<uint32, uint32> > socket2iMCbus;
    static std::vector<std::pair<uint32, uint32> > socket2UPIbus;
    static std::vector<std::pair<uint32, uint32> > socket2M2Mbus;

    ServerUncorePMUs();                                         // forbidden
    ServerUncorePMUs(ServerUncorePMUs &);                     // forbidden
    ServerUncorePMUs & operator = (const ServerUncorePMUs &); // forbidden
    static PciHandleType * createIntelPerfMonDevice(uint32 groupnr, int32 bus, uint32 dev, uint32 func, bool checkVendor = false);
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
    void initDirect(uint32 socket_, const PCM * pcm);
    void initPerf(uint32 socket_, const PCM * pcm);
    void initBuses(uint32 socket_, const PCM * pcm);
    void initRegisterLocations(const PCM * pcm);
    uint64 getPMUCounter(std::vector<UncorePMU> & pmu, const uint32 id, const uint32 counter);
    bool HBMAvailable() const;

public:
    enum EventPosition {
        READ=0,
        WRITE=1,
        READ2=2,
        WRITE2=3,
        READ_RANK_A=0,
        WRITE_RANK_A=1,
        READ_RANK_B=2,
        WRITE_RANK_B=3,
        PARTIAL=2,
        PMM_READ=2,
        PMM_WRITE=3,
        MM_MISS_CLEAN=2,
        MM_MISS_DIRTY=3,
        NM_HIT=0,  // NM :  Near Memory (DRAM cache) in Memory Mode
        M2M_CLOCKTICKS=1
    };
    //! \brief Initialize access data structures
    //! \param socket_ socket id
    //! \param pcm pointer to PCM instance
    ServerUncorePMUs(uint32 socket_, const PCM * pcm);
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
    //! \brief Get the number of Near Memory Hits
    uint64 getNMHits();
    //! \brief Get the number of Near Memory Misses
    uint64 getNMMisses();
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

    ~ServerUncorePMUs();

    //! \brief Program power counters (disables programming performance counters)
    //! \param mc_profile memory controller measurement profile. See description of profiles in pcm-power.cpp
    void program_power_metrics(int mc_profile);

    //! \brief Program memory counters (disables programming performance counters)
    //! \param rankA count DIMM rank1 statistics (disables memory channel monitoring)
    //! \param rankB count DIMM rank2 statistics (disables memory channel monitoring)
    //! \brief metrics metric set (see the ServerUncoreMemoryMetrics enum)
    void programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, const int rankA = -1, const int rankB = -1);

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
    //! \brief Get number HBM channel cycles
    //! \param channel channel number
    uint64 getHBMClocks(uint32 channel);
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
    //! \brief Direct read of HA counter
    //! \param box box ID/number
    //! \param counter counter number
    uint64 getHACounter(uint32 box, uint32 counter);

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
    uint64 getRawData() const {return data;}
    virtual ~SimpleCounterState() { }
};

typedef SimpleCounterState PCIeCounterState;
typedef SimpleCounterState IIOCounterState;
typedef SimpleCounterState IDXCounterState;

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
    friend class ClientUncore;
    friend class PerfVirtualControlRegister;
    friend class Aggregator;
    friend class ServerUncorePMUs;
    PCM();     // forbidden to call directly because it is a singleton
    PCM(const PCM &) = delete;
    PCM & operator = (const PCM &) = delete;

    int32 cpu_family;
    int32 cpu_model_private;
    int32 cpu_family_model;
    bool hybrid = false;
    int32 cpu_stepping;
    int64 cpu_microcode_level;
    uint32 max_cpuid;
    int32 threads_per_core;
    int32 num_cores;
    int32 num_sockets;
    int32 num_phys_cores_per_socket;
    int32 num_online_cores;
    int32 num_online_sockets;
    uint32 accel;
    uint32 accel_counters_num_max;
    uint32 core_gen_counter_num_max;
    uint32 core_gen_counter_num_used;
    uint32 core_gen_counter_width;
    uint32 core_fixed_counter_num_max;
    uint32 core_fixed_counter_num_used;
    uint32 core_fixed_counter_width;
    uint64 core_global_ctrl_value{0ULL};
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
    enum UFS_TPMI
    {
        UFS_ID = 2,
        UFS_FABRIC_CLUSTER_OFFSET = 1,
        UFS_STATUS = 0
    };
    std::vector<std::vector<std::shared_ptr<TPMIHandle> > > UFSStatus;

    std::vector<TopologyEntry> topology;
    SystemRoot* systemTopology;
    std::string errorMessage;

    static PCM * instance;
    bool programmed_core_pmu{false};
    std::vector<std::shared_ptr<SafeMsrHandle> > MSR;
    std::vector<std::shared_ptr<ServerUncorePMUs> > serverUncorePMUs;

    typedef std::vector<UncorePMURef> UncorePMUArrayType;
public:
    enum UncorePMUIDs
    {
        CBO_PMU_ID,
        MDF_PMU_ID,
        PCU_PMU_ID,
        UBOX_PMU_ID,
        PCIE_GEN5x16_PMU_ID,
        PCIE_GEN5x8_PMU_ID,
        INVALID_PMU_ID
    };
private:
    std::unordered_map<std::string, int> strToUncorePMUID_ {
        {"pciex8", PCIE_GEN5x8_PMU_ID},
        {"pciex16", PCIE_GEN5x16_PMU_ID}
    };
public:
    UncorePMUIDs strToUncorePMUID(const std::string & type) const
    {
        const auto iter = strToUncorePMUID_.find(type);
        return (iter == strToUncorePMUID_.end()) ? INVALID_PMU_ID : (UncorePMUIDs)iter->second;
    }
    size_t getNumUFSDies() const
    {
        if (UFSStatus.empty()) return 0;

        return UFSStatus[0].size();
    }
private:
    typedef std::unordered_map<int, UncorePMUArrayType> UncorePMUMapType;
    // socket -> die -> pmu map -> pmu ref array
    std::vector< std::vector<UncorePMUMapType> > uncorePMUs;

    template <class F>
    void forAllUncorePMUs(F f)
    {
        for (auto& s : uncorePMUs)
        {
            for (auto& d : s)
            {
                for (auto& p : d)
                {
                    for (auto& e : p.second)
                    {
                        if (e.get())
                        {
                            f(*e);
                        }
                    }
                }
            }
        }
    }

    template <class F>
    void forAllUncorePMUs(const int pmu_id, F f)
    {
        for (auto& s : uncorePMUs)
        {
            for (auto& d : s)
            {
                for (auto& e : d[pmu_id])
                {
                    if (e.get())
                    {
                        f(*e);
                    }
                }
            }
        }
    }

    template <class F>
    void forAllUncorePMUs(const size_t socket_id, const int pmu_id, F f)
    {
        if (socket_id < uncorePMUs.size())
        {
            for (auto& d : uncorePMUs[socket_id])
            {
                for (auto& e : d[pmu_id])
                {
                    if (e.get())
                    {
                        f(*e);
                    }
                }
            }
        }
    }

    template <class T>
    void readUncoreCounterValues(T& result, const size_t socket) const
    {
        if (socket < uncorePMUs.size())
        {
            result.Counters.resize(uncorePMUs[socket].size());
            for (size_t die = 0; die < uncorePMUs[socket].size(); ++die)
            {
                TemporalThreadAffinity tempThreadAffinity(socketRefCore[socket]); // speedup trick for Linux

                for (auto pmuIter = uncorePMUs[socket][die].begin(); pmuIter != uncorePMUs[socket][die].end(); ++pmuIter)
                {
                    const auto & pmu_id = pmuIter->first;
                    result.Counters[die][pmu_id].resize(pmuIter->second.size());
                    for (size_t unit = 0; unit < pmuIter->second.size(); ++unit)
                    {
                        auto& pmu = pmuIter->second[unit];
                        for (size_t i = 0; pmu.get() != nullptr && i < pmu->size(); ++i)
                        {
                            // std::cerr << "s " << socket << " d " << die << " pmu " << pmu_id << " unit " << unit << " ctr " << i << "\n";
                            result.Counters[die][pmu_id][unit][i] = *(pmu->counterValue[i]);
                        }
                    }
                }
            }
        }
    }

    uint64 getUncoreCounterState(const int pmu_id, const size_t socket, const uint32 ctr) const;

    template <class F>
    void programUncorePMUs(const int pmu_id, F pmuFunc)
    {
        if (MSR.empty()) return;

        for (size_t socket = 0; socket < uncorePMUs.size(); ++socket)
        {
            for (size_t die = 0; die < uncorePMUs[socket].size(); ++die)
            {
                TemporalThreadAffinity tempThreadAffinity(socketRefCore[socket]); // speedup trick for Linux

                for (size_t unit = 0; unit < uncorePMUs[socket][die][pmu_id].size(); ++unit)
                {
                    auto& pmu = uncorePMUs[socket][die][pmu_id][unit];
                    if (pmu.get())
                    {
                        pmuFunc(*pmu);
                    }
                }
            }
        }
    }

    // TODO: gradually move other PMUs to the uncorePMUs structure
    std::vector<std::map<int32, UncorePMU> > iioPMUs;
    std::vector<std::map<int32, UncorePMU> > irpPMUs;
    std::vector<std::vector<IDX_PMU> > idxPMUs;

    double joulesPerEnergyUnit;
    std::vector<std::shared_ptr<CounterWidthExtender> > energy_status;
    std::vector<std::shared_ptr<CounterWidthExtender> > dram_energy_status;
    std::vector<std::shared_ptr<CounterWidthExtender> > pp_energy_status;
    std::shared_ptr<CounterWidthExtender> system_energy_status;
    std::vector<std::vector<std::pair<UncorePMU, UncorePMU>>> cxlPMUs; // socket X CXL ports X UNIT {0,1}

    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_local;
    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_total;
#ifdef __linux__
    Resctrl resctrl;
#endif
    bool useResctrl;

    std::shared_ptr<FreeRunningBWCounters> clientBW;
    std::shared_ptr<CounterWidthExtender> clientImcReads;
    std::shared_ptr<CounterWidthExtender> clientImcWrites;
    std::shared_ptr<CounterWidthExtender> clientGtRequests;
    std::shared_ptr<CounterWidthExtender> clientIaRequests;
    std::shared_ptr<CounterWidthExtender> clientIoRequests;

    std::vector<std::shared_ptr<ServerBW> > serverBW;

    std::shared_ptr<UncorePMUDiscovery> uncorePMUDiscovery;

    template <class F>
    void getPCICFGPMUsFromDiscovery(const unsigned int BoxType, const size_t s, F f) const;

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

    std::vector<uint64> FrontendBoundSlots, BadSpeculationSlots, BackendBoundSlots, RetiringSlots, AllSlotsRaw;
    std::vector<uint64> MemBoundSlots, FetchLatSlots, BrMispredSlots, HeavyOpsSlots;
    bool isFixedCounterSupported(unsigned c);
    bool vm = false;
    bool linux_arch_perfmon = false;

public:

    size_t getMaxNumOfUncorePMUs(const int pmu_id, const size_t socket = 0) const
    {
        size_t count = 0ULL;
        if (socket < uncorePMUs.size())
        {
            const auto & s = uncorePMUs[socket];
            for (auto& d : s)
            {
                const auto iter = d.find(pmu_id);
                if (iter != d.end())
                {
                    count += iter->second.size();
                }
            }
        }
        return count;
    }
    enum { MAX_PP = 1 }; // max power plane number on Intel architecture (client)
    enum { MAX_C_STATE = 10 }; // max C-state on Intel architecture

    //! \brief Returns true if the specified core C-state residency metric is supported
    bool isCoreCStateResidencySupported(int state) const
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

    //! \brief Redirects output destination to provided file, instead of std::cout and std::cerr (optional)
    static void setOutput(const std::string filename, const bool cerrToo = false);

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
        IIO_MCP1 = 5
    };

    // Offsets/enumeration of IIO stacks Skylake server.
    enum SkylakeIIOStacks {
        SKX_IIO_CBDMA_DMI   = 0,
        SKX_IIO_PCIe0       = 1,
        SKX_IIO_PCIe1       = 2,
        SKX_IIO_PCIe2       = 3,
        SKX_IIO_MCP0        = 4,
        SKX_IIO_MCP1        = 5,
        SKX_IIO_STACK_COUNT = 6
    };

     // Offsets/enumeration of IIO stacks for IceLake server.
    enum IcelakeIIOStacks {
        ICX_IIO_PCIe0       = 0,
        ICX_IIO_PCIe1       = 1,
        ICX_IIO_MCP0        = 2,
        ICX_IIO_PCIe2       = 3,
        ICX_IIO_PCIe3       = 4,
        ICX_IIO_CBDMA_DMI   = 5,
        ICX_IIO_STACK_COUNT = 6
    };

    // Offsets/enumeration of IIO stacks for IceLake server.
    enum SnowridgeIIOStacks {
        SNR_IIO_QAT         = 0,
        SNR_IIO_CBDMA_DMI   = 1,
        SNR_IIO_NIS         = 2,
        SNR_IIO_HQM         = 3,
        SNR_IIO_PCIe0       = 4,
        SNR_IIO_STACK_COUNT = 5
    };

    enum BDXIIOStacks {
        BDX_IIO_STACK_COUNT = 1
    };

    enum IDX_IP
    {
        IDX_IAA = 0,
        IDX_DSA,
        IDX_QAT,
        IDX_MAX
    };

    enum IDX_OPERATION
    {
        QAT_TLM_STOP = 0,
        QAT_TLM_START,
        QAT_TLM_REFRESH,
        QAT_TLM_MAX
    };

    enum IDX_STATE
    {
        IDX_STATE_OFF = 0,
        IDX_STATE_ON,
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
        int32 event_number = 0, umask_value = 0;
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
        EventSelectRegister * gpCounterHybridAtomCfg; // general purpose counters for Atom cores in hybrid processors
        uint64 OffcoreResponseMsrValue[2];
        uint64 LoadLatencyMsrValue, FrontendMsrValue;
        bool defaultUncoreProgramming{true};
        static uint64 invalidMsrValue() { return ~0ULL; }
        ExtendedCustomCoreEventDescription() : fixedCfg(NULL), nGPCounters(0), gpCounterCfg(nullptr), gpCounterHybridAtomCfg(nullptr), LoadLatencyMsrValue(invalidMsrValue()), FrontendMsrValue(invalidMsrValue())
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

    struct MSREventPosition
    {
        enum constants
        {
            index = 0,
            type = 1
        };
    };
    enum MSRType
    {
        Static = 0,
        Freerun = 1
    };

private:
    ProgramMode mode;
    CustomCoreEventDescription coreEventDesc[PERF_MAX_CUSTOM_COUNTERS];
    CustomCoreEventDescription hybridAtomEventDesc[PERF_MAX_CUSTOM_COUNTERS];

    std::vector<int32> socketRefCore;

    bool canUsePerf;
#ifdef PCM_USE_PERF
    typedef std::vector<std::vector<int> > PerfEventHandleContainer;
    PerfEventHandleContainer perfEventHandle;
    std::vector<PerfEventHandleContainer> perfEventTaskHandle;
    void readPerfData(uint32 core, std::vector<uint64> & data);
    void closePerfHandles(const bool silent = false);

    enum {
        PERF_INST_RETIRED_POS = 0,
        PERF_CPU_CLK_UNHALTED_THREAD_POS = 1,
        PERF_CPU_CLK_UNHALTED_REF_POS = 2,
        PERF_GEN_EVENT_0_POS = 3,
        PERF_GEN_EVENT_1_POS = 4,
        PERF_GEN_EVENT_2_POS = 5,
        PERF_GEN_EVENT_3_POS = 6,
        PERF_TOPDOWN_SLOTS_POS = PERF_GEN_EVENT_0_POS + PERF_MAX_CUSTOM_COUNTERS,
        PERF_TOPDOWN_FRONTEND_POS = PERF_TOPDOWN_SLOTS_POS + 1,
        PERF_TOPDOWN_BADSPEC_POS = PERF_TOPDOWN_SLOTS_POS + 2,
        PERF_TOPDOWN_BACKEND_POS = PERF_TOPDOWN_SLOTS_POS + 3,
        PERF_TOPDOWN_RETIRING_POS = PERF_TOPDOWN_SLOTS_POS + 4,
        PERF_TOPDOWN_MEM_BOUND_POS = PERF_TOPDOWN_SLOTS_POS + 5,
        PERF_TOPDOWN_FETCH_LAT_POS = PERF_TOPDOWN_SLOTS_POS + 6,
        PERF_TOPDOWN_BR_MISPRED_POS = PERF_TOPDOWN_SLOTS_POS + 7,
        PERF_TOPDOWN_HEAVY_OPS_POS = PERF_TOPDOWN_SLOTS_POS + 8
    };

    std::array<int, (PERF_TOPDOWN_HEAVY_OPS_POS + 1)> perfTopDownPos;

    enum {
        PERF_GROUP_LEADER_COUNTER = PERF_INST_RETIRED_POS,
        PERF_TOPDOWN_GROUP_LEADER_COUNTER = PERF_TOPDOWN_SLOTS_POS
    };
#endif
    static std::ofstream * outfile;       // output file stream
    static std::streambuf * backup_ofile; // backup of original output = cout
    static std::streambuf * backup_ofile_cerr; // backup of original output = cerr
    int run_state;                 // either running (1) or sleeping (0)

    bool needToRestoreNMIWatchdog;
    bool cleanupPEBS{false};

    std::vector<std::vector<EventSelectRegister> > lastProgrammedCustomCounters;
    uint32 checkCustomCoreProgramming(std::shared_ptr<SafeMsrHandle> msr);
    ErrorCode programCoreCounters(int core, const PCM::ProgramMode mode, const ExtendedCustomCoreEventDescription * pExtDesc,
        std::vector<EventSelectRegister> & programmedCustomCounters, const std::vector<int> & tids);

    bool PMUinUse();
    void cleanupPMU(const bool silent = false);
    void cleanupRDT(const bool silent = false);

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
public:
    struct RawPMUConfig;
    void programCXLCM();
    void programCXLDP();
    template <class CounterStateType>
    void readAndAggregateCXLCMCounters(CounterStateType & counterState);

private:
    template <class CounterStateType>
    void readMSRs(std::shared_ptr<SafeMsrHandle> msr, const RawPMUConfig & msrConfig, CounterStateType & result);
    void readQPICounters(SystemCounterState & counterState);
    void readPCICFGRegisters(SystemCounterState& result);
    void readMMIORegisters(SystemCounterState& result);
    void readPMTRegisters(SystemCounterState& result);
    void reportQPISpeed() const;
    void readCoreCounterConfig(const bool complainAboutMSR = false);
    void readCPUMicrocodeLevel();
    void globalFreezeUncoreCountersInternal(const unsigned long long int freeze);

    uint64 CX_MSR_PMON_CTRY(uint32 Cbo, uint32 Ctr) const;
    uint64 CX_MSR_PMON_BOX_FILTER(uint32 Cbo) const;
    uint64 CX_MSR_PMON_BOX_FILTER1(uint32 Cbo) const;
    uint64 CX_MSR_PMON_CTLY(uint32 Cbo, uint32 Ctl) const;
    uint64 CX_MSR_PMON_BOX_CTL(uint32 Cbo) const;
    uint32 getMaxNumOfCBoxesInternal() const;
    void programCboOpcodeFilter(const uint32 opc0, UncorePMU & pmu, const uint32 nc_, const uint32 opc1, const uint32 loc, const uint32 rem);
    void initLLCReadMissLatencyEvents(uint64 * events, uint32 & opCode);
    void initCHARequestEvents(uint64 * events);
    void programCbo();
    template <class Iterator>
    static void program(UncorePMU& pmu, const Iterator& eventsBegin, const Iterator& eventsEnd, const uint32 extra)
    {
        if (!eventsBegin) return;
        Iterator curEvent = eventsBegin;
        const auto cpu_family_model = PCM::getInstance()->getCPUFamilyModel();
        for (int c = 0; curEvent != eventsEnd && size_t(c) < pmu.size(); ++c, ++curEvent)
        {
            auto ctrl = pmu.counterControl[c];
            if (ctrl.get() != nullptr)
            {
                switch (cpu_family_model)
                {
                case SPR:
                case EMR:
                case GNR:
                case GRR:
                case SRF:
                    *ctrl = *curEvent;
                    break;
                default:
                    *ctrl = MC_CH_PCI_PMON_CTL_EN;
                    *ctrl = MC_CH_PCI_PMON_CTL_EN | *curEvent;
                }
            }
        }
        if (extra)
        {
            pmu.resetUnfreeze(extra);
        }
    }
    void programPCU(uint32 * events, const uint64 filter);
    void programUBOX(const uint64* events);
    void programCXLDP(const uint64* events);
    void programCXLCM(const uint64* events);
    void cleanupUncorePMUs(const bool silent = false);

    bool isCLX() const // Cascade Lake-SP
    {
        return (PCM::SKX == cpu_family_model) && (cpu_stepping > 4 && cpu_stepping < 8);
    }

    static bool isCPX(int cpu_family_model_, int cpu_stepping_) // Cooper Lake
    {
        return (PCM::SKX == cpu_family_model_) && (cpu_stepping_ >= 10);
    }

    bool isCPX() const
    {
        return isCPX(cpu_family_model, cpu_stepping);
    }

    void initUncorePMUsDirect();
    void initUncorePMUsPerf();
    bool isRDTDisabled() const;

#ifdef __linux__
    bool perfSupportsTopDown();
#endif

public:
    static bool isInitialized() { return instance != nullptr; }

    //! check if TMA level 1 metrics are supported
    bool isHWTMAL1Supported() const;

    //! check if TMA level 2 metrics are supported
    bool isHWTMAL2Supported() const
    {
        return isHWTMAL1Supported() &&
                (
                    SPR == cpu_family_model
                ||  EMR == cpu_family_model
                ||  GNR == cpu_family_model
                ||  GNR_D == cpu_family_model
                );
    }

    enum EventPosition
    {
        TOR_OCCUPANCY = 0,
        TOR_INSERTS = 1,
        REQUESTS_ALL = 2,
        REQUESTS_LOCAL = 3,
        CXL_TxC_MEM = 0,   // works only on counters 0-3
        CXL_TxC_CACHE = 1, // works only on counters 0-3
        CXL_RxC_MEM = 4,   // works only on counters 4-7
        CXL_RxC_CACHE = 5  // works only on counters 4-7
    };
    //! check if in secure boot mode
    bool isSecureBoot() const;

    //! true if Linux perf for uncore PMU programming should AND can be used internally
    bool useLinuxPerfForUncore() const;

    //! true if the CPU is hybrid
    bool isHybrid() const
    {
        return hybrid;
    }

    /*!
             \brief The system, sockets, uncores, cores and threads are structured like a tree

             \returns a reference to a const System object representing the root of the tree
     */
    SystemRoot const & getSystemTopology() const {
        return *systemTopology;
    }

    //! prints detailed system topology
    void printDetailedSystemTopology(const int detailLevel = 0);

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

    //! \brief Returns the number of IIO stacks per socket
    uint32 getMaxNumOfIIOStacks() const;

    /*! \brief Returns the number of IDX accel devs
        \param accel index of IDX accel
    */
    uint32 getNumOfIDXAccelDevs(int accel) const;

    //! \brief Returns the number of IDX counters
    uint32 getMaxNumOfIDXAccelCtrs(int accel) const;

    //! \brief Returns the numa node of IDX accel dev
    uint32 getNumaNodeOfIDXAccelDev(uint32 accel, uint32 dev) const;

    //! \brief Returns the socketid of IDX accel dev
    uint32 getCPUSocketIdOfIDXAccelDev(uint32 accel, uint32 dev) const;

    //! \brief Returns the platform support IDX accel dev or NOT
    bool supportIDXAccelDev() const;

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
        \param silent set to true to silence diagnostic messages
        \param pid restrict core metrics only to specified pid (process id)

                Call this method before you start using the performance counting routines.

        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode program(const ProgramMode mode_ = DEFAULT_EVENTS, const void * parameter_ = NULL, const bool silent = false, const int pid = -1); // program counters and start counting

    /*! \brief checks the error without side effects.
        \throw std::system_error generic_category exception with PCM error code.
        \param code error code from the 'program' call
    */
    void checkStatus(const ErrorCode status);

    /*! \brief checks the error and suggests solution and/or exits the process
        \param code error code from the 'program' call
    */
    void checkError(const ErrorCode code);

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

    /*  \brief Program memory counters (disables programming performance counters)
        \param rankA count DIMM rank1 statistics (disables memory channel monitoring)
        \param rankB count DIMM rank2 statistics (disables memory channel monitoring)
        \brief metrics metric set (see the ServerUncoreMemoryMetrics enum)

        Call this method before you start using the memory counter routines on microarchitecture codename SandyBridge-EP and later Xeon uarch

        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode programServerUncoreMemoryMetrics(const ServerUncoreMemoryMetrics & metrics, int rankA = -1, int rankB = -1);

    // vector of IDs. E.g. for core {raw event} or {raw event, offcore response1 msr value, } or {raw event, offcore response1 msr value, offcore response2}
    // or for cha/cbo {raw event, filter value}, etc
    // + user-supplied name
    typedef std::array<uint64, 6> RawEventEncoding;
    typedef std::pair<RawEventEncoding, std::string> RawEventConfig;
    struct RawPMUConfig
    {
        std::vector<RawEventConfig> programmable;
        std::vector<RawEventConfig> fixed;
    };
    enum {
        OCR0Pos = 1,
        OCR1Pos = 2,
        LoadLatencyPos = 3,
        FrontendPos = 4
    };
    typedef std::map<std::string, RawPMUConfig> RawPMUConfigs;
    ErrorCode program(const RawPMUConfigs& curPMUConfigs, const bool silent = false, const int pid = -1);

    struct PCICFGEventPosition
    {
        enum constants
        {
            deviceID = 0,
            offset = 1,
            type = 2,
            width = 5
        };
    };
    typedef std::pair<std::shared_ptr<PciHandleType>, uint32> PCICFGRegisterEncoding; // PciHandleType shared ptr, offset
    struct PCICFGRegisterEncodingHash
    {
        std::size_t operator()(const RawEventEncoding & e) const
        {
            std::size_t h1 = std::hash<uint64>{}(e[PCICFGEventPosition::deviceID]);
            std::size_t h2 = std::hash<uint64>{}(e[PCICFGEventPosition::offset]);
            std::size_t h3 = std::hash<uint64>{}(e[PCICFGEventPosition::width]);
            return h1 ^ (h2 << 1ULL) ^ (h3 << 2ULL);
        }
    };
    struct PCICFGRegisterEncodingCmp
    {
        bool operator ()(const RawEventEncoding& a, const RawEventEncoding& b) const
        {
            return a[PCICFGEventPosition::deviceID] == b[PCICFGEventPosition::deviceID]
                && a[PCICFGEventPosition::offset] == b[PCICFGEventPosition::offset]
                && a[PCICFGEventPosition::width] == b[PCICFGEventPosition::width];
        }
    };
    struct MMIOEventPosition
    {
        enum constants
        {
            deviceID = PCICFGEventPosition::deviceID,
            offset = PCICFGEventPosition::offset,
            type = PCICFGEventPosition::type,
            membar_bits1 = 3,
            membar_bits2 = 4,
            width = PCICFGEventPosition::width
        };
    };
    typedef std::pair<std::shared_ptr<MMIORange>, uint32> MMIORegisterEncoding; // MMIORange shared ptr, offset
    struct MMIORegisterEncodingHash : public PCICFGRegisterEncodingHash
    {
        std::size_t operator()(const RawEventEncoding& e) const
        {
            std::size_t h4 = std::hash<uint64>{}(e[MMIOEventPosition::membar_bits1]);
            std::size_t h5 = std::hash<uint64>{}(e[MMIOEventPosition::membar_bits2]);
            return PCICFGRegisterEncodingHash::operator()(e) ^ (h4 << 3ULL) ^ (h5 << 4ULL);
        }
    };
    struct MMIORegisterEncodingCmp : public PCICFGRegisterEncodingCmp
    {
        bool operator ()(const RawEventEncoding& a, const RawEventEncoding& b) const
        {
            return PCICFGRegisterEncodingCmp::operator()(a,b)
                && a[MMIOEventPosition::membar_bits1] == b[MMIOEventPosition::membar_bits1]
                && a[MMIOEventPosition::membar_bits2] == b[MMIOEventPosition::membar_bits2];
        }
    };
    struct PMTEventPosition
    {
        enum constants
        {
            UID = PCICFGEventPosition::deviceID,
            offset = PCICFGEventPosition::offset,
            type = PCICFGEventPosition::type,
            lsb = 3,
            msb = 4
        };
    };
    struct PMTRegisterEncodingHash
    {
        std::size_t operator()(const RawEventEncoding & e) const
        {
            return std::hash<uint64>{}(e[PMTEventPosition::UID]);
        }
    };
    struct PMTRegisterEncodingHash2
    {
        std::size_t operator()(const RawEventEncoding & e) const
        {
            std::size_t h1 = std::hash<uint64>{}(e[PMTEventPosition::UID]);
            std::size_t h2 = std::hash<uint64>{}(e[PMTEventPosition::offset]);
            std::size_t h3 = std::hash<uint64>{}(e[PMTEventPosition::lsb]);
            return h1 ^ (h2 << 1ULL) ^ (h3 << 2ULL);
        }
    };
    struct PMTRegisterEncodingCmp
    {
        bool operator ()(const RawEventEncoding& a, const RawEventEncoding& b) const
        {
            return a[PMTEventPosition::UID] == b[PMTEventPosition::UID];
        }
    };
    typedef std::shared_ptr<TelemetryArray> PMTRegisterEncoding; // TelemetryArray shared ptr
private:
    std::unordered_map<RawEventEncoding, std::vector<PCICFGRegisterEncoding>, PCICFGRegisterEncodingHash, PCICFGRegisterEncodingCmp> PCICFGRegisterLocations{};
    std::unordered_map<RawEventEncoding, std::vector<MMIORegisterEncoding>, MMIORegisterEncodingHash, MMIORegisterEncodingCmp> MMIORegisterLocations{};
    std::unordered_map<RawEventEncoding, std::vector<PMTRegisterEncoding>, PMTRegisterEncodingHash, PMTRegisterEncodingCmp> PMTRegisterLocations{};
public:

    TopologyEntry::CoreType getCoreType(const unsigned coreID) const
    {
        assert(coreID < topology.size());
        return topology[coreID].core_type;
    }

    std::pair<unsigned, unsigned> getOCREventNr(const int event, const unsigned coreID) const
    {
       assert (coreID < topology.size());
       if (hybrid)
       {
            switch (cpu_family_model)
            {
            case ADL:
            case RPL:
            case MTL:
            case LNL:
            case ARL:
                if (topology[coreID].core_type == TopologyEntry::Atom)
                {
                    return std::make_pair(OFFCORE_RESPONSE_0_EVTNR, event + 1);
                }
                break;
            }
       }
       bool useGLCOCREvent = false;
       switch (cpu_family_model)
       {
       case SPR:
       case EMR:
       case ADL: // ADL big core (GLC)
       case RPL:
       case MTL:
       case LNL:
       case ARL:
           useGLCOCREvent = true;
           break;
       }
       switch (event)
       {
            case 0:
                return std::make_pair(useGLCOCREvent ? GLC_OFFCORE_RESPONSE_0_EVTNR : OFFCORE_RESPONSE_0_EVTNR, OFFCORE_RESPONSE_0_UMASK);
            case 1:
                return std::make_pair(useGLCOCREvent ? GLC_OFFCORE_RESPONSE_1_EVTNR : OFFCORE_RESPONSE_1_EVTNR, OFFCORE_RESPONSE_1_UMASK);
       }
       assert (false && "wrong event nr in getOCREventNr");
       return std::make_pair(0U, 0U);
    }

    //! \brief Freezes uncore event counting using global control MSR
    void globalFreezeUncoreCounters();

    //! \brief Unfreezes uncore event counting using global control MSR
    void globalUnfreezeUncoreCounters();

    //! \brief Freezes uncore event counting
    void freezeServerUncoreCounters();

    //! \brief Unfreezes uncore event counting
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
    void cleanup(const bool silent = false);

    /*! \brief Forces PMU reset

                If there is no chance to free up PMU from other applications you might try to call this method at your own risk.
    */
    void resetPMU();

    /*! \brief Reads all counter states (including system, sockets and cores)

        \param systemState system counter state (return parameter)
        \param socketStates socket counter states (return parameter)
        \param coreStates core counter states (return parameter)
        \param readAndAggregateSocketUncoreCounters read and aggregate socket uncore counters

    */
    void getAllCounterStates(SystemCounterState & systemState, std::vector<SocketCounterState> & socketStates, std::vector<CoreCounterState> & coreStates, const bool readAndAggregateSocketUncoreCounters = true);

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
    
    /*! \brief Reads  the accel type in the system
        \return acceltype
    */
    uint32 getAccel() const;

    /*! \brief Sets  the accel type in the system
        \return acceltype
    */
    void setAccel(uint32 input);

    /*! \brief Reads the Number of AccelCounters in the system
        \return None
    */
    uint32 getNumberofAccelCounters() const;

    /*! \brief Sets the Number of AccelCounters in the system
        \return number of counters
    */          
    void setNumberofAccelCounters(uint32 input);

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

    /*! \brief Returns cpu model id number from cpuid instruction
    */
    /*
    static int getCPUModelFromCPUID();
    */

    /*! \brief Returns cpu family and model id number from cpuid instruction
    *   \return cpu family and model id number (model id is in the lower 8 bits, family id is in the next 8 bits)
    */
    static int getCPUFamilyModelFromCPUID();

    #define PCM_CPU_FAMILY_MODEL(family_, model_) (((family_) << 8) + (model_))

    //! \brief Identifiers of supported CPU models
    enum SupportedCPUModels
    {
        NEHALEM_EP =    PCM_CPU_FAMILY_MODEL(6, 26),
        NEHALEM =       PCM_CPU_FAMILY_MODEL(6, 30),
        ATOM =          PCM_CPU_FAMILY_MODEL(6, 28),
        ATOM_2 =        PCM_CPU_FAMILY_MODEL(6, 53),
        CENTERTON =     PCM_CPU_FAMILY_MODEL(6, 54),
        BAYTRAIL =      PCM_CPU_FAMILY_MODEL(6, 55),
        AVOTON =        PCM_CPU_FAMILY_MODEL(6, 77),
        CHERRYTRAIL =   PCM_CPU_FAMILY_MODEL(6, 76),
        APOLLO_LAKE =   PCM_CPU_FAMILY_MODEL(6, 92),
        GEMINI_LAKE =   PCM_CPU_FAMILY_MODEL(6, 122),
        DENVERTON =     PCM_CPU_FAMILY_MODEL(6, 95),
        SNOWRIDGE =     PCM_CPU_FAMILY_MODEL(6, 134),
        ELKHART_LAKE =  PCM_CPU_FAMILY_MODEL(6, 150),
        JASPER_LAKE =   PCM_CPU_FAMILY_MODEL(6, 156),
        CLARKDALE =     PCM_CPU_FAMILY_MODEL(6, 37),
        WESTMERE_EP =   PCM_CPU_FAMILY_MODEL(6, 44),
        NEHALEM_EX =    PCM_CPU_FAMILY_MODEL(6, 46),
        WESTMERE_EX =   PCM_CPU_FAMILY_MODEL(6, 47),
        SANDY_BRIDGE =  PCM_CPU_FAMILY_MODEL(6, 42),
        JAKETOWN =      PCM_CPU_FAMILY_MODEL(6, 45),
        IVY_BRIDGE =    PCM_CPU_FAMILY_MODEL(6, 58),
        HASWELL =       PCM_CPU_FAMILY_MODEL(6, 60),
        HASWELL_ULT =   PCM_CPU_FAMILY_MODEL(6, 69),
        HASWELL_2 =     PCM_CPU_FAMILY_MODEL(6, 70),
        IVYTOWN =       PCM_CPU_FAMILY_MODEL(6, 62),
        HASWELLX =      PCM_CPU_FAMILY_MODEL(6, 63),
        BROADWELL =     PCM_CPU_FAMILY_MODEL(6, 61),
        BROADWELL_XEON_E3 = PCM_CPU_FAMILY_MODEL(6, 71),
        BDX_DE =        PCM_CPU_FAMILY_MODEL(6, 86),
        SKL_UY =        PCM_CPU_FAMILY_MODEL(6, 78),
        KBL =           PCM_CPU_FAMILY_MODEL(6, 158),
        KBL_1 =         PCM_CPU_FAMILY_MODEL(6, 142),
        CML =           PCM_CPU_FAMILY_MODEL(6, 166),
        CML_1 =         PCM_CPU_FAMILY_MODEL(6, 165),
        ICL =           PCM_CPU_FAMILY_MODEL(6, 126),
        ICL_1 =         PCM_CPU_FAMILY_MODEL(6, 125),
        RKL =           PCM_CPU_FAMILY_MODEL(6, 167),
        TGL =           PCM_CPU_FAMILY_MODEL(6, 140),
        TGL_1 =         PCM_CPU_FAMILY_MODEL(6, 141),
        ADL =           PCM_CPU_FAMILY_MODEL(6, 151),
        ADL_1 =         PCM_CPU_FAMILY_MODEL(6, 154),
        RPL =           PCM_CPU_FAMILY_MODEL(6, 0xb7),
        RPL_1 =         PCM_CPU_FAMILY_MODEL(6, 0xba),
        RPL_2 =         PCM_CPU_FAMILY_MODEL(6, 0xbf),
        RPL_3 =         PCM_CPU_FAMILY_MODEL(6, 0xbe),
        MTL =           PCM_CPU_FAMILY_MODEL(6, 0xAA),
        LNL =           PCM_CPU_FAMILY_MODEL(6, 0xBD),
        ARL =           PCM_CPU_FAMILY_MODEL(6, 197),
        ARL_1 =         PCM_CPU_FAMILY_MODEL(6, 198),
        BDX =           PCM_CPU_FAMILY_MODEL(6, 79),
        KNL =           PCM_CPU_FAMILY_MODEL(6, 87),
        SKL =           PCM_CPU_FAMILY_MODEL(6, 94),
        SKX =           PCM_CPU_FAMILY_MODEL(6, 85),
        ICX_D =         PCM_CPU_FAMILY_MODEL(6, 108),
        ICX =           PCM_CPU_FAMILY_MODEL(6, 106),
        SPR =           PCM_CPU_FAMILY_MODEL(6, 143),
        EMR =           PCM_CPU_FAMILY_MODEL(6, 207),
        GNR =           PCM_CPU_FAMILY_MODEL(6, 173),
        SRF =           PCM_CPU_FAMILY_MODEL(6, 175),
        GNR_D =         PCM_CPU_FAMILY_MODEL(6, 174),
        GRR =           PCM_CPU_FAMILY_MODEL(6, 182),
        END_OF_MODEL_LIST = 0x0ffff
    };

#define PCM_SKL_PATH_CASES \
        case PCM::SKL_UY:  \
        case PCM::KBL:     \
        case PCM::KBL_1:   \
        case PCM::CML:     \
        case PCM::ICL:     \
        case PCM::RKL:     \
        case PCM::TGL:     \
        case PCM::SKL:

private:
    bool useSKLPath() const
    {
        switch (cpu_family_model)
        {
            PCM_SKL_PATH_CASES
                return true;
        }
        return false;
    }
    RawPMUConfig threadMSRConfig{}, packageMSRConfig{}, pcicfgConfig{}, mmioConfig{}, pmtConfig{};
public:

    //! \brief Reads CPU family
    //! \return CPU family
    uint32 getCPUFamily() const { return (uint32)cpu_family; }

    //! \brief Reads CPU model id (use only with the family API together, don't always assume family 6)
    //! \return Internal CPU model ID
    uint32 getInternalCPUModel() const { return (uint32)cpu_model_private; }

    //! \brief Reads CPU family and model id
    //! \return CPU family and model ID (lowest 8 bits is the model, next 8 bits is the family)
    uint32 getCPUFamilyModel() const { return cpu_family_model; }

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
    int32 getSocketId(uint32 core_id) const { return (int32)topology[core_id].socket_id; }


    size_t getNumCXLPorts(uint32 socket) const
    {
        if (socket < cxlPMUs.size())
        {
            return cxlPMUs[socket].size();
        }
        return 0;
    }

    //! \brief Returns the number of Intel(r) Quick Path Interconnect(tm) links per socket
    //! \return number of QPI links per socket
    uint64 getQPILinksPerSocket() const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
            return (serverUncorePMUs.size() && serverUncorePMUs[0].get()) ? (serverUncorePMUs[0]->getNumQPIPorts()) : 0;
        }
        return 0;
    }

    //! \brief Returns the number of detected integrated memory controllers per socket
    uint32 getMCPerSocket() const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
        case BDX:
        case KNL:
            return (serverUncorePMUs.size() && serverUncorePMUs[0].get()) ? (serverUncorePMUs[0]->getNumMC()) : 0;
        }
        return 0;
    }

    //! \brief Returns the total number of detected memory channels on all integrated memory controllers per socket
    size_t getMCChannelsPerSocket() const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
        case BDX:
        case KNL:
        case SNOWRIDGE:
            return (serverUncorePMUs.size() && serverUncorePMUs[0].get()) ? (serverUncorePMUs[0]->getNumMCChannels()) : 0;
        }
        return 0;
    }

    //! \brief Returns the number of detected memory channels on given integrated memory controllers
    //! \param socket socket
    //! \param controller controller
    size_t getMCChannels(uint32 socket, uint32 controller) const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
        case BDX:
        case KNL:
        case SNOWRIDGE:
            return (socket < serverUncorePMUs.size() && serverUncorePMUs[socket].get()) ? (serverUncorePMUs[socket]->getNumMCChannels(controller)) : 0;
        }
        return 0;
    }


    //! \brief Returns the total number of detected memory channels on all integrated memory controllers per socket
    size_t getEDCChannelsPerSocket() const
    {
        switch (cpu_family_model)
        {
        case KNL:
            return (serverUncorePMUs.size() && serverUncorePMUs[0].get()) ? (serverUncorePMUs[0]->getNumEDCChannels()) : 0;
        }
        return 0;
    }


    //! \brief Returns the max number of instructions per cycle
    //! \return max number of instructions per cycle
    uint32 getMaxIPC() const
    {
        if (ICL == cpu_family_model || TGL == cpu_family_model || RKL == cpu_family_model) return 5;
        switch (cpu_family_model)
        {
        case ADL:
        case RPL:
        case MTL:
            return 6;
        case LNL:
        case ARL:
            return 12;
        case SNOWRIDGE:
        case ELKHART_LAKE:
        case JASPER_LAKE:
            return 4;
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
        case ICX:
            return 5;
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
            return 6;
        }
        if (isAtom())
        {
            return 2;
        }
        std::cerr << "MaxIPC is not defined for your cpu family " << cpu_family << " model " << cpu_model_private << '\n';
        assert (0);
        return 0;
    }

    //! \brief Returns the frequency of Power Control Unit
    uint64 getPCUFrequency() const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SNOWRIDGE:
            return 1100000000ULL; // 1.1 GHz
        }
        return 0;
    }

    //! \brief Returns whether it is a server part
    bool isServerCPU() const
    {
        switch (cpu_family_model)
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
        case ICX:
        case SNOWRIDGE:
        case SPR:
        case EMR:
        case GNR:
        case GRR:
        case SRF:
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

    uint64 getInvariantTSC_Fast(uint32 core = 0);

    //! \brief Returns uncore clock ticks on specified socket
    uint64 getUncoreClocks(const uint32 socket_);

    //! \brief Return QPI Link Speed in GBytes/second
    //! \warning Works only for Nehalem-EX (Xeon 7500) and Xeon E7 and E5 processors
    //! \return QPI Link Speed in GBytes/second
    uint64 getQPILinkSpeed(uint32 socketNr, uint32 linkNr) const
    {
        return hasPCICFGUncore() ? serverUncorePMUs[socketNr]->getQPILinkSpeed(linkNr) : max_qpi_speed;
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
    //! \param eventGroup - events to program for the same run
    void programPCIeEventGroup(eventGroup_t &eventGroup);
    uint64 getPCIeCounterData(const uint32 socket_, const uint32 ctr_);

    //! \brief Program CBO (or CHA on SKX+) counters
    //! \param events array with four raw event values
    //! \param opCode opcode match filter
    //! \param nc_ match non-coherent requests
    //! \param llc_lookup_tid_filter filter for LLC lookup event filter and TID filter (core and thread ID)
    //! \param loc match on local node target
    //! \param rem match on remote node target
    void programCbo(const uint64 * events, const uint32 opCode = 0, const uint32 nc_ = 0, const uint32 llc_lookup_tid_filter = 0, const uint32 loc = 1, const uint32 rem = 1);

    //! \brief Program CBO (or CHA on SKX+) counters
    //! \param events array with four raw event values
    //! \param filter0 raw filter value
    //! \param filter1 raw filter1 value
    void programCboRaw(const uint64* events, const uint64 filter0, const uint64 filter1);

    //! \brief Program MDF counters
    //! \param events array with four raw event values
    void programMDF(const uint64* events);

    //! \brief Get the state of PCIe counter(s)
    //! \param socket_ socket of the PCIe controller
    //! \return State of PCIe counter(s)
    PCIeCounterState getPCIeCounterState(const uint32 socket_, const uint32 ctr_ = 0);

    //! \brief Program uncore IIO events
    //! \param rawEvents events to program (raw format)
    //! \param IIOStack id of the IIO stack to program (-1 for all, if parameter omitted)
    void programIIOCounters(uint64 rawEvents[4], int IIOStack = -1);

    //! \brief Program uncore IRP events
    //! \param rawEvents events to program (raw format)
    //! \param IIOStack id of the IIO stack to program (-1 for all, if parameter omitted)
    void programIRPCounters(uint64 rawEvents[4], int IIOStack = -1);

    //! \brief Control QAT telemetry service
    //! \param dev device index
    //! \param operation control code 
    void controlQATTelemetry(uint32 dev, uint32 operation);

    //! \brief Program IDX events
    //! \param events config of event to program
    //! \param filters_wq filters(work queue) of event to program 
    //! \param filters_eng filters(engine) of event to program 
    //! \param filters_tc filters(traffic class) of event to program 
    //! \param filters_pgsz filters(page size) of event to program 
    //! \param filters_xfersz filters(transfer size) of event to program 
    void programIDXAccelCounters(uint32 accel, std::vector<uint64_t> &events, std::vector<uint32> &filters_wq, std::vector<uint32> &filters_eng, std::vector<uint32> &filters_tc, std::vector<uint32> &filters_pgsz, std::vector<uint32> &filters_xfersz);


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


    //! \brief Get the state of IDX accel counter
    //! \param accel ip index
    //! \param dev device index
    //! \param counter_id perf counter index
    //! \return State of IDX counter
    IDXCounterState getIDXAccelCounterState(uint32 accel, uint32 dev, uint32 counter_id);

    uint64 extractCoreGenCounterValue(uint64 val);
    uint64 extractCoreFixedCounterValue(uint64 val);
    uint64 extractUncoreGenCounterValue(uint64 val);
    uint64 extractUncoreFixedCounterValue(uint64 val);
    uint64 extractQOSMonitoring(uint64 val);

    //! \brief Get a string describing the codename of the processor microarchitecture
    //! \param cpu_family_model_ cpu model (if no parameter provided the codename of the detected CPU is returned)
    const char * getUArchCodename(const int32 cpu_family_model_ = -1) const;

    //! \brief Get Brand string of processor
    static std::string getCPUBrandString();
    std::string getCPUFamilyModelString();
    static std::string getCPUFamilyModelString(const uint32 cpu_family, const uint32 cpu_model, const uint32 cpu_stepping);

    //! \brief Enables "force all RTM transaction abort" mode also enabling 4+ programmable counters on Skylake generation processors
    void enableForceRTMAbortMode(const bool silent = false);

    //! \brief queries status of "force all RTM transaction abort" mode
    bool isForceRTMAbortModeEnabled() const;

    //! \brief Disables "force all RTM transaction abort" mode restricting the number of programmable counters on Skylake generation processors to 3
    void disableForceRTMAbortMode(const bool silent = false);

    //! \brief queries availability of "force all RTM transaction abort" mode
    static bool isForceRTMAbortModeAvailable();

    //! \brief Get microcode level (returns -1 if retrieval not supported due to some restrictions)
    int64 getCPUMicrocodeLevel() const { return cpu_microcode_level; }

    //! \brief returns true if CPU model is Atom-based
    static bool isAtom(const int32 cpu_family_model_)
    {
        return cpu_family_model_ == ATOM
            || cpu_family_model_ == ATOM_2
            || cpu_family_model_ == CENTERTON
            || cpu_family_model_ == BAYTRAIL
            || cpu_family_model_ == AVOTON
            || cpu_family_model_ == CHERRYTRAIL
            || cpu_family_model_ == APOLLO_LAKE
            || cpu_family_model_ == GEMINI_LAKE
            || cpu_family_model_ == DENVERTON
            // || cpu_family_model_ == SNOWRIDGE do not use Atom code for SNOWRIDGE
            ;
    }

    //! \brief returns true if CPU is Atom-based
    bool isAtom() const
    {
        return isAtom(cpu_family_model);
    }

    // From commit message: https://github.com/torvalds/linux/commit/e979121b1b1556e184492e6fc149bbe188fc83e6
    bool memoryEventErrata() const
    {
        switch (cpu_family_model)
        {
            case SANDY_BRIDGE:
            case JAKETOWN:
            case IVYTOWN:
            case IVY_BRIDGE:
            case HASWELL:
            case HASWELLX:
                return true;
        }
        return false;
    }

    bool packageEnergyMetricsAvailable() const
    {
        return (
                    cpu_family_model == PCM::JAKETOWN
                 || cpu_family_model == PCM::IVYTOWN
                 || cpu_family_model == PCM::SANDY_BRIDGE
                 || cpu_family_model == PCM::IVY_BRIDGE
                 || cpu_family_model == PCM::HASWELL
                 || cpu_family_model == PCM::AVOTON
                 || cpu_family_model == PCM::CHERRYTRAIL
                 || cpu_family_model == PCM::BAYTRAIL
                 || cpu_family_model == PCM::APOLLO_LAKE
                 || cpu_family_model == PCM::GEMINI_LAKE
                 || cpu_family_model == PCM::DENVERTON
                 || cpu_family_model == PCM::SNOWRIDGE
                 || cpu_family_model == PCM::ELKHART_LAKE
                 || cpu_family_model == PCM::JASPER_LAKE
                 || cpu_family_model == PCM::HASWELLX
                 || cpu_family_model == PCM::BROADWELL
                 || cpu_family_model == PCM::BDX_DE
                 || cpu_family_model == PCM::BDX
                 || cpu_family_model == PCM::KNL
                 || useSKLPath()
                 || cpu_family_model == PCM::SKX
                 || cpu_family_model == PCM::ICX
                 || cpu_family_model == PCM::ADL
                 || cpu_family_model == PCM::RPL
                 || cpu_family_model == PCM::MTL
                 || cpu_family_model == PCM::LNL
                 || cpu_family_model == PCM::ARL
                 || cpu_family_model == PCM::SPR
                 || cpu_family_model == PCM::EMR
                 || cpu_family_model == PCM::GNR
                 || cpu_family_model == PCM::SRF
                 || cpu_family_model == PCM::GRR
               );
    }

    bool dramEnergyMetricsAvailable() const
    {
        return (
             cpu_family_model == PCM::JAKETOWN
          || cpu_family_model == PCM::IVYTOWN
          || cpu_family_model == PCM::HASWELLX
          || cpu_family_model == PCM::BDX_DE
          || cpu_family_model == PCM::BDX
          || cpu_family_model == PCM::KNL
          || cpu_family_model == PCM::SKX
          || cpu_family_model == PCM::ICX
          || cpu_family_model == PCM::SPR
          || cpu_family_model == PCM::EMR
          || cpu_family_model == PCM::GNR
          || cpu_family_model == PCM::SRF
          || cpu_family_model == PCM::GRR
          );
    }

    bool systemEnergyMetricAvailable() const
    {
        return (
               useSKLPath()
            || cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::ADL
            || cpu_family_model == PCM::RPL
            || cpu_family_model == PCM::MTL
            || cpu_family_model == PCM::LNL
            || cpu_family_model == PCM::ARL
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::GNR
            || cpu_family_model == PCM::SRF
            || cpu_family_model == PCM::GRR
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
               cpu_family_model == PCM::NEHALEM_EX
            || cpu_family_model == PCM::WESTMERE_EX
            || cpu_family_model == PCM::JAKETOWN
            || cpu_family_model == PCM::IVYTOWN
            || cpu_family_model == PCM::HASWELLX
            || cpu_family_model == PCM::BDX
            || cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::GNR
            || cpu_family_model == PCM::SRF
            );
    }

    bool incomingQPITrafficMetricsAvailable() const
    {
        return getQPILinksPerSocket() > 0 &&
            (
               cpu_family_model == PCM::NEHALEM_EX
            || cpu_family_model == PCM::WESTMERE_EX
            || cpu_family_model == PCM::JAKETOWN
            || cpu_family_model == PCM::IVYTOWN
            || (cpu_family_model == PCM::SKX && cpu_stepping > 1)
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::GNR
            || cpu_family_model == PCM::SRF
               );
    }

    bool localMemoryRequestRatioMetricAvailable() const
    {
        return cpu_family_model == PCM::HASWELLX
            || cpu_family_model == PCM::BDX
            || cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::SRF
            || cpu_family_model == PCM::GNR
            ;
    }

    bool qpiUtilizationMetricsAvailable() const
    {
        return outgoingQPITrafficMetricsAvailable();
    }

    bool nearMemoryMetricsAvailable() const
    {
        return (
               cpu_family_model == PCM::SRF
            || cpu_family_model == PCM::GNR
            );
    }
    
    bool memoryTrafficMetricsAvailable() const
    {
        return (!(isAtom() || cpu_family_model == PCM::CLARKDALE))
               ;
    }

    bool HBMmemoryTrafficMetricsAvailable() const
    {
        return serverUncorePMUs.empty() == false && serverUncorePMUs[0].get() != nullptr && serverUncorePMUs[0]->HBMAvailable();
    }

    size_t getHBMCASTransferSize() const
    {
        return (SPR == cpu_family_model) ? 32ULL : 64ULL;
    }

    bool memoryIOTrafficMetricAvailable() const
    {
        if (cpu_family_model == TGL) return false;
        return (
            cpu_family_model == PCM::SANDY_BRIDGE
            || cpu_family_model == PCM::IVY_BRIDGE
            || cpu_family_model == PCM::HASWELL
            || cpu_family_model == PCM::BROADWELL
            || useSKLPath()
            );
    }

    bool IIOEventsAvailable() const
    {
        return (
               cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
	        || cpu_family_model == PCM::SNOWRIDGE
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::GRR
            || cpu_family_model == PCM::SRF
            || cpu_family_model == PCM::GNR
        );
    }

    bool uncoreFrequencyMetricAvailable() const
    {
        return MSR.empty() == false
                && getMaxNumOfUncorePMUs(UBOX_PMU_ID) > 0ULL
                && getNumCores() == getNumOnlineCores()
                && PCM::GNR != cpu_family_model
                && PCM::SRF != cpu_family_model
            ;
    }

    bool LatencyMetricsAvailable() const
    {
        return (
               cpu_family_model == PCM::HASWELLX
            || cpu_family_model == PCM::BDX
            || cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || useSKLPath()
            );
    }

    bool DDRLatencyMetricsAvailable() const
    {
        return (
               cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            );
    }

    bool PMMTrafficMetricsAvailable() const
    {
        return (
            isCLX()
                    ||  isCPX()
                     || cpu_family_model == PCM::ICX
                     || cpu_family_model == PCM::SNOWRIDGE
                     || cpu_family_model == SPR
                     || cpu_family_model == EMR
        );
    }

    bool PMMMemoryModeMetricsAvailable() const
    {
       return (
                  isCLX()
               || isCPX()
               || cpu_family_model == PCM::ICX
               || cpu_family_model == PCM::SNOWRIDGE
              );
    }

    bool PMMMixedModeMetricsAvailable() const
    {
       return PMMMemoryModeMetricsAvailable();
    }

    bool LLCReadMissLatencyMetricsAvailable() const
    {
        return (
               HASWELLX == cpu_family_model
            || BDX_DE == cpu_family_model
            || BDX == cpu_family_model
            || isCLX()
            || isCPX()
#ifdef PCM_ENABLE_LLCRDLAT_SKX_MP
            || SKX == cpu_family_model
#else
            || ((SKX == cpu_family_model) && (num_sockets == 1))
#endif
            || ICX == cpu_family_model
            || SPR == cpu_family_model
            || SNOWRIDGE == cpu_family_model
               );
    }

    bool hasBecktonUncore() const
    {
        return (
            cpu_family_model == PCM::NEHALEM_EX
            || cpu_family_model == PCM::WESTMERE_EX
            );
    }
    bool hasPCICFGUncore() const // has PCICFG uncore PMON
    {
        return (
            cpu_family_model == PCM::JAKETOWN
            || cpu_family_model == PCM::SNOWRIDGE
            || cpu_family_model == PCM::IVYTOWN
            || cpu_family_model == PCM::HASWELLX
            || cpu_family_model == PCM::BDX_DE
            || cpu_family_model == PCM::SKX
            || cpu_family_model == PCM::ICX
            || cpu_family_model == PCM::SPR
            || cpu_family_model == PCM::EMR
            || cpu_family_model == PCM::GNR
            || cpu_family_model == PCM::SRF
            || cpu_family_model == PCM::GRR
            || cpu_family_model == PCM::BDX
            || cpu_family_model == PCM::KNL
            );
    }

    bool isSkxCompatible() const
    {
        return (
            cpu_family_model == PCM::SKX
               );
    }

    static bool hasUPI(const int32 cpu_family_model_) // Intel(r) Ultra Path Interconnect
    {
        return (
            cpu_family_model_ == PCM::SKX
         || cpu_family_model_ == PCM::ICX
         || cpu_family_model_ == PCM::SPR
         || cpu_family_model_ == PCM::EMR
         || cpu_family_model_ == PCM::GNR
         || cpu_family_model_ == PCM::SRF
               );
    }

    bool hasUPI() const
    {
        return hasUPI(cpu_family_model);
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
            cpu_family_model == PCM::SKX
         || cpu_family_model == PCM::ICX
         || cpu_family_model == PCM::SPR
         || cpu_family_model == PCM::EMR
         || cpu_family_model == PCM::GNR
         || cpu_family_model == PCM::SRF
         || cpu_family_model == PCM::GRR
               );
    }

    bool supportsHLE() const;
    bool supportsRTM() const;
    bool supportsRDTSCP() const;

    bool useSkylakeEvents() const
    {
        return    useSKLPath()
               || PCM::SKX == cpu_family_model
               || PCM::ICX == cpu_family_model
               || PCM::SPR == cpu_family_model
               || PCM::EMR == cpu_family_model
               || PCM::GNR == cpu_family_model
               ;
    }

    bool hasClientMCCounters() const
    {
        return cpu_family_model == SANDY_BRIDGE
            || cpu_family_model == IVY_BRIDGE
            || cpu_family_model == HASWELL
            || cpu_family_model == BROADWELL
            || cpu_family_model == ADL
            || cpu_family_model == RPL
            || cpu_family_model == MTL
            || cpu_family_model == LNL
            || cpu_family_model == ARL
            || useSKLPath()
            ;
    }

    bool ppEnergyMetricsAvailable() const
    {
        return packageEnergyMetricsAvailable() && hasClientMCCounters() && num_sockets == 1;
    }

    static double getBytesPerFlit(int32 cpu_family_model_)
    {
        if (hasUPI(cpu_family_model_))
        {
            // 172 bits per UPI flit
            return 172./8.;
        }
        // 8 bytes per QPI flit
        return 8.;
    }

    double getBytesPerFlit() const
    {
        return getBytesPerFlit(cpu_family_model);
    }

    static double getDataBytesPerFlit(const int32 cpu_family_model_)
    {
        if (hasUPI(cpu_family_model_))
        {
            // 9 UPI flits to transfer 64 bytes
            return 64./9.;
        }
        // 8 bytes per QPI flit
        return 8.;
    }

    double getDataBytesPerFlit() const
    {
        return getDataBytesPerFlit(cpu_family_model);
    }

    static double getFlitsPerLinkCycle(const int32 cpu_family_model_)
    {
        if (hasUPI(cpu_family_model_))
        {
            // 5 UPI flits sent every 6 link cycles
            return 5./6.;
        }
        return 2.;
    }

    static double getBytesPerLinkCycle(const int32 cpu_family_model_)
    {
        return getBytesPerFlit(cpu_family_model_) * getFlitsPerLinkCycle(cpu_family_model_);
    }

    double getBytesPerLinkCycle() const
    {
        return getBytesPerLinkCycle(cpu_family_model);
    }

    double getLinkTransfersPerLinkCycle() const
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
    friend double getAverageFrequencyFromClocks(const int64 clocks, const CounterStateType& before, const CounterStateType& after);
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
    template <class CounterStateType>
    friend uint64 getAllSlotsRaw(const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getAllSlots(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getBackendBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getFrontendBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getBadSpeculation(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getRetiring(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getFetchLatencyBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getFetchBandwidthBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getBranchMispredictionBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getMachineClearsBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getMemoryBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getCoreBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getHeavyOperationsBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getLightOperationsBound(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMSREvent(const uint64 & index, const PCM::MSRType & type, const CounterStateType& before, const CounterStateType& after);
protected:
    checked_uint64 InstRetiredAny{};
    checked_uint64 CpuClkUnhaltedThread{};
    checked_uint64 CpuClkUnhaltedRef{};
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
            HSXL2MissPos = 2,
                L2HitPos = 3,
             HSXL2RefPos = 3
    };
    uint64 InvariantTSC; // invariant time stamp counter
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    int32 ThermalHeadroom;
    uint64 L3Occupancy;
    uint64 MemoryBWLocal;
    uint64 MemoryBWTotal;
    uint64 SMICount;
    uint64 FrontendBoundSlots, BadSpeculationSlots, BackendBoundSlots, RetiringSlots, AllSlotsRaw;
    uint64 MemBoundSlots, FetchLatSlots, BrMispredSlots, HeavyOpsSlots;
    std::unordered_map<uint64, uint64> MSRValues;

public:
    BasicCounterState() :
        InvariantTSC(0),
        ThermalHeadroom(PCM_INVALID_THERMAL_HEADROOM),
        L3Occupancy(0),
        MemoryBWLocal(0),
        MemoryBWTotal(0),
        SMICount(0),
    FrontendBoundSlots(0),
    BadSpeculationSlots(0),
    BackendBoundSlots(0),
    RetiringSlots(0),
    AllSlotsRaw(0),
    MemBoundSlots(0),
    FetchLatSlots(0),
    BrMispredSlots(0),
    HeavyOpsSlots(0)
    {
        std::fill(CStateResidency, CStateResidency + PCM::MAX_C_STATE + 1, 0);
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
        // std::cout << "before PCM debug aggregate "<< FrontendBoundSlots << " " << BadSpeculationSlots << " " << BackendBoundSlots << " " <<RetiringSlots << std::endl;
        BasicCounterState old = *this;
        FrontendBoundSlots += o.FrontendBoundSlots;
        BadSpeculationSlots += o.BadSpeculationSlots;
        BackendBoundSlots += o.BackendBoundSlots;
        RetiringSlots += o.RetiringSlots;
        AllSlotsRaw += o.AllSlotsRaw;
        MemBoundSlots += o.MemBoundSlots;
        FetchLatSlots += o.FetchLatSlots;
        BrMispredSlots += o.BrMispredSlots;
        HeavyOpsSlots += o.HeavyOpsSlots;
        //std::cout << "after PCM debug aggregate "<< FrontendBoundSlots << " " << BadSpeculationSlots << " " << BackendBoundSlots << " " <<RetiringSlots << std::endl;
        assert(FrontendBoundSlots >= old.FrontendBoundSlots);
        assert(BadSpeculationSlots >= old.BadSpeculationSlots);
        assert(BackendBoundSlots >= old.BackendBoundSlots);
        assert(RetiringSlots >= old.RetiringSlots);
        assert(MemBoundSlots >= old.MemBoundSlots);
        assert(FetchLatSlots >= old.FetchLatSlots);
        assert(BrMispredSlots >= old.BrMispredSlots);
        assert(HeavyOpsSlots >= old.HeavyOpsSlots);
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
    const auto clk = after.DRAMClocks[channel] - before.DRAMClocks[channel];
    const auto cpu_family_model = PCM::getInstance()->getCPUFamilyModel();
    if (cpu_family_model == PCM::ICX || cpu_family_model == PCM::SNOWRIDGE)
    {
        return 2 * clk;
    }
    return clk;
}

/*! \brief Returns HBM clock ticks
    \param channel HBM channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getHBMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after)
{
    return after.HBMClocks[channel] - before.HBMClocks[channel];
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

/*! \brief Direct read of CXLCM PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param port port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getCXLCMCounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.CXLCMCounter[port][counter] - before.CXLCMCounter[port][counter];
}

/*! \brief Direct read of CXLDP PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param port port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getCXLDPCounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.CXLDPCounter[port][counter] - before.CXLDPCounter[port][counter];
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

/*! \brief Direct read of uncore PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param pmu_id ID of PMU (unit type: CBO, etc)
    \param unit uncore unit ID
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getUncoreCounter(const int pmu_id, uint32 unit, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    for (size_t die = 0; counter < UncorePMU::maxCounters && die < after.Counters.size(); ++die)
    {
        assert(die < before.Counters.size());
        const auto afterIter = after.Counters[die].find(pmu_id);
        const auto beforeIter = before.Counters[die].find(pmu_id);
        if (afterIter != after.Counters[die].end() && beforeIter != before.Counters[die].end())
        {
            assert(afterIter->second.size() == beforeIter->second.size());
            if (unit < afterIter->second.size())
            {
                return afterIter->second[unit][counter] - beforeIter->second[unit][counter];
            }
            unit -= afterIter->second.size();
        }
    }
    return 0ULL;
}

/*! \brief Direct read of IIO PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param stack IIO stack number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getIIOCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.IIOCounter[stack][counter] - before.IIOCounter[stack][counter];
}

/*! \brief Direct read of IRP PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param stack IIO stack number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getIRPCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after)
{
    return after.IRPCounter[stack][counter] - before.IRPCounter[stack][counter];
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

/*! \brief Direct read of HA controller PMU counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param controller controller number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getHACounter(uint32 controller, uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    return after.HACounter[controller][counter] - before.HACounter[controller][counter];
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
    if (PCM::getInstance()->HBMmemoryTrafficMetricsAvailable())
        return after.EDCCounter[channel][counter] - before.EDCCounter[channel][counter];
    return 0ULL;
}

/*!  \brief Returns clock ticks of power control unit
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getPCUClocks(uint32 unit, const CounterStateType & before, const CounterStateType & after)
{
    return getUncoreCounter(PCM::PCU_PMU_ID, unit, 0, before, after);
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

/*!  \brief Returns energy consumed by processor, excluding DRAM (measured in internal units)
    \param powerPlane power plane ID
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getConsumedEnergy(const int powerPlane, const CounterStateType& before, const CounterStateType& after)
{
    assert(powerPlane <= PCM::MAX_PP);
    return after.PPEnergyStatus[powerPlane] - before.PPEnergyStatus[powerPlane];
}

/*!  \brief Returns energy consumed by system
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getSystemConsumedEnergy(const CounterStateType& before, const CounterStateType& after)
{
    return after.systemEnergyStatus - before.systemEnergyStatus;
}

/*!  \brief Checks is systemEnergyStatusValid is valid in the state
*   \param s CPU counter state
*/
template <class CounterStateType>
bool systemEnergyStatusValid(const CounterStateType& s)
{
    return s.systemEnergyStatus != 0;
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


/*!  \brief Returns free running counter if it exists, -1 otherwise
 *   \param counter name of the counter
 *   \param before CPU counter state before the experiment
 *   \param after CPU counter state after the experiment
 */
template <class CounterStateType>
int64 getFreeRunningCounter(const typename CounterStateType::FreeRunningCounterID & counter, const CounterStateType & before, const CounterStateType & after)
{
    const auto beforeIt = before.freeRunningCounter.find(counter);
    const auto afterIt = after.freeRunningCounter.find(counter);
    if (beforeIt != before.freeRunningCounter.end() &&
        afterIt != after.freeRunningCounter.end())
    {
        return afterIt->second - beforeIt->second;
    }
    return -1;
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

/*!  \brief Returns Joules consumed by processor (excluding DRAM)
    \param powePlane power plane
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
double getConsumedJoules(const int powerPlane, const CounterStateType& before, const CounterStateType& after)
{
    PCM* m = PCM::getInstance();
    if (!m) return -1.;

    return double(getConsumedEnergy(powerPlane, before, after)) * m->getJoulesPerEnergyUnit();
}

/*!  \brief Returns Joules consumed by system
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
double getSystemConsumedJoules(const CounterStateType& before, const CounterStateType& after)
{
    PCM* m = PCM::getInstance();
    if (!m) return -1.;

    auto unit = m->getJoulesPerEnergyUnit();

    switch (m->getCPUFamilyModel())
    {
           case PCM::SPR:
           case PCM::EMR:
           case PCM::GNR:
           case PCM::SRF:
                   unit = 1.0;
                   break;
    }

    return double(getSystemConsumedEnergy(before, after)) * unit;
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
    double dram_joules_per_energy_unit = 0.;
    const auto cpu_family_model = m->getCPUFamilyModel();

    if (PCM::HASWELLX == cpu_family_model
        || PCM::BDX_DE == cpu_family_model
        || PCM::BDX == cpu_family_model
        || PCM::SKX == cpu_family_model
        || PCM::ICX == cpu_family_model
        || PCM::GNR == cpu_family_model
        || PCM::SRF == cpu_family_model
        || PCM::GRR == cpu_family_model
        || PCM::KNL == cpu_family_model
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

    friend uint64 getNMHits(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getNMMisses(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getNMHitRate(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getNMMissBW(const CounterStateType & before, const CounterStateType & after);

    template <class CounterStateType>
    friend uint64 getBytesReadFromPMM(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToPMM(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesReadFromEDC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToEDC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getGTRequestBytesFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getIARequestBytesFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getIORequestBytesFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getConsumedEnergy(const int pp, const CounterStateType& before, const CounterStateType& after);
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
    template <class CounterStateType>
    friend double getAverageUncoreFrequency(const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend std::vector<double> getUncoreFrequency(const CounterStateType& state);
    template <class CounterStateType>
    friend std::vector<uint64> getUncoreDieTypes(const CounterStateType& state);
    template <class CounterStateType>
    friend double getAverageFrequencyFromClocks(const int64 clocks, const CounterStateType& before, const CounterStateType& after);

public:
    enum DieTypeBits
    {
        Compute = 1<<23,
        LLC     = 1<<24,
        Memory  = 1<<25,
        IO      = 1<<26
    };
    static std::string getDieTypeStr(const uint64 d)
    {
        std::string type{};
        if (d & UncoreCounterState::Compute)
        {
            type += "COR";
        }
        if (d & UncoreCounterState::IO)
        {
            type += "IO";
        }
        if (d & UncoreCounterState::LLC)
        {
            type += "LLC";
        }
        if (d & UncoreCounterState::Memory)
        {
            type += "M";
        }
        return type;
    }
protected:
    std::vector<uint64> UFSStatus;
    uint64 UncMCFullWrites;
    uint64 UncMCNormalReads;
    uint64 UncHARequests;
    uint64 UncHALocalRequests;
    uint64 UncNMMiss;
    uint64 UncNMHit;
    uint64 UncPMMWrites;
    uint64 UncPMMReads;
    uint64 UncEDCFullWrites;
    uint64 UncEDCNormalReads;
    uint64 UncMCGTRequests;
    uint64 UncMCIARequests;
    uint64 UncMCIORequests;
    uint64 PackageEnergyStatus;
    uint64 PPEnergyStatus[PCM::MAX_PP + 1];
    uint64 DRAMEnergyStatus;
    uint64 TOROccupancyIAMiss;
    uint64 TORInsertsIAMiss;
    uint64 UncClocks;
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    void readAndAggregate(std::shared_ptr<SafeMsrHandle>);

public:
    UncoreCounterState() :
        UFSStatus{{}},
        UncMCFullWrites(0),
        UncMCNormalReads(0),
        UncHARequests(0),
        UncHALocalRequests(0),
        UncNMMiss(0),
        UncNMHit(0),
        UncPMMWrites(0),
        UncPMMReads(0),
        UncEDCFullWrites(0),
        UncEDCNormalReads(0),
        UncMCGTRequests(0),
        UncMCIARequests(0),
        UncMCIORequests(0),
        PackageEnergyStatus(0),
        DRAMEnergyStatus(0),
        TOROccupancyIAMiss(0),
        TORInsertsIAMiss(0),
        UncClocks(0)
    {
        UFSStatus.clear();
        std::fill(CStateResidency, CStateResidency + PCM::MAX_C_STATE + 1, 0);
        std::fill(PPEnergyStatus, PPEnergyStatus + PCM::MAX_PP + 1, 0);
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
        UncMCGTRequests += o.UncMCGTRequests;
        UncMCIARequests += o.UncMCIARequests;
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


//! \brief Server uncore counter state
//!
class ServerUncoreCounterState : public UncoreCounterState
{
public:
    enum {
        maxControllers = 32,
        maxChannels = 32,
        maxXPILinks = 6,
        maxIIOStacks = 16,
        maxCXLPorts = 6,
        maxCounters = UncorePMU::maxCounters
    };
    enum EventPosition
    {
        xPI_TxL0P_POWER_CYCLES = 0,
        xPI_L1_POWER_CYCLES = 2,
        xPI_CLOCKTICKS = 3
    };
    enum FreeRunningCounterID
    {
        ImcReads,
        ImcWrites,
        PMMReads,
        PMMWrites
    };

    // typedef std::array<uint64, maxCounters> CounterArrayType;
    class CounterArrayType
    {
        std::array<uint64, maxCounters> data;
    public:
        CounterArrayType() : data{{}}
        {
            std::fill(data.begin(), data.end(), 0ULL);
        }
        const uint64& operator [] (size_t i) const
        {
            return data[i];
        }
        uint64& operator [] (size_t i)
        {
            return data[i];
        }
    };
    typedef std::vector<CounterArrayType> PMUCounterArrayType;
    typedef std::unordered_map<int, PMUCounterArrayType> PMUMapCounterArrayType;
    // die -> pmu map -> PMUs -> counters
    std::vector<PMUMapCounterArrayType>  Counters;

    std::array<std::array<uint64, maxCounters>, maxXPILinks> xPICounter;
    std::array<std::array<uint64, maxCounters>, maxXPILinks> M3UPICounter;
    std::array<std::array<uint64, maxCounters>, maxIIOStacks> IIOCounter;
    std::array<std::array<uint64, maxCounters>, maxIIOStacks> IRPCounter;
    std::array<std::array<uint64, maxCounters>, maxCXLPorts> CXLCMCounter;
    std::array<std::array<uint64, maxCounters>, maxCXLPorts> CXLDPCounter;
    std::array<uint64, maxChannels> DRAMClocks;
    std::array<uint64, maxChannels> HBMClocks;
    std::array<std::array<uint64, maxCounters>, maxChannels> MCCounter; // channel X counter
    std::array<std::array<uint64, maxCounters>, maxControllers> M2MCounter; // M2M/iMC boxes x counter
    std::array<std::array<uint64, maxCounters>, maxControllers> HACounter; // HA boxes x counter
    std::array<std::array<uint64, maxCounters>, maxChannels> EDCCounter; // EDC controller X counter
    std::unordered_map<int, uint64> freeRunningCounter;
    int32 PackageThermalHeadroom;
    uint64 InvariantTSC;    // invariant time stamp counter
    friend class PCM;
    template <class CounterStateType>
    friend uint64 getDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getHBMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getCXLCMCounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getCXLDPCounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getM3UPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getUncoreCounter(const int pmu_id, uint32 unit, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getIIOCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getIRPCounter(uint32 stack, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getXPICounter(uint32 port, uint32 counter, const CounterStateType& before, const CounterStateType& after);
    template <class CounterStateType>
    friend uint64 getM2MCounter(uint32 controller, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getHACounter(uint32 controller, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getEDCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getDRAMConsumedEnergy(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getInvariantTSC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend int64 getFreeRunningCounter(const typename CounterStateType::FreeRunningCounterID &, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getAverageFrequencyFromClocks(const int64 clocks, const CounterStateType& before, const CounterStateType& after);

public:
    //! Returns current thermal headroom below TjMax
    int32 getPackageThermalHeadroom() const { return PackageThermalHeadroom; }
    ServerUncoreCounterState() :
        xPICounter{{}},
        M3UPICounter{{}},
        IIOCounter{{}},
        IRPCounter{{}},
        CXLCMCounter{{}},
        CXLDPCounter{{}},
        DRAMClocks{{}},
        HBMClocks{{}},
        MCCounter{{}},
        M2MCounter{{}},
        HACounter{{}},
        EDCCounter{{}},
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
    virtual ~ CoreCounterState() {}
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

    virtual ~ SocketCounterState() {}
};

//! \brief System-wide counter state
class SystemCounterState : public SocketCounterState
{
    friend class PCM;
    friend std::vector<uint64> getPCICFGEvent(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after);
    friend std::vector<uint64> getMMIOEvent(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after);
    friend std::vector<uint64> getPMTEvent(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after);
    template <class CounterStateType> friend bool systemEnergyStatusValid(const CounterStateType& s);
    template <class CounterStateType> friend uint64 getSystemConsumedEnergy(const CounterStateType& before, const CounterStateType& after);

    std::vector<std::vector<uint64> > incomingQPIPackets; // each 64 byte
    std::vector<std::vector<uint64> > outgoingQPIFlits; // idle or data/non-data flits depending on the architecture
    std::vector<std::vector<uint64> > TxL0Cycles;
    uint64 uncoreTSC;
    uint64 systemEnergyStatus;
    std::unordered_map<PCM::RawEventEncoding, std::vector<uint64> , PCM::PCICFGRegisterEncodingHash, PCM::PCICFGRegisterEncodingCmp> PCICFGValues{};
    std::unordered_map<PCM::RawEventEncoding, std::vector<uint64>, PCM::MMIORegisterEncodingHash, PCM::MMIORegisterEncodingCmp> MMIOValues{};
    std::unordered_map<PCM::RawEventEncoding, std::vector<uint64>, PCM::PMTRegisterEncodingHash2> PMTValues{};

protected:
    void readAndAggregate(std::shared_ptr<SafeMsrHandle> handle)
    {
        BasicCounterState::readAndAggregate(handle);
        UncoreCounterState::readAndAggregate(handle);
    }

public:
    typedef uint32_t h_id;
    typedef uint32_t v_id;
    typedef std::map<std::pair<h_id,v_id>,uint64_t> ctr_data;
    typedef std::vector<ctr_data> dev_content;
    std::vector<SimpleCounterState> accel_counters;
    std::vector<uint64> CXLWriteMem,CXLWriteCache;
    friend uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getIncomingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & now);
    friend double getOutgoingQPILinkUtilization(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getOutgoingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & before, const SystemCounterState & after);
    friend uint64 getOutgoingQPILinkBytes(uint32 socketNr, uint32 linkNr, const SystemCounterState & now);

    SystemCounterState() :
        uncoreTSC(0),
        systemEnergyStatus(0)
    {
        PCM * m = PCM::getInstance();
        accel_counters.resize(m->getNumberofAccelCounters());
        CXLWriteMem.resize(m->getNumSockets(),0);
        CXLWriteCache.resize(m->getNumSockets(),0);
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

    virtual ~ SystemCounterState() {}
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

// \brief Returns current uncore frequency vector
template <class CounterStateType>
std::vector<double> getUncoreFrequency(const CounterStateType& state)
{
    std::vector<double> result;
    for (auto & e : state.UFSStatus)
    {
        result.push_back(extract_bits(e, 0, 6) * 100000000.);
    }
    return result;
}

// \brief Returns uncore die type vector
template <class CounterStateType>
std::vector<uint64> getUncoreDieTypes(const CounterStateType& state)
{
    std::vector<uint64> result;
    for (auto & e : state.UFSStatus)
    {
        result.push_back(extract_bits(e, 23, 26) << 23);
    }
    return result;
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

/*! \brief Computes average number of retired instructions per time interval

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
template <class CounterStateType>
inline double getCoreIPC(const CounterStateType & before, const CounterStateType & after) // instructions per cycle
{
    double ipc = getIPC(before, after);
    PCM * m = PCM::getInstance();
    if (ipc >= 0. && m && (m->getNumCores() == m->getNumOnlineCores()))
        return ipc * double(m->getThreadsPerCore());
    return -1;
}

/*! \brief Computes average number of retired instructions per time interval for the entire system combining instruction counts from logical cores to corresponding physical cores

        Use this metric to evaluate cores utilization improvement between SMT(Hyperthreading) on and SMT off.

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return usage
*/
template <class CounterStateType>
inline double getTotalExecUsage(const CounterStateType & before, const CounterStateType & after) // usage
{
    double usage = getExecUsage(before, after);
    PCM * m = PCM::getInstance();
    if (usage >= 0. && m && (m->getNumCores() == m->getNumOnlineCores()))
        return usage * double(m->getThreadsPerCore());
    return -1;
}

template <class StateType>
double getAverageFrequencyFromClocks(const int64 clocks, const StateType& before, const StateType& after) // in Hz
{
    const int64 timer_clocks = after.InvariantTSC - before.InvariantTSC;
    PCM* m = PCM::getInstance();
    if (timer_clocks != 0 && m)
        return double(m->getNominalFrequency()) * double(clocks) / double(timer_clocks);
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
    return getAverageFrequencyFromClocks(after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread, before, after);
}

/*! \brief Computes average uncore frequency

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return frequency in Hz
*/
template <class UncoreStateType>
double getAverageUncoreFrequency(const UncoreStateType& before, const UncoreStateType & after) // in Hz
{
    auto m = PCM::getInstance();
    assert(m);
    return double(m->getNumOnlineCores()) * getAverageFrequencyFromClocks(after.UncClocks - before.UncClocks, before, after) / double(m->getNumOnlineSockets());
}

/*! \brief Computes uncore frequency for all dies

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return frequency in Hz
*/
template <class UncoreStateType>
std::vector<double> getUncoreFrequencies(const UncoreStateType& before, const UncoreStateType & after) // in Hz
{
    auto m = PCM::getInstance();
    assert(m);
    std::vector<double> uncoreFrequencies{getUncoreFrequency(after)};
    if (m->uncoreFrequencyMetricAvailable())
    {
        uncoreFrequencies.push_back(getAverageUncoreFrequency(before, after));
    }
    return uncoreFrequencies;
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
    auto* pcm = PCM::getInstance();
    if (!pcm->isL2CacheHitRatioAvailable()) return 0;
    const auto hits = getL2CacheHits(before, after);
    if (pcm->memoryEventErrata())
    {
        const auto all = after.Event[BasicCounterState::HSXL2RefPos] - before.Event[BasicCounterState::HSXL2RefPos];
        if (all == 0ULL) return 0.;
        return double(hits) / double(all);
    }
    const auto misses = getL2CacheMisses(before, after);
    const auto all = double(hits + misses);
    if (all == 0.0) return 0.;
    return double(hits) / all;
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
    const auto all = double(hits + misses);
    if (all == 0.0) return 0.;
    return double(hits) / all;
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
    const auto cpu_family_model = pcm->getCPUFamilyModel();
    if (pcm->useSkylakeEvents()
        || cpu_family_model == PCM::SNOWRIDGE
        || cpu_family_model == PCM::ELKHART_LAKE
        || cpu_family_model == PCM::JASPER_LAKE
        || cpu_family_model == PCM::SRF
        || cpu_family_model == PCM::GRR
        || cpu_family_model == PCM::ADL
        || cpu_family_model == PCM::RPL
        || cpu_family_model == PCM::MTL
        || cpu_family_model == PCM::LNL
        || cpu_family_model == PCM::ARL
        ) {
        return after.Event[BasicCounterState::SKLL2MissPos] - before.Event[BasicCounterState::SKLL2MissPos];
    }
    else if (pcm->isAtom() || cpu_family_model == PCM::KNL)
    {
        return after.Event[BasicCounterState::ArchLLCMissPos] - before.Event[BasicCounterState::ArchLLCMissPos];
    }
    else if (pcm->memoryEventErrata())
    {
        return after.Event[BasicCounterState::ArchLLCRefPos] - before.Event[BasicCounterState::ArchLLCRefPos];
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
    if (pcm->isAtom() || pcm->getCPUFamilyModel() == PCM::KNL)
    {
        uint64 L2Miss = after.Event[BasicCounterState::ArchLLCMissPos] - before.Event[BasicCounterState::ArchLLCMissPos];
        uint64 L2Ref = after.Event[BasicCounterState::ArchLLCRefPos] - before.Event[BasicCounterState::ArchLLCRefPos];
        return L2Ref - L2Miss;
    }
    else if (pcm->memoryEventErrata())
    {
        const auto all = after.Event[BasicCounterState::HSXL2RefPos] - before.Event[BasicCounterState::HSXL2RefPos];
        const auto misses = after.Event[BasicCounterState::HSXL2MissPos] - before.Event[BasicCounterState::HSXL2MissPos];
        const auto hits = (all > misses) ? (all - misses) : 0ULL;
        return hits;
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
    auto pcm = PCM::getInstance();
    if (!pcm->isL3CacheHitsSnoopAvailable()) return 0;
    const auto cpu_family_model = pcm->getCPUFamilyModel();
    if (cpu_family_model == PCM::SNOWRIDGE
        || cpu_family_model == PCM::GRR
        || cpu_family_model == PCM::ELKHART_LAKE
        || cpu_family_model == PCM::JASPER_LAKE
        || cpu_family_model == PCM::SRF
        || cpu_family_model == PCM::ADL
        || cpu_family_model == PCM::RPL
        || cpu_family_model == PCM::MTL
        || cpu_family_model == PCM::LNL
        || cpu_family_model == PCM::ARL
        )
    {
        const int64 misses = getL3CacheMisses(before, after);
        const int64 refs = after.Event[BasicCounterState::ArchLLCRefPos] - before.Event[BasicCounterState::ArchLLCRefPos];
        const int64 hits = refs - misses;
        return (hits > 0)? hits : 0;
    }
    if (pcm->useSkylakeEvents()) {
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
    auto * pcm = PCM::getInstance();
    assert(pcm);
    if (!pcm->isL3CacheHitsAvailable()) return 0;
    else if (pcm->memoryEventErrata())
    {
        uint64 LLCMiss = after.Event[BasicCounterState::ArchLLCMissPos] - before.Event[BasicCounterState::ArchLLCMissPos];
        uint64 LLCRef = after.Event[BasicCounterState::ArchLLCRefPos] - before.Event[BasicCounterState::ArchLLCRefPos];
        return (LLCRef > LLCMiss) ? (LLCRef - LLCMiss) : 0ULL;
    }
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

/*! \brief Computes number of Near Memory Hits

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getNMHits(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->nearMemoryMetricsAvailable())
        return (after.UncNMHit - before.UncNMHit);
    return 0ULL;
}

/*! \brief Computes number of Near Memory Misses

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of NMMisses
*/
template <class CounterStateType>
uint64 getNMMisses(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->nearMemoryMetricsAvailable())
        return (after.UncNMMiss - before.UncNMMiss);
    return 0ULL;
}

/*! \brief Computes Near Memory Misses Bandwidth

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getNMMissBW(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->nearMemoryMetricsAvailable())
        return (after.UncNMMiss - before.UncNMMiss)*64*2;
    return 0ULL;
}

/*! \brief Computes Near Memory Hit/Miss rate as a percentage

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
double getNMHitRate(const CounterStateType & before, const CounterStateType & after)
{

    if (PCM::getInstance()->nearMemoryMetricsAvailable())
    {
        auto hit = (after.UncNMHit - before.UncNMHit);
        auto miss = (after.UncNMMiss - before.UncNMMiss);
        if((hit+miss) != 0 )
        return (hit*100.0/(hit+miss));}

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

/*! \brief Computes number of bytes read from HBM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromEDC(const CounterStateType & before, const CounterStateType & after)
{
    auto m = PCM::getInstance();
    assert(m);
    if (m->HBMmemoryTrafficMetricsAvailable())
        return (after.UncEDCNormalReads - before.UncEDCNormalReads) * m->getHBMCASTransferSize();
    return 0ULL;
}

/*! \brief Computes number of bytes written to HBM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToEDC(const CounterStateType & before, const CounterStateType & after)
{
    auto m = PCM::getInstance();
    assert(m);
    if (m->HBMmemoryTrafficMetricsAvailable())
        return (after.UncEDCFullWrites - before.UncEDCFullWrites) * m->getHBMCASTransferSize();
    return 0ULL;
}

/*! \brief Computes number of bytes of read/write requests from GT engine

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getGTRequestBytesFromMC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->memoryIOTrafficMetricAvailable())
        return (after.UncMCGTRequests - before.UncMCGTRequests) * 64;
    return 0ULL;
}

/*! \brief Computes number of bytes of read/write requests from all IA

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getIARequestBytesFromMC(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->memoryIOTrafficMetricAvailable())
        return (after.UncMCIARequests - before.UncMCIARequests) * 64;
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

/*! \brief Returns the number of occurred system management interrupts

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of SMIs (system manegement interrupts)
*/
template <class CounterStateType>
uint64 getSMICount(const CounterStateType & before, const CounterStateType & after)
{
    return after.SMICount - before.SMICount;
}

/*! \brief Returns the number of occurred custom core events

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


/*! \brief Computes number of bytes Writen from CXL Cache

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
//template <class CounterStateType>
inline uint64 getCXLWriteCacheBytes(uint32 socket,const SystemCounterState & before,const SystemCounterState & after)
{
        return (after.CXLWriteCache[socket] - before.CXLWriteCache[socket]) * 64;
}

/*! \brief Computes number of bytes Writen from CXL Memory

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
//template <class CounterStateType>
inline uint64 getCXLWriteMemBytes(uint32 socket, const SystemCounterState & before,const SystemCounterState & after)
{

        return (after.CXLWriteMem[socket] - before.CXLWriteMem[socket]) * 64;
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
    const double max_bytes = (double)(double(max_speed) * double(getInvariantTSC(before, after) / double(m->getNumOnlineCores())) / double(m->getNominalFrequency()));
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
        if (idle_flits >= tsc) return 0.; // prevent overflows due to potential counter dissynchronization

        return (1. - (idle_flits / tsc));
    } else if (m->hasPCICFGUncore())
    {
        const uint64 b = before.outgoingQPIFlits[socketNr][linkNr]; // data + non-data flits or idle (null) flits
        const uint64 a = after.outgoingQPIFlits[socketNr][linkNr]; // data + non-data flits or idle (null) flits
        // prevent overflows due to counter dissynchronisation
        double flits = (double)((a > b) ? (a - b) : 0);
        const double max_flits = ((double(getInvariantTSC(before, after)) * double(m->getQPILinkSpeed(socketNr, linkNr)) / m->getBytesPerFlit()) / double(m->getNominalFrequency())) / double(m->getNumOnlineCores());
        if(m->hasUPI())
        {
            flits = flits/3.;
        }
        if (flits > max_flits) return 1.; // prevent overflows due to potential counter dissynchronization
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
    const double max_bytes = (double(m->getQPILinkSpeed(socketNr, linkNr)) * double(getInvariantTSC(before, after) / double(m->getNumOnlineCores())) / double(m->getNominalFrequency()));

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
    if (memTraffic == 0) return -1.;
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
    // std::cout << "PCM DEBUG "<< 64*all/1e6 << " " << 64*local/1e6 << "\n";
    return double(local)/double(all);
}

//! \brief Returns the raw count of events
//! \param before counter state before the experiment
//! \param after counter state after the experiment
template <class CounterType>
inline uint64 getNumberOfEvents(const CounterType & before, const CounterType & after)
{
    // prevent overflows due to counter dissynchronisation
    if (after.data < before.data)
    {
        return 0;
    }

    return after.data - before.data;
}
//! \brief Returns average last level cache read+prefetch miss latency in ns

template <class CounterStateType>
inline double getLLCReadMissLatency(const CounterStateType & before, const CounterStateType & after)
{
    auto * m = PCM::getInstance();
    if (m->LLCReadMissLatencyMetricsAvailable() == false) return -1.;
    const double occupancy = double(after.TOROccupancyIAMiss) - double(before.TOROccupancyIAMiss);
    const double inserts = double(after.TORInsertsIAMiss) - double(before.TORInsertsIAMiss);
    const double unc_clocks = double(after.UncClocks) - double(before.UncClocks);
    const double seconds = double(getInvariantTSC(before, after)) / double(m->getNumOnlineCores()/m->getNumSockets()) / double(m->getNominalFrequency());
    return 1e9*seconds*(occupancy/inserts)/unc_clocks;
}

template <class CounterStateType>
inline uint64 getAllSlots(const CounterStateType & before, const CounterStateType & after)
{
    const int64 a = after.BackendBoundSlots - before.BackendBoundSlots;
    const int64 b = after.FrontendBoundSlots - before.FrontendBoundSlots;
    const int64 c = after.BadSpeculationSlots - before.BadSpeculationSlots;
    const int64 d = after.RetiringSlots - before.RetiringSlots;
    // std::cout << "before DEBUG: " << before.FrontendBoundSlots << " " << before.BadSpeculationSlots << " "<< before.BackendBoundSlots << " " << before.RetiringSlots << std::endl;
    // std::cout << "after DEBUG: " <<  after.FrontendBoundSlots << " " << after.BadSpeculationSlots << " " << after.BackendBoundSlots << " " << after.RetiringSlots << std::endl;
    assert(a >= 0);
    assert(b >= 0);
    assert(c >= 0);
    assert(d >= 0);
    return a + b + c + d;
}

template <class CounterStateType>
inline uint64 getAllSlotsRaw(const CounterStateType& before, const CounterStateType& after)
{
    return after.AllSlotsRaw - before.AllSlotsRaw;
}

//! \brief Returns unutilized pipeline slots where no uop was delivered due to lack of back-end resources as range 0..1
template <class CounterStateType>
inline double getBackendBound(const CounterStateType & before, const CounterStateType & after)
{
//    std::cout << "DEBUG: "<< after.BackendBoundSlots - before.BackendBoundSlots << " " << getAllSlots(before, after) << std::endl;
    if (PCM::getInstance()->isHWTMAL1Supported())
        return double(after.BackendBoundSlots - before.BackendBoundSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns unutilized pipeline slots where no uop was delivered due to stalls on buffer, cache or memory resources as range 0..1
template <class CounterStateType>
inline double getMemoryBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return double(after.MemBoundSlots - before.MemBoundSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns unutilized pipeline slots where no uop was delivered due to lack of core resources as range 0..1
template <class CounterStateType>
inline double getCoreBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return getBackendBound(before, after) - getMemoryBound(before, after);
    return 0.;
}

//! \brief Returns unutilized pipeline slots where Front-end did not deliver a uop while back-end is ready as range 0..1
template <class CounterStateType>
inline double getFrontendBound(const CounterStateType & before, const CounterStateType & after)
{
//    std::cout << "DEBUG: "<< after.FrontendBoundSlots - before.FrontendBoundSlots << " " << getAllSlots(before, after) << std::endl;
    if (PCM::getInstance()->isHWTMAL1Supported())
        return double(after.FrontendBoundSlots - before.FrontendBoundSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns unutilized pipeline slots where Front-end due to fetch latency constraints did not deliver a uop while back-end is ready as range 0..1
template <class CounterStateType>
inline double getFetchLatencyBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return double(after.FetchLatSlots - before.FetchLatSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns unutilized pipeline slots where Front-end due to fetch bandwidth constraints did not deliver a uop while back-end is ready as range 0..1
template <class CounterStateType>
inline double getFetchBandwidthBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return getFrontendBound(before, after) - getFetchLatencyBound(before, after);
    return 0.;
}

//! \brief Returns wasted pipeline slots due to incorrect speculation, covering whole penalty: Utilized by uops that do not retire, or Recovery Bubbles (unutilized slots) as range 0..1
template <class CounterStateType>
inline double getBadSpeculation(const CounterStateType & before, const CounterStateType & after)
{
//    std::cout << "DEBUG: "<< after.BadSpeculationSlots - before.BadSpeculationSlots << " " << getAllSlots(before, after) << std::endl;
    if (PCM::getInstance()->isHWTMAL1Supported())
        return double(after.BadSpeculationSlots - before.BadSpeculationSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns wasted pipeline slots due to incorrect speculation (branch misprediction), covering whole penalty: Utilized by uops that do not retire, or Recovery Bubbles (unutilized slots) as range 0..1
template <class CounterStateType>
inline double getBranchMispredictionBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return double(after.BrMispredSlots - before.BrMispredSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns wasted pipeline slots due to incorrect speculation (machine clears), covering whole penalty: Utilized by uops that do not retire, or Recovery Bubbles (unutilized slots) as range 0..1
template <class CounterStateType>
inline double getMachineClearsBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return getBadSpeculation(before, after) - getBranchMispredictionBound(before, after);
    return 0.;
}

//! \brief Returns pipeline slots utilized by uops that eventually retire (commit)
template <class CounterStateType>
inline double getRetiring(const CounterStateType & before, const CounterStateType & after)
{
//    std::cout << "DEBUG: "<< after.RetiringSlots - before.RetiringSlots << " " << getAllSlots(before, after) << std::endl;
    if (PCM::getInstance()->isHWTMAL1Supported())
        return double(after.RetiringSlots - before.RetiringSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns pipeline slots utilized by uops that eventually retire (commit) - heavy operations
template <class CounterStateType>
inline double getHeavyOperationsBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return double(after.HeavyOpsSlots - before.HeavyOpsSlots)/double(getAllSlots(before, after));
    return 0.;
}

//! \brief Returns pipeline slots utilized by uops that eventually retire (commit) - light operations
template <class CounterStateType>
inline double getLightOperationsBound(const CounterStateType & before, const CounterStateType & after)
{
    if (PCM::getInstance()->isHWTMAL2Supported())
        return getRetiring(before, after) - getHeavyOperationsBound(before, after);
    return 0.;
}

template <class ValuesType>
inline std::vector<uint64> getRegisterEvent(const PCM::RawEventEncoding& eventEnc, const ValuesType& beforeValues, const ValuesType& afterValues)
{
    std::vector<uint64> result{};
    auto beforeIter = beforeValues.find(eventEnc);
    auto afterIter = afterValues.find(eventEnc);
    if (beforeIter != beforeValues.end() &&
        afterIter != afterValues.end())
    {
        const auto& beforeValues = beforeIter->second;
        const auto& afterValues = afterIter->second;
        assert(beforeValues.size() == afterValues.size());
        const size_t sz = beforeValues.size();
        for (size_t i = 0; i < sz; ++i)
        {
            switch (eventEnc[PCM::PCICFGEventPosition::type])
            {
            case PCM::MSRType::Freerun:
                result.push_back(afterValues[i] - beforeValues[i]);
                break;
            case PCM::MSRType::Static:
                result.push_back(afterValues[i]);
                break;
            }
        }
    }
    return result;
}

inline std::vector<uint64> getPCICFGEvent(const PCM::RawEventEncoding & eventEnc, const SystemCounterState& before, const SystemCounterState& after)
{
    return getRegisterEvent(eventEnc, before.PCICFGValues, after.PCICFGValues);
}

inline std::vector<uint64> getMMIOEvent(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after)
{
    return getRegisterEvent(eventEnc, before.MMIOValues, after.MMIOValues);
}

inline std::vector<uint64> getPMTEvent(const PCM::RawEventEncoding& eventEnc, const SystemCounterState& before, const SystemCounterState& after)
{
    return getRegisterEvent(eventEnc, before.PMTValues, after.PMTValues);
}

template <class CounterStateType>
uint64 getMSREvent(const uint64& index, const PCM::MSRType& type, const CounterStateType& before, const CounterStateType& after)
{
    switch (type)
    {
    case PCM::MSRType::Freerun:
        {
            const auto beforeIt = before.MSRValues.find(index);
            const auto afterIt = after.MSRValues.find(index);
            if (beforeIt != before.MSRValues.end() && afterIt != after.MSRValues.end())
            {
                return afterIt->second - beforeIt->second;
            }
            break;
        }
    case PCM::MSRType::Static:
        {
            const auto result = after.MSRValues.find(index);
            if (result != after.MSRValues.end())
            {
                return result->second;
            }
            break;
        }
    }
    return 0ULL;
}

} // namespace pcm

#endif
