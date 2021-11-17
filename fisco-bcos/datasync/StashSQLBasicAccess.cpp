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
/** @file StashSQLBasicAccess.cpp
 *  @author wesleywang
 *  @date 2021-5-10
 */

#include "StashSQLBasicAccess.h"
#include "libconfig/GlobalConfigure.h"
#include <libstorage/SQLConnectionPool.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/dynamic_bitset.hpp>

using namespace dev::storage;
using namespace std;

StashSQLBasicAccess::StashSQLBasicAccess() {}

int StashSQLBasicAccess::Select(int64_t, const string& _table, const string&, Condition::Ptr _condition,
                                vector<map<string, string>>& _values)
{
    string sql = this->BuildQuerySql(_table, _condition);
    Connection_T conn = m_connPool->GetConnection();
    uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    while (conn == NULL && retryCnt++ < retryMax)
    {
        StashSQLBasicAccess_LOG(WARNING)
                << "table:" << _table << "sql:" << sql << " get connection failed";
        sleep(1);
        conn = m_connPool->GetConnection();
    }

    if (conn == NULL)
    {
        StashSQLBasicAccess_LOG(ERROR) << "table:" << _table << "sql:" << sql
                                       << " get connection failed";
        return -1;
    }
    TRY
            {
                PreparedStatement_T _prepareStatement =
                    Connection_prepareStatement(conn, "%s", sql.c_str());
                if (_condition)
                {
                    uint32_t index = 0;
                    for (auto& it : *(_condition))
                    {
                        PreparedStatement_setString(
                            _prepareStatement, ++index, it.second.right.second.c_str());
                    }
                }
                ResultSet_T result = PreparedStatement_executeQuery(_prepareStatement);
                int32_t columnCnt = ResultSet_getColumnCount(result);

                bool tableWithBlobField = isBlobType(_table);
                while (ResultSet_next(result))
                {
                    map<string, string> value;
                    for (int32_t index = 1; index <= columnCnt; ++index)
                    {
                        auto fieldName = ResultSet_getColumnName(result, index);
                        if (tableWithBlobField)
                        {
                            int size;
                            auto bytes = ResultSet_getBlob(result, index, &size);
                            // Note: Since the field may not be set, it must be added here to determine
                            //       whether the value of the obtained field is nullptr
                            if (bytes)
                            {
                                value[fieldName] = string((char*)bytes, size);
                            }
                        }
                        else
                        {
                            auto selectResult = ResultSet_getString(result, index);
                            if (selectResult)
                            {
                                value[fieldName] = selectResult;
                            }
                        }
                    }
                    _values.push_back(move(value));
                }
            }
        CATCH(SQLException)
            {
                m_connPool->ReturnConnection(conn);
                // Note: when select exception caused by table doesn't exist and sql error,
                //       here must return 0, in case of the DB is normal but the sql syntax error or the
                //       table will be created later
                StashSQLBasicAccess_LOG(ERROR) << "select exception for sql error:" << Exception_frame.message;
                return 0;
            }
    END_TRY;
    m_connPool->ReturnConnection(conn);
    return 0;
}

string StashSQLBasicAccess::BuildQuerySql(string _table, Condition::Ptr _condition)
{
    _table = boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
    _table = boost::algorithm::replace_all_copy(_table, "`", "\\`");
    string sql = "select * from `";
    sql.append(_table).append("`");
    if (_condition)
    {
        bool hasWhereClause = false;
        for (auto it = _condition->begin(); it != _condition->end(); ++it)
        {
            if (!hasWhereClause)
            {
                hasWhereClause = true;
                sql.append(BuildConditionSql(" where", it, _condition));
            }
            else
            {
                sql.append(BuildConditionSql(" and", it, _condition));
            }
        }
    }
    return sql;
}

string StashSQLBasicAccess::BuildConditionSql(const string& _strPrefix,
                                              map<string, Condition::Range>::const_iterator& _it, Condition::Ptr _condition)
{
    string strConditionSql = _strPrefix;
    if (_it->second.left.second == _it->second.right.second && _it->second.left.first &&
        _it->second.right.first)
    {
        strConditionSql.append(" `").append(_it->first).append("`=").append("?");
    }
    else
    {
        if (_it->second.left.second != _condition->unlimitedField())
        {
            if (_it->second.left.first)
            {
                strConditionSql.append(" `").append(_it->first).append("`>=").append("?");
            }
            else
            {
                strConditionSql.append(" `").append(_it->first).append("`>").append("?");
            }
        }
        if (_it->second.right.second != _condition->unlimitedField())
        {
            if (_it->second.right.first)
            {
                strConditionSql.append(" `").append(_it->first).append("`<=").append("?");
            }
            else
            {
                strConditionSql.append(" `").append(_it->first).append("`<").append("?");
            }
        }
    }
    return strConditionSql;
}

