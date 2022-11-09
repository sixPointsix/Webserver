#ifndef _HTTP_CONN_H_
#define _HTTP_CONN_H_

#include <iostream>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <map>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdarg.h> //变参数函数
#include <netinet/in.h>
#include <sys/uio.h>

#include "../locker/locker.h"
#include "../threadpool.h"
#include "../sql_connection_pool/sql_connection_pool.h"
// #include "../timer/"

class http_conn
{
public:
    //文件名的最大长度
    static const int FILENAME_LEN = 200;
    //读写缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    //http请求方法
    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATCH
    };
    //http主状态机
    enum CHECK_STATE {
        CHECK_STATE_QUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    //行的状态，从状态机
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };
    //http请求的处理结果
    enum HTTP_CODE {
        NO_REQUEST, GET_REQUEST, BAD_REQUEST,
        NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
        INTERNAL_ERROR, CLOSED_CONNECTION
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    void init(int sockfd, const sockaddr_in& addr, char* root, int TRIGMode, int close_log, string user, string password, string sqlname);
    void close_conn(bool real_close = true);
    void process(); //处理客户请求
    bool read_once(); //非阻塞读
    bool write(); //非阻塞写

    sockaddr_in* get_address() {
        return m_address;
    }
    void initmysql_result(connection_pool* connPool);
    int timer_flag; // 定时器标志
    int improv;

public:
    //所有socket的时间都被注册在同一个epoll内核时间表中，所以m_epollfd是静态的
    static int m_epollfd;
    //用户数量，静态成员
    static int m_user_count;
    MYSQL* mysql; //数据库
    int m_state; //读为0，写为1

private:
    void init(); //初始化
    HTTP_CODE process_read(); //解析http请求，主状态机
    bool process_write(HTTP_CODE ret); //填充http应答

    //下面一组函数被process_read()调用共同实现http请求的解析
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_headers(char* text);
    HTTP_CODE parse_content(char* text);
    HTTP_CODE do_request();
    char* get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line(); //分解出一行

    //下面一组函数被process_write()调用共同实现http应答的填充
    void unmap();
    void add_response(const char* format, ...); //变参数函数
    bool add_status_line(int status, const char* title);
    bool add_headers(int content_length);
    bool add_content(const char* content);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

private:
    //该http连接的通信socket
    int m_sockfd;
    //客户端的socket地址
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_idx; //已读入的数据的下一个位置
    int m_checked_idx;
    int m_start_line; //正在解析的行的起始位置

    char m_wirte_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;

    //主状态机状态
    CHECK_STATE m_check_state; 
    METHOD m_method;

    char m_real_file[FILENAME_LEN];
    char* m_url;
    char* m_version;
    char* m_host;
    int m_content_length;
    bool linger; //keep-alive 标志

    //把目标文件mmap到的位置
    char* m_file_address;
    //目标文件的状态
    struct stat m_file_stat;
    //writev集中写
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi; //是否启用的post
    char* m_string; //储存请求头的数据
    int bytes_to_send;
    int bytes_have_send;
    char* doc_root;
    unordered_map<string, string> m_users;
    int m_TRIGMode; //epoll模式，0为LT，1为ET
    int m_close_log;
    char sql_user[100];
    char sql_password[100];
    char sql_name[100];
};

#endif
