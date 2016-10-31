/*
 Copyright (c) 2013, Intel Corporation
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
// written by Patrick Konsor
//

#ifndef pci_driver_driverinterface_h
#define pci_driver_driverinterface_h

#ifdef __cplusplus
extern "C" {
#endif

#define PCI_ENABLE                         0x80000000
#define FORM_PCI_ADDR(bus,dev,fun,off)     (((PCI_ENABLE))          |   \
                                            ((bus & 0xFF) << 16)    |   \
                                            ((dev & 0x1F) << 11)    |   \
                                            ((fun & 0x07) <<  8)    |   \
                                            ((off & 0xFF) <<  0))

uint32_t PCIDriver_read32(uint32_t addr, uint32_t* val);
uint32_t PCIDriver_read64(uint32_t addr, uint64_t* val);
uint32_t PCIDriver_write32(uint32_t addr, uint32_t val);
uint32_t PCIDriver_write64(uint32_t addr, uint64_t val);
uint32_t PCIDriver_mapMemory(uint32_t address, uint8_t** virtual_address);
uint32_t PCIDriver_unmapMemory(uint8_t* virtual_address);
uint32_t PCIDriver_readMemory32(uint8_t* address, uint32_t* val);
uint32_t PCIDriver_readMemory64(uint8_t* address, uint64_t* val);

#ifdef __cplusplus
}
#endif
	
#endif
