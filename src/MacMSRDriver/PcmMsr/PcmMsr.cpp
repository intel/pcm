// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//
#include <IOKit/IOLib.h>
#include <libkern/sysctl.h>
#include "PcmMsr.h"

PcmMsrDriverClassName *g_pci_driver = NULL;

#define wrmsr(msr,lo,hi) \
asm volatile ("wrmsr" : : "c" (msr), "a" (lo), "d" (hi))
#define rdmsr(msr,lo,hi) \
asm volatile ("\trdmsr\n" : "=a" (lo), "=d" (hi) : "c" (msr))

extern "C" {
    extern void mp_rendezvous_no_intrs(void (*func)(void *),
                                       void *arg);
    extern int cpu_number(void);
}

inline uint64_t RDMSR(uint32_t msr)
{
    uint64_t value;
	uint32_t low, hi;
	rdmsr(msr, low, hi);
	value = ((uint64_t) hi << 32) | low;
	return value;
}

inline void WRMSR(uint32_t msr, uint64_t value)
{
    uint32_t low, hi;
    low = (uint32_t)value;
    hi = (uint32_t) (value >> 32);
    wrmsr(msr, low, hi);
}

void cpuReadMSR(void* pIData){
    pcm_msr_data_t* data = (pcm_msr_data_t*)pIData;
    int cpu = cpu_number();
    if(data->cpu_num == cpu)
    {
        data->value = RDMSR(data->msr_num);
    }
}

void cpuWriteMSR(void* pIDatas){
    pcm_msr_data_t* idatas = (pcm_msr_data_t*)pIDatas;
    int cpu = cpu_number();
    if(idatas->cpu_num == cpu)
    {
        WRMSR(idatas->msr_num, idatas->value);
    }
}

void cpuGetTopoData(void* pTopos){
    TopologyEntry* entries = (TopologyEntry*)pTopos;
    const int cpu = cpu_number();

    TopologyEntry & entry = entries[cpu];
    entry.os_id = cpu;

    uint32 smtMaskWidth = 0;
    uint32 coreMaskWidth = 0;
    uint32 l2CacheMaskShift = 0;
    initCoreMasks(smtMaskWidth, coreMaskWidth, l2CacheMaskShift);
    PCM_CPUID_INFO cpuid_args;
    pcm_cpuid(0xb, 0x0, cpuid_args);
    fillEntry(entry, smtMaskWidth, coreMaskWidth, l2CacheMaskShift, cpuid_args.array[3]);
}

OSDefineMetaClassAndStructors(com_intel_driver_PcmMsr, IOService)

#define super IOService

bool PcmMsrDriverClassName::start(IOService* provider){
    bool	success;
    success = super::start(provider);

	if (!g_pci_driver) {
		g_pci_driver = this;
	}

	if (success) {
		registerService();
	}

    return success;
}

int32_t PcmMsrDriverClassName::getNumCores()
{
    int32_t ncpus = 0;
    size_t ncpus_size = sizeof(ncpus);
    if(sysctlbyname("hw.logicalcpu", &ncpus, &ncpus_size, NULL, 0))
    {
         IOLog("%s[%p]::%s() -- sysctl failure retrieving hw.logicalcpu",
               getName(), this, __FUNCTION__);
         ncpus = 0;
    }

    return ncpus;
}

bool PcmMsrDriverClassName::init(OSDictionary *dict)
{
    bool result = super::init(dict);

    if (result) {
         num_cores = getNumCores();
    }

    return result && num_cores;
}

void PcmMsrDriverClassName::free()
{
    super::free();
}

// We override handleOpen, handleIsOpen, and handleClose to allow multiple clients to access the driver
// simultaneously. We always return true for these because we don't care who is accessing and we
// don't know how many people will be accessing it.
bool PcmMsrDriverClassName::handleOpen(IOService * forClient, IOOptionBits opts, void* args){
    return true;
}

bool PcmMsrDriverClassName::handleIsOpen(const IOService* forClient) const{
    return true;
}

void PcmMsrDriverClassName::handleClose(IOService* forClient, IOOptionBits opts){
}

IOReturn PcmMsrDriverClassName::readMSR(pcm_msr_data_t* idatas,pcm_msr_data_t* odatas){
    // All the msr_nums should be the same, so we just use the first one to pass to all cores
    IOReturn ret = kIOReturnBadArgument;
    if(idatas->cpu_num < num_cores)
    {
        mp_rendezvous_no_intrs(cpuReadMSR, (void*)idatas);

        odatas->cpu_num = idatas->cpu_num;
        odatas->msr_num = idatas->msr_num;
        odatas->value = idatas->value;
        ret = kIOReturnSuccess;
    }
    else
    {
        IOLog("Tried to read from a core with id higher than max core id.\n");
    }
    return ret;
}

