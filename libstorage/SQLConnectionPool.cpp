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
#include <libdevcore/easylog.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <csignal>
#include <memory>

using namespace dev::storage;
using namespace std;

bool SQLConnectionPool::InitConnectionPool(const storage::ZDBConfig& _dbConfig)
{
    if (_dbConfig.dbType == "mysql")
    {
        stringstream ss;
        ss << "mysql://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort << "/" << _dbConfig.dbName
           << "?user=" << _dbConfig.dbUsername << "&password=" << _dbConfig.dbPasswd
           << "&charset=" << _dbConfig.dbCharset;
        auto connetionBuf = ss.str();
        URL_T _url = URL_new(connetionBuf.c_str());
        if (_url == NULL)
        {
            stringstream _exitInfo;
            _exitInfo << "parse url[" << connetionBuf << "] error please check";
            // TODO: chang errorExitOut to throw exception
            errorExitOut(_exitInfo);
        }
        SQLConnectionPool_LOG(DEBUG) << "init connection pool  url:" << connetionBuf;

        TRY
        {
            m_connectionPool = ConnectionPool_new(_url);
            ConnectionPool_setInitialConnections(m_connectionPool, _dbConfig.initConnections);
            ConnectionPool_setMaxConnections(m_connectionPool, _dbConfig.maxConnections);
            ConnectionPool_start(m_connectionPool);
        }
        CATCH(SQLException)
        {
            URL_free(&_url);
            SQLConnectionPool_LOG(ERROR)
                << "init connection pool failed url:" << connetionBuf << " please check";
            stringstream _exitInfo;
            _exitInfo << "init connection pool failed url:" << connetionBuf << " please check";
            errorExitOut(_exitInfo);
        }
        END_TRY;

        URL_free(&_url);
    }
    else
    {
        stringstream _exitInfo;
        _exitInfo << "not support db type:" << _dbConfig.dbType;
        // TODO: chang errorExitOut to throw exception
        errorExitOut(_exitInfo);
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

// TODO: chang errorExitOut to throw exception
inline void dev::storage::errorExitOut(std::stringstream& _exitInfo)
{
    SQLConnectionPool_LOG(ERROR) << _exitInfo.str();
    std::cerr << _exitInfo.str();
    raise(SIGTERM);
}

SQLConnectionPool::~SQLConnectionPool()
{
    ConnectionPool_stop(m_connectionPool);
    ConnectionPool_free(&m_connectionPool);
}

int SQLConnectionPool::GetActiveConnections()
{
    return ConnectionPool_active(m_connectionPool);
}
int SQLConnectionPool::GetMaxConnections()
{
    return ConnectionPool_getMaxConnections(m_connectionPool);
}


void SQLConnectionPool::createDataBase(const ZDBConfig& _dbConfig)
{
    if (_dbConfig.dbType == "mysql")
    {
        stringstream ss;
        ss << "mysql://" << _dbConfig.dbIP << ":" << _dbConfig.dbPort
           << "/information_schema?user=" << _dbConfig.dbUsername
           << "&password=" << _dbConfig.dbPasswd << "&charset=" << _dbConfig.dbCharset;
        auto connetionBuf = ss.str();
        URL_T _url = URL_new(connetionBuf.c_str());
        if (_url == NULL)
        {
            stringstream _exitInfo;
            _exitInfo << "parse url[" << connetionBuf << "] error please check";
            // TODO: chang errorExitOut to throw exception
            errorExitOut(_exitInfo);
        }
        SQLConnectionPool_LOG(DEBUG) << "init connection pool  url:" << connetionBuf;

        ConnectionPool_T _connectionPool = nullptr;
        volatile Connection_T _connection = nullptr;

        TRY
        {
            _connectionPool = ConnectionPool_new(_url);
            ConnectionPool_setInitialConnections(_connectionPool, 2);
            ConnectionPool_setMaxConnections(_connectionPool, 2);
            ConnectionPool_start(_connectionPool);

            _connection = ConnectionPool_getConnection(_connectionPool);
            if (_connection == nullptr)
            {
                THROW(SQLException, "create database get connection failed");
            }

            string _dbName = _dbConfig.dbName;
            boost::algorithm::replace_all_copy(_dbName, "\\", "\\\\");
            boost::algorithm::replace_all_copy(_dbName, "`", "\\`");
            string _sql = "CREATE DATABASE IF NOT EXISTS ";
            _sql.append(_dbName);
            Connection_execute(_connection, "%s", _sql.c_str());
        }
        CATCH(SQLException)
        {
            if (_connection)
            {
                ConnectionPool_returnConnection(_connectionPool, _connection);
            }
            ConnectionPool_stop(_connectionPool);
            ConnectionPool_free(&_connectionPool);
            URL_free(&_url);
            SQLConnectionPool_LOG(ERROR)
                << "init connection pool failed url:" << connetionBuf << " please check";
            stringstream _exitInfo;
            _exitInfo << "init connection pool failed url:" << connetionBuf << " please check";
            errorExitOut(_exitInfo);
        }


        END_TRY;
        ConnectionPool_returnConnection(_connectionPool, _connection);
        ConnectionPool_stop(_connectionPool);
        ConnectionPool_free(&_connectionPool);
        URL_free(&_url);
    }
    else
    {
        stringstream _exitInfo;
        _exitInfo << "not support db type:" << _dbConfig.dbType;
        // TODO: chang errorExitOut to throw exception
        errorExitOut(_exitInfo);
    }
}
