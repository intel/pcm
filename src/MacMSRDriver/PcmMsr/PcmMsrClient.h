// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include "PcmMsr.h"

#define PcmMsrClientClassName com_intel_driver_PcmMsrClient

class PcmMsrClientClassName : public IOUserClient
{
    OSDeclareDefaultStructors(com_intel_driver_PcmMsrClient)

protected:
    PcmMsrDriverClassName*                  fProvider;
    void*                                   sSecurityToken;
    static const IOExternalMethodDispatch   sMethods[kNumberOfMethods];

public:
    virtual bool initWithTask(task_t owningTask, void *securityToken, UInt32 type, OSDictionary *properties) override;
    virtual bool start(IOService *provider) override;
    
    virtual IOReturn clientClose(void) override;

    virtual bool didTerminate(IOService* provider, IOOptionBits opts, bool* defer) override;

protected:
    IOReturn checkActiveAndOpened (const char* memberFunction);

    virtual IOReturn externalMethod(uint32_t selector,
                                    IOExternalMethodArguments* arguments,
                                    IOExternalMethodDispatch* dispatch,
                                    OSObject* target, void* reference) override;

    static IOReturn sOpenDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn openUserClient(void);
    
    static  IOReturn sCloseDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn closeUserClient(void);
    
    static IOReturn sReadMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn readMSR(pcm_msr_data_t* idata, pcm_msr_data_t* odata);
    
    static IOReturn sWriteMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn writeMSR(pcm_msr_data_t* data);
    
    static IOReturn sBuildTopology(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn buildTopology(TopologyEntry* data, size_t output_size);
    
    static IOReturn sGetNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn getNumInstances(uint32_t* num_insts);
    
    static IOReturn sIncrementNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn incrementNumInstances(uint32_t* num_insts);
    
    static IOReturn sDecrementNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn decrementNumInstances(uint32_t* num_insts);
	
	// PCI functions
	static IOReturn sRead(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments);
	virtual IOReturn read(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize);
	
	static IOReturn sWrite(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments);
	virtual IOReturn write(const uint64_t* input, uint32_t inputSize);
	
	static IOReturn sMapMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments);
	virtual IOReturn mapMemory(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize);
	
	static IOReturn sUnmapMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments);
	virtual IOReturn unmapMemory(const uint64_t* input, uint32_t inputSize);
	
	static IOReturn sReadMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments);
	virtual IOReturn readMemory(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize);
};
