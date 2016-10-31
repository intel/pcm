/*
Copyright (c) 2009-2013, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev,
//            Patrick Konsor
//

#include <iostream>
#include <string.h>
#ifndef _MSC_VER
#include <sys/types.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>
#include "pci.h"
#include "client_bw.h"

#ifndef _MSC_VER
#include <sys/mman.h>
#include <errno.h>
#endif

#ifdef _MSC_VER

#include <windows.h>

class PCMPmem: public WinPmem {
 protected:
  virtual int load_driver_()
  {
      SYSTEM_INFO sys_info;
      ZeroMemory(&sys_info, sizeof(sys_info));

      GetCurrentDirectory(MAX_PATH - 10, driver_filename);

      GetNativeSystemInfo(&sys_info);
      switch(sys_info.wProcessorArchitecture)
      {
          case PROCESSOR_ARCHITECTURE_AMD64:
            wcscat_s(driver_filename, MAX_PATH, L"\\winpmem_64.sys");
            if(GetFileAttributes(driver_filename) ==  INVALID_FILE_ATTRIBUTES)
            {
                std::cout << "ERROR: winpmem_64.sys not found in current directory. Download it from https://volatility.googlecode.com/svn-history/r2813/branches/scudette/tools/windows/winpmem/binaries/winpmem_64.sys ." << std::endl;
                std::cout << "ERROR: Memory bandwidth statistics will not be available." << std::endl;
            }
            break;
          case PROCESSOR_ARCHITECTURE_INTEL:
            wcscat_s(driver_filename, MAX_PATH, L"\\winpmem_32.sys");
            if(GetFileAttributes(driver_filename) ==  INVALID_FILE_ATTRIBUTES)
            {
                std::cout << "ERROR: winpmem_32.sys not found in current directory. Download it from https://volatility.googlecode.com/svn-history/r2813/branches/scudette/tools/windows/winpmem/binaries/winpmem_32.sys ." << std::endl;
                std::cout << "ERROR: Memory bandwidth statistics will not be available." << std::endl;
            }
            break;
        default:
            return -1;
      }
      return 1;
  }
  virtual int write_crashdump_header_(struct PmemMemoryInfo *info)
  {
      return -1;
  }
};

ClientBW::ClientBW() : pmem(new PCMPmem())
{
    pmem->install_driver(false);
    pmem->set_acquisition_mode(PMEM_MODE_IOSPACE);

    PciHandleM imcHandle(0,0,0,0); // memory controller device coordinates: domain 0, bus 0, device 0, function 0
    uint64 imcbar = 0;
    imcHandle.read64(PCM_CLIENT_IMC_BAR_OFFSET, &imcbar);
    // std::cout << "DEBUG: imcbar="<<std::hex << imcbar <<std::endl;
    if(!imcbar)
    {
       std::cerr <<"ERROR: imcbar is zero." << std::endl;
       throw std::exception();
    }
    startAddr = imcbar & (~(4096ULL-1ULL)); // round down to 4K
}

uint64 ClientBW::getImcReads()
{
    mutex.lock();
    uint32 res = pmem->read32(startAddr + PCM_CLIENT_IMC_DRAM_DATA_READS); 
    mutex.unlock();
    return (uint64)res;
}

uint64 ClientBW::getImcWrites()
{
   mutex.lock();
   uint32 res = pmem->read32(startAddr + PCM_CLIENT_IMC_DRAM_DATA_WRITES);
   mutex.unlock();
   return (uint64)res;
}

uint64 ClientBW::getIoRequests()
{
   mutex.lock();
   uint32 res = pmem->read32(startAddr + PCM_CLIENT_IMC_DRAM_IO_REQESTS);
   mutex.unlock();
   return (uint64)res;
}

ClientBW::~ClientBW()
{
    pmem->uninstall_driver();
}


#elif __APPLE__

#include "PCIDriverInterface.h"

#define CLIENT_BUS			0
#define CLIENT_DEV			0
#define CLIENT_FUNC			0
#define CLIENT_BAR_MASK		0x0007FFFFF8000LL
#define CLIENT_EVENT_BASE	0x5000

ClientBW::ClientBW()
{
	uint64_t bar = 0;
	uint32_t pci_address = FORM_PCI_ADDR(CLIENT_BUS, CLIENT_DEV, CLIENT_FUNC, PCM_CLIENT_IMC_BAR_OFFSET);
	PCIDriver_read64(pci_address, &bar);
	uint64_t physical_address = (bar & CLIENT_BAR_MASK) + CLIENT_EVENT_BASE;//bar & (~(4096-1));
	mmapAddr = NULL;
	if (physical_address) {
		PCIDriver_mapMemory((uint32_t)physical_address, (uint8_t**)&mmapAddr);
	}
}

uint64 ClientBW::getImcReads()
{
	uint32_t val = 0;
	PCIDriver_readMemory32((uint8_t*)mmapAddr + PCM_CLIENT_IMC_DRAM_DATA_READS - CLIENT_EVENT_BASE, &val);
	return (uint64_t)val;
}

uint64 ClientBW::getImcWrites()
{
	uint32_t val = 0;
	PCIDriver_readMemory32((uint8_t*)mmapAddr + PCM_CLIENT_IMC_DRAM_DATA_WRITES - CLIENT_EVENT_BASE, &val);
	return (uint64_t)val;
}

uint64 ClientBW::getIoRequests()
{
	uint32_t val = 0;
	PCIDriver_readMemory32((uint8_t*)mmapAddr + PCM_CLIENT_IMC_DRAM_IO_REQESTS - CLIENT_EVENT_BASE, &val);
	return (uint64_t)val;
}

ClientBW::~ClientBW() {
	PCIDriver_unmapMemory((uint8_t*)mmapAddr);
}

#else

#if defined(__linux__) || defined(__FreeBSD__)
// Linux implementation

ClientBW::ClientBW() :
    fd(-1),
    mmapAddr(NULL)
{
    int handle = ::open("/dev/mem", O_RDONLY);
    if (handle < 0) throw std::exception();
    fd = handle;

    PciHandleM imcHandle(0,0,0,0); // memory controller device coordinates: domain 0, bus 0, device 0, function 0
    uint64 imcbar = 0;
    imcHandle.read64(PCM_CLIENT_IMC_BAR_OFFSET, &imcbar);
    // std::cout << "DEBUG: imcbar="<<std::hex << imcbar <<std::endl;
    if(!imcbar)
    {
       std::cerr <<"ERROR: imcbar is zero." << std::endl;
       throw std::exception();
    }
    uint64 startAddr = imcbar & (~(4096-1)); // round down to 4K
    // std::cout << "DEBUG: startAddr="<<std::hex << startAddr <<std::endl;
    mmapAddr = (char*) mmap(NULL, PCM_CLIENT_IMC_MMAP_SIZE, PROT_READ, MAP_SHARED , fd, startAddr);

    if(mmapAddr == MAP_FAILED)
    {
      std::cout << "mmap failed: errno is "<< errno<< " ("<< strerror(errno) << ")"<< std::endl;
      throw std::exception();
    }

}

uint64 ClientBW::getImcReads()
{
   return *((uint32*)(mmapAddr + PCM_CLIENT_IMC_DRAM_DATA_READS));
}

uint64 ClientBW::getImcWrites()
{
   return *((uint32*)(mmapAddr + PCM_CLIENT_IMC_DRAM_DATA_WRITES));
}

uint64 ClientBW::getIoRequests()
{
   return *((uint32*)(mmapAddr + PCM_CLIENT_IMC_DRAM_IO_REQESTS));
}

ClientBW::~ClientBW()
{
    if (mmapAddr) munmap(mmapAddr, PCM_CLIENT_IMC_MMAP_SIZE);
    if (fd >= 0) ::close(fd);
}

#endif

#endif
