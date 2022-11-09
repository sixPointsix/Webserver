#include "http_conn.h"
#include <mysql/mysql.h>
#include <fstream>

//http响应的常用短语
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You don't have authority to access to the file you request.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The file you request was not found in this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
unordered_map<string, string> users;

void http_conn::initmysql_result(connection_pool* connPool)
{

}

int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot, bool TRIGMode) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = EPOLLIN | EPOLLRDHUP;//对端断开连接处理
    if(TRIGMode == 1) ev.events |= EPOLLET;
    if(one_shot) ev.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

//EPOLLONESHOT重置
void modfd(int epollfd, int fd, int ev，int TRIGMode) {
    struct epoll_event ev;
    ev.data.fd = fd;
    ev.events = ev | ERPOLLONESHOT | EPOLLRDHUP;
    if(TRIGMode == 1) ev.events |= EPOLLET;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

//将类中用到的两个静态成员变量初始化
int http_conn::m_epollfd = -1;
int http_conn::m_user_count = 0;

void http_conn::close_conn(bool real_close) {
    if(real_close && m_sockfd != -1) {
        printf("close %d\n", m_sockfd);
        remove(m_epollfd, m_sockfd);
        m_user_count --;
        m_sockfd = -1;
    }
}

void http_conn::init(int sockfd, const sockaddr& addr,
                     char* root, int TRIGMode, int close_log,
                     string user, string password, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;
    strcpy(sql_user, user.c_str());
    strcpy(sql_password, password.c_str());
    strcpy(sql_sqlname, sqlname.c_str());

    //SO_REUSRADDR，可以重用time_wait的端口
    //仅为调试使用，发布时应去除
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);

    addfd(m_epollfd, m_sockfd);
    m_user_count ++;

    init(); //调用私有的init完成剩余的初始化
}

void http_conn::init() {
    mysql = NULL;
    m_state = 0; //读
    cgi = 0;
    timer_flag = 0;
    imporv = 0;

    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    m_write_idx = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET; //默认是GET
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;
    m_linger = false;
    bytes_to_send = 0;
    bytes_have_send = 0;

    memset(m_read_buf, '\0', sizeof m_read_buf);
    memset(m_write_buf, '\0', sizeof m_write_buf);
    memset(m_real_file, '\0', sizeof m_real_file);
}

//循环读取数据
//分为LT, ET模式，ET需要一次性读完
bool http_conn::read_once() {
    if(m_read_idx >= READ_BUFFER_SIZE) return false;

    int bytes_read = 0;
    if(m_TRIGMode == 1) { // ET
        while(1) {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if(bytes_read == -1) {
                if(errno == EAGAIN || errno == EWOULDBLOCK) break; //读完了
                else return false;
            }
            else if(bytes_read == 0) { //client正常断开连接
                return false;
            }
            m_read_idx += bytes_read;
        }

        return true;
    }
    else { //LT
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_size, 0);
        if(bytes_read <= 0) return false;
        m_read_idx += bytes_read;

        return true;
    }
}

//http的主状态机
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE retcode = NO_RQUEST;
    char* text = nullptr;

    while(line_status == LINE_OK && m_check_state == CHECK_STATE_CONTENT
          || (line_status = parse_line()) == LINE_OK)
    {
        text = get_line();
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text); //写入日志

        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                retcode = parse_request_line(text);
                if(retcode == BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADERS:
            {
                retcode = parse_headers(text);
                if(retcode == BAD_REQUEST) return BAD_REQUEST;
                else if(retcode == GET_REQUEST) return do_request();
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                retcode = parse_content(text);
                if(retcode == GET_REQUEST) return do_request();
                else line_status = LINE_OPEN;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }

    return NO_REQUEST;
}

//取出一行，已"\r\n"结束
http_conn::LINE_STATUS http_conn::parse_line() {
    for(; m_checked_idx < m_read_idx; m_check_idx ++ ) {
        char tmp = m_read_buf[m_checked_idx];
        if(tmp == '\r') {
            if(m_checked_idx < m_read_idx - 1 && m_read_bud[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx ++ ] = '\0';
                m_read_buf[m_checked_idx ++ ] = '\0';
                return LINE_OK;
            }
            else if(m_checked_idx == m_read_idx - 1) return LINE_OPEN;
            else return LINE_BAD;
        }
        if(tmp == '\n') {
            if(m_checked_idx && m_read_buf[m_checked_idx - 1] == '\r') {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx ++ ] = '\0';
                return LINE_OPEN;
            }
            else return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

//解析请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {

}

//解析头部字段
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {

}

//解析请求体，其实就是判断它是不是被完整读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text) {

}
