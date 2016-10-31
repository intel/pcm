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
#include <IOKit/IOUserClient.h>
#include "PcmMsr.h"

#define PcmMsrClientClassName com_intel_driver_PcmMsrClient

class PcmMsrClientClassName : public IOUserClient
{
    OSDeclareDefaultStructors(com_intel_driver_PcmMsrClient)
    
protected:
    PcmMsrDriverClassName*                  fProvider;
    static const IOExternalMethodDispatch   sMethods[kNumberOfMethods];
    
public:
    virtual bool start(IOService *provider);
    
    virtual IOReturn clientClose(void);
    
    virtual bool didTerminate(IOService* provider, IOOptionBits opts, bool* defer);
    
protected:
    IOReturn checkActiveAndOpened (const char* memberFunction);
    
    virtual IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments,
									IOExternalMethodDispatch* dispatch, OSObject* target, void* reference);
    
    static IOReturn sOpenDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn openUserClient(void);
    
    static  IOReturn sCloseDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn closeUserClient(void);
    
    static IOReturn sReadMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn readMSR(pcm_msr_data_t* idata, pcm_msr_data_t* odata);
    
    static IOReturn sWriteMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn writeMSR(pcm_msr_data_t* data);
    
    static IOReturn sBuildTopology(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args);
    virtual IOReturn buildTopology(topologyEntry* data, size_t output_size);
    
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