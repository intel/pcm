/*
Copyright (c) 2009-2012, Intel Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of Intel Corporation nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// written by Roman Dementiev
//

#define HACK_TO_REMOVE_DUPLICATE_ERROR
#include "cpucounters.h"
#include "cpuasynchcounter.h"
#include <iostream>
#include <list>
#include <vector>
#include <algorithm>
#include <sys/time.h>

/*!     \file realtime.cpp
        \brief Two use-cases: realtime data structure performance analysis and memory-bandwidth aware scheduling
*/

using std::cout;
using std::endl;

inline double my_timestamp()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return double(tp.tv_sec) + tp.tv_usec / 1000000.;
}

long long int fib(long long int num)
{
    long long int result = 1, a = 1, b = 1;

    for (long long int i = 3; i <= num; ++i)
    {
        result = a + b;
        a = b;
        b = result;
    }

    return result;
}

SystemCounterState before_sstate, after_sstate;
double before_time, after_time;

AsynchronCounterState counters;

long long int all_fib = 0;


void CPU_intensive_task()
{
    cout << "CPU task" << endl;
    all_fib += fib(80000000ULL + (rand() % 2));
}


template <class DS>
void Memory_intensive_task(DS & ds)
{
    cout << "Mem task" << endl;
    std::find(ds.begin(), ds.end(), ds.size());
}

double currentMemoryBandwidth()
{
    return (counters.getSystem<uint64, ::getBytesReadFromMC>() + counters.getSystem<uint64, ::getBytesWrittenToMC>()) / (1024 * 1024);
}

template <class DS>
void measure(DS & ds, size_t repeat, size_t nelements)
{
    SystemCounterState before_sstate, after_sstate;
    double before_ts, after_ts;

    // warm up
    std::find(ds.begin(), ds.end(), nelements);

    double before1_ts;
#if 0
    for (int kkk = 1000; kkk > 0; --kkk)
    {
        ::sleep(1);
        before1_ts = my_timestamp();

        // start measuring
        before_sstate = getSystemCounterState();
        before_ts = my_timestamp();

        cout << "Response time of getSystemCounterState(): " << 1000. * (before_ts - before1_ts) << " ms" << std::endl;
    }
#endif

    for (int j = 0; j < repeat; ++j) std::find(ds.begin(), ds.end(), nelements);

    // stop measuring
    after_sstate = getSystemCounterState();
    after_ts = my_timestamp();


    cout << "\nSearch runtime: " << ((after_ts - before_ts) * 1000. / repeat) << " ms " << std::endl;
    cout << "Search runtime per element: " << ((after_ts - before_ts) * 1000000000. / repeat) / nelements << " ns " << std::endl;

    cout << "Number of L2 cache misses per 1000 elements: "
         << (1000. * getL2CacheMisses(before_sstate, after_sstate) / repeat) / nelements <<
    " \nL2 Cache hit ratio : " << getL2CacheHitRatio(before_sstate, after_sstate) * 100. << " %" << std::endl;


    cout << "Number of L3 cache misses per 1000 elements: "
         << (1000. * getL3CacheMisses(before_sstate, after_sstate) / repeat) / nelements <<
    " \nL3 Cache hit ratio : " << getL3CacheHitRatio(before_sstate, after_sstate) * 100. << " %" << std::endl;

    cout << "Bytes written to memory controller per element: " <<
    (double(getBytesWrittenToMC(before_sstate, after_sstate)) / repeat) / nelements << std::endl;

    cout << "Bytes read from memory controller per element : " <<
    (double(getBytesReadFromMC(before_sstate, after_sstate)) / repeat) / nelements << std::endl;


    cout << "Used memory bandwidth: " <<
    ((getBytesReadFromMC(before_sstate, after_sstate) + getBytesWrittenToMC(before_sstate, after_sstate)) / (after_ts - before_ts)) / (1024 * 1024) << " MByte/sec" << std::endl;

    cout << "Instructions retired: " << getInstructionsRetired(before_sstate, after_sstate) / 1000000 << "mln" << std::endl;

    cout << "CPU cycles: " << getCycles(before_sstate, after_sstate) / 1000000 << "mln" << std::endl;

    cout << "Instructions per cycle: " << getCoreIPC(before_sstate, after_sstate) << std::endl;
}

#if 0
typedef int T;

#else

struct T
{
    int key[1];
    int data[15];

    T() { }
    T(int a) { key[0] = a; }

    bool operator == (const T & k) const
    {
        return k.key[0] == key[0];
    }
};

#endif

int main(int argc, char * argv[])
{
    PCM * m = PCM::getInstance();

    if (!m->good())
    {
        cout << "Can not access CPU counters" << endl;
        cout << "Try to execute 'modprobe msr' as root user and then" << endl;
        cout << "you also must have read and write permissions for /dev/cpu/?/msr devices (the 'chown' command can help).";
        return -1;
    }

    if(m->program() != PCM::Success){ 
	cout << "Program was not successful..." << endl;
	delete m;
	return -1;
    }	  

    int nelements = atoi(argv[1]);


#if 1 /* use-case: compare data structures in real-time */
    std::list<T> list;
    std::vector<T> vector;
    int i = 0;

    for ( ; i < nelements; ++i)
    {
        list.push_back(i);
        vector.push_back(i);
    }


    unsigned long long int totalops = 200000ULL * 1000ULL * 64ULL / sizeof(T);
    int repeat = totalops / nelements, j;

    cout << std::endl << std::endl << "Elements to traverse: " << totalops << std::endl;
    cout << "Items in data structure: " << nelements << std::endl;
    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB" << std::endl;
    cout << "Test repetions: " << repeat << std::endl;

    cout << "\n*List data structure*" << endl;
    measure(list, repeat, nelements);

    cout << "\n\n*Vector/array data structure*" << endl;
    measure(vector, repeat, nelements);

#else
    /* use-case: memory bandwidth-aware scheduling */

    std::vector<T> vector;
    nelements = 13000000;

    int i = 0;

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB" << std::endl;

    for ( ; i < nelements; ++i)
    {
        vector.push_back(i);
    }

    double before_ts, after_ts;

    before_ts = my_timestamp();
    {
        int m_tasks = 1000;
        int c_tasks = 1000;
        while (m_tasks + c_tasks != 0)
        {
            if (m_tasks > 0)
            {
                Memory_intensive_task(vector);
                --m_tasks;
                continue;
            }

            if (c_tasks > 0)
            {
                CPU_intensive_task();
                --c_tasks;
            }
        }
    }
    after_ts = my_timestamp();

    cout << "In order scheduling, Running time: " << (after_ts - before_ts) << " seconds" << endl;


    before_ts = my_timestamp();
    {
        int m_tasks = 1000;
        int c_tasks = 1000;
        while (m_tasks + c_tasks != 0)
        {
            double band = currentMemoryBandwidth();
            //cout << "Mem band: "<< band << " MB/sec" << endl;
            if (m_tasks > 0 && (band < (25 * 1024 /* MB/sec*/)
                                || c_tasks == 0))
            {
                Memory_intensive_task(vector);
                --m_tasks;
                continue;
            }

            if (c_tasks > 0)
            {
                CPU_intensive_task();
                --c_tasks;
                continue;
            }
        }
    }

    after_ts = my_timestamp();

    cout << "CPU monitoring conscoius scheduling, Running time: " << (after_ts - before_ts) << " seconds" << endl;

#endif
    m->cleanup();

    return 0;
}
