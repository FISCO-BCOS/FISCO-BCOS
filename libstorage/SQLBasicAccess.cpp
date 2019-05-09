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
    std::string strSql = this->BuildQuerySql(_table, condition);
    LOG(DEBUG) << "hash:" << hash.hex() << " num:" << num << " table:" << _table << " key:" << key
               << " query sql:" << strSql;
    Connection_T oConn = m_oConnPool.GetConnection();
    if (oConn == NULL)
    {
        LOG(ERROR) << "hash:" << hash.hex() << " num:" << num << " table:" << _table
                   << " key:" << key << " query sql:" << strSql << " can not get connection";
        return -1;
    }
    TRY
    {
        PreparedStatement_T oPreSatement = Connection_prepareStatement(oConn, "%s", strSql.c_str());
        if (condition)
        {
            uint32_t dwIndex = 0;
            for (auto it : *(condition->getConditions()))
            {
                PreparedStatement_setString(
                    oPreSatement, ++dwIndex, it.second.right.second.c_str());
                LOG(DEBUG) << "hash:" << hash.hex() << " num:" << num << " table:" << _table
                           << " key:" << key << " index:" << dwIndex
                           << " value:" << it.second.right.second;
            }
        }
        ResultSet_T result = PreparedStatement_executeQuery(oPreSatement);
        string strColumnName;
        int32_t iColumnCnt = ResultSet_getColumnCount(result);
        for (int32_t iIndex = 1; iIndex <= iColumnCnt; ++iIndex)
        {
            strColumnName = ResultSet_getColumnName(result, iIndex);
            respJson["result"]["columns"].append(strColumnName);
        }

        while (ResultSet_next(result))
        {
            Json::Value oValueJson;
            for (int32_t iIndex = 1; iIndex <= iColumnCnt; ++iIndex)
            {
                oValueJson.append(ResultSet_getString(result, iIndex));
            }
            respJson["result"]["data"].append(oValueJson);
        }
    }
    CATCH(SQLException)
    {
        respJson["result"]["columns"].resize(0);
        LOG(ERROR) << "select exception:";
        m_oConnPool.ReturnConnection(oConn);
        return 0;
    }
    END_TRY;
    m_oConnPool.ReturnConnection(oConn);
    return 0;
}

std::string SQLBasicAccess::BuildQuerySql(const std::string& _table, Condition::Ptr condition)
{
    std::string strSql = "select * from ";
    strSql.append(_table);
    if (condition)
    {
        uint32_t dwIndex = 0;
        auto conditionmap = *(condition->getConditions());
        auto it = conditionmap.begin();
        for (; it != conditionmap.end(); ++it)
        {
            if (dwIndex == 0)
            {
                ++dwIndex;
                strSql.append(GenerateConditionSql(" where", it, condition));
            }
            else
            {
                strSql.append(GenerateConditionSql(" and", it, condition));
            }
        }
    }

    return strSql;
}

std::string SQLBasicAccess::GenerateConditionSql(const std::string& strPrefix,
    std::map<std::string, Condition::Range>::iterator& it, Condition::Ptr condition)
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

    LOG(DEBUG) << "value:" << it->second.right.second;
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
    ss << "`" << keyfield << "` varchar(128) default '',\n";

    LOG(DEBUG) << "valuefield:" << valuefield;
    std::vector<std::string> vecSplit;
    boost::split(vecSplit, valuefield, boost::is_any_of(","));
    auto it = vecSplit.begin();
    for (; it != vecSplit.end(); ++it)
    {
        boost::algorithm::replace_all_copy(*it, "\\", "\\\\");
        boost::algorithm::replace_all_copy(*it, "`", "\\`");
        ss << "`" << *it << "` text default '',\n";
    }
    LOG(DEBUG) << "valuefield:" << valuefield;

    ss << " PRIMARY KEY( `_id_` ),\n";
    ss << " KEY(`" << keyfield << "`),\n";
    ss << " KEY(`_num_`)\n";
    ss << ")ENGINE=InnoDB default charset=utf8mb4;";

    return ss.str();
}


std::string SQLBasicAccess::GetCreateTableSql(const Entry::Ptr& entry)
{
    auto fields = *entry->fields();
    string table_name(fields["table_name"]);
    string key_field(fields["key_field"]);
    string value_field(fields["value_field"]);
    /*generate create table sql*/
    string _sql = BuildCreateTableSql(table_name, key_field, value_field);
    LOG(DEBUG) << "create table:" << table_name << " keyfield:" << key_field
               << " value field:" << value_field << " sql:" << _sql;
    return _sql;
}


void SQLBasicAccess::GetCommitFieldNameAndValue(const Entries::Ptr& data, h256 hash,
    const std::string& _num, std::vector<std::string>& _fieldName,
    std::vector<std::string>& _fieldValue, bool& _hasGetField)
{
    for (size_t i = 0; i < data->size(); ++i)
    {
        Entry::Ptr entry = data->get(i);
        /*different fields*/
        for (auto fieldIt : *entry->fields())
        {
            if (fieldIt.first == "_num_" || fieldIt.first == "_hash_")
            {
                continue;
            }
            if (i == 0 && !_hasGetField)
            {
                _fieldName.push_back(fieldIt.first);
            }
            _fieldValue.push_back(fieldIt.second);
            LOG(DEBUG) << "new entry key:" << fieldIt.first << " value:" << fieldIt.second;
        }
        _fieldValue.push_back(hash.hex());
        _fieldValue.push_back(_num);
    }

    if (_fieldName.size() > 0 && !_hasGetField)
    {
        _fieldName.push_back("_hash_");
        _fieldName.push_back("_num_");
        _hasGetField = true;
    }
}

