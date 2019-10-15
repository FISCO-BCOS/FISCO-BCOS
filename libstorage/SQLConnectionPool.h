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
/** @file SQLConnectionPool.h
 *  @author darrenyin
 *  @date 2019-04-24
 */

#pragma once

#include <zdb.h>
#include <iostream>
#include <memory>
#include <string>

#define SQLConnectionPool_LOG(LEVEL) LOG(LEVEL) << "[SQLConnectionPool] "

namespace dev
{
namespace storage
{
struct ConnectionPoolConfig
{
    std::string dbType;
    std::string dbIP;
    uint32_t dbPort;
    std::string dbUsername;
    std::string dbPasswd;
    std::string dbName;
    std::string dbCharset;
    uint32_t initConnections;
    uint32_t maxConnections;
};

#define SQLConnectionPool_LOG(LEVEL) LOG(LEVEL) << "[SQLConnectionPool] "

class SQLConnectionPool
{
public:
    typedef std::shared_ptr<SQLConnectionPool> Ptr;

    SQLConnectionPool(){};
    ~SQLConnectionPool();
    bool InitConnectionPool(const ConnectionPoolConfig& _dbConfig);
    Connection_T GetConnection();
    int ReturnConnection(const Connection_T& _connection);
    int BeginTransaction(const Connection_T& _connection);
    int Commit(const Connection_T& _connection);
    int RollBack(const Connection_T& _connection);

    int GetActiveConnections();
    int GetMaxConnections();

    int GetTotalConnections();

    void createDataBase(const ConnectionPoolConfig& _dbConfig);

private:
    ConnectionPool_T m_connectionPool;
    URL_T m_url;
};

inline void errorExitOut(std::stringstream& _exitInfo);

}  // namespace storage
}  // namespace dev
