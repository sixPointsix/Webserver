#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "webserver.h"

using namespace std;

class Config
{
public:
    Config();
    ~Config() {}
    void parse_arg(int argc, char* argv[]);

    int PORT;
    int LOGWrite; //日志同步/异步
    int TRIGMode; //触发组合模式

    int LISTENTrigmode; //listenfd触发方式
    int CONNTrigmode; //connfd触发方式

    int OPT_LINGER; //优雅关闭连接
    int sql_num;
    int thread_num;

    int close_log; //是否关闭日志
    int actor_model; //并发模型reactor/proactor
};
#endif
