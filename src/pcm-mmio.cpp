// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012-2020, Intel Corporation

// written by Roman Dementiev
#include "cpucounters.h"
#ifdef _MSC_VER
#include <windows.h>
#include "windows/windriver.h"
#else
#include <unistd.h>
#endif
#include <iostream>
#include <functional>
#include <stdlib.h>
#include <iomanip>
#include <string.h>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif

using namespace pcm;

void print_usage(const char* progname)
{
    std::cout << "Usage " << progname << " [-w value] [-q] [-d] address\n\n";
    std::cout << "  Reads/writes MMIO (memory mapped) register in the specified address\n";
    std::cout << "   -w value : write the value before reading \n";
    std::cout << "   -q       : read/write 64-bit quad word (default is 32-bit double word)\n";
    std::cout << "   -d       : output all numbers in dec (default is hex)\n";
    std::cout << "\n";
}

template <class T, class RD, class WR>
void doOp(const uint64 address, const uint64 offset, const bool write, T value, RD readOp, WR writeOp, const bool dec)
{
    if (!dec) std::cout << std::hex << std::showbase;
    constexpr auto bit = sizeof(T) * 8;
    if (write)
    {
        std::cout << " Writing " << value << " to " << std::dec << bit;
        if (!dec) std::cout << std::hex << std::showbase;
        std::cout <<"-bit MMIO register " << address << "\n";
        writeOp(offset, value);
    }
    value = readOp(offset);
    std::cout << " Read value " << value << " from " << std::dec << bit;
    if (!dec) std::cout << std::hex << std::showbase;
    std::cout << "-bit MMIO register " << address << "\n\n";
}

int main(int argc, char * argv[])
{
    std::cout << "\n Processor Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n MMIO register read/write utility\n\n";

    uint64 value = ~(0ULL);
    bool write = false;
    uint64 address = 0;
    bool dec = false;
    bool quad = false;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:dq")) != -1)
    {
        switch (my_opt)
        {
        case 'w':
            write = true;
            value = read_number(optarg);
            break;
        case 'd':
            dec = true;
            break;
        case 'q':
            quad = true;
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if (optind >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    address = read_number(argv[optind]);

    try
    {
        constexpr uint64 rangeSize = 4096ULL;
        const uint64 baseAddr = address & (~(rangeSize - 1ULL)); // round down to 4K boundary
        const uint64 offset = address - baseAddr;

        MMIORange mmio(baseAddr, rangeSize, !write);

        using namespace std::placeholders;
        if (quad)
        {
            doOp(address, offset, write, (uint64)value, std::bind(&MMIORange::read64, &mmio, _1), std::bind(&MMIORange::write64, &mmio, _1, _2), dec);
        }
        else
        {
            doOp(address, offset, write, (uint32)value, std::bind(&MMIORange::read32, &mmio, _1), std::bind(&MMIORange::write32, &mmio, _1, _2), dec);
        }
    }
    catch (std::exception & e)
    {
        std::cerr << "Error accessing MMIO registers: " << e.what() << "\n";
        std::cerr << "Please check if the program can access MMIO drivers.\n";
    }
    return 0;
}
