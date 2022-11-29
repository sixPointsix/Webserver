#include "config.h"
#include "log/log.h"
#include <iostream>

using namespace std;

int main(int argc, char* argv[]) {
    string user = "root";
    string passwd = "root";
    string dbName = "webserver";

    Config config; //解析命令行参数
    config.parse_arg(argc, argv);

    WebServer server;
    server.init(config.PORT, user, passwd, dbName, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num,
                config.close_log, config.actor_model);

    //初始化环境
    server.log_write();
    server.sql_pool();
    server.thread_pool();
    server.trig_mode();

    //监听
    server.eventListen();

    cout << "eventLoop" << endl;
    //开始运行
    server.eventLoop();

    return 0;
}
