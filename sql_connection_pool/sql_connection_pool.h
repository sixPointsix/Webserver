#ifndef _SQL_CONNECTION_POOL_
#define _SQL_CONNECTION_POOL_

#include <iostream>
#include <list>
#include <mysql/mysql.h>
#include "../locker/locker.h"
#include "../log/log.h"

using namespace std;

//单例模式的数据库连接池类
class ConnectionPool
{
public:
    static ConnectionPool* getConnectionPool();
    void init(string username, string passwd, string dbName, string ip, unsigned short port, int maxSize, int close_log);
    ConnectionPool(const ConnectionPool& obj) = delete;
    ConnectionPool& operator = (const ConnectionPool& obj) = delete;

    //获取与释放连接
    MYSQL* getConnection();
    bool releaseConnection(MYSQL* conn);

    inline int getFreeConn(); //获取当前空闲连接数
    void destroyPool(); // 销毁数据库连接池的外部接口

private:
    ConnectionPool();
    ~ConnectionPool();

    locker m_lock;
    sem m_sem;
    list<MYSQL*> m_connList;

private:
    int m_maxSize; //最大连接数
    int m_usedSize; //已使用连接数
    int m_freeSize; //当前空闲连接数
    string m_username;
    string m_passwd;
    string m_ip;
    string m_dbName;
    unsigned short m_port;
    int m_close_log; //日志开关
};

class connection
{
public:
    connection(MYSQL** conn, ConnectionPool* pool);
    ~connection();
private:
    MYSQL* m_conn;
    ConnectionPool* m_pool;
};

#endif
