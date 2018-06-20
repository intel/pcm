/*
Copyright (c) 2009-2017, Intel Corporation
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

#include "types.h"
#include "msr.h"
#include "pci.h"
#include "client_bw.h"
#include "width_extender.h"
#include "exceptions/unsupported_processor_exception.hpp"

#include <vector>
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

class SystemCounterState;
class SocketCounterState;
class CoreCounterState;
class BasicCounterState;
class ServerUncorePowerState;
class PCM;
class CoreTaskQueue;

#ifdef _MSC_VER
#if _MSC_VER>= 1600
#include <intrin.h>
#endif
void PCM_API restrictDriverAccess(LPCWSTR path);
#endif

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

//! Object to access uncore counters in a socket/processor with microarchitecture codename SandyBridge-EP (Jaketown) or Ivytown-EP or Ivytown-EX
class ServerPCICFGUncore
{
    int32 iMCbus,UPIbus;
    uint32 groupnr;
    int32 cpu_model;
    std::vector<std::shared_ptr<PciHandleType> > imcHandles;
    std::vector<std::shared_ptr<PciHandleType> > edcHandles;
    std::vector<std::shared_ptr<PciHandleType> > qpiLLHandles;
    std::vector<uint64> qpi_speed;
    uint32 num_imc;
    uint32 MCX_CHY_REGISTER_DEV_ADDR[2][4];
    uint32 MCX_CHY_REGISTER_FUNC_ADDR[2][4];
    uint32 EDCX_ECLK_REGISTER_DEV_ADDR[8];
    uint32 EDCX_ECLK_REGISTER_FUNC_ADDR[8];
    uint32 QPI_PORTX_REGISTER_DEV_ADDR[3];
    uint32 QPI_PORTX_REGISTER_FUNC_ADDR[3];
    uint32 LINK_PCI_PMON_BOX_CTL_ADDR;
    uint32 LINK_PCI_PMON_CTL_ADDR[4];
    uint32 LINK_PCI_PMON_CTR_ADDR[4];

    static PCM_Util::Mutex socket2busMutex;
    static std::vector<std::pair<uint32, uint32> > socket2iMCbus;
    static std::vector<std::pair<uint32, uint32> > socket2UPIbus;
    void initSocket2Bus(std::vector<std::pair<uint32, uint32> > & socket2bus, uint32 device, uint32 function, const uint32 DEV_IDS[], uint32 devIdsSize);

    ServerPCICFGUncore();                                         // forbidden
    ServerPCICFGUncore(ServerPCICFGUncore &);                     // forbidden
    ServerPCICFGUncore & operator = (const ServerPCICFGUncore &); // forbidden
    PciHandleType * createIntelPerfMonDevice(uint32 groupnr, int32 bus, uint32 dev, uint32 func, bool checkVendor = false);
    void programIMC(const uint32 * MCCntConfig);
    void programEDC(const uint32 * EDCCntConfig);
    typedef std::pair<size_t, std::vector<uint64 *> > MemTestParam;
    void initMemTest(MemTestParam & param);
    void doMemTest(const MemTestParam & param);
    void cleanupMemTest(const MemTestParam & param);
    void cleanupQPIHandles();

public:
    //! \brief Initialize access data structures
    //! \param socket_ socket id
    //! \param pcm pointer to PCM instance
    ServerPCICFGUncore(uint32 socket_, const PCM * pcm);
    //! \brief Program performance counters (disables programming power counters)
    void program();
    //! \brief Get the number of integrated controller reads (in cache lines)
    uint64 getImcReads();
    //! \brief Get the number of integrated controller writes (in cache lines)
    uint64 getImcWrites();

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

    virtual ~ServerPCICFGUncore();

    //! \brief Program power counters (disables programming performance counters)
    //! \param mc_profile memory controller measurement profile. See description of profiles in pcm-power.cpp
    void program_power_metrics(int mc_profile);

    //! \brief Program memory counters (disables programming performance counters)
    //! \param rankA count DIMM rank1 statistics (disables memory channel monitoring)
    //! \param rankB count DIMM rank2 statistics (disables memory channel monitoring)
    void programServerUncoreMemoryMetrics(int rankA = -1, int rankB = -1);

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

    //! \brief Freezes event counting
    void freezeCounters();
    //! \brief Unfreezes event counting
    void unfreezeCounters();

    //! \brief Measures/computes the maximum theoretical QPI link bandwidth speed in GByte/seconds
    uint64 computeQPISpeed(const uint32 ref_core, const int cpumodel);

    //! \brief Enable correct counting of various LLC events (with memory access perf penalty)
    void enableJKTWorkaround(bool enable);

    //! \brief Returns the number of detected QPI ports
    size_t getNumQPIPorts() const { return (size_t)qpiLLHandles.size(); }

    //! \brief Returns the speed of the QPI link
    uint64 getQPILinkSpeed(const uint32 linkNr) const
    {
        return qpi_speed.empty() ? 0 : qpi_speed[linkNr];
    }

    //! \brief Print QPI Speeds
    void reportQPISpeed() const;

    //! \brief Returns the number of detected integrated memory controllers
    uint32 getNumMC() const { return num_imc; }

    //! \brief Returns the total number of detected memory channels on all integrated memory controllers
    size_t getNumMCChannels() const { return (size_t)imcHandles.size(); }

    //! \brief Returns the total number of detected memory channels on all embedded DRAM controllers (EDC)
    size_t getNumEDCChannels() const { return (size_t)edcHandles.size(); }
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

#ifndef HACK_TO_REMOVE_DUPLICATE_ERROR
template class PCM_API std::allocator<TopologyEntry>;
template class PCM_API std::vector<TopologyEntry>;
template class PCM_API std::allocator<CounterWidthExtender *>;
template class PCM_API std::vector<CounterWidthExtender *>;
template class PCM_API std::allocator<uint32>;
template class PCM_API std::vector<uint32>;
template class PCM_API std::allocator<char>;
#endif
/*!
        \brief CPU Performance Monitor

        This singleton object needs to be instantiated for each process
        before accessing counting and measuring routines
*/
class PCM_API PCM
{
    friend class BasicCounterState;
    friend class UncoreCounterState;
    PCM();     // forbidden to call directly because it is a singleton

