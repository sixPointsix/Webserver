#include <iostream>
#include <time.h>
#include "min_heap.h"


heap_timer::heap_timer(int delay) {
    expire = time(NULL) + delay;
}


// array[] start from index 1
time_heap::time_heap(int cap) : capacity(cap), cur_size(0) {
    array = new heap_timer*[capacity + 1];
    array[0] = NULL;
    for(int i = 1; i <= capacity; i ++ )
        array[i] = NULL;
}

time_heap::time_heap(heap_timer** init_array, int size, int capacity) : cur_size(size), capacity(capacity) {
    array = new heap_timer*[capacity + 1];

    array[0] = NULL;
    for(int i = 1; i <= capacity; i ++ )
        if(i < size) array[i] = init_array[i];
        else array[i] = NULL;

    // 初始化堆
    for(int i = size / 2; i; i -- ) down(i);
}

void time_heap::down(int x) {
    int t = x;
    if(2 * x <= cur_size && array[2 * x]->expire < array[t]->expire) t = 2 * x;
    if(2 * x + 1 <= cur_size && array[2 * x + 1]->expire < array[t]->expire) t = 2 * x + 1;

    if(t != x) {
        std::swap(array[t], array[x]);
        down(t);
    }
}

void time_heap::up(int x) {
    while(x / 2 && array[x]->expire < array[x / 2]->expire) {
        std::swap(array[x], array[x / 2]);
        x /= 2;
    }
}

void time_heap::add_timer(heap_timer* timer) {
    if(!timer) return;
    if(cur_size >= capacity) resize();

    array[++ cur_size] = timer;
    up(cur_size);
}

heap_timer* time_heap::top() {
    if(!cur_size) return nullptr;
    return array[1];
}

void time_heap::pop_timer() {
    if(empty()) return;
    array[1] = array[cur_size];
    cur_size --;
    down(1);
}

void time_heap::tick() {
    time_t cur = time(NULL);
    while(!empty()) {
        auto t = top();
        pop_timer();

        if(t->expire > cur) break;
        else {
            t->cb_func(t->user_data);
        }
    }
}

void time_heap::resize(){
    heap_timer** tmp = new heap_timer*[2 * capacity + 1];
    tmp[0] = NULL;
    for(int i = 1; i <= capacity; i ++ ) {
        if(i <= capacity) tmp[i] = array[i];
        else tmp[i] = NULL;
    }

    capacity *= 2;
    delete []array;
    array = tmp;
}

void Utils::init(int timeslot) {
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);

    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode) {
    epoll_event ev;
    ev.data.fd = fd;

    if(TRIGMode == 1)
        ev.events = EPOLLET | EPOLLIN | EPOLLRDHUP;
    else
        ev.events = EPOLLIN | EPOLLRDHUP;

    if(one_shot) ev.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);
}

//信号处理函数，所做的任务就是把信号传递给主线程
void Utils::sig_handler(int sig) {
    int save_errno = errno; //可重入
    int msg = sig;
    send(u_pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof sa);
    sa.sa_handler = handler;
    if(restart) sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//timer_handler--->tick--->cb_func
void Utils::timer_handler() {
    m_min_heap.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd, const char* info) {
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int Utils::u_epollfd = 0;
int *Utils::u_pipefd = NULL;

class Utils; //向前声明
void cb_func(client_data* user_data) {
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, NULL);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count --;
}
