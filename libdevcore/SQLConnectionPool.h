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

#include <libdevcore/Guards.h>
#include <libdevcore/easylog.h>
#include <zdb.h>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>

namespace dev
{
namespace db
{
class SQLConnectionPool
{
public:
    SQLConnectionPool(){};
    ~SQLConnectionPool();
    int InitConnectionPool(const std::string& strDbType, const std::string& strDbIp,
        uint32_t dwDbPort, const std::string& strDbname, const std::string& strDbUser,
        const std::string& strDbPasswd, const std::string& strDbCharset, uint32_t dwInitConnections,
        uint32_t dwMaxInitConnection);
    Connection_T GetConnection();
    int ReturnConnection(const Connection_T& t);
    int BeginTransaction(const Connection_T& t);
    int Commit(const Connection_T& t);
    int RollBack(const Connection_T& t);

private:
    ConnectionPool_T m_oPool;

protected:
};

inline void errorExitOut(std::stringstream& _exitInfo)
{
    LOG(ERROR) << _exitInfo.str();
    std::cerr << _exitInfo.str();
    raise(SIGTERM);
}


}  // namespace db
}  // namespace dev
