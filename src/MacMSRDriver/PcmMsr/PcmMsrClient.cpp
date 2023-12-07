// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012, Intel Corporation
// written by Austen Ott
//    
#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <libkern/OSByteOrder.h>
#include "PcmMsrClient.h"

#define super IOUserClient

OSDefineMetaClassAndStructors(com_intel_driver_PcmMsrClient, IOUserClient)

const IOExternalMethodDispatch PcmMsrClientClassName::sMethods[kNumberOfMethods] = {
    { (IOExternalMethodAction) &PcmMsrClientClassName::sOpenDriver, 0, 0, 0, 0},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sCloseDriver, 0, 0, 0, 0},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sReadMSR, 0, kIOUCVariableStructureSize, 0, kIOUCVariableStructureSize},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sWriteMSR, 0, kIOUCVariableStructureSize, 0, 0},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sBuildTopology, 0, 0, 0, kIOUCVariableStructureSize},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sGetNumInstances, 0, 0, 1, 0},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sIncrementNumInstances, 0, 0, 1, 0},
    { (IOExternalMethodAction) &PcmMsrClientClassName::sDecrementNumInstances, 0, 0, 1, 0},
	{ (IOExternalMethodAction) &PcmMsrClientClassName::sRead, 1, 0, 1, 0 },
	{ (IOExternalMethodAction) &PcmMsrClientClassName::sWrite, 2, 0, 0, 0 },
	{ (IOExternalMethodAction) &PcmMsrClientClassName::sMapMemory, 1, 0, 2, 0 },
	{ (IOExternalMethodAction) &PcmMsrClientClassName::sUnmapMemory, 1, 0, 0, 0 },
	{ (IOExternalMethodAction) &PcmMsrClientClassName::sReadMemory, 1, 0, 1, 0 }

};

IOReturn PcmMsrClientClassName::externalMethod(uint32_t selector, IOExternalMethodArguments* args,													IOExternalMethodDispatch* dispatch, OSObject* target, void* reference)
{
    if (selector < (uint32_t) kNumberOfMethods) {
        dispatch = (IOExternalMethodDispatch *) &sMethods[selector];
        
        if (!target) {
			target = this;
		}
    }
	
	return super::externalMethod(selector, args, dispatch, target, reference);
}

bool PcmMsrClientClassName::initWithTask(task_t owningTask, void *securityToken, UInt32 type, OSDictionary *properties)
{
    if(!IOUserClient::initWithTask(owningTask, securityToken, type, properties)) {
        return false;
    }
    
    sSecurityToken = securityToken;
    return true;
}

bool PcmMsrClientClassName::start(IOService* provider)
{
	bool result = false;

    if(clientHasPrivilege(sSecurityToken, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) 
		return false;
		
    fProvider = OSDynamicCast(PcmMsrDriverClassName, provider);
    
    if (fProvider != NULL) {
		result = super::start(provider);
	}
    else
 		IOLog("PcmMsrClientClassName::start failed.\n");
        
    return result;
}

IOReturn PcmMsrClientClassName::clientClose(void)
{    
    closeUserClient();
    
	if (!terminate()) {
		IOLog("PcmMsrClientClassName::clientClose failed.\n");
	}
	
    return kIOReturnSuccess;
}

bool PcmMsrClientClassName::didTerminate(IOService* provider, IOOptionBits options, bool* defer)
{	
	closeUserClient();
	*defer = false;
	
	return super::didTerminate(provider, options, defer);
}


IOReturn PcmMsrClientClassName::sOpenDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments)
{
    return target->openUserClient();
}

IOReturn PcmMsrClientClassName::openUserClient(void)
{
    IOReturn	result = kIOReturnSuccess;
    
    if (fProvider == NULL || isInactive()) {
        result = kIOReturnNotAttached;
		IOLog("%s::%s returned kIOReturnNotAttached.\n", getName(), __FUNCTION__);
	} else if (!fProvider->open(this)) {
		result = kIOReturnExclusiveAccess;
		IOLog("%s::%s returned kIOReturnExclusiveAccess.\n", getName(), __FUNCTION__);
	}
	
    return result;
}

