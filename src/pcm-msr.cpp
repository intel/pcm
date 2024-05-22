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
#include <stdlib.h>
#include <iomanip>
#include <string.h>
#ifdef _MSC_VER
#include "freegetopt/getopt.h"
#endif
#include <stdio.h>
#include <list>
#include <string>

using namespace pcm;

void print_usage(const char * progname)
{
    std::cout << "Usage " << progname << " [-w value] [-c core] [-a] [-d] msr\n\n";
    std::cout << "  Reads/writes specified msr (model specific register) \n";
    std::cout << "   -w value    : write the value before reading \n";
    std::cout << "   -c corelist : perform msr read/write on specified cores (default is 0)\n";
    std::cout << "                (examples: -c 10  -c 10-11 -c 4,6,12-20,6)\n";
    std::cout << "   -x          : print core number in hex (instead of decimal)\n";
    std::cout << "   -b low:high : read or write only low..high bits of the register\n";
    std::cout << "   -d          : output all numbers in dec (default is hex)\n";
    std::cout << "   -a          : perform msr read/write operations on all cores (same as -c -1)\n";
    std::cout << "   -s <d>      : iterate with <d> seconds between each iteration\n";
    std::cout << "   -o <f>      : write results of each iteration to file <f>\n";
    std::cout << "   --version   : print application version\n";
    std::cout << "\n";
}

PCM_MAIN_NOTHROW;

bool outflag = false;
FILE *ofile;
int loop_cnt = 0;
std::list<int> corelist;

int mainThrows(int argc, char * argv[])
{
    if(print_version(argc, argv))
        return 0;

    std::cout << "\n Intel(r) Performance Counter Monitor " << PCM_VERSION << "\n";

    std::cout << "\n MSR read/write utility\n\n";

    uint64 value = 0;
    bool write = false;
    bool core_in_dec = true;
    int msr = -1;
    bool dec = false;
    std::pair<int64,int64> bits{-1, -1};
    float sleep_delay = -1;
    std::string outfile;

    int my_opt = -1;
    while ((my_opt = getopt(argc, argv, "xw:c:dab:s:o:")) != -1)
    {
        switch (my_opt)
        {
        case 'w':
            write = true;
            value = read_number(optarg);
            break;
        case 'x':
            core_in_dec = false;
            break;
        case 's':
            sleep_delay = atof(optarg);
            break;
        case 'o':
            outfile = optarg;
            break;
        case 'c':
            corelist = extract_integer_list(optarg);
            break;
        case 'd':
            dec = true;
            break;
        case 'a':
            corelist.clear();
            corelist.push_back(-1);
            break;
        case 'b':
            bits = parseBitsParameter(optarg);
            break;
        default:
            print_usage(argv[0]);
            return -1;
        }
    }
    if (corelist.size()==0) corelist.push_back(0);
    if (1==2){
        for (auto const &v : corelist){
            printf("coreid=%d\n",v);
        }
    }

    if (optind >= argc)
    {
        print_usage(argv[0]);
        return -1;
    }

    msr = (int)read_number(argv[optind]);

    #ifdef _MSC_VER
    // Increase the priority a bit to improve context switching delays on Windows
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    // WARNING: This driver code (msr.sys) is only for testing purposes, not for production use
    Driver drv = Driver(Driver::msrLocalPath());
    // drv.stop();     // restart driver (usually not needed)
    if (!drv.start())
    {
        tcerr << "Can not load MSR driver.\n";
        tcerr << "You must have a signed  driver at " << drv.driverPath() << " and have administrator rights to run this program\n";
        return -1;
    }
    #endif
    if (outfile.length() > 0){
      outflag = true;
      ofile = fopen(outfile.c_str(),"w");
      if (ofile==NULL){
        printf("ERROR: can not open '%s' (skipping write)\n",outfile.c_str());
        printf("      (maybe a sudo issue .. need o+rwx on directory)\n");
        outflag = false;
      }
    }
    while(1){
       for (std::list<int>::iterator it=corelist.begin(); it != corelist.end(); ++it){
            int core = *it;
 
            // lambda funtion [<caputure-varibles](<fucntion arguments>)
            auto doOne = [&dec, &write, &msr, &bits, &it, &core_in_dec](int core, uint64 value)
            {
                try {
                    MsrHandle h(core);
                    if (!dec) std::cout << std::hex << std::showbase;
                    if (!readOldValueHelper(bits, value, write, [&h, &msr](uint64 & old_value){ return h.read(msr, &old_value) == 8; }))
                    {
                        std::cout << " Read error!\n";
                        return;
                    }
                    if (write)
                    {
                        std::cout << " Writing " << value << " to MSR " << msr << " on core " << core << "\n";
                        if (h.write(msr, value) != 8)
                        {
                            std::cout << " Write error!\n";
                        }
                    }   
                    value = 0;
                    if (h.read(msr, &value) == 8)
                    {
                        uint64 value2 = value;
                        extractBitsPrintHelper(bits, value, dec);
                        char cname[100];
                        if (core_in_dec) std::snprintf(cname, sizeof(cname), "%d", core);
                        else std::snprintf(cname, sizeof(cname), "0x%x", core);
                        std::cout << " from MSR " << msr << " on core " << cname << "\n";
                        auto itx = it;
                        itx++;
                        if (itx == corelist.end()) std::cout << "\n";
                        if (outflag){
                            if (bits.first >= 0){
                                uint32 value3 = extract_bits(value2,bits.first,bits.second);
                                if (dec)fprintf(ofile,"%d,%u\n",loop_cnt,value3);
                                else fprintf(ofile,"%d,0x%x\n",loop_cnt,value3);
                            }else{
                                if (dec)fprintf(ofile,"%d,%llu\n",loop_cnt,value2);
                                else fprintf(ofile,"%d,0x%llx\n",loop_cnt,value2);
                            }
                            fflush(ofile);
                        }
                    }
                    else
                    {
                        std::cout << " Read error!\n";
                    }
                }
                catch (std::exception & e)
                {
                    std::cerr << "Error accessing MSRs: " << e.what() << "\n";
                    std::cerr << "Please check if the program can access MSR drivers.\n";
                }
            };  // end of lambda definition

            if (core >= 0)
            {
                doOne(core, value);
            }
            else
            {
                set_signal_handlers();
                auto m = PCM::getInstance();
                for (uint32 i = 0; i < m->getNumCores(); ++i)
                {
                    if (m->isCoreOnline(i))
                    {
                        doOne(i, value);
                    }
                }
            }
       }
        if (sleep_delay == -1) break;
        loop_cnt++;
        MySleepMs(sleep_delay*1000.0);
    }
    return 0;
}
