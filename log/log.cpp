#include "log.h"
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <cstring>

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

Log::Log() {
    if(m_fp != NULL) fclose(m_fp);
}

void Log::init(const char* file_name, int log_buf_size, int split_lines, int max_queue_size)
{
    if(max_queue_size > 0) {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);

        //创建一个负责异步写日志的线程
        //这里涉及到一个知识点，就是说pthread_create的指向的必须是静态函数;静态函数如何调用类的动态成员
        //this做参数 / 类的静态对象
        pthread_t tid;
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', sizeof(m_buf));
    m_split_lines = split_lines;
    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //从后往前找到第一个'\'的位置
    const char* p = strrchr(file_name, '\');
    char log_full_name[256] = {0};

    //给日志命名
    if(p == NULL) {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s",
                my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else {
        p ++;
        strcpy(log_name, p);
        strncpy(dir_name, filename, p - filename);

        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", 
                dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.mday, log_name);
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
    struct tm my_tm = &sys_tm; //获取时间

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


}

void Log::flush()
{
    m_mutex.lock();
    fflush(m_fp); //强制刷新写入流缓冲区
    m_mutex.unlock();
}
