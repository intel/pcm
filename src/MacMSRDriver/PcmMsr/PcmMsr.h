// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//    
#include <IOKit/IOService.h>
#include "UserKernelShared.h"

class PcmMsrDriverClassName : public IOService
{
    OSDeclareDefaultStructors(com_intel_driver_PcmMsr)
public:
    // IOService methods
    virtual bool start(IOService* provider) override;
    
    virtual IOReturn writeMSR(pcm_msr_data_t* data);
    virtual IOReturn readMSR(pcm_msr_data_t* idata,pcm_msr_data_t* odata);
    virtual IOReturn buildTopology(TopologyEntry* odata, uint32_t input_num_cores);
    virtual bool init(OSDictionary *dict) override;
    virtual void free(void) override;
    virtual bool handleOpen(IOService* forClient, IOOptionBits opts, void* args) override;
    virtual bool handleIsOpen(const IOService* forClient) const override;
    virtual void handleClose(IOService* forClient, IOOptionBits opts) override;
    
    virtual int32_t getNumCores();
    
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
    int32_t num_cores;
};

#ifdef DEBUG
#define _DEBUG 1
#else
#define _DEBUG 0
#endif
#define PRINT_DEBUG if (_DEBUG) IOLog
