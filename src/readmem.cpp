// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2022, Intel Corporation
// written by Roman Dementiev
//

#include "cpucounters.h"
#include <iostream>
#include <algorithm>
#include <list>
#include <vector>
#include <sys/time.h>

using std::cout;

inline double my_timestamp()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return double(tp.tv_sec) + tp.tv_usec / 1000000.;
}

struct T
{
    int key[1] = { 0 };
    int data[15] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    T() { }
    T(int a) { key[0] = a; }

    bool operator == (const T & k) const
    {
        return k.key[0] == key[0];
    }
};

template <class DS>
void Memory_intensive_task(DS & ds)
{
    // cppcheck-suppress ignoredReturnValue
    std::find(ds.begin(), ds.end(), ds.size());
}


int main(int argc, char * argv[])
{
    std::vector<T> vector;
    int nelements = 13000000;

    int i = 0;
    int delay = atoi(argv[1]);

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB\n";

    for ( ; i < nelements; ++i)
    {
        vector.push_back(i);
    }

    double before_ts, after_ts;


    while (1)
    {
        before_ts = my_timestamp();
        cout << "Reading memory for " << delay << " seconds\n" << flush;
        do
        {
            Memory_intensive_task(vector);
            after_ts = my_timestamp();
        } while ((after_ts - before_ts) < delay);


        cout << "Sleeping for " << delay << " seconds\n" << flush;
        sleep(delay);
    }


    return 0;
}
