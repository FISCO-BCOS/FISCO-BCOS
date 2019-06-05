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
#include <libdevcore/easylog.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>

using namespace dev::storage;
using namespace std;

int SQLBasicAccess::Select(h256 hash, int num, const std::string& _table, const std::string& key,
    Condition::Ptr condition, Json::Value& respJson)
{
    std::string sql = this->BuildQuerySql(_table, condition);
    SQLBasicAccess_LOG(DEBUG) << "hash:" << hash.hex() << " num:" << num << " table:" << _table
                              << " key:" << key << " query sql:" << sql;
    Connection_T conn = m_connPool->GetConnection();
    uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    while (conn == NULL && retryCnt++ < retryMax)
    {
        SQLBasicAccess_LOG(DEBUG) << "table:" << _table << "sql:" << sql
                                  << " get connection failed";
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
                SQLBasicAccess_LOG(DEBUG)
                    << "hash:" << hash.hex() << " num:" << num << " table:" << _table
                    << " key:" << key << " index:" << index << " value:" << it.second.right.second;
            }
        }
        ResultSet_T result = PreparedStatement_executeQuery(_prepareStatement);
        int32_t columnCnt = ResultSet_getColumnCount(result);
        for (int32_t index = 1; index <= columnCnt; ++index)
        {
            respJson["result"]["columns"].append(ResultSet_getColumnName(result, index));
        }
        while (ResultSet_next(result))
        {
            Json::Value valueJson;
            for (int32_t index = 1; index <= columnCnt; ++index)
            {
                valueJson.append(ResultSet_getString(result, index));
            }
            respJson["result"]["data"].append(valueJson);
        }
    }
    CATCH(SQLException)
    {
        respJson["result"]["columns"].resize(0);
        SQLBasicAccess_LOG(ERROR) << "select exception:" << Exception_frame.message;
        m_connPool->ReturnConnection(conn);
        return 0;
    }
    END_TRY;
    SQLBasicAccess_LOG(DEBUG) << "select now active connections:"
                              << m_connPool->GetActiveConnections()
                              << " max connections:" << m_connPool->GetMaxConnections();
    m_connPool->ReturnConnection(conn);
    return 0;
}

std::string SQLBasicAccess::BuildQuerySql(const std::string& _table, Condition::Ptr condition)
{
    boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
    boost::algorithm::replace_all_copy(_table, "`", "\\`");
    std::string sql = "select * from ";
    sql.append(_table);
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
    ss << " `_id_` int unsigned auto_increment,\n";
    ss << " `_hash_` varchar(128) not null,\n";
    ss << " `_num_` int not null,\n";
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
        ss << "`" << *it << "` text,\n";
    }
    ss << " PRIMARY KEY( `_id_` ),\n";
    ss << " KEY(`" << keyfield << "`),\n";
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


void SQLBasicAccess::GetCommitFieldNameAndValue(const Entries::Ptr& data, h256 hash,
    const std::string& _num, std::vector<std::string>& _fieldName,
    std::vector<std::string>& _fieldValue, bool& _hasGetField)
{
    for (size_t i = 0; i < data->size(); ++i)
    {
        Entry::Ptr entry = data->get(i);
        /*different fields*/
        for (auto fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == "_id_" || fieldIt.first == "_status_")
            {
                continue;
            }
            if (i == 0 && !_hasGetField)
            {
                _fieldName.push_back(fieldIt.first);
            }
            _fieldValue.push_back(fieldIt.second);
            SQLBasicAccess_LOG(DEBUG)
                << "new entry key:" << fieldIt.first << " value:" << fieldIt.second;
        }
        _fieldValue.push_back(hash.hex());
        _fieldValue.push_back(_num);
        _fieldValue.push_back(boost::lexical_cast<std::string>(entry->getID()));
        _fieldValue.push_back(boost::lexical_cast<std::string>(entry->getStatus()));
    }

    if (_fieldName.size() > 0 && !_hasGetField)
    {
        _fieldName.push_back("_hash_");
        _fieldName.push_back(NUM_FIELD);
        _fieldName.push_back(ID_FIELD);
        _fieldName.push_back(STATUS);
        _hasGetField = true;
    }
}


int SQLBasicAccess::Commit(h256 hash, int num, const std::vector<TableData::Ptr>& datas)
{
    string errmsg;
    uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    int ret = CommitDo(hash, num, datas, errmsg);
    while (ret < 0 && ++retryCnt < retryMax)
    {
        sleep(1);
        ret = CommitDo(hash, num, datas, errmsg);
    }
    if (ret < 0)
    {
        SQLBasicAccess_LOG(ERROR) << "commit failed errmsg:" << errmsg;
        return -1;
    }
    return ret;
}

