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
#include <emmintrin.h>
#include <assert.h>

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
    int data[3] = { 0, 0, 0 };

    T() { }
    T(int a) { key[0] = a; }

    bool operator == (const T & k) const
    {
        return k.key[0] == key[0];
    }
};


template <class Y>
void write_intensive_task(Y * p, Y * e, int value)
{
    __m128i i = _mm_set_epi32(value, value, value, value);

#if 0
    while (p != e)
    {
        *p = value;
        ++p;
    }
#else
    while (p != e)
    {
        _mm_store_si128((__m128i *)p++, i);
    }
#endif
}

template <class Y>
void stream_write_task(Y * p, Y * e, int value)
{
    __m128i i = _mm_set_epi32(value, value, value, value);

    while (p != e)
    {
        _mm_stream_si128((__m128i *)p++, i);
    }
}

template <class Y>
void read_intensive_task(Y * p, Y * e, int value)
{
    // cppcheck-suppress ignoredReturnValue
    std::find(p, e, -1);
}


int main(int argc, char * argv[])
{
    assert((argc > 1) && "Need operation type as parameter: 0 - read, 1 - write, 2 - streaming write ");
    int op = atoi(argv[1]);
    T * vector;
    int nelements = 1024 * 1024 * 1024 / sizeof(T);
    vector = new T[nelements];

    int i = 0;

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB\n";

    for ( ; i < nelements; ++i)
    {
        vector[i].key[0] = 10;
    }

    double before_ts, after_ts;


    while (1)
    {
        before_ts = my_timestamp();
        switch (op)
        {
        case 1:
            cout << "Writing memory\n";
            break;
        case 0:
            cout << "Reading memory\n";
            break;
        default:
            cout << "Streaming to memory\n";
        }
        cout << std::flush;

        int niter = 32;
        i = niter;
        int r = rand();
        while (i--)
        {
            switch (op)
            {
            case 1:
                write_intensive_task(vector, vector + nelements, r);
                break;
            case 0:
                read_intensive_task(vector, vector + nelements, r);
                break;
            default:
                stream_write_task(vector, vector + nelements, r);
            }

            after_ts = my_timestamp();
        }
        cout << "Bandwidth: " << (sizeof(T) * nelements * niter) / ((after_ts - before_ts) * 1024 * 1024) << " MByte/sec\n" << std::flush;
    }

    deleteAndNullifyArray(vector);

    return 0;
}
