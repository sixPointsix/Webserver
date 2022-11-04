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
    block_queue(int capacity = 1000);
    ~block_queue();
    bool push(T& item);
    T front();
    bool pop();
    int size();
private:
    queue<T> taskQ;
    locker m_mutex;
    cond m_cond;
    int m_size;
    int m_capacity;
};

#endif
