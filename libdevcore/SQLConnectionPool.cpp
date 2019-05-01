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

using namespace dev::db;
using namespace std;

int  SQLConnectionPool::InitConnectionPool(const std::string &strDbType,
                const std::string &strDbIp,
                uint32_t    dwDbPort,
                const std::string &strDbUser,
                const std::string &strDbPasswd,
                const std::string &strDbname,
                const std::string &strDbCharset,
                uint32_t    dwInitConnections,
                uint32_t    dwMaxConnection)
{
    if(strDbType == "mysql")
    {
        char cConnectionBuf[2048] = {0};
        snprintf(cConnectionBuf,sizeof(cConnectionBuf),"mysql://%s:%u/%s?user=%s&password=%s&charset=%s",
                strDbIp.c_str(),dwDbPort,
                strDbname.c_str(),strDbUser.c_str(),
                strDbPasswd.c_str(),strDbCharset.c_str());
        string strConnetionBuf = cConnectionBuf;
        URL_T oUrl = URL_new(strConnetionBuf.c_str());
        if(oUrl == NULL)
        {
            stringstream _exitInfo;
            _exitInfo<<"parse url["<<strConnetionBuf<<"] error please check";
            errorExitOut(_exitInfo);
        }
        LOG(DEBUG)<<"init connection pool  url:"<<strConnetionBuf;
	try
        {
            m_oPool = ConnectionPool_new(oUrl);
            ConnectionPool_setInitialConnections(m_oPool, dwInitConnections);
            ConnectionPool_setMaxConnections(m_oPool, dwMaxConnection);
            ConnectionPool_start(m_oPool);
        }
	catch(Exception_T &e)
	{
            LOG(ERROR)<<"init connection pool failed url:"<<strConnetionBuf<<" please check";
             stringstream _exitInfo;
            _exitInfo<<"init connection pool failed url:"<<strConnetionBuf<<" please check";
            errorExitOut(_exitInfo);
        }
        
    }
    else
    {
        stringstream _exitInfo;
        _exitInfo<<"not support db type:"<<strDbType;
        errorExitOut(_exitInfo);
    }
    return 0;
}
/*
    this function is used to obtain a new connection from the pool,
    If there are no connections available, a new connection is created 
    and returned. If the pool has already handed out maxConnections,
    this call will return NULL
*/
Connection_T    SQLConnectionPool::GetConnection()
{
    return  ConnectionPool_getConnection(m_oPool);
}

/*
    Returns a connection to the pool
*/
int SQLConnectionPool::ReturnConnection(const Connection_T &con)
{
    ConnectionPool_returnConnection(m_oPool,con);
    return 0;
}


int SQLConnectionPool::BeginTransaction(const Connection_T &t)
{
    Connection_beginTransaction(t);
    return 0;
}
int SQLConnectionPool::Commit(const Connection_T &t)
{
    Connection_commit(t);
    return 0;
}
int SQLConnectionPool::RollBack(const Connection_T &t)
{
    Connection_rollback(t);
    return 0;
}
