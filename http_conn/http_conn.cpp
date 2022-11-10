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

//全局的互斥锁和用户列表
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
//方法，url，版本
http_conn::HTTP_CODE http_conn::parse_request_line(char* text) {
    m_url = strpbrk(text, " \t"); //返回第一个匹配字符的后续字符串
    if(!m_url) return BAD_REQUEST;
    *m_url ++ = '\0';

    char* method = text;
    if(strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if(strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1; // 标记一下启用POST
    } else {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t"); //返回str1第一个不在str2中出现的下标
    m_version = strpbrk(m_url, " \t");
    if(!m_version) return BAD_REQUEST;
    *m_version ++ = '\0';
    m_version += strspn(m_url, " \t");

    if(strcasecmp(m_version, "HTTP/1.1") != 0)
        return BAD_REQUEST; //只解析HTTP/1.1

    if(strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if(strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if(!m_url || m_url[0] != '/')
        return BAD_REQUEST;
    if(strlen(m_url) == 1) m_url = "/judge.html"; //默认url

    m_check_state = CHECK_STATE_HEADERS;

    return NO_REQUEST;
}

//解析头部字段
http_conn::HTTP_CODE http_conn::parse_headers(char* text) {
    if(text[0] == '\0') {
        //parse_line中已经把"\r\n"转换为'\0'了
        //直接读到'\0'说明一行已经结束
        if(m_content_length == 0) {
            return GET_REQUEST;
        } else {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
    }

    //处理三个头部字段：Connection, Content-Length, Host
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("Server can't parse this header: %s", text);
    }
    return NO_REQUEST;
}

//解析请求体，其实就是判断它是不是被完整读入了
http_conn::HTTP_CODE http_conn::parse_content(char* text) {
    //m_checked_idx最多只读到请求头结束
    if(m_read_idx >= m_checked_idx + m_content_length) {
        text[m_content_length] = '\0';
        //保存请求体的数据，因为POST最后的用户名密码在请求体中
        m_string = text;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

//得到合法且完整的http请求后，我们就分析目标文件的属性，若目标文件存在且不是文件夹，而且所有用户都有读的权限，就把该文件mmap映射到m_file_address处，返回成功
http_conn::HTTP_CODE http_conn::do_request() {
    //获取文件名根路径 m_real_file = doc_root + m_url
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);

    //处理cgi
    const char* p = strchr(m_url, '/');
    if(cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        //根据标志判断是登录检测或是注册检测
        char flag = m_url[1];

        char* m_real_url = new char[200];
        strcpy(m_real_url, "/");
        strcat(m_real_url, m_url + 2);
        strncpy(m_real_file + len, m_real_url, FILENAME_LEN - len - 1);
        delete [] m_real_url;

        //提取出用户名和密码，在m_string中
        //格式：user=root&password=root
        sting username, password;
        int i;
        for(i = 5; m_string != '&'; ++ i)
            username += m_string[i];

        for(i = i + 10, j = 0; m_string[i]; ++ i, ++ j)
            password += m_string[j];

        if(*(p + 1) == '2') { //登录
            if(users[username] == password) {
                strcpy(m_url, "/welcome.html");
            } else {
                strcpy(m_url, "/logError.html");
            }
        }
        else if(*(p + 1) == '3') { //注册
            //先检测数据库中有无重名的
            //若没有重名的，就增加数据
            string sql_insert = "INSERT INTO USER(username, passwd) VALUES('"+ username + "', '" + password + "')";

            if(!users.count(username))
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert.c_str());
                users[username] = password;
                m_lock.unlock();

                if(!res) strcpy(m_url, "/log.html");
                else strcpy(m_url, "/registerError.html");
            }
            else strcpy(m_url, "/registerError.html");
       }
    }

    if(*(p + 1) == '0') {
        char* m_real_url = new char[200];
        strcpy(m_real_url, "/register.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));

        delete [] m_real_url;
    }
    else if(*(p + 1) == '1') {
        char* m_real_url = new char[200];
        strcpy(m_real_url, "/log.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));

        delete [] m_real_url;
    }
    else if(*(p + 1) == '5') {
        char* m_real_url = new char[200];
        strcpy(m_real_url, "/picture.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));

        delete [] m_real_url;
    }
    else if(*(p + 1) == '6') {
        char* m_real_url = new char[200];
        strcpy(m_real_url, "/video.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));

        delete [] m_real_url;
    }
    else if(*(p + 1) == '7') {
        char* m_real_url = new char[200];
        strcpy(m_real_url, "/fans.html");
        strncpy(m_real_file + len, m_real_url, strlen(m_real_url));

        delete [] m_real_url;
    }
    else {
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    //判断目标文件是否存在及其属性
    if(stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode & S_IROTH)) { //其他用户是否有可读权限
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode)) { //是否是文件夹
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    //将该文件内存映射到m_file_address处
    //是一个私有文件映射
    m_file_address = (char*)mmap(NULL, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

void http_conn::unmap() {
    if(m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = nullptr; //谨记！防止内存泄露
    }
}