bool endswith(const std::string& str, const std::string& end)
{
    int srclen = str.size();
    int endlen = end.size();
    if(srclen >= endlen)
    {
        string temp = str.substr(srclen - endlen, endlen);
        if(temp == end)
            return true;
    }

    return false;
}

int StashSQLBasicAccess::SelectTableDataByNum(int64_t num, TableInfo::Ptr tableInfo, uint64_t start,
                                              uint32_t counts, vector<map<string, string>>& _values)
{
    string tableName = tableInfo->name;
    if (dev::stringCmpIgnoreCase(tableName, "_sys_tables_"))
    {
        if (endswith(tableName, "_"))
        {
            tableName = tableName + "d_";
        }
        else
        {
            tableName = tableName + "_d_";
        }
    }
    string sql =
        "select * from " + tableName + " a inner join (select _id_,max(_num_) as num from "
        + tableName + " where _id_ > " + to_string(start) + " and _num_ <= " + to_string(num)
        +  " group by _id_ order by _id_ limit " + to_string(counts)
        + ") b on a._id_=b._id_ and a._num_=b.num";

    Connection_T conn = m_connPool->GetConnection();
    uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    while (conn == NULL && retryCnt++ < retryMax)
    {
        StashSQLBasicAccess_LOG(WARNING)
                << "table:" << tableInfo->name << "sql:" << sql << " get connection failed";
        sleep(1);
        conn = m_connPool->GetConnection();
    }
    if (conn == NULL)
    {
        StashSQLBasicAccess_LOG(ERROR)
                << "table:" << tableInfo->name << "sql:" << sql << " get connection failed";
        return -1;
    }
    TRY
            {
                PreparedStatement_T _prepareStatement =
                    Connection_prepareStatement(conn, "%s", sql.c_str());
                ResultSet_T result = PreparedStatement_executeQuery(_prepareStatement);
                int32_t columnCnt = ResultSet_getColumnCount(result);
                bool tableWithBlobField = isBlobType(tableInfo->name);
                while (ResultSet_next(result))
                {
                    map<string, string> value;
                    for (int32_t index = 1; index <= columnCnt; ++index)
                    {
                        auto fieldName = ResultSet_getColumnName(result, index);
                        string field = fieldName;
                        if (!dev::stringCmpIgnoreCase(field, "pk_id") || !dev::stringCmpIgnoreCase(field, "_hash_")){
                            continue;
                        }
                        if (tableWithBlobField)
                        {
                            int size;
                            auto bytes = ResultSet_getBlob(result, index, &size);
                            // Note: Since the field may not be set, it must be added here to determine
                            //       whether the value of the obtained field is nullptr
                            if (bytes)
                            {
                                value[fieldName] = string((char*)bytes, size);
                            }
                        }
                        else
                        {
                            auto selectResult = ResultSet_getString(result, index);
                            if (selectResult)
                            {
                                value[fieldName] = selectResult;
                            }
                        }
                    }
                    _values.push_back(move(value));
                }
            }
        CATCH(SQLException)
            {
                m_connPool->ReturnConnection(conn);
                // Note: when select exception caused by table doesn't exist and sql error,
                //       here must return 0, in case of the DB is normal but the sql syntax error or the
                //       table will be created later
                StashSQLBasicAccess_LOG(ERROR)
                        << "select exception for sql error:" << Exception_frame.message;
                return 0;
            }
    END_TRY;
    m_connPool->ReturnConnection(conn);
    return 0;
}

void StashSQLBasicAccess::setConnPool(SQLConnectionPool::Ptr& _connPool)
{
    this->m_connPool = _connPool;
}

SQLFieldType StashSQLBasicAccess::getFieldType(std::string const& _tableName)
{
    // _sys_hash_2_block_ or _sys_block_2_nonce, the SYS_VALUE field is blob
    if (_tableName == SYS_HASH_2_BLOCK || _tableName == SYS_BLOCK_2_NONCES || _tableName == SYS_CNS)
    {
        if (g_BCOSConfig.version() >= V2_6_0)
        {
            return SQLFieldType::LongBlobType;
        }
        return SQLFieldType::LongStringType;
    }
    // _sys_hash_2_blockheader_, the SYS_VALUE and SYS_SIG_LIST are blob
    if (_tableName == SYS_HASH_2_BLOCKHEADER)
    {
        return SQLFieldType::LongBlobType;
    }
    // contract table, all the fields are blob type
    if (boost::starts_with(_tableName, string("c_")))
    {
        if (g_BCOSConfig.version() >= V2_5_0)
        {
            return SQLFieldType::MediumBlobType;
        }
    }
    // supported_version >= v2.6.0, all the field type of user created table is mediumblob
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        return SQLFieldType::MediumBlobType;
    }
    // supported_version < v2.6.0, the user created table is mediumtext
    return SQLFieldType::MediumStringType;
}
