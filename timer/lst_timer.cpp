#include "lst_timer.h"

sort_timer_list::~sort_timer_list() {
    auto tmp = head;
    while(tmp) {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

void sort_timer_list::add_timer(util_timer* timer) {
    if(!timer) return;
    if(!head) {
        head = tail = timer;
        return ;
    }

    if(timer->expire < head->expire) {
        timer->next = head;
        timer->pre = nullptr;
        head->pre = next;
        head = timer;
        return ;
    } else {
        add_timer(timer, head);
    }
}

//调整计时器函数，只考虑timer->expire增大的情况
void sort_timer_list::adjust_timer(util_timer* timer) {
    if(!timer) return;
    auto tmp = timer->next;
    if(!tmp || tmp->expire >= timer->expire) { //不用动的情况
        return ;
    }
    if(head == timer) {
        head = head->next;
        head->pre = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
    }
    else {
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        add_timer(timer, tmp);
    }
}

void sort_timer_list::del_timer(util_timer* timer) {
    if(!timer) return;

    if(timer == head && timer == tail) {
        head = tail = nullptr;
    }
    else if(timer == head) {
        head = head->next;
        head->pre = nullptr;
        delete timer;
    }
    else if(timer == tail) {
        tail = tail->pre;
        tail->next = nullptr;
        delete timer;
    }
    else {
        timer->pre->next = timer->next;
        timer->next->pre = timer->pre;
        delete timer;
    }
}

void sort_timer_list::add_timer(util_timer* timer, util_timer* lst_head) {
    auto prev = lst_timer
    auto p = lst_timer->next;

    while(p) {
        if(timer->expire < p->expire) {
            timer->pre = prev;
            timer->next = p;
            prev->next = timer;
            p->pre = timer;
            break;
        }
        pre = p;
        p = p->next;
    }
    if(!p) { //说明是新tail
        tail->next = timer;
        timer->pre = prev;
        timer->next = nullptr;
        tail = timer;
    }
}

//处理到期任务
void sort_timer_list::tick() {
    if(!head) return;

    time_t cur_time = time(NULL); //获取当前的绝对时间

    util_timer* p = head;
    while(p) {
        if(cur_time < p->expire) break;
        p->cb_func(p->user_data);
        head = p->next;
        if(head) head->pre = nullptr;
        delete p;
        p = head;
    }
}

void Utils::init(int timeslot): m_TIMESLOT(timeslot) {}

int Utils::setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_potion = old_option | O_NONBLOCK;
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
