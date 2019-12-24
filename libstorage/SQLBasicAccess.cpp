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
/** @file SQLBasicAccess.cpp
 *  @author darrenyin
 *  @date 2019-04-24
 */

#include "SQLBasicAccess.h"
#include "StorageException.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>


const static char HAS_VAlue = '1';
const static char NOT_HAS_VAlue = '0';

using namespace dev::storage;
using namespace std;

int SQLBasicAccess::Select(int64_t, const std::string& _table, const std::string&,
    Condition::Ptr condition, std::vector<std::map<std::string, std::string>>& values)
{
    std::string sql = this->BuildQuerySql(_table, condition);
    Connection_T conn = m_connPool->GetConnection();
    uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    while (conn == NULL && retryCnt++ < retryMax)
    {
        SQLBasicAccess_LOG(WARNING)
            << "table:" << _table << "sql:" << sql << " get connection failed";
        sleep(1);
        conn = m_connPool->GetConnection();
    }

    if (conn == NULL)
    {
        SQLBasicAccess_LOG(ERROR) << "table:" << _table << "sql:" << sql
                                  << " get connection failed";
        return -1;
    }
    TRY
    {
        PreparedStatement_T _prepareStatement =
            Connection_prepareStatement(conn, "%s", sql.c_str());
        if (condition)
        {
            uint32_t index = 0;
            for (auto& it : *(condition))
            {
                PreparedStatement_setString(
                    _prepareStatement, ++index, it.second.right.second.c_str());
            }
        }
        ResultSet_T result = PreparedStatement_executeQuery(_prepareStatement);
        int32_t columnCnt = ResultSet_getColumnCount(result);
        while (ResultSet_next(result))
        {
            std::map<std::string, std::string> value;
            for (int32_t index = 1; index <= columnCnt; ++index)
            {
                if (ResultSet_getString(result, index) != NULL)
                {
                    value[ResultSet_getColumnName(result, index)] =
                        ResultSet_getString(result, index);
                }
            }
            values.push_back(std::move(value));
        }
    }
    CATCH(SQLException)
    {
        SQLBasicAccess_LOG(ERROR) << "select exception:" << Exception_frame.message;
        m_connPool->ReturnConnection(conn);
        return 0;
    }
    END_TRY;
    m_connPool->ReturnConnection(conn);
    return 0;
}

std::string SQLBasicAccess::BuildQuerySql(const std::string& _table, Condition::Ptr condition)
{
    boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
    boost::algorithm::replace_all_copy(_table, "`", "\\`");
    std::string sql = "select * from `";
    sql.append(_table).append("`");
    if (condition)
    {
        uint32_t index = 0;
        auto it = condition->begin();
        for (; it != condition->end(); ++it)
        {
            if (index == 0)
            {
                ++index;
                sql.append(GenerateConditionSql(" where", it, condition));
            }
            else
            {
                sql.append(GenerateConditionSql(" and", it, condition));
            }
        }
    }
    return sql;
}
std::string SQLBasicAccess::GenerateConditionSql(const std::string& strPrefix,
    std::map<std::string, Condition::Range>::const_iterator& it, Condition::Ptr condition)
{
    string strConditionSql = strPrefix;
    if (it->second.left.second == it->second.right.second && it->second.left.first &&
        it->second.right.first)
    {
        strConditionSql.append(" `").append(it->first).append("`=").append("?");
    }
    else
    {
        if (it->second.left.second != condition->unlimitedField())
        {
            if (it->second.left.first)
            {
                strConditionSql.append(" `").append(it->first).append("`>=").append("?");
            }
            else
            {
                strConditionSql.append(" `").append(it->first).append("`>").append("?");
            }
        }
        if (it->second.right.second != condition->unlimitedField())
        {
            if (it->second.right.first)
            {
                strConditionSql.append(" `").append(it->first).append("`<=").append("?");
            }
            else
            {
                strConditionSql.append(" `").append(it->first).append("`<").append("?");
            }
        }
    }
    return strConditionSql;
}

