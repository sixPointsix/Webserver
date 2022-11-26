#ifndef _LST_TIMER_
#define _LST_TIMER_

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <assert.h>
#include "../log/log.h"

class util_timer;

struct client_data
{
    sockaddr_in address;
    int sockfd;
    util_timer *timer;
};

//定时器类
class util_timer
{
public:
    util_timer(): pre(NULL), next(NULL) {}
public:
    time_t expire; //超时的绝对时间
    void (*cb_func)(client_data*); //任务回调函数
    client_data* user_data;
    util_timer *pre, *next;
};

//定时器容器类，升序双向链表
class sort_timer_list
{
private:
    util_timer* head;
    util_timer* tail;
public:
    sort_timer_list():head(NULL), tail(NULL) {}
    ~sort_timer_list();
    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer);
    void del_timer(util_timer* timer);
    void tick(); //心搏函数
private:
    void add_timer(util_timer* timer, util_timer* lst_head);
};

//处理定时任务
class Utils
{
public:
    Utils();
    ~Utils();
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
    sort_timer_list m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT;
};

//声明client的任务回调函数
void cb_func(client_data* user_data);

#endif
