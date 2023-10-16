/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/** @file SQLConnectionPool.cpp
 *  @author darrenyin
 *  @date 2019-04-25
 */

#include "SQLConnectionPool.h"
#include "StorageException.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <csignal>
#include <memory>

using namespace dev::storage;
using namespace std;

bool SQLConnectionPool::InitConnectionPool(const storage::ConnectionPoolConfig& _dbConfig)
{
    if (_dbConfig.dbType == "mysql")
    {   //用于进行字符串的输入和输出操作
        stringstream ss;
        // Note: [auth-plugin=mysql_native_password] only appliable for mysql-connector with version
        // no smaller than 8.0
        //       in current period, this param is useless since mysql-connector-c has not been
        //       upgraded, we can only configure this option in mysql server and configure ssl=0
        //       when using mysql 8.x
        ss << "oracle://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
           << "/?user=" << _dbConfig.dbUsername
           << "&password=" << _dbConfig.dbPasswd;
        //将ss转换为c风格的然后调用URL_new
        m_url = URL_new(ss.str().c_str());
        if (m_url == NULL)
        {
            //连接失败抛出异常
            stringstream exitInfo;
            exitInfo << "parse IP[" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
                     << "] error please check";
            errorExitOut(exitInfo);
        }
        SQLConnectionPool_LOG(DEBUG)
            << "init connection pool IP:" << _dbConfig.dbIP << ":" << _dbConfig.dbPort;

        TRY
        {
            m_connectionPool = ConnectionPool_new(m_url);
            ConnectionPool_setInitialConnections(m_connectionPool, _dbConfig.initConnections);
            ConnectionPool_setMaxConnections(m_connectionPool, _dbConfig.maxConnections);
            ConnectionPool_setConnectionTimeout(m_connectionPool, 28800);
            ConnectionPool_setReaper(m_connectionPool, 10);
            ConnectionPool_start(m_connectionPool);
        }
        CATCH(SQLException)
        {
            SQLConnectionPool_LOG(ERROR)
                << "init connection pool failed IP:" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
                << " error msg:" << Exception_frame.message;
            stringstream exitInfo;
            exitInfo << "init connection pool failed IP:" << _dbConfig.dbIP << ":"
                     << _dbConfig.dbPort << " error msg:" << Exception_frame.message;
            errorExitOut(exitInfo);
        }
        END_TRY;
    }
    else
    {
        stringstream exitInfo;
        exitInfo << "not support db type:" << _dbConfig.dbType;
        errorExitOut(exitInfo);
    }
    return true;
}


/*
    this function is used to obtain a new connection from the pool,
    If there are no connections available, a new connection is created
    and returned. If the pool has already handed out maxConnections,
    this call will return NULL
*/
    // 如果连接池中还有可用的连接，函数将从连接池中获取一个连接并返回。
    // 如果连接池中没有可用的连接，函数将创建一个新的连接，并将其返回。
    // 如果连接池已经分配了最大连接数，函数将返回 NULL，表示无法获取更多的连接。
Connection_T SQLConnectionPool::GetConnection()
{
    return ConnectionPool_getConnection(m_connectionPool);
}

/*
    Returns a connection to the pool
*/
int SQLConnectionPool::ReturnConnection(const Connection_T& _connection)
{
    ConnectionPool_returnConnection(m_connectionPool, _connection);
    return 0;
}