    int32 cpu_family;
    int32 cpu_model, original_cpu_model;
    int32 cpu_stepping;
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
    std::string errorMessage;

    static PCM * instance;
    bool allow_multiple_instances;
    bool programmed_pmu;
    std::vector<std::shared_ptr<SafeMsrHandle> > MSR;
    std::vector<std::shared_ptr<ServerPCICFGUncore> > server_pcicfg_uncore;
    uint64 PCU_MSR_PMON_BOX_CTL_ADDR, PCU_MSR_PMON_CTRX_ADDR[4];
    std::map<int32, uint32>    IIO_UNIT_STATUS_ADDR;
    std::map<int32, uint32>    IIO_UNIT_CTL_ADDR;
    std::map<int32, std::vector<uint32> > IIO_CTR_ADDR;
    std::map<int32, uint32>    IIO_CLK_ADDR;
    std::map<int32, std::vector<uint32> > IIO_CTL_ADDR;
    double joulesPerEnergyUnit;
    std::vector<std::shared_ptr<CounterWidthExtender> > energy_status;
    std::vector<std::shared_ptr<CounterWidthExtender> > dram_energy_status;

    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_local;
    std::vector<std::shared_ptr<CounterWidthExtender> > memory_bw_total;

    std::shared_ptr<ClientBW> clientBW;
    std::shared_ptr<CounterWidthExtender> clientImcReads;
    std::shared_ptr<CounterWidthExtender> clientImcWrites;
    std::shared_ptr<CounterWidthExtender> clientIoRequests;

    bool disable_JKT_workaround;
    bool blocked;              // track if time-driven counter update is running or not: PCM is blocked

    uint64 * coreCStateMsr;    // MSR addresses of core C-state free-running counters
    uint64 * pkgCStateMsr;     // MSR addresses of package C-state free-running counters

    std::vector<std::shared_ptr<CoreTaskQueue> > coreTaskQueues;

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
    CustomCoreEventDescription coreEventDesc[4];

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

    bool PMUinUse();
    void cleanupPMU();
    void freeRMID();
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
    static bool isCPUModelSupported(int model_);
    std::string getSupportedUarchCodenames() const;
    std::string getUnsupportedMessage() const;
    bool detectModel();
    bool checkModel();

