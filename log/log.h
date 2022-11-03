#ifndef _LOG_H_
#define _LOG_H_

class Log
{
public:
    static Log* get_instance() {
        static Log instance;
        return &instance;
    }

    //输出地文件名，日志缓冲区大小，日志文件行数，异步日志队列长度
    //通过max_queue_size长度确定是否是异步
    bool init(const char* file_name, int log_buf_size = 8192,
              int split_lines = 5000000, int max_queue_size = 0);

    //异步写日志的公有方法
    static void* flush_log_thread(void* args) {
        Log::get_instance()->async_write_log();
    }

    //标准格式输入写日志，分成日志级别
    void write_log(int log_level, const char* format, ...);

    //强制刷新缓冲区
    void flush();

private:
    Log();
    virtual ~Log();

    void* async_write_log() {
        string log;
        while(m_log_queue.pop(log)) {
            m_mutex.lock();
            fputs(log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log输出的文件名
    int m_split_lines; //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count; //日志行数
    int m_today; //日期，用于日志输出
    FILE* m_fp; //文件指针
    char* m_buf; //日志输出缓冲区
    block_queue<string> *m_log_queue; //阻塞队列
    bool m_is_async; //是否异步的标志位
    locker m_mutex; //线程同步类
};

//不同日志级别的日志输出，参照log4J
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, __VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, __VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, __VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, __VA_ARGS__)

#endif
