#ifndef _WEBSERVER_H_
#define _WEBSERVER_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <iostream>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>
#include <time.h>
#include <signal.h>

#include "threadpool.h"
#include "http_conn/http_conn.h"
#include "sql_connection_pool/sql_connection_pool.h"
#include "timer/lst_timer.h"

const int MAX_FD = 65535; // 最大文件描述符数
const int MAX_EVENT_NUMBER = 10000; //最大连接数
const int TIMESLOT = 5; //最小超时单位

class WebServer {
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passwd, string databaseName,
            int log_write, int opt_linger, int trigmode, int sql_num,
            int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode();
    void eventListen();
    void eventLoop();
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer* timer);
    void deal_timer(util_timer* timer, int sockfd);

    bool deal_clientdata();
    bool deal_signal(bool& timeout, bool& stop_server);
    void deal_read(int sockfd);
    void deal_write(int sockfd);

public:
    int m_port;
    char* m_root;
    int m_log_write; //日志写入方式
    int m_close_log; //日志是否关闭，0同1异
    int m_actormodel;

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //sql_connection_pool
    ConnectionPool* m_connPool;
    string m_user;
    string m_passwd;
    string m_databaseName;
    int m_sql_num;

    //threadpool
    threadpool<http_conn> *m_pool;
    int m_thread_num;

    //epoll_event相关
    struct epoll_event events[MAX_EVENT_NUMBER];
    int m_listenfd;
    int m_OPT_LINGER; //优雅关闭连接
    int m_TRIGMode;
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    //timer
    client_data* users_timer;
    Utils utils;
};

#endif
