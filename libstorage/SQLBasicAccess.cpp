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
#include "SQLConnectionPool.h"
#include "StorageException.h"
#include "libconfig/GlobalConfigure.h"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/dynamic_bitset.hpp>

using namespace dev::storage;
using namespace std;

SQLBasicAccess::SQLBasicAccess()
{
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        // supported_version >= v2.6.0, enable compress
        m_rowFormat = " ROW_FORMAT=COMPRESSED ";
    }
}

int SQLBasicAccess::Select(int64_t, const string& _table, const string&, Condition::Ptr _condition,
    vector<map<string, string>>& _values)
{
    string sql = this->BuildQuerySql(_table, _condition);
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
        SQLBasicAccess_LOG(ERROR) << "select exception:" << Exception_frame.message;
        m_connPool->ReturnConnection(conn);
        return -1;
    }
    END_TRY;
    m_connPool->ReturnConnection(conn);
    return 0;
}

string SQLBasicAccess::BuildQuerySql(string _table, Condition::Ptr _condition)
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
string SQLBasicAccess::BuildConditionSql(const string& _strPrefix,
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

SQLFieldType SQLBasicAccess::getFieldType(std::string const& _tableName)
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

string SQLBasicAccess::BuildCreateTableSql(
    const string& _tableName, const string& _keyField, const string& _valueField)
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS `" << _tableName << "`(\n";
    ss << " `_id_` BIGINT unsigned auto_increment,\n";
    if (g_BCOSConfig.version() <= V2_1_0)
    {
        ss << " `_hash_` varchar(128) DEFAULT NULL,\n";
    }
    ss << " `_num_` BIGINT not null,\n";
    ss << " `_status_` int not null,\n";
    ss << "`" << _keyField << "` varchar(255) default '',\n";

    SQLBasicAccess_LOG(DEBUG) << "valuefield:" << _valueField;
    vector<string> vecSplit;
    boost::split(vecSplit, _valueField, boost::is_any_of(","));
    auto it = vecSplit.begin();

    auto fieldType = getFieldType(_tableName);
    std::string fieldTypeName = SQLFieldTypeName[fieldType];
    for (; it != vecSplit.end(); ++it)
    {
        *it = boost::algorithm::replace_all_copy(*it, "\\", "\\\\");
        *it = boost::algorithm::replace_all_copy(*it, "`", "\\`");
        ss << "`" << *it << "` " << fieldTypeName << ",\n";
    }
    ss << " PRIMARY KEY( `_id_` ),\n";
    ss << " KEY(`" << _keyField << "`(191)),\n";
    ss << " KEY(`_num_`)\n";
    // supported_version >= v2.6.0, enable compress
    ss << ") " << m_rowFormat << " ENGINE=InnoDB default charset=utf8mb4;";
    return ss.str();
}

string SQLBasicAccess::BuildCreateTableSql(const Entry::Ptr& _entry)
{
    string tableName(_entry->getField("table_name"));
    string keyField(_entry->getField("key_field"));
    string valueField(_entry->getField("value_field"));

    tableName = boost::algorithm::replace_all_copy(tableName, "\\", "\\\\");
    tableName = boost::algorithm::replace_all_copy(tableName, "`", "\\`");

    keyField = boost::algorithm::replace_all_copy(keyField, "\\", "\\\\");
    keyField = boost::algorithm::replace_all_copy(keyField, "`", "\\`");

    string sql = BuildCreateTableSql(tableName, keyField, valueField);
    SQLBasicAccess_LOG(DEBUG) << "create table:" << tableName << " keyfield:" << keyField
                              << " value field:" << valueField << " sql:" << sql;
    return sql;
}

void SQLBasicAccess::GetCommitFieldNameAndValueEachTable(const string& _num,
    const Entries::Ptr& _data, const vector<size_t>& _indexList, string& _fieldStr,
    vector<string>& _valueList)
{
    bool isFirst = true;
    for (auto index : _indexList)
    {
        const auto& entry = _data->get(index);
        for (auto fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }
            if (isFirst)
            {
                _fieldStr.append("`").append(fieldIt.first).append("`,");
            }
            _valueList.push_back(fieldIt.second);
        }

        if (g_BCOSConfig.version() <= V2_1_0)
        {
            _valueList.push_back("0");
        }

        _valueList.push_back(_num);
        _valueList.push_back(boost::lexical_cast<string>(entry->getID()));
        _valueList.push_back(boost::lexical_cast<string>(entry->getStatus()));
        isFirst = false;
    }

    if (!_fieldStr.empty())
    {
        if (g_BCOSConfig.version() <= V2_1_0)
        {
            _fieldStr.append("`_hash_`,");
        }
        _fieldStr.append("`").append(NUM_FIELD).append("`,");
        _fieldStr.append("`").append(ID_FIELD).append("`,");
        _fieldStr.append("`").append(STATUS).append("`");
    }
}