IOReturn PcmMsrClientClassName::checkActiveAndOpened (const char* memberFunction)
{
    if (fProvider == NULL || isInactive()) {
        IOLog("%s::%s returned kIOReturnNotAttached.\n", getName(), memberFunction);
        return (IOReturn)kIOReturnNotAttached;
        
    } else if (!fProvider->isOpen(this)) {
        IOLog("%s::%s returned kIOReturnNotOpen.\n", getName(), memberFunction);
        return  (IOReturn)kIOReturnNotOpen;
    }
    return kIOReturnSuccess;
}


IOReturn PcmMsrClientClassName::sCloseDriver(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments)
{
    return target->closeUserClient();
}

IOReturn PcmMsrClientClassName::closeUserClient(void)
{
    IOReturn	result = checkActiveAndOpened (__FUNCTION__);
    
    if (result == kIOReturnSuccess)
 		fProvider->close(this);

    return result;
}

IOReturn PcmMsrClientClassName::sReadMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments){
    return target->readMSR((pcm_msr_data_t*) arguments->structureInput, (pcm_msr_data_t*) arguments->structureOutput);
}

IOReturn PcmMsrClientClassName::readMSR(pcm_msr_data_t* idata, pcm_msr_data_t* odata)
{
    IOReturn	result = checkActiveAndOpened (__FUNCTION__);
    
    if (result == kIOReturnSuccess)
 		result = fProvider->readMSR(idata, odata);
    
    return result;
}

IOReturn PcmMsrClientClassName::sWriteMSR(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments){
    return target -> writeMSR((pcm_msr_data_t*)arguments->structureInput);
}

IOReturn PcmMsrClientClassName::writeMSR(pcm_msr_data_t* data)
{
    IOReturn	result = checkActiveAndOpened (__FUNCTION__);
    
    if (result == kIOReturnSuccess)
		result = fProvider->writeMSR(data);
    
    return result;
}

IOReturn PcmMsrClientClassName::sBuildTopology(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args){
    return target -> buildTopology((TopologyEntry*)args->structureOutput, args->structureOutputSize);
}

IOReturn PcmMsrClientClassName::buildTopology(TopologyEntry* data, size_t output_size)
{
    uint32_t num_cores = (uint32_t) (output_size / sizeof(TopologyEntry) );
    IOReturn	result = checkActiveAndOpened (__FUNCTION__);
    
    if (result == kIOReturnSuccess)
		result = fProvider->buildTopology(data, num_cores);
    
    return result;
}

IOReturn PcmMsrClientClassName::sGetNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args){
    return target->getNumInstances((uint32_t*)&args->scalarOutput[0]);
}
IOReturn PcmMsrClientClassName::getNumInstances(uint32_t* num_insts){
    return fProvider->getNumInstances(num_insts);
}

IOReturn PcmMsrClientClassName::sIncrementNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args){
    return target->incrementNumInstances((uint32_t*)&args->scalarOutput[0]);
}
IOReturn PcmMsrClientClassName::incrementNumInstances(uint32_t* num_insts){
    return fProvider->incrementNumInstances(num_insts);
}

IOReturn PcmMsrClientClassName::sDecrementNumInstances(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* args){
    return target->decrementNumInstances((uint32_t*)&args->scalarOutput[0]);
}
IOReturn PcmMsrClientClassName::decrementNumInstances(uint32_t* num_insts){
    return fProvider->decrementNumInstances(num_insts);
}



extern PcmMsrDriverClassName* g_pci_driver;

// read32
IOReturn PcmMsrClientClassName::sRead(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments) {
    return target->read(arguments->scalarInput, arguments->scalarInputCount, arguments->scalarOutput, arguments->scalarOutputCount);
}
IOReturn PcmMsrClientClassName::read(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);
	
	if (inputSize != 1) {
		IOLog("%s[%p]::%s(): returning kIOReturnBadArgument.\n", getName(), this, __FUNCTION__);
		return kIOReturnBadArgument;
	}
	
	uint32_t addr = (uint32_t)input[0];
	PRINT_DEBUG("addr: %x\n", addr);
	
	if (g_pci_driver) {
        output[0] = g_pci_driver->read(addr);
    }
	IOLog("val: %llx\n", output[0]);
	
    return kIOReturnSuccess;
}


