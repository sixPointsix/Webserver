#include <mysql/mysql.h>
#include <string>
#include "sql_connection_pool.h"

using namespace std;

ConnectionPool::ConnectionPool() {
    m_usedSize = m_freeSize = 0;
}

ConnectionPool::~ConnectionPool() {
    destroyPool();
}

//局部静态变量的懒汉模式，线程安全的
ConnectionPool* ConnectionPool::getConnectionPool() {
    static ConnectionPool pool;
    return &pool;
}

//init初始化
void ConnectionPool::init(string username, string passwd, string dbName, string ip, unsigned short port, int maxSize, int close_log) {
    m_username = username;
    m_passwd = passwd;
    m_ip = ip;
    m_port = port;
    m_dbName = dbName;
    m_maxSize = maxSize;
    m_close_log = close_log;

    //创建连接并加入List
    for(int i = 0; i < maxSize; ++ i) {
        MYSQL* conn = nullptr;
        conn = mysql_init(conn);

        if(conn == nullptr) {
            LOG_ERROR("%s", "MYSQL Error");
            exit(1);
        }
        conn = mysql_real_connect(conn, ip.c_str(), username.c_str(), passwd.c_str(), dbName.c_str(), port, NULL, 0);
        if(conn == nullptr) {
            LOG_ERROR("%s", "MYSQL Error");
            exit(1);
        }

        ++ m_freeSize;
        m_connList.push_back(conn);
    }

    m_sem = sem(m_freeSize);
}

//获取连接
MYSQL* ConnectionPool::getConnection() {
    if(m_connList.size() == 0) return nullptr;

    m_sem.wait();
    m_lock.lock();
    MYSQL* conn = m_connList.front();
    m_connList.pop_front();
    -- m_freeSize;
    ++ m_usedSize;
    m_lock.unlock();

    return conn;
}

//释放连接
bool ConnectionPool::releaseConnection(MYSQL* conn) {
    if(conn == nullptr) return false;

    m_lock.lock();
    m_connList.push_back(conn);
    -- m_usedSize;
    ++ m_freeSize;
    m_lock.unlock();
    m_sem.post();

    return true;
}

int ConnectionPool::getFreeConn() {
    return this->m_freeSize;
}

//销毁连接池，把析构函数私有化了
void ConnectionPool::destroyPool() {
    m_lock.lock();
    while(m_connList.size() > 0) {
        MYSQL* conn = m_connList.front();
        m_connList.pop_front();
        mysql_close(conn);
    }
    m_usedSize = 0;
    m_freeSize = 0;
    m_lock.unlock();
}


//利用connection类的封装实现RAII
connection::connection(MYSQL** conn, ConnectionPool* pool) {
    *conn = pool->getConnection();

    m_conn = *conn;
    m_pool = pool;
}

connection::~connection() {
    m_pool->releaseConnection(m_conn);
}
