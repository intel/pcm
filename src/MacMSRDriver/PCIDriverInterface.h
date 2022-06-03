// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2013, Intel Corporation
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
