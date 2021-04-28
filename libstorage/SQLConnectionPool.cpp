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
    {
        stringstream ss;
        // Note: [auth-plugin=mysql_native_password] only appliable for mysql-connector with version
        // no smaller than 8.0
        //       in current period, this param is useless since mysql-connector-c has not been
        //       upgraded, we can only configure this option in mysql server and configure ssl=0
        //       when using mysql 8.x
        ss << "mysql://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort << "/" << _dbConfig.dbName
           << "?auth-plugin=mysql_native_password&user=" << _dbConfig.dbUsername
           << "&password=" << _dbConfig.dbPasswd << "&charset=" << _dbConfig.dbCharset
           << "&useUnicode=yes";

        m_url = URL_new(ss.str().c_str());
        std::cout << "ConnectionPoolConfig mysql... " << std::endl;

        if (m_url == NULL)
        {
            stringstream exitInfo;
            exitInfo << "parse IP[" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
                     << "] error please check";
            errorExitOut(exitInfo);
        }
        SQLConnectionPool_LOG(DEBUG)
            << "init connection pool IP:" << _dbConfig.dbIP << ":" << _dbConfig.dbPort;
        std::cout << "init connection pool IP:" << std::endl;
        std::cout << _dbConfig.dbIP << std::endl;
        std::cout << _dbConfig.dbPort << std::endl;
        std::cout << ss.str().c_str() << std::endl;


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
            std::cout << Exception_frame.message << std::endl;
            std::cout << Exception_frame.exception << std::endl;

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


int SQLConnectionPool::BeginTransaction(const Connection_T& _connection)
{
    Connection_beginTransaction(_connection);
    return 0;
}
int SQLConnectionPool::Commit(const Connection_T& _connection)
{
    Connection_commit(_connection);
    return 0;
}
int SQLConnectionPool::RollBack(const Connection_T& _connection)
{
    Connection_rollback(_connection);
    return 0;
}

inline void dev::storage::errorExitOut(std::stringstream& _exitInfo)
{
    SQLConnectionPool_LOG(ERROR) << _exitInfo.str();

    raise(SIGTERM);
    BOOST_THROW_EXCEPTION(StorageException(-1, _exitInfo.str()));
}

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

int SQLConnectionPool::GetActiveConnections()
{
    return ConnectionPool_active(m_connectionPool);
}
int SQLConnectionPool::GetMaxConnections()
{
    return ConnectionPool_getMaxConnections(m_connectionPool);
}

int SQLConnectionPool::GetTotalConnections()
{
    return ConnectionPool_size(m_connectionPool);
}

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
        ss << "mysql://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
           << "/information_schema?auth-plugin=mysql_native_password&user=" << _dbConfig.dbUsername
           << "&password=" << _dbConfig.dbPasswd << "&charset=" << _dbConfig.dbCharset
           << "&useUnicode=yes";
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

            string _dbName = _dbConfig.dbName;
            _dbName = boost::algorithm::replace_all_copy(_dbName, "\\", "\\\\");
            _dbName = boost::algorithm::replace_all_copy(_dbName, "`", "\\`");
            string _sql = "CREATE DATABASE IF NOT EXISTS ";
            _sql.append(_dbName);
            Connection_execute(_connection, "%s", _sql.c_str());
            _sql = "set global max_allowed_packet = 1073741824";
            Connection_execute(_connection, "%s", _sql.c_str());

            _sql = "SET GLOBAL sql_mode = 'STRICT_TRANS_TABLES'";
            Connection_execute(_connection, "%s", _sql.c_str());

            // support ROW_FORMAT=COMPRESSED, please ref to
            // https://mariadb.com/kb/en/innodb-compressed-row-format/
            // Use the select * from INFORMATION_SCHEMA.INNODB_CMP;
            // command to see if mysql has table compression enabled

            // when mysql version>8, no need to set innodb_file_format
            stringstream ss;
            ss << "SET @s = IF(version() < 8 OR version() LIKE '%MariaDB%',"
               << "'SET GLOBAL innodb_file_per_table = ON,"
               << "innodb_large_prefix = ON;',"
               << "'SET GLOBAL innodb_file_per_table = ON;')";

            PreparedStatement_T _prepareStatement =
                Connection_prepareStatement(_connection, "%s", ss.str().c_str());
            PreparedStatement_execute(_prepareStatement);
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
