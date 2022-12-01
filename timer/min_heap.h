#ifndef _MIN_HEAP_
#define _MIN_HEAP_

#define BUFFER_SIZE 64

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "../log/log.h"


class heap_timer;

struct client_data { 
    sockaddr_in address;
    int socketfd;
    heap_timer* timer;
};

class heap_timer {
public:
    heap_timer(int delay);
public:
    time_t expire;
    void (*cb_func) (client_data*);
    client_data* user_data;
};

//时间堆
class time_heap {
public:
    // 两种初始化堆的方式
    time_heap(int cap); 
    time_heap(heap_timer** init_array, int size, int capacity);
    ~time_heap();
public:
    void add_timer(heap_timer* timer); //添加计时器
    heap_timer* top(); //获得堆顶的计时器
    void pop_timer(); // 删除堆顶计时器
    void tick(); // 心搏函数
    bool empty() {return cur_size == 0;}
private:
    void down(int hole); //堆的down操作
    void up(int hole);
    void resize(); //将堆数组的容量扩大一倍
private:
    heap_timer** array;
    int capacity;
    int cur_size;
};

//处理定时任务
class Utils
{
public:
    Utils() {}
    ~Utils() {}
    void init(int timeslot);
    int setnonblocking(int fd);
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);
    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);
    //定时处理任务
    void timer_handler();

    void show_error(int connfd, const char* info);

public:
    static int* u_pipefd;
    time_heap m_time_heap;
    static int u_epollfd;
    int m_TIMESLOT;
};

//声明client的任务回调函数
void cb_func(client_data* user_data);

#endif
