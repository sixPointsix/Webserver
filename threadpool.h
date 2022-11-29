#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <iostream>
#include <pthread.h>
#include <list>
#include <exception>
#include "locker/locker.h"
#include "sql_connection_pool/sql_connection_pool.h"

template<class T>
class threadpool
{
public:
    threadpool(int actor_model, ConnectionPool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T* request, int state);
    bool append_p(T* request); // proactor模式添加任务

private:
    static void* worker(void* arg);
    void run();

private:
    int m_thread_number; //线程池中的线程数
    int m_max_requests; //最大请求数
    pthread_t* m_threads; //线程数组
    std::list<T*> m_workqueue; //请求队列
    locker m_queuelocker; //请求队列的互斥锁
    sem m_queuestat; //是否有任务需要处理
    bool m_stop; //是否结束线程池
    ConnectionPool *m_connPool;//数据库连接池
    int m_actor_model; //模型
};

template <class T>
threadpool<T>::threadpool(int actor_model, ConnectionPool* connPool, int thread_number, int max_requests) :
    m_actor_model(actor_model), m_connPool(connPool), m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(nullptr)
{
    if(thread_number <= 0 || max_requests <= 0) {
        throw std::exception();
    }

    m_threads = new pthread_t[thread_number];
    if(m_threads == nullptr) {
        throw std::exception();
    }

    for(int i = 0; i < thread_number; ++ i) {
        printf("create the %dth thread\n", i + 1);
        if(pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete [] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])) {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template <class T>
threadpool<T>::~threadpool() {
    delete [] m_threads;
}

template <class T> 
bool threadpool<T>::append(T* request, int state) {
    m_queuelocker.lock();
    if(m_workqueue.size() >=  m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();

    return true;
}

template <class T>
bool threadpool<T>::append_p(T* request) {
    m_queuelocker.lock();
    if(m_workqueue.size() >= m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();

    return true;
}

template <class T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*)arg;
    pool->run();

    return pool;
}

template <class T>
void threadpool<T>::run() {
    while(1)
    {
        //取出任务
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        //运行任务
        if(!request) continue;
        if(m_actor_model == 1) { // Reactor模式，要区分读写线程，读为1，写为1
            if(request->m_state == 0) { //read
                if(request->read_once()) {
                    request->improv = 1;
                    connection mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else { //write
                if(request->write()) {
                    request->improv = 1;
                }
                else {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        } else { //模拟Proactor模式
            connection mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}

#endif
