#ifndef _BLOCK_QUEUE_H_
#define _BLOCK_QUEUE_H_

#include <iostream>
#include <stdio.h>
#include "../locker/locker.h"
#include <pthread.h>
#include <sys/time.h>
#include <queue>

using namespace std;

template <class T>
class block_queue
{
public:
    block_queue(int capacity = 1000) : m_capacity(capacity), m_size(0) {
        if(capacity <= 0) exit(-1);
    }
    ~block_queue() {}

    bool push(T& item) {
        m_mutex.lock();
        if(m_size >= m_capacity) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        taskQ.push(item);
        m_size ++;
        m_cond.broadcast();
        m_mutex.unlock();

        return true;
    }

	//直接返回的版本
    T frontAndPop() {
        T res;
        m_mutex.lock();
        while(m_size <= 0) {
            m_cond.wait(m_mutex.get());
        }

        res = taskQ.front();
        taskQ.pop();
        m_size --;
        m_mutex.unlock();

        return res;
    }

	//作为传入参数的版本
	bool pop(T &item)
    {
        m_mutex.lock();
        while (m_size <= 0)
        {
            if (!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }

        item = taskQ.front();
		taskQ.pop();
        m_size --;
        m_mutex.unlock();
        return true;
    }

    //超时处理，单位毫秒
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if (m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if (!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        item = taskQ.front();
		taskQ.pop();
        m_size--;
        m_mutex.unlock();
        return true;
    }

    int size() {
        int tmp;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();

        return tmp;
    }

    bool full() {
        bool res;
        m_mutex.lock();
        res = m_size >= m_capacity;
        m_mutex.unlock();

        return res;
    }
private:
    queue<T> taskQ;
    locker m_mutex;
    cond m_cond;
    int m_size;
    int m_capacity;
};

#endif