int SQLBasicAccess::Commit(h256 hash, int num, const std::vector<TableData::Ptr>& datas)
{
    LOG(DEBUG) << " commit hash:" << hash.hex() << " num:" << num;
    string strNum = to_string(num);
    if (datas.size() == 0)
    {
        LOG(DEBUG) << "Empty data just return";
        return 0;
    }

    /*execute commit operation*/

    Connection_T oConn = m_oConnPool.GetConnection();
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
                    string strSql = GetCreateTableSql(entry);
                    Connection_execute(oConn, "%s", strSql.c_str());
                }

                for (size_t i = 0; i < it->newEntries->size(); ++i)
                {
                    Entry::Ptr entry = it->newEntries->get(i);
                    string strSql = GetCreateTableSql(entry);
                    Connection_execute(oConn, "%s", strSql.c_str());
                }
            }
        }
    }
    CATCH(SQLException)
    {
        LOG(ERROR) << "create table exception:";
        m_oConnPool.ReturnConnection(oConn);
        return 0;
    }
    END_TRY;

    volatile int32_t dwRowCount = 0;
    m_oConnPool.BeginTransaction(oConn);
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
            string strSql = this->BuildCommitSql(strTableName, _fieldName, _fieldValue);
            LOG(DEBUG) << " commit hash:" << hash.hex() << " num:" << num
                       << " commit sql:" << strSql;
            PreparedStatement_T oPreSatement =
                Connection_prepareStatement(oConn, "%s", strSql.c_str());
            int32_t dwIndex = 0;
            auto itValue = _fieldValue.begin();
            for (; itValue != _fieldValue.end(); ++itValue)
            {
                PreparedStatement_setString(oPreSatement, ++dwIndex, itValue->c_str());
                LOG(DEBUG) << " index:" << dwIndex << " num:" << num
                           << " setString:" << itValue->c_str();
            }
            PreparedStatement_execute(oPreSatement);

            dwRowCount += (int32_t)PreparedStatement_rowsChanged(oPreSatement);
        }
    }
    CATCH(SQLException)
    {
        LOG(ERROR) << "insert data exception:";
        m_oConnPool.RollBack(oConn);
        m_oConnPool.ReturnConnection(oConn);
        return 0;
    }
    END_TRY;
    m_oConnPool.Commit(oConn);
    m_oConnPool.ReturnConnection(oConn);
    return dwRowCount;
}


std::string SQLBasicAccess::BuildCommitSql(const std::string& _table,
    const std::vector<std::string>& _fieldName, const std::vector<std::string>& _fieldValue)
{
    if (_fieldName.size() == 0 || _fieldValue.size() == 0 ||
        (_fieldValue.size() % _fieldName.size()))
    {
        /*throw execption*/
        LOG(ERROR) << "field size:" << _fieldName.size() << " value size:" << _fieldValue.size()
                   << " field size and value should be greate than 0";
        THROW(SQLException, "PreparedStatement_executeQuery");
    }
    uint32_t dwColumnSize = _fieldName.size();
    std::string strSql = "replace into ";
    strSql.append(_table).append("(");
    auto it = _fieldName.begin();
    for (; it != _fieldName.end(); ++it)
    {
        strSql.append("`").append(*it).append("`").append(",");
    }
    strSql = strSql.substr(0, strSql.size() - 1);
    strSql.append(") values");

    LOG(DEBUG) << "field size:" << _fieldName.size() << " value size:" << _fieldValue.size();

    uint32_t dwSize = _fieldValue.size();
    for (uint32_t dwIndex = 0; dwIndex < dwSize; ++dwIndex)
    {
        if (dwIndex % dwColumnSize == 0)
        {
            strSql.append("(?,");
        }
        else
        {
            strSql.append("?,");
        }
        if (dwIndex % dwColumnSize == (dwColumnSize - 1))
        {
            strSql = strSql.substr(0, strSql.size() - 1);
            strSql.append("),");
        }
    }
    strSql = strSql.substr(0, strSql.size() - 1);
    return strSql;
}


bool SQLBasicAccess::initConnPool(const storage::ZDBConfig& _dbConfig)
{
    return m_oConnPool.InitConnectionPool(_dbConfig);
}

void SQLBasicAccess::ExecuteSql(const std::string& _sql)
{
    Connection_T conn = m_oConnPool.GetConnection();
    if (conn == NULL)
    {
        LOG(DEBUG) << "get connection failed sql:" << _sql;
        THROW(SQLException, "PreparedStatement_executeQuery");
    }

    TRY { Connection_execute(conn, "%s", _sql.c_str()); }
    CATCH(SQLException)
    {
        LOG(ERROR) << "execute sql failed sql:" << _sql;
        m_oConnPool.ReturnConnection(conn);
        throw StorageException(-1, "execute sql failed sql:" + _sql);
    }
    END_TRY;
    LOG(DEBUG) << "execute sql success sql:" << _sql;
    m_oConnPool.ReturnConnection(conn);
}
