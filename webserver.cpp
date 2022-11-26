#include "webserver.h"

using namespace std;

WebServer::WebServer() {
    //http_conn对象
    users = new http_conn[MAX_FD];

    //root路径
    char server_path[200];
    getcwd(server_path, 200); //获取当前工作路径
    char root[6] = "/root";
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
    m_pool = new threadpool<http_conn>(m_actor_model, m_connPool, m_thread_num);
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
            LOG::get_instance()->init("./serverLog", 2000, 1000000, 1000);
        }
        else {
            LOG::get_instance()->init("./serverLog", 2000, 1000000, 0);
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

}

void WebServer::eventLoop() {

}

void WebServer::timer(int connfd, struct sockaddr_in client_address) {

}

void WebServer::adjust_timer(util_timer* timer) {

}

void WebServer::deal_timer(util_timer* timer, int sockfd) {

}

bool WebServer::deal_client_data() {

}

bool WebServer::deal_signal(bool& timeout, bool& stop_server) {

}

void WebServer::deal_read() {

}

void WebServer::deal_write() {

}
