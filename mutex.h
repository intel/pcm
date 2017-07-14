#ifndef MUTEX_HEADER_
#define MUTEX_HEADER_

#ifdef _MSC_VER
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdlib.h>

namespace PCM_Util
{
    class Mutex {
#ifdef _MSC_VER
        HANDLE mutex_;
#else
        pthread_mutex_t mutex_;
#endif

    public:
        Mutex()
        {
#ifdef _MSC_VER
            mutex_ = CreateMutex(NULL, FALSE, NULL);
#else
            pthread_mutex_init(&mutex_, NULL);
#endif
        }
        virtual ~Mutex()
        {
#ifdef _MSC_VER
            CloseHandle(mutex_);
#else
            pthread_mutex_destroy(&mutex_);
#endif
        }

        void lock()
        {
#ifdef _MSC_VER
            WaitForSingleObject(mutex_, INFINITE);
#else
            pthread_mutex_lock(&mutex_);
#endif
        }
        void unlock()
        {
#ifdef _MSC_VER
            ReleaseMutex(mutex_);
#else
            pthread_mutex_unlock(&mutex_);
#endif
        }

        class Scope {
            Mutex & m;
        public:
            Scope(Mutex & m_) : m(m_)
            {
                m.lock();
            }
            ~Scope() {
                m.unlock();
            }
        };
    };
}

#endif
