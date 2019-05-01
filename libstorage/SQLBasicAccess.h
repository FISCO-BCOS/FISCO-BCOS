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
/** @file SQLBasicAccess.h
 *  @author darrenyin
 *  @date 2019-04-24
 */


#pragma once

#include "Storage.h"
#include "Table.h"
#include <json/json.h>
#include "libdevcore/SQLConnectionPool.h"


namespace dev
{
namespace storage
{
class SQLBasicAccess
{
public:
    int Select(h256 hash, int num, const std::string& table, const std::string& key,
                Condition::Ptr condition,Json::Value &respJson);
    int Commit(h256 hash, int num, 
            const std::vector<TableData::Ptr>& datas,h256 const& blockHash);
private:
    std::string BuildQuerySql(const std::string& table, Condition::Ptr condition);
    std::string GenerateConditionSql(const std::string &strPrefix,
                std::map<std::string, std::pair<Condition::Op, std::string>>::iterator &it);
    
    std::string BuildCommitSql(const std::string& table,
        const std::vector<std::string> &oVecFieldName,
        const std::vector<std::string> &oVecFieldValue);
public:
    void initConnPool( const std::string &dbtype,
        const std::string &dbip,
        uint32_t    dbport,
        const std::string &dbusername,
        const std::string &dbpasswd,
        const std::string &dbname,
        const std::string &dbcharset,
        uint32_t    initconnections,
        uint32_t    maxconnections);
private:
    dev::db::SQLConnectionPool m_oConnPool;
};

}  // namespace storage

}  // namespace dev