std::string SQLBasicAccess::BuildCreateTableSql(
    const std::string& tablename, const std::string& keyfield, const std::string& valuefield)
{
    boost::algorithm::replace_all_copy(tablename, "\\", "\\\\");
    boost::algorithm::replace_all_copy(tablename, "`", "\\`");

    boost::algorithm::replace_all_copy(keyfield, "\\", "\\\\");
    boost::algorithm::replace_all_copy(keyfield, "`", "\\`");

    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << tablename << "`(\n";
    ss << " `_id_` BIGINT unsigned auto_increment,\n";
    if (g_BCOSConfig.version() <= V2_1_0)
    {
        ss << " `_hash_` varchar(128) DEFAULT NULL,\n";
    }
    ss << " `_num_` BIGINT not null,\n";
    ss << " `_status_` int not null,\n";
    ss << "`" << keyfield << "` varchar(255) default '',\n";

    SQLBasicAccess_LOG(DEBUG) << "valuefield:" << valuefield;
    std::vector<std::string> vecSplit;
    boost::split(vecSplit, valuefield, boost::is_any_of(","));
    auto it = vecSplit.begin();
    for (; it != vecSplit.end(); ++it)
    {
        boost::algorithm::replace_all_copy(*it, "\\", "\\\\");
        boost::algorithm::replace_all_copy(*it, "`", "\\`");
        ss << "`" << *it << "` mediumtext,\n";
    }
    ss << " PRIMARY KEY( `_id_` ),\n";
    ss << " KEY(`" << keyfield << "`(191)),\n";
    ss << " KEY(`_num_`)\n";
    ss << ")ENGINE=InnoDB default charset=utf8mb4;";

    return ss.str();
}


std::string SQLBasicAccess::GetCreateTableSql(const Entry::Ptr& entry)
{
    string table_name(entry->getField("table_name"));
    string key_field(entry->getField("key_field"));
    string value_field(entry->getField("value_field"));
    /*generate create table sql*/
    string sql = BuildCreateTableSql(table_name, key_field, value_field);
    SQLBasicAccess_LOG(DEBUG) << "create table:" << table_name << " keyfield:" << key_field
                              << " value field:" << value_field << " sql:" << sql;
    return sql;
}

void SQLBasicAccess::GetCommitFieldNameAndValueEachTable(const std::string& _num,
    const Entries::Ptr& data, const std::vector<size_t>& indexlist, std::string& fieldStr,
    std::vector<std::string>& valueList)
{
    uint32_t loopcount = 0;
    for (auto index : indexlist)
    {
        const Entry::Ptr& entry = data->get(index);
        for (auto fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }
            if (loopcount == 0)
            {
                fieldStr.append("`").append(fieldIt.first).append("`,");
            }
            valueList.push_back(fieldIt.second);
        }
        if (g_BCOSConfig.version() <= V2_1_0)
        {
            valueList.push_back("0");
        }
        valueList.push_back(_num);
        valueList.push_back(boost::lexical_cast<std::string>(entry->getID()));
        valueList.push_back(boost::lexical_cast<std::string>(entry->getStatus()));
        ++loopcount;
    }
    if (!fieldStr.empty())
    {
        if (g_BCOSConfig.version() <= V2_1_0)
        {
            fieldStr.append("`_hash_`,");
        }
        fieldStr.append("`").append(NUM_FIELD).append("`,");
        fieldStr.append("`").append(ID_FIELD).append("`,");
        fieldStr.append("`").append(STATUS).append("`");
    }
}

void SQLBasicAccess::GetCommitFieldNameAndValue(const Entries::Ptr& data, const std::string& _num,
    std::map<std::string, std::vector<std::string>>& _fieldValue)
{
    std::map<std::string, std::vector<size_t>> splitDataItem;
    std::map<std::string, uint32_t> field2Index;
    uint32_t fillIndex = 0;

    set<std::string> keySet;
    for (size_t i = 0; i < data->size(); ++i)
    {
        const Entry::Ptr& entry = data->get(i);
        for (const auto& fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }
            keySet.insert(fieldIt.first);
        }
    }

    for (size_t i = 0; i < data->size(); ++i)
    {
        std::string dataValue(keySet.size(), NOT_HAS_VAlue);
        const Entry::Ptr& entry = data->get(i);
        for (const auto& fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }
            if (field2Index.find(fieldIt.first) == field2Index.end())
            {
                field2Index[fieldIt.first] = fillIndex;
                dataValue[fillIndex] = HAS_VAlue;
                ++fillIndex;
            }
            else
            {
                uint32_t scoreExist = field2Index[fieldIt.first];
                dataValue[scoreExist] = HAS_VAlue;
            }
        }
        // SQLBasicAccess_LOG(DEBUG) << "fieldBuff value:" << dataValue;
        splitDataItem[dataValue].push_back(i);
    }
    for (const auto& it : splitDataItem)
    {
        std::string fieldStr;
        std::vector<std::string> valueList;
        GetCommitFieldNameAndValueEachTable(_num, data, it.second, fieldStr, valueList);
        _fieldValue[fieldStr].insert(_fieldValue[fieldStr].end(),
            make_move_iterator(valueList.begin()), make_move_iterator(valueList.end()));
    }
}