int SQLBasicAccess::CommitDo(
    h256 hash, int num, const std::vector<TableData::Ptr>& datas, string& errmsg)
{
    SQLBasicAccess_LOG(INFO) << " commit hash:" << hash.hex() << " num:" << num;
    string strNum = to_string(num);
    if (datas.size() == 0)
    {
        SQLBasicAccess_LOG(DEBUG) << "Empty data just return";
        return 0;
    }

    /*execute commit operation*/

    Connection_T oConn = m_connPool->GetConnection();
    TRY
    {
        for (auto it : datas)
        {
            auto tableInfo = it->info;
            std::string strTableName = tableInfo->name;

            if (strTableName == "_sys_tables_")
            {
                for (size_t i = 0; i < it->dirtyEntries->size(); ++i)
                {
                    Entry::Ptr entry = it->dirtyEntries->get(i);
                    string sql = GetCreateTableSql(entry);
                    Connection_execute(oConn, "%s", sql.c_str());
                }

                for (size_t i = 0; i < it->newEntries->size(); ++i)
                {
                    Entry::Ptr entry = it->newEntries->get(i);
                    string sql = GetCreateTableSql(entry);
                    Connection_execute(oConn, "%s", sql.c_str());
                }
            }
        }
    }
    CATCH(SQLException)
    {
        errmsg = Exception_frame.message;
        SQLBasicAccess_LOG(ERROR) << "create table exception:" << errmsg;
        m_connPool->ReturnConnection(oConn);
        return -1;
    }
    END_TRY;

    volatile int32_t rowCount = 0;
    m_connPool->BeginTransaction(oConn);
    TRY
    {
        for (auto it : datas)
        {
            auto tableInfo = it->info;
            std::string strTableName = tableInfo->name;
            std::vector<std::string> _fieldName;
            std::vector<std::string> _fieldValue;
            bool _hasGetField = false;

            this->GetCommitFieldNameAndValue(
                it->dirtyEntries, hash, strNum, _fieldName, _fieldValue, _hasGetField);
            this->GetCommitFieldNameAndValue(
                it->newEntries, hash, strNum, _fieldName, _fieldValue, _hasGetField);
            /*build commit sql*/
            std::vector<SQLPlaceHoldItem> sqlList =
                this->BuildCommitSql(strTableName, _fieldName, _fieldValue);
            auto itSql = sqlList.begin();
            auto itValue = _fieldValue.begin();
            for (; itSql != sqlList.end(); ++itSql)
            {
                SQLBasicAccess_LOG(DEBUG) << " commit hash:" << hash.hex() << " num:" << num
                                          << " commit sql:" << itSql->sql;

                PreparedStatement_T preSatement =
                    Connection_prepareStatement(oConn, "%s", itSql->sql.c_str());

                uint32_t index = 0;

                /*
                    if not set string firstly
                    need to move itValue to next
                */
                if (itValue != _fieldValue.begin() && itValue != _fieldValue.end())
                {
                    ++itValue;
                }

                for (; itValue != _fieldValue.end(); ++itValue)
                {
                    PreparedStatement_setString(preSatement, ++index, itValue->c_str());
                    SQLBasicAccess_LOG(TRACE) << " index:" << index << " num:" << num
                                              << " setString:" << itValue->c_str();
                    if (index == itSql->placeHolerCnt)
                    {
                        PreparedStatement_execute(preSatement);
                        rowCount += (int32_t)PreparedStatement_rowsChanged(preSatement);
                        break;
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
        m_connPool->RollBack(oConn);
        m_connPool->ReturnConnection(oConn);
        return -1;
    }
    END_TRY;

    SQLBasicAccess_LOG(INFO) << "commit now active connections:"
                             << m_connPool->GetActiveConnections()
                             << " max connections:" << m_connPool->GetMaxConnections();
    m_connPool->Commit(oConn);
    m_connPool->ReturnConnection(oConn);
    return rowCount;
}


std::vector<SQLPlaceHoldItem> SQLBasicAccess::BuildCommitSql(const std::string& _table,
    const std::vector<std::string>& _fieldName, const std::vector<std::string>& _fieldValue)
{
    std::vector<SQLPlaceHoldItem> sqlList;
    if (_fieldName.size() == 0 || _fieldValue.size() == 0 ||
        (_fieldValue.size() % _fieldName.size()))
    {
        /*throw execption*/
        SQLBasicAccess_LOG(ERROR) << "field size:" << _fieldName.size()
                                  << " value size:" << _fieldValue.size()
                                  << " field size and value should be greate than 0";
        THROW(SQLException, "PreparedStatement_executeQuery");
    }
    uint32_t columnSize = _fieldName.size();
    std::string sqlHeader = "replace into ";
    sqlHeader.append(_table).append("(");
    auto it = _fieldName.begin();
    for (; it != _fieldName.end(); ++it)
    {
        sqlHeader.append("`").append(*it).append("`").append(",");
    }
    sqlHeader = sqlHeader.substr(0, sqlHeader.size() - 1);
    sqlHeader.append(") values");

    SQLBasicAccess_LOG(INFO) << "table name:" << _table << "field size:" << _fieldName.size()
                             << " value size:" << _fieldValue.size();

    string sql = sqlHeader;
    uint32_t placeHolderCnt = 0;
    uint32_t valueSize = _fieldValue.size();
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
                sqlList.push_back(item);
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
        sqlList.push_back(item);
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