void SQLBasicAccess::GetCommitFieldNameAndValue(
    const Entries::Ptr& _data, const string& _num, map<string, vector<string>>& _field2Values)
{
    set<string> keySet;
    for (size_t i = 0; i < _data->size(); ++i)
    {
        const auto& entry = _data->get(i);
        for (auto fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }
            keySet.insert(fieldIt.first);
        }
    }

    map<string, vector<size_t>> groupedEntries;
    map<string, uint32_t> field2Position;
    uint32_t currentFieldPostion = 0;
    for (size_t i = 0; i < _data->size(); ++i)
    {
        boost::dynamic_bitset<> keyVec(keySet.size(), 0);
        const auto& entry = _data->get(i);
        for (const auto& fieldIt : *entry)
        {
            if (fieldIt.first == NUM_FIELD || fieldIt.first == "_hash_" ||
                fieldIt.first == ID_FIELD || fieldIt.first == STATUS)
            {
                continue;
            }

            if (field2Position.find(fieldIt.first) == field2Position.end())
            {
                field2Position[fieldIt.first] = currentFieldPostion;
                keyVec[currentFieldPostion] = 1;
                ++currentFieldPostion;
            }
            else
            {
                keyVec[field2Position[fieldIt.first]] = 1;
            }
        }
        string key;
        boost::to_string(keyVec, key);
        groupedEntries[key].push_back(i);
    }

    for (const auto& it : groupedEntries)
    {
        string fieldStr;
        vector<string> valueList;
        GetCommitFieldNameAndValueEachTable(_num, _data, it.second, fieldStr, valueList);
        _field2Values[fieldStr].insert(_field2Values[fieldStr].end(),
            make_move_iterator(valueList.begin()), make_move_iterator(valueList.end()));
    }
}

