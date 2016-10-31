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
    int data[15];

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
    std::find(ds.begin(), ds.end(), ds.size());
}


int main(int argc, char * argv[])
{
    std::vector<T> vector;
    int nelements = 13000000;

    int i = 0;
    int delay = atoi(argv[1]);

    cout << "Elements data size: " << sizeof(T) * nelements / 1024 << " KB" << std::endl;

    for ( ; i < nelements; ++i)
    {
        vector.push_back(i);
    }

    double before_ts, after_ts;


    while (1)
    {
        before_ts = my_timestamp();
        cout << "Reading memory for " << delay << " seconds" << std::endl;
        do
        {
            Memory_intensive_task(vector);
            after_ts = my_timestamp();
        } while ((after_ts - before_ts) < delay);


        cout << "Sleeping for " << delay << " seconds" << std::endl;
        sleep(delay);
    }


    return 0;
}
