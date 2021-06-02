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
/** @file StashSQLBasicAccess.h
 *  @author wesleywang
 *  @date 2021-5-10
 */
#pragma once
#include <json/json.h>
#include <libstorage/SQLBasicAccess.h>
#include <libstorage/SQLConnectionPool.h>
#include <libstorage/Storage.h>
#include <libstorage/Table.h>

#define StashSQLBasicAccess_LOG(LEVEL) LOG(LEVEL) << "[StashSQLBasicAccess] "


namespace dev
{
namespace storage
{
class SQLConnectionPool;

class StashSQLBasicAccess
{
public:
    StashSQLBasicAccess();
    virtual ~StashSQLBasicAccess() {}
    typedef std::shared_ptr<StashSQLBasicAccess> Ptr;
    virtual int Select(int64_t _num, const std::string& _table, const std::string& _key,
                       Condition::Ptr _condition, std::vector<std::map<std::string, std::string>>& _values);
    virtual int SelectTableDataByNum(int64_t num, TableInfo::Ptr tableInfo, uint64_t start,
        uint32_t counts, std::vector<std::map<std::string, std::string>>& _values);

private:
    std::string BuildQuerySql(std::string _table, Condition::Ptr _condition);

    std::string BuildConditionSql(const std::string& _strPrefix,
                                  std::map<std::string, Condition::Range>::const_iterator& _it, Condition::Ptr _condition);

    SQLFieldType getFieldType(std::string const& _tableName);
    bool inline isBlobType(std::string const& _tableName)
    {
        auto fieldType = getFieldType(_tableName);
        if (fieldType == SQLFieldType::MediumBlobType || fieldType == SQLFieldType::LongBlobType)
        {
            return true;
        }
        return false;
    }

public:
    void setConnPool(std::shared_ptr<SQLConnectionPool>& _connPool);

private:
    std::shared_ptr<SQLConnectionPool> m_connPool;
};

}  // namespace storage

}  // namespace dev
