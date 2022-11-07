#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <cstring>

using namespace std;

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::~Log() {
    if(m_fp != NULL) fclose(m_fp);
}

bool Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    if(max_queue_size > 0) {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);

        //创建一个负责异步写日志的线程
        //这里涉及到一个知识点，就是说pthread_create的指向的必须是静态函数;静态函数如何调用类的动态成员
        //this做参数 or 类的静态对象
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL); //只有一个写线程
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf));
    m_split_lines = split_lines;
    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //从后往前找到第一个'/'的位置
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    //给日志命名
    if(p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        p ++;
        strcpy(log_name, p);
        strncpy(dir_name, file_name, p - file_name);

        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", 
                dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;

    m_fp = fopen(log_full_name, "a");
    if(!m_fp) return false;
    return true;
}

void Log::write_log(int log_level, const char* format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm; //获取时间

    char s[16] = {0};
    switch(log_level) {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[error]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    m_mutex.lock();
    m_count ++;

    //日志不是今天的或已达到最大行数
    //需要新开文件写日志
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char new_time[16] = {0};

        snprintf(new_time, 255, "%d_%02d_%02d_", 
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        //如果是新的一天
        if(m_today != my_tm.tm_mday) {
            snprintf(new_log, 255, "%s%s%s", dir_name, new_time, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }
        //如果是行数达到上限新开文件，名字就是原文件名+数字后缀
        if(m_count % m_split_lines == 0) {
            snprintf(new_log, 255, "%s%s%s.%lld",
                    dir_name, new_time, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    //处理变参数函数
    va_list valst;
    va_start(valst, format);

    string log_str;
    m_mutex.lock();
    //年月日 小时:分:秒.微秒 日志级别
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    //日志内容写入缓冲区
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';

    log_str = m_buf;
    m_mutex.unlock();

    //同步就直接写，异步就加入阻塞队列
    if(m_is_async && !m_log_queue->full()) {
        auto res = m_log_queue->push(log_str);
    } else {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    va_end(valst);
}

void Log::flush()
{
    m_mutex.lock();
    fflush(m_fp); //强制刷新写入流缓冲区
    m_mutex.unlock();
}
