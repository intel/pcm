/*
 Copyright (c) 2012, Intel Corporation
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
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
#define cpuid(func1, func2, a, b, c, d) \
asm volatile ("cpuid" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "a" (func1), "c" (func2));

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
    volatile uint cpu = cpu_number();
    if(data->cpu_num == cpu)
    {
        data->value = RDMSR(data->msr_num);
    }
}

void cpuWriteMSR(void* pIDatas){
    pcm_msr_data_t* idatas = (pcm_msr_data_t*)pIDatas;
    volatile uint cpu = cpu_number();
    if(idatas->cpu_num == cpu)
    {
        WRMSR(idatas->msr_num, idatas->value);
    }
}

void cpuGetTopoData(void* pTopos){
    kTopologyEntry* entries = (kTopologyEntry*)pTopos;
    volatile uint cpu = cpu_number();
    int info[4];
    entries[cpu].os_id = cpu;
    cpuid(0xB, 1, info[0], info[1], info[2], info[3]);
    entries[cpu].socket = info[3] >> info[0] & 0xF;
    
    cpuid(0xB, 0, info[0], info[1], info[2], info[3]);
    entries[cpu].core_id = info[3] >> info[0] & 0xF;
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
uint32_t PcmMsrDriverClassName::getNumCores()
{
    size_t size;
    char* pParam;
    uint32_t ret = 0;
    if(!sysctlbyname("hw.logicalcpu", NULL, &size, NULL, 0))
    {
        if(NULL != (pParam = (char*)IOMalloc(size)))
        {
            if(!sysctlbyname("hw.logicalcpu", (void*)pParam, &size, NULL, 0))
            {
                if(sizeof(int) == size)
                    ret = *(int*)pParam;
                else if(sizeof(long) == size)
                    ret = (uint32_t) *(long*)pParam;
                else if(sizeof(long long) == size)
                    ret = (uint32_t) *(long long*)pParam;
                else
                    ret = *(int*)pParam;
            }
            IOFree(pParam, size);
        }
    }
    return ret;
}

bool PcmMsrDriverClassName::init(OSDictionary *dict)
{
    num_cores = getNumCores();
    bool result = super::init(dict);
    topologies = 0;
    if(result && num_cores != 0)
    {
        topologies = (kTopologyEntry*)IOMallocAligned(sizeof(kTopologyEntry)*num_cores, 128);
    }
    return (result && topologies && num_cores != 0);
}

void PcmMsrDriverClassName::free()
{
    if(topologies)
        IOFreeAligned(topologies, sizeof(kTopologyEntry)*num_cores);
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

IOReturn PcmMsrDriverClassName::buildTopology(topologyEntry* odata, uint32_t input_num_cores){
    mp_rendezvous_no_intrs(cpuGetTopoData, (void*)topologies);
    for(uint32_t i = 0; i < num_cores && i < input_num_cores; i++)
    {
        odata[i].core_id = topologies[i].core_id;
        odata[i].os_id = topologies[i].os_id;
        odata[i].socket = topologies[i].socket;
    }
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
	
    memory_descriptor = IOMemoryDescriptor::withPhysicalAddress(address,
                                                                4096,
                                                                kIODirectionInOut);
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
            memory_descriptor->release();
			IOLog("%s[%p]::%s() -- IOMemoryDescriptor::prepare() failure\n", getName(), this, __FUNCTION__);
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
        m_map->getMemoryDescriptor()->release();
        m_map->unmap();
        m_map->release();
    }
	
    return;
}
