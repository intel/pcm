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

#include "cpucounters.h"
#include <iostream>
#include <algorithm>
#include <list>
#include <vector>
#include <sys/time.h>
#include <emmintrin.h>
#include <assert.h>

using std::cout;
using std::endl;

inline double my_timestamp()
{
    struct timeval tp;
    gettimeofday(&tp, NULL);
    return double(tp.tv_sec) + tp.tv_usec / 1000000.;
}

struct T
{
    int key[1];
    int data[3];

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
    std::find(p, e, -1);
}


int main(int argc, char * argv[])
{
    assert((argc > 1) && "Need operation type as parameter: 0 - read, 1 - write, 2 - streaming write ");
    int op = atoi(argv[1]);
    T * vector;
    int nelements = 13000000;
    vector = new T[nelements];

    int i = 0;

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB" << std::endl;

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
            cout << "Writing memory" << std::endl;
            break;
        case 0:
            cout << "Reading memory" << std::endl;
            break;
        default:
            cout << "Streaming to memory" << std::endl;
        }

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
        cout << "Bandwidth: " << (sizeof(T) * nelements * niter) / ((after_ts - before_ts) * 1024 * 1024) << " MByte/sec" << endl;
    }

    delete[] vector;

    return 0;
}