IOReturn PcmMsrDriverClassName::writeMSR(pcm_msr_data_t* idata){
    IOReturn ret = kIOReturnBadArgument;
    if(idata->cpu_num < num_cores)
    {
        mp_rendezvous_no_intrs(cpuWriteMSR, (void*)idata);

        ret = kIOReturnSuccess;
    }
    else
    {
        IOLog("Tried to write to a core with id higher than max core id.\n");
    }

    return ret;
}

IOReturn PcmMsrDriverClassName::buildTopology(TopologyEntry* odata, uint32_t input_num_cores)
{
     size_t topologyBufferSize;

     // TODO figure out when input_num_cores is used rather than num_cores
     if (os_mul_overflow(sizeof(TopologyEntry), (size_t) num_cores, &topologyBufferSize))
     {
          return kIOReturnBadArgument;
     }

    TopologyEntry *topologies =
         (TopologyEntry *)IOMallocAligned(topologyBufferSize, 32);

    if (topologies == nullptr)
    {
        return kIOReturnNoMemory;
    }

    mp_rendezvous_no_intrs(cpuGetTopoData, (void*)topologies);

    for(uint32_t i = 0; i < num_cores && i < input_num_cores; i++)
    {
        odata[i].os_id = topologies[i].os_id;
        odata[i].thread_id = topologies[i].thread_id;
        odata[i].core_id = topologies[i].core_id;
        odata[i].tile_id = topologies[i].tile_id;
        odata[i].socket_id = topologies[i].socket_id;
    }

    IOFreeAligned(topologies, topologyBufferSize);
    return kIOReturnSuccess;
}

IOReturn PcmMsrDriverClassName::getNumInstances(uint32_t* num_insts){
    *num_insts = num_clients;
    return kIOReturnSuccess;
}

IOReturn PcmMsrDriverClassName::incrementNumInstances(uint32_t* num_insts){
    *num_insts = ++num_clients;
    return kIOReturnSuccess;
}

IOReturn PcmMsrDriverClassName::decrementNumInstances(uint32_t* num_insts){
    *num_insts = --num_clients;
    return kIOReturnSuccess;
}

// read
uint32_t PcmMsrDriverClassName::read(uint32_t pci_address)
{
    uint32_t value = 0;

    __asm__("\t"
			"movw $0xCF8,%%dx\n\t"
			"andb $0xFC,%%al\n\t"
			"outl %%eax,%%dx\n\t"
			"movl $0xCFC,%%edx\n\t"
			"in   %%dx,%%eax\n"
			: "=a"(value)
			: "a"(pci_address)
			: "%edx");

    return value;
}


// write
void PcmMsrDriverClassName::write(uint32_t pci_address, uint32_t value)
{

	__asm__("\t"
			"movw $0xCF8,%%dx\n\t"
			"andb $0xFC,%%al\n\t"
			"outl %%eax,%%dx\n\t"
			"movl $0xCFC,%%edx\n\t"
			"movl %%ebx,%%eax\n\t"
			"outl %%eax,%%dx\n"
			:
			: "a"(pci_address), "b"(value)
			: "%edx");
}


// mapMemory
void* PcmMsrDriverClassName::mapMemory (uint32_t address, UInt8 **virtual_address)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);

    IOMemoryMap        *memory_map        = NULL;
    IOMemoryDescriptor *memory_descriptor = NULL;
	#ifndef __clang_analyzer__ // address a false-positive
    memory_descriptor = IOMemoryDescriptor::withPhysicalAddress(address,
                                                                4096,
                                                                kIODirectionInOut);
    #endif
    if (memory_descriptor) {
        IOReturn ioErr = memory_descriptor->prepare(kIODirectionInOut);
        if (ioErr == kIOReturnSuccess) {
            memory_map = memory_descriptor->map();
            if (memory_map) {
                if (virtual_address) {
                    *virtual_address = (UInt8*)memory_map->getVirtualAddress();
                } else {
					IOLog("%s[%p]::%s() -- virtual_address is null\n", getName(), this, __FUNCTION__);
				}
            } else {
				IOLog("%s[%p]::%s() -- IOMemoryDescriptor::map() failure\n", getName(), this, __FUNCTION__);
			}
        }
        else {
			IOLog("%s[%p]::%s() -- IOMemoryDescriptor::prepare() failure\n", getName(), this, __FUNCTION__);
        }
        if (!memory_map)
        {
            memory_descriptor->release();
        }
    } else {
		IOLog("%s[%p]::%s() -- IOMemoryDescriptor::withPhysicalAddress() failure\n", getName(), this, __FUNCTION__);
	}

    return (void*)memory_map;
}


// unmapMemory
void PcmMsrDriverClassName::unmapMemory (void *memory_map)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);

    IOMemoryMap *m_map = (IOMemoryMap*)memory_map;

    if (m_map) {
        m_map->getMemoryDescriptor()->complete();
        #ifndef __clang_analyzer__ // address a false-positive
        m_map->getMemoryDescriptor()->release();
        #endif
        m_map->unmap();
        m_map->release();
    }

    return;
}