int SQLBasicAccess::Commit(int64_t _num, const vector<TableData::Ptr>& _datas)
{
    string errorMsg;
    volatile uint32_t retryCnt = 0;
    uint32_t retryMax = 10;
    volatile int ret = 0;
    TRY
    {
        ret = CommitDo(_num, _datas, errorMsg);
        while (ret < 0 && ++retryCnt < retryMax)
        {
            sleep(1);
            ret = CommitDo(_num, _datas, errorMsg);
        }
        if (ret < 0)
        {
            SQLBasicAccess_LOG(ERROR) << "commit failed error message:" << errorMsg;
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

int SQLBasicAccess::CommitDo(int64_t _num, const vector<TableData::Ptr>& _datas, string& _errorMsg)
{
    SQLBasicAccess_LOG(INFO) << " commit num:" << _num;
    string strNum = to_string(_num);
    if (_datas.size() == 0)
    {
        SQLBasicAccess_LOG(DEBUG) << "empty data just return";
        return 0;
    }

    /*execute commit operation*/

    Connection_T conn = m_connPool->GetConnection();
    TRY
    {
        for (auto tableDataPtr : _datas)
        {
            if (tableDataPtr->info->name == SYS_TABLES)
            {
                for (size_t i = 0; i < tableDataPtr->newEntries->size(); ++i)
                {
                    Entry::Ptr entry = tableDataPtr->newEntries->get(i);
                    string sql = BuildCreateTableSql(entry);
                    Connection_execute(conn, "%s", sql.c_str());
                }
            }
        }
    }
    CATCH(SQLException)
    {
        _errorMsg = Exception_frame.message;
        SQLBasicAccess_LOG(ERROR) << "create table exception:" << _errorMsg;
        m_connPool->ReturnConnection(conn);
        return -1;
    }
    END_TRY;

    volatile int32_t rowCount = 0;
    m_connPool->BeginTransaction(conn);
    TRY
    {
        for (auto data : _datas)
        {
            auto tableInfo = data->info;
            string tableName = tableInfo->name;
            map<string, vector<string>> field2Values;
            this->GetCommitFieldNameAndValue(data->dirtyEntries, strNum, field2Values);
            this->GetCommitFieldNameAndValue(data->newEntries, strNum, field2Values);

            SQLBasicAccess_LOG(DEBUG) << "table:" << tableName << " split to "
                                      << field2Values.size() << " parts to commit";

            tableName = boost::algorithm::replace_all_copy(tableName, "\\", "\\\\");
            tableName = boost::algorithm::replace_all_copy(tableName, "`", "\\`");

            auto tableWithBlobField = isBlobType(tableName);
            for (auto item : field2Values)
            {
                const auto& name = item.first;
                const auto& values = item.second;
                vector<SQLPlaceholderItem> sqlPlaceholders =
                    this->BuildCommitSql(tableName, name, values);
                auto itValue = values.begin();
                for (auto& placeholder : sqlPlaceholders)
                {
                    PreparedStatement_T preStatement =
                        Connection_prepareStatement(conn, "%s", placeholder.sql.c_str());
                    SQLBasicAccess_LOG(TRACE)
                        << "table:" << tableName << " sql:" << placeholder.sql;

                    uint32_t index = 0;
                    for (; itValue != values.end(); ++itValue)
                    {
                        if (tableWithBlobField)
                        {
                            PreparedStatement_setBlob(
                                preStatement, ++index, itValue->c_str(), itValue->size());
                        }
                        else
                        {
                            PreparedStatement_setString(preStatement, ++index, itValue->c_str());
                            SQLBasicAccess_LOG(TRACE) << " index:" << index << " num:" << _num
                                                      << " setString:" << itValue->c_str();
                        }

                        if (index == placeholder.placeholderCnt)
                        {
                            PreparedStatement_execute(preStatement);
                            rowCount += (int32_t)PreparedStatement_rowsChanged(preStatement);
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
        _errorMsg = Exception_frame.message;
        SQLBasicAccess_LOG(ERROR) << "insert data exception:" << _errorMsg;
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

vector<SQLPlaceholderItem> SQLBasicAccess::BuildCommitSql(
    const string& _table, const string& _fieldStr, const vector<string>& _fieldValues)
{
    vector<string> fieldNames;
    boost::split(fieldNames, _fieldStr, boost::is_any_of(","));
    vector<SQLPlaceholderItem> sqlPlaceholders;
    if (fieldNames.size() == 0 || _fieldValues.size() == 0 ||
        (_fieldValues.size() % fieldNames.size()))
    {
        /*throw exception*/
        SQLBasicAccess_LOG(ERROR) << "table name:" << _table << "field size:" << fieldNames.size()
                                  << " value size:" << _fieldValues.size()
                                  << " field size and value should be greate than 0";
        THROW(SQLException, "PreparedStatement_executeQuery");
    }

    string sqlHeader = "replace into `";
    sqlHeader.append(_table).append("`(");
    sqlHeader.append(_fieldStr);
    sqlHeader.append(") values");

    SQLBasicAccess_LOG(INFO) << "table name:" << _table << "field size:" << fieldNames.size()
                             << " value size:" << _fieldValues.size();

    string sql = sqlHeader;
    uint32_t placeholderCnt = 0;
    uint32_t valueSize = _fieldValues.size();
    uint32_t columnSize = fieldNames.size();
    for (uint32_t index = 0; index < valueSize; ++index)
    {
        ++placeholderCnt;
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
                so we need to execute in multiple sqls
            */
            if (placeholderCnt >= maxPlaceHolderCnt)
            {
                sql = sql.substr(0, sql.size() - 1);
                SQLPlaceholderItem item;
                item.sql = sql;
                item.placeholderCnt = placeholderCnt;
                sqlPlaceholders.emplace_back(item);
                sql = sqlHeader;
                placeholderCnt = 0;
            }
        }
    }
    if (placeholderCnt > 0)
    {
        sql = sql.substr(0, sql.size() - 1);
        SQLPlaceholderItem item;
        item.sql = sql;
        item.placeholderCnt = placeholderCnt;
        sqlPlaceholders.emplace_back(item);
    }
    return sqlPlaceholders;
}

void SQLBasicAccess::setConnPool(SQLConnectionPool::Ptr& _connPool)
{
    this->m_connPool = _connPool;
}

void SQLBasicAccess::ExecuteSql(const string& _sql)
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