int SQLBasicAccess::Commit(int64_t num, const std::vector<TableData::Ptr>& datas)
{
    string errmsg;
    volatile uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    volatile int ret = 0;
    TRY
    {
        ret = CommitDo(num, datas, errmsg);
        while (ret < 0 && ++retryCnt < retryMax)
        {
            sleep(1);
            ret = CommitDo(num, datas, errmsg);
        }
        if (ret < 0)
        {
            SQLBasicAccess_LOG(ERROR) << "commit failed errmsg:" << errmsg;
            return -1;
        }
    }
    ELSE
    {
        SQLBasicAccess_LOG(ERROR) << "commit failed just return";
        return -1;
    }
    END_TRY;
    return ret;
}

int SQLBasicAccess::CommitDo(int64_t num, const std::vector<TableData::Ptr>& datas, string& errmsg)
{
    SQLBasicAccess_LOG(INFO) << " commit num:" << num;
    string strNum = to_string(num);
    if (datas.size() == 0)
    {
        SQLBasicAccess_LOG(DEBUG) << "Empty data just return";
        return 0;
    }

    /*execute commit operation*/

    Connection_T conn = m_connPool->GetConnection();
    TRY
    {
        for (auto tableDataPtr : datas)
        {
            if (tableDataPtr->info->name == SYS_TABLES)
            {
                // FIXME: dirtyEntries should always empty, cause table struct can't be modified
                for (size_t i = 0; i < tableDataPtr->dirtyEntries->size(); ++i)
                {
                    SQLBasicAccess_LOG(FATAL)
                        << SYS_TABLES << " dirtyEntries should always be empty";
                    Entry::Ptr entry = tableDataPtr->dirtyEntries->get(i);
                    string sql = GetCreateTableSql(entry);
                    Connection_execute(conn, "%s", sql.c_str());
                }

                for (size_t i = 0; i < tableDataPtr->newEntries->size(); ++i)
                {
                    Entry::Ptr entry = tableDataPtr->newEntries->get(i);
                    string sql = GetCreateTableSql(entry);
                    Connection_execute(conn, "%s", sql.c_str());
                }
            }
        }
    }
    CATCH(SQLException)
    {
        errmsg = Exception_frame.message;
        SQLBasicAccess_LOG(ERROR) << "create table exception:" << errmsg;
        m_connPool->ReturnConnection(conn);
        return -1;
    }
    END_TRY;

    volatile int32_t rowCount = 0;
    m_connPool->BeginTransaction(conn);
    TRY
    {
        for (auto it : datas)
        {
            auto tableInfo = it->info;
            std::string table_name = tableInfo->name;
            std::map<std::string, std::vector<std::string>> _fieldValueMap;
            this->GetCommitFieldNameAndValue(it->dirtyEntries, strNum, _fieldValueMap);
            this->GetCommitFieldNameAndValue(it->newEntries, strNum, _fieldValueMap);

            SQLBasicAccess_LOG(DEBUG) << "table:" << table_name << " split to "
                                      << _fieldValueMap.size() << " parts to commit";

            for (auto iter : _fieldValueMap)
            {
                const auto& _fieldName = iter.first;
                const auto& _fieldValue = iter.second;
                std::vector<SQLPlaceHoldItem> sqlList =
                    this->BuildCommitSql(table_name, _fieldName, _fieldValue);
                auto itValue = _fieldValue.begin();
                for (auto itSql : sqlList)
                {
                    PreparedStatement_T preSatement =
                        Connection_prepareStatement(conn, "%s", itSql.sql.c_str());
                    SQLBasicAccess_LOG(DEBUG) << "table:" << table_name << " sql:" << itSql.sql;
                    uint32_t index = 0;

                    for (; itValue != _fieldValue.end(); ++itValue)
                    {
                        PreparedStatement_setString(preSatement, ++index, itValue->c_str());
                        SQLBasicAccess_LOG(TRACE) << " index:" << index << " num:" << num
                                                  << " setString:" << itValue->c_str();
                        if (index == itSql.placeHolerCnt)
                        {
                            PreparedStatement_execute(preSatement);
                            rowCount += (int32_t)PreparedStatement_rowsChanged(preSatement);
                            ++itValue;
                            break;
                        }
                    }
                }
            }
        }
    }
    CATCH(SQLException)
    {
        errmsg = Exception_frame.message;
        SQLBasicAccess_LOG(ERROR) << "insert data exception:" << errmsg;
        SQLBasicAccess_LOG(DEBUG) << "active connections:" << m_connPool->GetActiveConnections()
                                  << " max connetions:" << m_connPool->GetMaxConnections()
                                  << " now connections:" << m_connPool->GetTotalConnections();
        m_connPool->RollBack(conn);
        m_connPool->ReturnConnection(conn);
        return -1;
    }
    END_TRY;

    SQLBasicAccess_LOG(INFO) << "commit now active connections:"
                             << m_connPool->GetActiveConnections()
                             << " max connections:" << m_connPool->GetMaxConnections();
    m_connPool->Commit(conn);
    m_connPool->ReturnConnection(conn);
    return rowCount;
}

