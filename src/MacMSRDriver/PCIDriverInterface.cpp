// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013, Intel Corporation
// written by Patrick Konsor
//

#include <stdio.h>
#include <map>
#include "PCIDriverInterface.h"
#include <IOKit/IOKitLib.h>
#include "PcmMsr/UserKernelShared.h"

io_connect_t PCIDriver_connect = 0;
std::map<uint8_t*,void*> PCIDriver_mmap;

// setupDriver
#ifdef __cplusplus
extern "C"
#endif
int PCIDriver_setupDriver()
{
     kern_return_t   kern_result;
     io_iterator_t   iterator;
     bool            driverFound = false;
     io_service_t    local_driver_service;

     // get services
     kern_result = IOServiceGetMatchingServices(kIOMainPortDefault,
                                                IOServiceMatching(kPcmMsrDriverClassName),
                                                &iterator);
     if (kern_result != KERN_SUCCESS) {
          fprintf(stderr, "[error] IOServiceGetMatchingServices returned 0x%08x\n", kern_result);
          return kern_result;
     }

     // find service
     while ((local_driver_service = IOIteratorNext(iterator)) != IO_OBJECT_NULL) {
          driverFound = true;
          break;
     }

     if (driverFound == false) {
          fprintf(stderr, "[error] No matching drivers found \"%s\".\n", kPcmMsrDriverClassName);
          return KERN_FAILURE;
     }
     IOObjectRelease(iterator);

     // connect to service
     kern_result = IOServiceOpen(local_driver_service, mach_task_self(), 0, &PCIDriver_connect);
     if (kern_result != KERN_SUCCESS) {
          fprintf(stderr, "[error] IOServiceOpen returned 0x%08x\n", kern_result);
          return kern_result;
     }

     return KERN_SUCCESS;
}


// read32
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_read32(uint32_t addr, uint32_t* val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	uint64_t input[] = { (uint64_t)addr };
	uint64_t val_ = 0;
	uint32_t outputCnt = 1;
	kern_return_t result = IOConnectCallScalarMethod(PCIDriver_connect, kRead, input, 1, &val_, &outputCnt);
	*val = (uint32_t)val_;
	return result;
}


// read64
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_read64(uint32_t addr, uint64_t* val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	kern_return_t result;
	uint64_t input[] = { (uint64_t)addr };
	uint64_t lo = 0;
	uint64_t hi = 0;
	uint32_t outputCnt = 1;
	result  = IOConnectCallScalarMethod(PCIDriver_connect, kRead, input, 1, &lo, &outputCnt);
	input[0] = (uint64_t)addr + 4;
	result |= IOConnectCallScalarMethod(PCIDriver_connect, kRead, input, 1, &hi, &outputCnt);
	*val = (hi << 32) | lo;
	return result;
}


// write32
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_write32(uint32_t addr, uint32_t val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	uint64_t input[] = { (uint64_t)addr, (uint64_t)val };
	return IOConnectCallScalarMethod(PCIDriver_connect, kWrite, input, 2, NULL, 0);
}


// write64
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_write64(uint32_t addr, uint64_t val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	kern_return_t result;
	uint64_t input[] = { (uint64_t)addr, val & 0xffffffff };
	result  = IOConnectCallScalarMethod(PCIDriver_connect, kWrite, input, 2, NULL, 0);
	input[0] = (uint64_t)addr + 4;
	input[1] = val >> 32;
	result |= IOConnectCallScalarMethod(PCIDriver_connect, kWrite, input, 2, NULL, 0);
	return result;
}

// mapMemory
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_mapMemory(uint32_t address, uint8_t** virtual_address)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	uint64_t input[] = { (uint64_t)address };
	uint64_t output[2];
	uint32_t outputCnt = 2;
	kern_return_t result = IOConnectCallScalarMethod(PCIDriver_connect, kMapMemory, input, 1, output, &outputCnt);
	PCIDriver_mmap[(uint8_t*)output[1]] = (void*)output[0];
	*virtual_address = (uint8_t*)output[1];
	return result;
}


// unmapMemory
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_unmapMemory(uint8_t* virtual_address)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	
	void* memory_map = PCIDriver_mmap[virtual_address];
	if (memory_map != NULL) {
		uint64_t input[] = { (uint64_t)memory_map };
		kern_return_t result = IOConnectCallScalarMethod(PCIDriver_connect, kUnmapMemory, input, 1, NULL, 0);
		PCIDriver_mmap.erase(virtual_address); // remove from map
		return result;
	} else {
		return KERN_INVALID_ADDRESS;
	}
}

// readMemory32
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_readMemory32(uint8_t* address, uint32_t* val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	uint64_t input[] = { (uint64_t)address };
	uint64_t val_ = 0;
	uint32_t outputCnt = 1;
	kern_return_t result = IOConnectCallScalarMethod(PCIDriver_connect, kReadMemory, input, 1, &val_, &outputCnt);
	*val = (uint32_t)val_;
	return result;
}


// readMemory64
#ifdef __cplusplus
extern "C"
#endif
uint32_t PCIDriver_readMemory64(uint8_t* address, uint64_t* val)
{
	if (!PCIDriver_connect) {
		if (PCIDriver_setupDriver() != KERN_SUCCESS) {
			return KERN_FAILURE;
		}
	}
	kern_return_t result;
	uint64_t input[] = { (uint64_t)address };
	uint64_t lo = 0;
	uint64_t hi = 0;
	uint32_t outputCnt = 1;
	result  = IOConnectCallScalarMethod(PCIDriver_connect, kReadMemory, input, 1, &lo, &outputCnt);
	input[0] = (uint64_t)address + 4;
	result |= IOConnectCallScalarMethod(PCIDriver_connect, kReadMemory, input, 1, &hi, &outputCnt);
	*val = (hi << 32) | lo;
	return result;
}
