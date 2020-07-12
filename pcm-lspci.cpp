/*
Copyright (c) 2017-2018, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// written by Patrick Lu
#include "cpucounters.h"
#ifdef _MSC_VER
#pragma warning(disable : 4996) // for sprintf
#include <windows.h>
#include "../PCM_Win/windriver.h"
#else
#include <unistd.h>
#endif
#include <iostream>
#include <stdlib.h>
#include <cstdint>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

#include "lspci.h"
using namespace std;
using namespace pcm;

void scanBus(int bus, const PCIDB & pciDB)
{
    if(!PciHandleType::exists(0, bus, 8, 2)) return;

    std::cout << "BUS 0x" << std::hex << bus << std::dec << "\n";

    struct iio_skx iio_skx;

    PciHandleType h(0, bus, 8, 2);
    uint32 cpubusno = 0;

    h.read32(0xcc, &cpubusno); // CPUBUSNO register
    iio_skx.stacks[0].busno = cpubusno & 0xff;
    iio_skx.stacks[1].busno = (cpubusno >> 8) & 0xff;
    iio_skx.stacks[2].busno = (cpubusno >> 16) & 0xff;
    iio_skx.stacks[3].busno = (cpubusno >> 24) & 0xff;
    h.read32(0xd0, &cpubusno); // CPUBUSNO1 register
    iio_skx.stacks[4].busno = cpubusno & 0xff;
    iio_skx.stacks[5].busno = (cpubusno >> 8) & 0xff;

    for (uint8_t stack = 0; stack < 6; stack++) {
        uint8_t busno = iio_skx.stacks[stack].busno;
        std::cout << "stack" << unsigned(stack) << std::hex << ":0x" << unsigned(busno) << std::dec << ",(" << unsigned(busno) << ")\n";
        for (uint8_t part = 0; part < 3; part++) {
                struct pci *pci = &iio_skx.stacks[stack].parts[part].root_pci_dev;
                struct bdf *bdf = &pci->bdf;
                bdf->busno = busno;
                bdf->devno = part;
                bdf->funcno = 0;
                if (stack != 0 && busno == 0) /* This is a workaround to catch some IIO stack does not exist */
                    pci->exist = false;
                else
                    probe_pci(pci);
            }
        }
    for (uint8_t stack = 0; stack < 6; stack++) {
        for (uint8_t part = 0; part < 4; part++) {
            struct pci p = iio_skx.stacks[stack].parts[part].root_pci_dev;
            if (!p.exist)
                continue;
            for (uint8_t b = p.secondary_bus_number; b <= p.subordinate_bus_number; b++) { /* FIXME: for 0:0.0, we may need to scan from secondary switch down */
                for (uint8_t d = 0; d < 32; d++) {
                    for (uint8_t f = 0; f < 8; f++) {
                        struct pci pci;
                        pci.exist = false;
                        pci.bdf.busno = b;
                        pci.bdf.devno = d;
                        pci.bdf.funcno = f;
                        probe_pci(&pci);
                        if (pci.exist)
                            iio_skx.stacks[stack].parts[part].child_pci_devs.push_back(pci);
                    }
                }
            }
        }
    }

    for (uint8_t stack = 1; stack < 6; stack++) { /* XXX: Maybe there is no point to display all built-in devices on DMI/CBDMA stacks, if so, change stack = 1 */
        for (uint8_t part = 0; part < 4; part++) {
            vector<struct pci> v = iio_skx.stacks[stack].parts[part].child_pci_devs;
            struct pci pp = iio_skx.stacks[stack].parts[part].root_pci_dev;
            if (pp.exist)
                print_pci(pp, pciDB);
            for (vector<struct pci>::const_iterator iunit = v.begin(); iunit != v.end(); ++iunit) {
                struct pci p = *iunit;
                if (p.exist)
                    print_pci(p, pciDB);
            }
        }
    }
}

int main(int /*argc*/, char * /*argv*/[])
{
    PCIDB pciDB;
    load_PCIDB(pciDB);
    PCM * m = PCM::getInstance();

    if (!m->isSkxCompatible())
    {
        cerr << "Unsupported processor model (" << m->getCPUModel() << ").\n";
        exit(EXIT_FAILURE);
    }
    std::cout << "\n Display PCI tree information\n\n";
    for(int bus=0; bus < 256; ++bus)
        scanBus(bus, pciDB);
}
