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

#include "SQLConnectionPool.h"
#include "Storage.h"
#include "Table.h"
#include <json/json.h>


const static uint32_t maxPlaceHolderCnt = 60000;

#define SQLBasicAccess_LOG(LEVEL) LOG(LEVEL) << "[SQLBasicAccess] "

namespace dev
{
namespace storage
{
struct SQLPlaceHoldItem
{
    std::string sql;
    uint32_t placeHolerCnt;

    SQLPlaceHoldItem() : placeHolerCnt(0) {}
};

class SQLBasicAccess
{
public:
    virtual ~SQLBasicAccess() {}
    typedef std::shared_ptr<SQLBasicAccess> Ptr;
    virtual int Select(int64_t num, const std::string& table, const std::string& key,
        Condition::Ptr condition, std::vector<std::map<std::string, std::string>>& values);
    virtual int Commit(int64_t num, const std::vector<TableData::Ptr>& datas);

private:
    std::string BuildQuerySql(const std::string& table, Condition::Ptr condition);
    std::string GenerateConditionSql(const std::string& strPrefix,
        std::map<std::string, Condition::Range>::const_iterator& it, Condition::Ptr condition);

    std::vector<SQLPlaceHoldItem> BuildCommitSql(const std::string& _table,
        const std::string& _fieldStr, const std::vector<std::string>& _fieldValue);

    std::string BuildCreateTableSql(
        const std::string& tablename, const std::string& keyfield, const std::string& valuefield);

    std::string GetCreateTableSql(const Entry::Ptr& data);
    void GetCommitFieldNameAndValue(const Entries::Ptr& data, const std::string& strNum,
        std::map<std::string, std::vector<std::string>>& _fieldValue);

    void GetCommitFieldNameAndValueEachTable(const std::string& _num, const Entries::Ptr& data,
        const std::vector<size_t>& indexlist, std::string& fieldList,
        std::vector<std::string>& valueList);

    int CommitDo(int64_t num, const std::vector<TableData::Ptr>& datas, std::string& errmsg);

public:
    virtual void ExecuteSql(const std::string& _sql);
    void setConnPool(SQLConnectionPool::Ptr& _connPool);

private:
    SQLConnectionPool::Ptr m_connPool;
};

}  // namespace storage

}  // namespace dev
