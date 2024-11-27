// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2012-2022, Intel Corporation

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

#define MAX_BATCH_OPERATE_BYTES 1024
#define MAX_BATCH_READ_ROW_DISPLAY_BYTES 16

void print_usage(const char* progname)
{
    std::cout << "Usage " << progname << " [-w value] [-q] [-d] [-c core] address\n\n";
    std::cout << "  Reads/writes MMIO (memory mapped) register in the specified address\n";
    std::cout << "   -w value    : write the value before reading \n";
    std::cout << "   -b low:high : read or write only low..high bits of the register\n";
    std::cout << "   -q          : read/write 64-bit quad word (default is 32-bit double word)\n";
    std::cout << "   -d          : output all numbers in dec (default is hex)\n";
    std::cout << "   -n size     : number of bytes read from specified address(batch read mode), max bytes=" << MAX_BATCH_OPERATE_BYTES << "\n";
    std::cout << "   -c core     : perform the operation from specified core\n";
    std::cout << "   --version   : print application version\n";
    std::cout << "\n";
}

template <class T, class RD, class WR>
void doOp(  const std::pair<int64,int64> & bits,
            const uint64 address, const uint64 offset,
            const uint32 batch_bytes, const bool write,
            T value,
            RD readOp,
            WR writeOp,
            const bool dec,
            const int core)
{
    auto printCoreEndl = [&]() {
        if (core >= 0)
        {
            std::cout << " on core " << core;
        }
        std::cout << "\n\n";
    };
    if (batch_bytes == 0) //single mode
    {
        if (!dec) std::cout << std::hex << std::showbase;
        constexpr auto bit = sizeof(T) * 8;
        readOldValueHelper(bits, value, write, [&readOp, & offset](T & old_value){ old_value = readOp(offset); return true; });
        if (write)
        {
            std::cout << " Writing " << value << " to " << std::dec << bit;
            if (!dec) std::cout << std::hex << std::showbase;
            std::cout <<"-bit MMIO register " << address << "\n";
            writeOp(offset, value);
        }
        value = readOp(offset);
        extractBitsPrintHelper(bits, value, dec);
        std::cout << " from " << std::dec << bit;
        if (!dec) std::cout << std::hex << std::showbase;
        std::cout << "-bit MMIO register " << address;
        printCoreEndl();
    }
    else //batch mode
    {
        uint32 i = 0, j= 0;
        std::cout << std::hex << " Dumping MMIO register range from 0x" << address <<
            ", number of bytes=0x" << batch_bytes;
        printCoreEndl();
        for(i = 0; i < batch_bytes; i+=MAX_BATCH_READ_ROW_DISPLAY_BYTES)
        {
            std::ostringstream row_disp_str(std::ostringstream::out);
            std::cout << " 0x" << (address + i) << ": ";
            for(j = 0; j < (MAX_BATCH_READ_ROW_DISPLAY_BYTES/sizeof(T)); j++)
            {
                value = readOp(offset + i + j*sizeof(T));
                row_disp_str << "0x" << std::hex << std::setw(sizeof(T)*2) << std::setfill('0') << value << " ";
            }
            std::cout << row_disp_str.str() << ";\n";
        }
        std::cout << "\n";
    }
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        exit(EXIT_SUCCESS);

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n MMIO register read/write utility\n\n";

    uint64 value = ~(0ULL);
    bool write = false;
    uint64 address = 0;
    bool dec = false;
    bool quad = false;
    uint32 batch_bytes = 0;
    std::pair<int64,int64> bits{-1, -1};
    int core = -1;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "w:dqn:b:c:")) != -1)
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
        case 'b':
            bits = parseBitsParameter(optarg);
            break;
        case 'n':
            batch_bytes = read_number(optarg);
            if (batch_bytes > MAX_BATCH_OPERATE_BYTES)
            {
                batch_bytes = MAX_BATCH_OPERATE_BYTES;
            }
            break;
        case 'c':
            core = read_number(optarg);
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

    if (write == true)
    {
        batch_bytes = 0; //batch mode only support read.
    }

    try
    {
        constexpr uint64 rangeSize = 4096ULL;
        const uint64 baseAddr = address & (~(rangeSize - 1ULL)); // round down to 4K boundary
        const uint64 offset = address - baseAddr;

        if ((batch_bytes != 0) && (offset + batch_bytes > rangeSize))
        {
            batch_bytes = (rangeSize - offset); //limit the boundary 
        }

        MMIORange mmio(baseAddr, rangeSize, !write, false, core);

        using namespace std::placeholders;
        if (quad)
        {
            doOp(bits, address, offset, batch_bytes, write, (uint64)value,
                std::bind(&MMIORange::read64, &mmio, _1), std::bind(&MMIORange::write64, &mmio, _1, _2), dec, core);
        }
        else
        {
            doOp(bits, address, offset, batch_bytes, write, (uint32)value,
                std::bind(&MMIORange::read32, &mmio, _1), std::bind(&MMIORange::write32, &mmio, _1, _2), dec, core);
        }
    }
    catch (std::exception & e)
    {
        std::cerr << "Error accessing MMIO registers: " << e.what() << "\n";
        std::cerr << "Please check if the program can access MMIO drivers.\n";
    }
    return 0;
}
