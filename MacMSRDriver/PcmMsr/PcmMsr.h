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
#include <IOKit/IOService.h>
#include "UserKernelShared.h"

class PcmMsrDriverClassName : public IOService
{
    OSDeclareDefaultStructors(com_intel_driver_PcmMsr)
public:
    // IOService methods
    virtual bool start(IOService* provider);
    
    virtual IOReturn writeMSR(pcm_msr_data_t* data);
    virtual IOReturn readMSR(pcm_msr_data_t* idata,pcm_msr_data_t* odata);
    virtual IOReturn buildTopology(topologyEntry* odata, uint32_t input_num_cores);
    virtual bool init(OSDictionary *dict);
    virtual void free(void);
    virtual bool handleOpen(IOService* forClient, IOOptionBits opts, void* args);
    virtual bool handleIsOpen(const IOService* forClient) const;
    virtual void handleClose(IOService* forClient, IOOptionBits opts);
    
    virtual uint32_t getNumCores();
    
    virtual IOReturn incrementNumInstances(uint32_t* num_instances);
    virtual IOReturn decrementNumInstances(uint32_t* num_instances);
    virtual IOReturn getNumInstances(uint32_t* num_instances);
	
	// PCI classes
	static uint32_t read(uint32_t pci_address);
	static void write(uint32_t pci_address, uint32_t value);
    void* mapMemory(uint32_t address, UInt8 **virtual_address);
    void unmapMemory(void* memory_map);
	
private:
    // number of providers currently using the driver
    uint32_t num_clients = 0;
    uint32_t num_cores;
    kTopologyEntry *topologies;
};

#ifdef DEBUG
#define _DEBUG 1
#else
#define _DEBUG 0
#endif
#define PRINT_DEBUG if (_DEBUG) IOLog