// write32
IOReturn PcmMsrClientClassName::sWrite(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments) {
    return target->write(arguments->scalarInput, arguments->scalarInputCount);
}
IOReturn PcmMsrClientClassName::write(const uint64_t* input, uint32_t inputSize)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);
	
	if (inputSize != 2) {
		IOLog("%s[%p]::%s(): returning kIOReturnBadArgument.\n", getName(), this, __FUNCTION__);
		return kIOReturnBadArgument;
	}
	
	uint32_t addr = (uint32_t)input[0];
	uint32_t val  = (uint32_t)input[1];
	PRINT_DEBUG("addr: %x, val: %x\n", addr, val);
	
	if (g_pci_driver) {
        g_pci_driver->write(addr, val);
    }
	
    return kIOReturnSuccess;
}


// mapMemory
IOReturn PcmMsrClientClassName::sMapMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments) {
    return target->mapMemory(arguments->scalarInput, arguments->scalarInputCount, arguments->scalarOutput, arguments->scalarOutputCount);
}
IOReturn PcmMsrClientClassName::mapMemory(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);
	
	if (inputSize != 1) {
		IOLog("%s[%p]::%s(): returning kIOReturnBadArgument.\n", getName(), this, __FUNCTION__);
		return kIOReturnBadArgument;
	}
	
	uint32_t address = (uint32_t)input[0];
	PRINT_DEBUG("address: %x\n", address);
	
	if (g_pci_driver) {
		uint8_t* virtual_address = NULL;
		void* memory_map = g_pci_driver->mapMemory(address, (uint8_t**)&virtual_address);
		output[0] = (uint64_t)memory_map;
		output[1] = (uint64_t)virtual_address;
		PRINT_DEBUG("memory_map: %p\n", memory_map);
		PRINT_DEBUG("virtual_address: %p\n", virtual_address);
    }
	
    return kIOReturnSuccess;
}


// unmapMemory
IOReturn PcmMsrClientClassName::sUnmapMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments) {
    return target->unmapMemory(arguments->scalarInput, arguments->scalarInputCount);
}
IOReturn PcmMsrClientClassName::unmapMemory(const uint64_t* input, uint32_t inputSize)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);
	
	if (inputSize != 1) {
		IOLog("%s[%p]::%s(): returning kIOReturnBadArgument.\n", getName(), this, __FUNCTION__);
		return kIOReturnBadArgument;
	}
	
	void* memory_map = (void*)input[0];
	PRINT_DEBUG("memory_map: %p\n", memory_map);
	
	if (g_pci_driver) {
		g_pci_driver->unmapMemory(memory_map);
    }
	
    return kIOReturnSuccess;
}


// readMemory
IOReturn PcmMsrClientClassName::sReadMemory(PcmMsrClientClassName* target, void* reference, IOExternalMethodArguments* arguments) {
    return target->readMemory(arguments->scalarInput, arguments->scalarInputCount, arguments->scalarOutput, arguments->scalarOutputCount);
}
IOReturn PcmMsrClientClassName::readMemory(const uint64_t* input, uint32_t inputSize, uint64_t* output, uint32_t outputSize)
{
	PRINT_DEBUG("%s[%p]::%s()\n", getName(), this, __FUNCTION__);
	
	if (inputSize != 1) {
		IOLog("%s[%p]::%s(): returning kIOReturnBadArgument.\n", getName(), this, __FUNCTION__);
		return kIOReturnBadArgument;
	}
	
	uint8_t* address = (uint8_t*)input[0];
	PRINT_DEBUG("address: %p\n", address);
	
	uint32_t val = 0;
	if (g_pci_driver) {
		val = *(uint32_t*)address;
    }
	output[0] = (uint64_t)val;
	PRINT_DEBUG("val: %x\n", val);
	
    return kIOReturnSuccess;
}