std::vector<SQLPlaceHoldItem> SQLBasicAccess::BuildCommitSql(const std::string& _table,
    const std::string& _fieldStr, const std::vector<std::string>& _fieldValue)
{
    std::vector<std::string> fieldName;
    boost::split(fieldName, _fieldStr, boost::is_any_of(","));
    std::vector<SQLPlaceHoldItem> sqlList;
    if (fieldName.size() == 0 || _fieldValue.size() == 0 || (_fieldValue.size() % fieldName.size()))
    {
        /*throw exception*/
        SQLBasicAccess_LOG(ERROR) << "tablename:" << _table << "field size:" << fieldName.size()
                                  << " value size:" << _fieldValue.size()
                                  << " field size and value should be greate than 0";
        THROW(SQLException, "PreparedStatement_executeQuery");
    }

    boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
    boost::algorithm::replace_all_copy(_table, "`", "\\`");
    std::string sqlHeader = "replace into `";
    sqlHeader.append(_table).append("`(");
    sqlHeader.append(_fieldStr);
    sqlHeader.append(") values");

    SQLBasicAccess_LOG(INFO) << "table name:" << _table << "field size:" << fieldName.size()
                             << " value size:" << _fieldValue.size();

    string sql = sqlHeader;
    uint32_t placeHolderCnt = 0;
    uint32_t valueSize = _fieldValue.size();
    uint32_t columnSize = fieldName.size();
    for (uint32_t index = 0; index < valueSize; ++index)
    {
        ++placeHolderCnt;
        if (index % columnSize == 0)
        {
            sql.append("(?,");
        }
        else
        {
            sql.append("?,");
        }
        if (index % columnSize == (columnSize - 1))
        {
            sql = sql.substr(0, sql.size() - 1);
            sql.append("),");
            /*
                if placeholders count is great than 65535 sql will execute failed
                so we need to execute  in multiple sqls
            */
            if (placeHolderCnt >= maxPlaceHolderCnt)
            {
                sql = sql.substr(0, sql.size() - 1);
                SQLPlaceHoldItem item;
                item.sql = sql;
                item.placeHolerCnt = placeHolderCnt;
                sqlList.emplace_back(item);
                sql = sqlHeader;
                placeHolderCnt = 0;
            }
        }
    }
    if (placeHolderCnt > 0)
    {
        sql = sql.substr(0, sql.size() - 1);
        SQLPlaceHoldItem item;
        item.sql = sql;
        item.placeHolerCnt = placeHolderCnt;
        sqlList.emplace_back(item);
    }
    return sqlList;
}

void SQLBasicAccess::setConnPool(SQLConnectionPool::Ptr& _connPool)
{
    this->m_connPool = _connPool;
}

void SQLBasicAccess::ExecuteSql(const std::string& _sql)
{
    Connection_T conn = m_connPool->GetConnection();
    if (conn == NULL)
    {
        SQLBasicAccess_LOG(ERROR) << "get connection failed sql:" << _sql;
        THROW(SQLException, "PreparedStatement_executeQuery");
    }

    TRY { Connection_execute(conn, "%s", _sql.c_str()); }
    CATCH(SQLException)
    {
        SQLBasicAccess_LOG(ERROR) << "execute sql failed sql:" << _sql;
        m_connPool->ReturnConnection(conn);
        throw StorageException(-1, "execute sql failed sql:" + _sql);
    }
    END_TRY;
    SQLBasicAccess_LOG(INFO) << "execute sql success sql:" << _sql
                             << " now active connection:" << m_connPool->GetActiveConnections()
                             << " max connection :" << m_connPool->GetMaxConnections();

    m_connPool->ReturnConnection(conn);
}