    void initCStateSupportTables();
    bool discoverSystemTopology();
    void printSystemTopology() const;
    bool initMSR();
    bool detectNominalFrequency();
    void initEnergyMonitoring();
    void initUncoreObjects();
    /*!
    *       \brief initializes each core with an RMID
    *
    *       \returns nothing
    */
    void initRMID();
    /*!
     *      \brief initializes each core event MSR with an RMID for QOS event (L3 cache monitoring or memory bandwidth monitoring)
     *
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

    uint64 CX_MSR_PMON_CTRY(uint32 Cbo, uint32 Ctr) const;
    uint64 CX_MSR_PMON_BOX_FILTER(uint32 Cbo) const;
    uint64 CX_MSR_PMON_BOX_FILTER1(uint32 Cbo) const;
    uint64 CX_MSR_PMON_CTLY(uint32 Cbo, uint32 Ctl) const;
    uint64 CX_MSR_PMON_BOX_CTL(uint32 Cbo) const;
    uint32 getMaxNumOfCBoxes() const;
    void programCboOpcodeFilter(const uint32 opc, const uint32 cbo, std::shared_ptr<SafeMsrHandle> msr, const uint32 nc_ = 0);

public:
    /*!
             \brief checks if QOS monitoring support present

             \returns true or false
     */
    bool QOSMetricAvailable();
    /*!
             \brief checks L3 cache support for QOS present

             \returns true or false
     */
    bool L3QOSMetricAvailable();
    /*!
             \brief checks if L3 cache monitoring present

             \returns true or false
     */
    bool L3CacheOccupancyMetricAvailable();
    /*!
            \brief checks if local memory bandwidth monitoring present

            \returns true or false
    */
    bool CoreLocalMemoryBWMetricAvailable();
    /*!
    \brief checks if total memory bandwidth monitoring present

    \returns true or false
    */
    bool CoreRemoteMemoryBWMetricAvailable();
    /*!
     *      \brief returns the max number of RMID supported by socket
     *
     *      \returns maximum number of RMID supported by socket
     */
    unsigned getMaxRMID() const;

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

        Call this method before you start using the memory counter routines on microarchitecture codename SandyBridge-EP and later Xeon uarch

        \warning Using this routines with other tools that *program* Performance Monitoring
        Units (PMUs) on CPUs is not recommended because PMU can not be shared. Tools that are known to
        program PMUs: Intel(r) VTune(tm), Intel(r) Performance Tuning Utility (PTU). This code may make
        VTune or PTU measurements invalid. VTune or PTU measurement may make measurement with this code invalid. Please enable either usage of these routines or VTune/PTU/etc.
    */
    ErrorCode programServerUncoreMemoryMetrics(int rankA = -1, int rankB = -1);

    //! \brief Freezes uncore event counting (works only on microarchitecture codename SandyBridge-EP and IvyTown)
    void freezeServerUncoreCounters();

    //! \brief Unfreezes uncore event counting (works only on microarchitecture codename SandyBridge-EP and IvyTown)
    void unfreezeServerUncoreCounters();

    /*! \brief Reads the power/energy counter state of a socket (works only on microarchitecture codename SandyBridge-EP)
        \param socket socket id
        \return State of power counters in the socket
    */
    ServerUncorePowerState getServerUncorePowerState(uint32 socket);

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

    //! \brief Identifiers of supported CPU models
    enum SupportedCPUModels
    {
        NEHALEM_EP = 26,
        NEHALEM = 30,
        ATOM = 28,
        ATOM_2 = 53,
        ATOM_CENTERTON = 54,
        ATOM_BAYTRAIL = 55,
        ATOM_AVOTON = 77,
        ATOM_CHERRYTRAIL = 76,
	    ATOM_APOLLO_LAKE = 92,
        ATOM_DENVERTON = 95,
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
        BDX = 79,
        KNL = 87,
        SKL = 94,
        SKX = 85,
        END_OF_MODEL_LIST = 0x0ffff
    };

    //! \brief Reads CPU model id
    //! \return CPU model ID
    uint32 getCPUModel() const { return (uint32)cpu_model; }

