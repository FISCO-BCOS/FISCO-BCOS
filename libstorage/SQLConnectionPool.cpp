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
        URL_T oUrl = URL_new(connetionBuf.c_str());
        if (oUrl == NULL)
        {
            stringstream _exitInfo;
            _exitInfo << "parse url[" << connetionBuf << "] error please check";
            // TODO: chang errorExitOut to throw exception
            errorExitOut(_exitInfo);
        }
        LOG(DEBUG) << "init connection pool  url:" << connetionBuf;

        TRY
        {
            m_connectionPool = ConnectionPool_new(oUrl);
            ConnectionPool_setInitialConnections(m_connectionPool, _dbConfig.initConnections);
            ConnectionPool_setMaxConnections(m_connectionPool, _dbConfig.maxConnections);
            ConnectionPool_start(m_connectionPool);
        }
        CATCH(SQLException)
        {
            URL_free(&oUrl);
            LOG(ERROR) << "init connection pool failed url:" << connetionBuf << " please check";
            stringstream _exitInfo;
            _exitInfo << "init connection pool failed url:" << connetionBuf << " please check";
            errorExitOut(_exitInfo);
        }
        END_TRY;

        URL_free(&oUrl);
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
    LOG(ERROR) << _exitInfo.str();
    std::cerr << _exitInfo.str();
    raise(SIGTERM);
}

SQLConnectionPool::~SQLConnectionPool()
{
    ConnectionPool_stop(m_connectionPool);
    ConnectionPool_free(&m_connectionPool);
}