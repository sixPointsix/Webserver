#include "webserver.h"

using namespace std;

WebServer::WebServer() {
    //http_conn对象
    users = new http_conn[MAX_FD];

    //root路径
    char server_path[200];
    getcwd(server_path, 200); //获取当前工作路径
    char root[6] = "/html";
    m_root = (char*)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

WebServer::~WebServer() {
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete [] users;
    delete [] users_timer;
    delete m_pool;
}

void WebServer::init(int port, string user, string passwd, string databaseName, 
                     int log_write, int opt_linger, int trigmode, int sql_num, 
                     int thread_num, int close_log, int actor_model) 
{
    m_port = port;
    m_user = user;
    m_databaseName = m_databaseName;
    m_passwd = passwd;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

void WebServer::thread_pool() {
    m_pool = new threadpool<http_conn>(m_actormodel, m_connPool, m_thread_num);
}

void WebServer::sql_pool() {
    m_connPool = ConnectionPool::getConnectionPool();
    m_connPool->init(m_user, m_passwd, m_databaseName, "localhost", 3306, m_sql_num, m_close_log);

    //预先读取数据库表
    users->initmysql_result(m_connPool);
}

void WebServer::log_write() {
    if(m_close_log == 0) {
        //日志初始化
        if(m_log_write == 1) {
            Log::get_instance()->init("./serverLog", 2000, 1000000, 1000);
        }
        else {
            Log::get_instance()->init("./serverLog", 2000, 1000000, 0);
        }
    }
}

void WebServer::trig_mode() {
    if(m_TRIGMode == 0) { //LT+LT
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    else if(m_TRIGMode == 1) {//LT+ET
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    else if(m_TRIGMode == 0) {//ET+LT
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    else if(m_TRIGMode == 0) { //ET+ET
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;

    }
    else {
        perror("-m arg error\n");
        exit(1);
    }
}

void WebServer::eventListen() {
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    //优雅关闭连接
    if(m_OPT_LINGER == 0) {
        struct linger tmp = {0, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof tmp);
    }
    else if(m_OPT_LINGER == 1) {
        struct linger tmp = {1, 1};
        setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof tmp);
    }

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(m_port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);


    //让处于TIME_WT状态的端口被重用
    int reuse = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    ret = bind(m_listenfd, (struct sockaddr*)&address, sizeof address);
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);
    assert(ret >= 0);

    //epoll
    //struct epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //Utils
    utils.init(TIMESLOT);
    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    http_conn::m_epollfd = m_epollfd;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN); //忽略sigpipe
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);
}

//事件循环
void WebServer::eventLoop() {
    bool timeout = false;
    bool stop_server = false;

    while(!stop_server) {
        int number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if(number < 0 && errno != EINTR) {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        for(int i = 0; i < number; ++ i) {
            int sockfd = events[i].data.fd;

            if(sockfd == m_listenfd) {
                //处理新连接
                bool flag = deal_clientdata();
                if(flag == false) continue;
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                //断开连接
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            else if(sockfd == m_pipefd[0] && events[i].events & EPOLLIN) {
                //处理信号
                bool flag = deal_signal(timeout, stop_server);
                if(!flag) {
                    LOG_ERROR("%s", "deal signals failure");
                }
            }
            else if(events[i].events & EPOLLIN) {
                //connfd可读
                deal_read(sockfd);
            }
            else if(events[i].events & EPOLLOUT) {
                //connfd可写
                deal_write(sockfd);
            }
        }

        if(timeout) {
            //最后处理定时器
            utils.timer_handler();
            LOG_INFO("%s", "timer tick");
            timeout = false;
        }
    }
}

//创建http_conn对象和定时器
void WebServer::timer(int connfd, struct sockaddr_in client_address) {
    //http_conn初始化
    users[connfd].init(connfd, client_address, m_root, m_CONNTrigmode, m_close_log, m_user, m_passwd, m_databaseName);

    //创建定时器，设置回调函数和超时时间，添加至链表中
    util_timer* timer = new util_timer();
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    
    users_timer[connfd].sockfd = connfd;
    users_timer[connfd].address = client_address;
    users_timer[connfd].timer = timer;

    //加入链表中
    utils.m_timer_lst.add_timer(timer);
}

//将处理数据的连接定时器延时
void WebServer::adjust_timer(util_timer* timer) {
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer");
}

//处理timer到时的连接
void WebServer::deal_timer(util_timer* timer, int sockfd) {
    timer->cb_func(&users_timer[sockfd]);
    if(timer) {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", "users_timer[sockfd].sockfd");
}

bool WebServer::deal_clientdata() {
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    if(m_LISTENTrigmode == 0) {
        int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
        if(connfd < 0) {
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        if(http_conn::m_user_count >= MAX_FD) {
            utils.show_error(connfd, "Internal server busy");
            LOG_ERROR("%s", "Internal server busy");
            return false;
        }
        timer(connfd, client_address);
    }
    else {
        while(1) {
            int connfd = accept(m_listenfd, (struct sockaddr*)&client_address, &client_addrlength);
            if (connfd < 0)
            {
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if (http_conn::m_user_count >= MAX_FD)
            {
                utils.show_error(connfd, "Internal server busy");
                LOG_ERROR("%s", "Internal server busy");
                break;
            }
            timer(connfd, client_address); 
        }
        return false;
    }

    return true;
}

//统一事件源，接收管道内的信号
bool WebServer::deal_signal(bool& timeout, bool& stop_server) {
    int ret = 0;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);
    if(ret == -1) return false;
    else if(ret == 0) return false;
    else {
        for(int i = 0; i < ret; i ++ ) {
            switch(signals[i])
            {
                case SIGALRM:
                {
                    timeout = true;
                    break;
                }
                case SIGTERM:
                {
                    stop_server = true;
                    break;
                }
                default: break;
            }
        }
    }

    return true;
}

//处理读
void WebServer::deal_read(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;

    if(m_actormodel == 1) { //reactor
        if(timer) adjust_timer(timer);
        //检测到读事件，将该事件放入请求队列
        m_pool->append(users + sockfd, 0);

        while(1) {
            //improv为1，说明工作线程进行完这次的读或写了
            //timer_flag说明读写出现错误，断开连接
            if(users[sockfd].improv == 1) {
                if(users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else { //模拟proactor
        if(users[sockfd].read_once()) {
            LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
            m_pool->append_p(users + sockfd);

            if(timer) adjust_timer(timer);
        }
        else {
            deal_timer(timer, sockfd);
        }
    }
}

//处理写
void WebServer::deal_write(int sockfd) {
    util_timer* timer = users_timer[sockfd].timer;
    //reactor
    if (1 == m_actormodel)
    {
        if (timer)
        {
            adjust_timer(timer);
        }

        //交付给子线程处理
        m_pool->append(users + sockfd, 1);

        while (true)
        {
            if (1 == users[sockfd].improv)
            {
                if (1 == users[sockfd].timer_flag)
                {
                    deal_timer(timer, sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    else //proactor
    {
        if (users[sockfd].write())
        {
            LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

            if (timer)
            {
                adjust_timer(timer);
            }
        }
        else
        {
            deal_timer(timer, sockfd);
        }
    }
}
