// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev
//

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
    cout << "CPU task\n";
    all_fib += fib(80000000ULL + (rand() % 2));
}


template <class DS>
void Memory_intensive_task(DS & ds)
{
    cout << "Mem task\n";
    // cppcheck-suppress ignoredReturnValue
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
    double before_ts = 0.0, after_ts;

    // warm up
    // cppcheck-suppress ignoredReturnValue
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

        cout << "Response time of getSystemCounterState(): " << 1000. * (before_ts - before1_ts) << " ms\n";
    }
#endif

    // cppcheck-suppress ignoredReturnValue
    for (int j = 0; j < repeat; ++j) std::find(ds.begin(), ds.end(), nelements);

    // stop measuring
    after_sstate = getSystemCounterState();
    after_ts = my_timestamp();


    cout << "\nSearch runtime: " << ((after_ts - before_ts) * 1000. / repeat) << " ms \n";
    cout << "Search runtime per element: " << ((after_ts - before_ts) * 1000000000. / repeat) / nelements << " ns \n";

    cout << "Number of L2 cache misses per 1000 elements: "
         << (1000. * getL2CacheMisses(before_sstate, after_sstate) / repeat) / nelements <<
        " \nL2 Cache hit ratio : " << getL2CacheHitRatio(before_sstate, after_sstate) * 100. << " %\n";


    cout << "Number of L3 cache misses per 1000 elements: "
         << (1000. * getL3CacheMisses(before_sstate, after_sstate) / repeat) / nelements <<
        " \nL3 Cache hit ratio : " << getL3CacheHitRatio(before_sstate, after_sstate) * 100. << " %\n";

    cout << "Bytes written to memory controller per element: " <<
    (double(getBytesWrittenToMC(before_sstate, after_sstate)) / repeat) / nelements << "\n";

    cout << "Bytes read from memory controller per element : " <<
    (double(getBytesReadFromMC(before_sstate, after_sstate)) / repeat) / nelements << "\n";


    cout << "Used memory bandwidth: " <<
    ((getBytesReadFromMC(before_sstate, after_sstate) + getBytesWrittenToMC(before_sstate, after_sstate)) / (after_ts - before_ts)) / (1024 * 1024) << " MByte/sec\n";

    cout << "Instructions retired: " << getInstructionsRetired(before_sstate, after_sstate) / 1000000 << "mln\n";

    cout << "CPU cycles: " << getCycles(before_sstate, after_sstate) / 1000000 << "mln\n";

    cout << "Instructions per cycle: " << getCoreIPC(before_sstate, after_sstate) << "\n";
    cout << flush;
}

#if 0
typedef int T;

#else

struct T
{
    int key[1] = { 0 };
    int data[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };;

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
        cout << "Can not access CPU counters\n";
        cout << "Try to execute 'modprobe msr' as root user and then\n";
        cout << "you also must have read and write permissions for /dev/cpu/?/msr devices (the 'chown' command can help).";
        return -1;
    }

    if (m->program() != PCM::Success) {
        cout << "Program was not successful...\n";
        deleteAndNullify(m);
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

    cout << "\n\nElements to traverse: " << totalops << "\n";
    cout << "Items in data structure: " << nelements << "\n";
    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB\n";
    cout << "Test repetitions: " << repeat << "\n";

    cout << "\n*List data structure*\n";
    measure(list, repeat, nelements);

    cout << "\n\n*Vector/array data structure*\n";
    measure(vector, repeat, nelements);

#else
    /* use-case: memory bandwidth-aware scheduling */

    std::vector<T> vector;
    nelements = 13000000;

    int i = 0;

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB\n";

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

    cout << "In order scheduling, Running time: " << (after_ts - before_ts) << " seconds\n";


    before_ts = my_timestamp();
    {
        int m_tasks = 1000;
        int c_tasks = 1000;
        while (m_tasks + c_tasks != 0)
        {
            double band = currentMemoryBandwidth();
            //cout << "Mem band: " << band << " MB/sec\n";
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

    cout << "CPU monitoring conscoius scheduling, Running time: " << (after_ts - before_ts) << " seconds\n";

#endif
    m->cleanup();

    return 0;
}