    //! \brief Reads original CPU model id
    //! \return CPU model ID
    uint32 getOriginalCPUModel() const { return (uint32)original_cpu_model; }

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
        switch (cpu_model)
        {
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
        case SKL:
        case KBL:
        case SKX:
            return 4;
        case ATOM:
        case KNL:
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


    //! \brief Return QPI Link Speed in GBytes/second
    //! \warning Works only for Nehalem-EX (Xeon 7500) and Xeon E7 and E5 processors
    //! \return QPI Link Speed in GBytes/second
    uint64 getQPILinkSpeed(uint32 socketNr, uint32 linkNr) const
    { return hasPCICFGUncore() ? server_pcicfg_uncore[socketNr]->getQPILinkSpeed(linkNr) : max_qpi_speed; }

    //! \brief Returns how many joules are in an internal processor energy unit
    double getJoulesPerEnergyUnit() const { return joulesPerEnergyUnit; }

    //! \brief Returns thermal specification power of the package domain in Watt
    int32 getPackageThermalSpecPower() const { return pkgThermalSpecPower; }

    //! \brief Returns minimum power derived from electrical spec of the package domain in Watt
    int32 getPackageMinimumPower() const { return pkgMinimumPower; }

    //! \brief Returns maximum power derived from electrical spec of the package domain in Watt
    int32 getPackageMaximumPower() const { return pkgMaximumPower; }

    //! \brief Loads and initializes Winring0 third party library for access to processor model specific and PCI configuration registers
    //! \return returns true in case of success
    static bool initWinRing0Lib();

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
    //! \param event_ a PCIe event to monitor
    //! \param tid_ tid filter (PCM supports it only on Haswell server)
    void programPCIeCounters(const PCIeEventCode event_, const uint32 tid_ = 0, const uint32 miss_ = 0, const uint32 q_ = 0, const uint32 nc_ = 0);
    void programPCIeMissCounters(const PCIeEventCode event_, const uint32 tid_ = 0, const uint32 q_ = 0, const uint32 nc_ = 0);

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
    const char * getUArchCodename(int32 cpu_model_ = -1) const;

    //! \brief Get Brand string of processor
    static std::string getCPUBrandString();
    std::string getCPUFamilyModelString();

    bool packageEnergyMetricsAvailable() const
    {
        return (
                    cpu_model == PCM::JAKETOWN
                 || cpu_model == PCM::IVYTOWN
                 || cpu_model == PCM::SANDY_BRIDGE 
                 || cpu_model == PCM::IVY_BRIDGE
                 || cpu_model == PCM::HASWELL
                 || original_cpu_model == PCM::ATOM_AVOTON
                 || original_cpu_model == PCM::ATOM_CHERRYTRAIL
                 || original_cpu_model == PCM::ATOM_BAYTRAIL
		         || original_cpu_model == PCM::ATOM_APOLLO_LAKE
                 || original_cpu_model == PCM::ATOM_DENVERTON
                 || cpu_model == PCM::HASWELLX
                 || cpu_model == PCM::BROADWELL
                 || cpu_model == PCM::BDX_DE
                 || cpu_model == PCM::BDX
                 || cpu_model == PCM::KNL
                 || cpu_model == PCM::SKL
                 || cpu_model == PCM::KBL
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

    bool qpiUtilizationMetricsAvailable() const
    {
        return outgoingQPITrafficMetricsAvailable();
    }

    bool memoryTrafficMetricsAvailable() const
    {
        return !(
            cpu_model == PCM::ATOM
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
            || cpu_model == PCM::SKL
            || cpu_model == PCM::KBL
            );
    }

    bool IIOEventsAvailable() const
    {
        return (
            cpu_model == PCM::SKX
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

    bool supportsHLE() const;
    bool supportsRTM() const;

    bool useSkylakeEvents() const
    {
        return PCM::SKL == cpu_model
            || PCM::SKX == cpu_model
            || PCM::KBL == cpu_model
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

    ~PCM();
};

//! \brief Basic core counter state
//!
//! Intended only for derivation, but not for the direct use
class BasicCounterState
{
    friend class PCM;
    template <class CounterStateType>
    friend double getExecUsage(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getIPC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getAverageFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getActiveAverageFrequency(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getCyclesLostDueL3CacheMisses(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getCyclesLostDueL2CacheMisses(const CounterStateType & before, const CounterStateType & after);
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
    friend uint64 getL3CacheHitsNoSnoop(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheHitsSnoop(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getL3CacheHits(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getNumberOfCustomEvents(int32 eventCounterNr, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getInvariantTSC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getRefCycles(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend double getCoreCStateResidency(int state, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getSMICount(const CounterStateType & before, const CounterStateType & after);

protected:
    uint64 InstRetiredAny;
    uint64 CpuClkUnhaltedThread;
    uint64 CpuClkUnhaltedRef;
    // dont put any additional fields between Event 0-Event 3 because getNumberOfCustomEvents assumes there are none
    union {
        uint64 L3Miss;
        uint64 Event0;
        uint64 ArchLLCMiss;
    };
    union {
        uint64 L3UnsharedHit;
        uint64 Event1;
        uint64 ArchLLCRef;
        uint64 SKLL3Hit;
    };
    union {
        uint64 L2HitM;
        uint64 Event2;
        uint64 SKLL2Miss;
    };
    union {
        uint64 L2Hit;
        uint64 Event3;
    };
    uint64 InvariantTSC; // invariant time stamp counter
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    int32 ThermalHeadroom;
    uint64 L3Occupancy;
    void readAndAggregate(std::shared_ptr<SafeMsrHandle>);
    void readAndAggregateTSC(std::shared_ptr<SafeMsrHandle>);
    uint64 MemoryBWLocal;
    uint64 MemoryBWTotal;
    uint64 SMICount;

public:
    BasicCounterState() :
        InstRetiredAny(0),
        CpuClkUnhaltedThread(0),
        CpuClkUnhaltedRef(0),
        L3Miss(0),
        L3UnsharedHit(0),
        L2HitM(0),
        L2Hit(0),
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

    BasicCounterState & operator += (const BasicCounterState & o)
    {
        InstRetiredAny += o.InstRetiredAny;
        CpuClkUnhaltedThread += o.CpuClkUnhaltedThread;
        CpuClkUnhaltedRef += o.CpuClkUnhaltedRef;
        Event0 += o.Event0;
        Event1 += o.Event1;
        Event2 += o.Event2;
        Event3 += o.Event3;
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

/*! \brief Returns QPI LL clock ticks
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIClocks(uint32 port, const CounterStateType & before, const CounterStateType & after)
{
    return after.QPIClocks[port] - before.QPIClocks[port];
}


template <class CounterStateType>
int32 getThermalHeadroom(const CounterStateType & /* before */, const CounterStateType & after)
{
    return after.getThermalHeadroom();
}

/*! \brief Returns the number of QPI cycles in power saving half-lane mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIL0pTxCycles(uint32 port, const CounterStateType & before, const CounterStateType & after)
{
    return after.QPIL0pTxCycles[port] - before.QPIL0pTxCycles[port];
}

/*! \brief Returns the number of QPI cycles in power saving shutdown mode
    \param port QPI port number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getQPIL1Cycles(uint32 port, const CounterStateType & before, const CounterStateType & after)
{
    return after.QPIL1Cycles[port] - before.QPIL1Cycles[port];
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

/*! \brief Direct read of embedded DRAM memory controller counter (counter meaning depends on the programming: power/performance/etc)
    \param counter counter number
    \param channel channel number
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
*/
template <class CounterStateType>
uint64 getEDCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after)
{
    return after.EDCCounter[channel][counter] - before.EDCCounter[channel][counter];
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

/*!  \brief Returns energy consumed by processor, exclusing DRAM (measured in internal units)
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
    template <class CounterStateType>
    friend uint64 getBytesReadFromMC(const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getBytesWrittenToMC(const CounterStateType & before, const CounterStateType & after);
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
    friend double getPackageCStateResidency(int state, const CounterStateType & before, const CounterStateType & after);

protected:
    uint64 UncMCFullWrites;
    uint64 UncMCNormalReads;
    uint64 UncEDCFullWrites;
    uint64 UncEDCNormalReads;
    uint64 UncMCIORequests;
    uint64 PackageEnergyStatus;
    uint64 DRAMEnergyStatus;
    uint64 CStateResidency[PCM::MAX_C_STATE + 1];
    void readAndAggregate(std::shared_ptr<SafeMsrHandle>);

public:
    UncoreCounterState() :
        UncMCFullWrites(0),
        UncMCNormalReads(0),
        UncEDCFullWrites(0),
        UncEDCNormalReads(0),
        UncMCIORequests(0),
        PackageEnergyStatus(0),
        DRAMEnergyStatus(0)
    {
        memset(CStateResidency, 0, sizeof(CStateResidency));
    }
    virtual ~UncoreCounterState() { }

    UncoreCounterState & operator += (const UncoreCounterState & o)
    {
        UncMCFullWrites += o.UncMCFullWrites;
        UncMCNormalReads += o.UncMCNormalReads;
        UncEDCFullWrites += o.UncEDCFullWrites;
        UncEDCNormalReads += o.UncEDCNormalReads;
        UncMCIORequests += o.UncMCIORequests;
        PackageEnergyStatus += o.PackageEnergyStatus;
        DRAMEnergyStatus += o.DRAMEnergyStatus;
        for (int i = 0; i <= (int)PCM::MAX_C_STATE; ++i)
            CStateResidency[i] += o.CStateResidency[i];
        return *this;
    }
};


//! \brief Server uncore power counter state
//!
class ServerUncorePowerState : public UncoreCounterState
{
    uint64 QPIClocks[3], QPIL0pTxCycles[3], QPIL1Cycles[3];
    uint64 DRAMClocks[8];
    uint64 MCDRAMClocks[16];
    uint64 MCCounter[8][4]; // channel X counter
    uint64 EDCCounter[8][4]; // EDC controller X counter
    uint64 PCUCounter[4];
    int32 PackageThermalHeadroom;
    uint64 InvariantTSC;    // invariant time stamp counter
    friend class PCM;
    template <class CounterStateType>
    friend uint64 getQPIClocks(uint32 port, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getQPIL0pTxCycles(uint32 port, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getQPIL1Cycles(uint32 port, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMCDRAMClocks(uint32 channel, const CounterStateType & before, const CounterStateType & after);
    template <class CounterStateType>
    friend uint64 getMCCounter(uint32 channel, uint32 counter, const CounterStateType & before, const CounterStateType & after);
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
    ServerUncorePowerState() :
        PackageThermalHeadroom(0),
        InvariantTSC(0)
    {
        memset(&(QPIClocks[0]), 0, 3 * sizeof(uint64));
        memset(&(QPIL0pTxCycles[0]), 0, 3 * sizeof(uint64));
        memset(&(QPIL1Cycles[0]), 0, 3 * sizeof(uint64));
        memset(&(DRAMClocks[0]), 0, 8 * sizeof(uint64));
        memset(&(MCDRAMClocks[0]), 0, 16 * sizeof(uint64));
        memset(&(PCUCounter[0]), 0, 4 * sizeof(uint64));
        for (int i = 0; i < 8; ++i) {
            memset(&(MCCounter[i][0]), 0, 4 * sizeof(uint64));
            memset(&(EDCCounter[i][0]), 0, 4 * sizeof(uint64));
	}
    }
};

//! \brief (Logical) core-wide counter state
class CoreCounterState : public BasicCounterState
{
    friend class PCM;

public:
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
    void accumulateCoreState(const CoreCounterState & o)
    {
        BasicCounterState::operator += (o);
    }
};

//! \brief System-wide counter state
class SystemCounterState : public BasicCounterState, public UncoreCounterState
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

    void accumulateSocketState(const SocketCounterState & o)
    {
        {
            BasicCounterState::operator += (o);
            UncoreCounterState::operator += (o);
        }
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
    return now.InstRetiredAny;
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
    return now.CpuClkUnhaltedThread;
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
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    int64 ref_clocks = after.CpuClkUnhaltedRef - before.CpuClkUnhaltedRef;
    if (ref_clocks != 0)
        return double(clocks) / double(ref_clocks);
    return -1;
}

/*! \brief Estimates how many core cycles were potentially lost due to L3 cache misses.

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return ratio that is usually beetween 0 and 1 ; in some cases could be >1.0 due to a lower memory latency estimation
*/
template <class CounterStateType>
double getCyclesLostDueL3CacheMisses(const CounterStateType & before, const CounterStateType & after) // 0.0 - 1.0
{
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL) return -1;
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    if (clocks != 0)
    {
        return 180. * double(after.L3Miss - before.L3Miss) / double(clocks);
    }
    return -1;
}

/*! \brief Estimates how many core cycles were potentially lost due to missing L2 cache but still hitting L3 cache

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
        \warning Currently not supported on Intel(R) Atom(tm) processor
    \return ratio that is usually beetween 0 and 1 ; in some cases could be >1.0 due to a lower access latency estimation
*/
template <class CounterStateType>
double getCyclesLostDueL2CacheMisses(const CounterStateType & before, const CounterStateType & after) // 0.0 - 1.0
{
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL || PCM::getInstance()->useSkylakeEvents()) return -1;
    int64 clocks = after.CpuClkUnhaltedThread - before.CpuClkUnhaltedThread;
    if (clocks != 0)
    {
        double L3UnsharedHit = (double)(after.L3UnsharedHit - before.L3UnsharedHit);
        double L2HitM = (double)(after.L2HitM - before.L2HitM);
        return (35. * L3UnsharedHit + 74. * L2HitM) / double(clocks);
    }
    return -1;
}

/*! \brief Computes L2 cache hit ratio

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return value between 0 and 1
*/
template <class CounterStateType>
double getL2CacheHitRatio(const CounterStateType & before, const CounterStateType & after) // 0.0 - 1.0
{
    if (PCM::getInstance()->useSkylakeEvents()) {
        uint64 L2Hit = after.L2Hit - before.L2Hit;
        uint64 L2Ref = L2Hit + after.SKLL2Miss - before.SKLL2Miss;
        if (L2Ref) {
            return double(L2Hit) / double(L2Ref);
        }
        return 1;
    }
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL)
    {
        uint64 L2Miss = after.ArchLLCMiss - before.ArchLLCMiss;
        uint64 L2Ref = after.ArchLLCRef - before.ArchLLCRef;
        if (L2Ref) {
            return 1. - (double(L2Miss) / double(L2Ref));
        }
        return 1;
    }
    uint64 L3Miss = after.L3Miss - before.L3Miss;
    uint64 L3UnsharedHit = after.L3UnsharedHit - before.L3UnsharedHit;
    uint64 L2HitM = after.L2HitM - before.L2HitM;
    uint64 L2Hit = after.L2Hit - before.L2Hit;
    uint64 hits = L2Hit;
    uint64 all = L2Hit + L2HitM + L3UnsharedHit + L3Miss;
    if (all) return double(hits) / double(all);

    return 1;
}

/*! \brief Computes L3 cache hit ratio

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \warning Works only in the DEFAULT_EVENTS programming mode (see program() method)
    \return value between 0 and 1
*/
template <class CounterStateType>
double getL3CacheHitRatio(const CounterStateType & before, const CounterStateType & after) // 0.0 - 1.0
{
    if (PCM::getInstance()->useSkylakeEvents()) {
        uint64 L3Hit = after.SKLL3Hit - before.SKLL3Hit;
        uint64 L3Ref = L3Hit + after.L3Miss - before.L3Miss;
        if (L3Ref) {
            return double(L3Hit) / double(L3Ref);
        }
        return 1;
    }

    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL) return -1;

    uint64 L3Miss = after.L3Miss - before.L3Miss;
    uint64 L3UnsharedHit = after.L3UnsharedHit - before.L3UnsharedHit;
    uint64 L2HitM = after.L2HitM - before.L2HitM;
    uint64 hits = L3UnsharedHit + L2HitM;
    uint64 all = L2HitM + L3UnsharedHit + L3Miss;
    if (all) return double(hits) / double(all);

    return 1;
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
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL) return 0;
    return after.L3Miss - before.L3Miss;
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
    if (PCM::getInstance()->useSkylakeEvents()) {
        return after.SKLL2Miss - before.SKLL2Miss;
    }
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL)
    {
        return after.ArchLLCMiss - before.ArchLLCMiss;
    }
    uint64 L3Miss = after.L3Miss - before.L3Miss;
    uint64 L3UnsharedHit = after.L3UnsharedHit - before.L3UnsharedHit;
    uint64 L2HitM = after.L2HitM - before.L2HitM;
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
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL)
    {
        uint64 L2Miss = after.ArchLLCMiss - before.ArchLLCMiss;
        uint64 L2Ref = after.ArchLLCRef - before.ArchLLCRef;
        return L2Ref - L2Miss;
    }
    return after.L2Hit - before.L2Hit;
}

/*! \brief Computes L3 Cache Occupancy

*/
template <class CounterStateType>
uint64 getL3CacheOccupancy(const CounterStateType & now)
{
    return now.L3Occupancy;
}
/*! \brief Computes Local Memory Bandwidth

 */
template <class CounterStateType>
uint64 getLocalMemoryBW(const CounterStateType & before, const CounterStateType & after)
{
    return after.MemoryBWLocal - before.MemoryBWLocal;
}

/*! \brief Computes Remote Memory Bandwidth

 */
template <class CounterStateType>
uint64 getRemoteMemoryBW(const CounterStateType & before, const CounterStateType & after)
{
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
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL || PCM::getInstance()->useSkylakeEvents()) return 0;
    return after.L3UnsharedHit - before.L3UnsharedHit;
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
    if (PCM::getInstance()->useSkylakeEvents()) {
        return after.SKLL3Hit - before.SKLL3Hit;
    }
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL) return 0;
    return after.L2HitM - before.L2HitM;
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
    const int cpu_model = PCM::getInstance()->getCPUModel();
    if (cpu_model == PCM::ATOM || cpu_model == PCM::KNL) return 0;
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

/*! \brief Computes residency in the package C-state

    \param state C-state
    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return residence ratio (0..1): 0 - 0%, 1.0 - 100%
*/
template <class CounterStateType>
inline double getPackageCStateResidency(int state, const CounterStateType & before, const CounterStateType & after)
{
    return double(after.UncoreCounterState::CStateResidency[state] - before.UncoreCounterState::CStateResidency[state]) / double(getInvariantTSC(before, after));
}


/*! \brief Computes number of bytes read from DRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromMC(const CounterStateType & before, const CounterStateType & after)
{
    return (after.UncMCNormalReads - before.UncMCNormalReads) * 64;
}

/*! \brief Computes number of bytes written to DRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToMC(const CounterStateType & before, const CounterStateType & after)
{
    return (after.UncMCFullWrites - before.UncMCFullWrites) * 64;
}

/*! \brief Computes number of bytes read from MCDRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesReadFromEDC(const CounterStateType & before, const CounterStateType & after)
{
    return (after.UncEDCNormalReads - before.UncEDCNormalReads) * 64;
}

/*! \brief Computes number of bytes written to MCDRAM memory controllers

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getBytesWrittenToEDC(const CounterStateType & before, const CounterStateType & after)
{
    return (after.UncEDCFullWrites - before.UncEDCFullWrites) * 64;
}


/*! \brief Computes number of bytes of read/write requests from all IO sources

    \param before CPU counter state before the experiment
    \param after CPU counter state after the experiment
    \return Number of bytes
*/
template <class CounterStateType>
uint64 getIORequestBytesFromMC(const CounterStateType & before, const CounterStateType & after)
{
    return (after.UncMCIORequests - before.UncMCIORequests) * 64;
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
    return ((&after.Event0)[eventCounterNr] - (&before.Event0)[eventCounterNr]);
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
    if (!PCM::getInstance()->incomingQPITrafficMetricsAvailable()) return 0;
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
    if (!(m->outgoingQPITrafficMetricsAvailable())) return 0;

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
    return 64 * now.incomingQPIPackets[socketNr][linkNr];
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
    const uint64 memTraffic = getBytesReadFromMC(before, after) + getBytesWrittenToMC(before, after);
    return double(totalQPI) / double(memTraffic);
}

//! \brief Returns the raw count of events
//! \param before counter state before the experiment
//! \param after counter state after the experiment
template <class CounterType>
inline uint64 getNumberOfEvents(const CounterType & before, const CounterType & after)
{
    return after.data - before.data;
}

#endif