//mysql开启事务
int SQLConnectionPool::BeginTransaction(const Connection_T& _connection)
{
    Connection_beginTransaction(_connection);
    return 0;
}
//mysql提交事务
int SQLConnectionPool::Commit(const Connection_T& _connection)
{
    Connection_commit(_connection);
    return 0;
}
//mysql回滚
int SQLConnectionPool::RollBack(const Connection_T& _connection)
{
    Connection_rollback(_connection);
    return 0;
}
//出错退出
inline void dev::storage::errorExitOut(std::stringstream& _exitInfo)
{
    SQLConnectionPool_LOG(ERROR) << _exitInfo.str();
// 这行代码通过调用 raise 函数来向当前进程发送 SIGTERM 信号。SIGTERM 是一种常见的信号，通常用于请求进程正常终止。
    raise(SIGTERM);
    // 使用 Boost 库中的 BOOST_THROW_EXCEPTION 宏，抛出一个名为 StorageException 的异常。异常的构造函数接受两个参数：一个整数值 -1 和 _exitInfo 对象的字符串内容。
    BOOST_THROW_EXCEPTION(StorageException(-1, _exitInfo.str()));
}
//析构函数
SQLConnectionPool::~SQLConnectionPool()
{
    if (m_connectionPool)
    {
        ConnectionPool_stop(m_connectionPool);
        ConnectionPool_free(&m_connectionPool);
    }
    if (m_url)
    {
        URL_free(&m_url);
    }
}
//获取当前活跃连接
int SQLConnectionPool::GetActiveConnections()
{
    return ConnectionPool_active(m_connectionPool);
}
//获取最大连接数
int SQLConnectionPool::GetMaxConnections()
{
    return ConnectionPool_getMaxConnections(m_connectionPool);
}
//获取总连接数
int SQLConnectionPool::GetTotalConnections()
{
    return ConnectionPool_size(m_connectionPool);
}
//创建数据库
void SQLConnectionPool::createDataBase(const ConnectionPoolConfig& _dbConfig)
{
    if (_dbConfig.dbType == "mysql")
    {
        stringstream ss;
        // Note: [auth-plugin=mysql_native_password] only appliable for mysql-connector with version
        // no smaller than 8.0
        //       in current period, this param is useless since mysql-connector-c has not been
        //       upgraded, we can only configure this option in mysql server and configure ssl=0
        //       when using mysql 8.x
        //URL:=oracle://101.201.81.164:5236/?user=SYSDBA&password=Dameng111
        ss << "oracle://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
           << "/?user=" << _dbConfig.dbUsername
           << "&password=" << _dbConfig.dbPasswd;
        URL_T url = URL_new(ss.str().c_str());
        if (url == NULL)
        {
            stringstream _exitInfo;
            _exitInfo << "parse url[" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
                      << "] error please check";
            errorExitOut(_exitInfo);
        }
        SQLConnectionPool_LOG(DEBUG)
            << "init connection pool  IP:" << _dbConfig.dbIP << ":" << _dbConfig.dbPort;
        ConnectionPool_T _connectionPool = nullptr;
        volatile Connection_T _connection = nullptr;

        TRY
        {
            _connectionPool = ConnectionPool_new(url);
            ConnectionPool_setInitialConnections(_connectionPool, 2);
            ConnectionPool_setMaxConnections(_connectionPool, 2);
            ConnectionPool_start(_connectionPool);
            _connection = ConnectionPool_getConnection(_connectionPool);
            if (_connection == nullptr)
            {
                THROW(SQLException, "create database get connection failed");
            }

            // string _dbName = _dbConfig.dbName;
            // _dbName = boost::algorithm::replace_all_copy(_dbName, "\\", "\\\\");
            // _dbName = boost::algorithm::replace_all_copy(_dbName, "`", "\\`");
            // string _sql = "CREATE DATABASE IF NOT EXISTS ";
            // _sql.append(_dbName);
            // Connection_execute(_connection, "%s", _sql.c_str());
            // _sql = "set global max_allowed_packet = 1073741824";
            // Connection_execute(_connection, "%s", _sql.c_str());
            // _sql = "SET GLOBAL sql_mode = 'STRICT_TRANS_TABLES'";
            // Connection_execute(_connection, "%s", _sql.c_str());

            // support ROW_FORMAT=COMPRESSED, please ref to
            // https://mariadb.com/kb/en/innodb-compressed-row-format/
            // Use the select * from INFORMATION_SCHEMA.INNODB_CMP;
            // command to see if mysql has table compression enabled

            // when mysql version>8, no need to set innodb_file_format
            // stringstream ss;
            // ss << "SET @s = IF(version() < 8 OR version() LIKE '%MariaDB%',"
            //    << "'SET GLOBAL innodb_file_per_table = ON,"
            //    << "innodb_large_prefix = ON;',"
            //    << "'SET GLOBAL innodb_file_per_table = ON;')";

            // PreparedStatement_T _prepareStatement =
            //     Connection_prepareStatement(_connection, "%s", ss.str().c_str());
            // PreparedStatement_execute(_prepareStatement);
        }
        CATCH(SQLException)
        {
            if (_connection)
            {
                ConnectionPool_returnConnection(_connectionPool, _connection);
            }
            ConnectionPool_stop(_connectionPool);
            ConnectionPool_free(&_connectionPool);
            URL_free(&url);
            SQLConnectionPool_LOG(ERROR)
                << "init connection pool failed url:" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
                << " error msg:" << Exception_frame.message;
            stringstream _exitInfo;
            _exitInfo << "init connection pool failed url:" << _dbConfig.dbIP << ":"
                      << _dbConfig.dbPort << " error msg:" << Exception_frame.message;
            errorExitOut(_exitInfo);
        }


        END_TRY;
        ConnectionPool_returnConnection(_connectionPool, _connection);
        ConnectionPool_stop(_connectionPool);
        ConnectionPool_free(&_connectionPool);
        URL_free(&url);
    }
    else
    {
        stringstream exitInfo;
        exitInfo << "not support db type:" << _dbConfig.dbType;
        errorExitOut(exitInfo);
    }
}
