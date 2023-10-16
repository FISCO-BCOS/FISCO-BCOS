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
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
using namespace dev::storage;
using namespace std;
//与sql数据的基本连接
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
    uint32_t retryMax = 30;
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
        //根据表格的名称，获取对应列的名称

        PreparedStatement_T _prepareStatement =
            Connection_prepareStatement(conn, "%s", sql.c_str());
        SQLBasicAccess_LOG(INFO)<<"使用了select方法:"<<"所执行的查询语句为:"<<sql.c_str();
        if (_condition)
        {
            uint32_t index = 0;
            for (auto& it : *(_condition))
            {
                // string str="'";
                // str.append(it.second.right.second.c_str()).append("'");
                PreparedStatement_setString(
                    _prepareStatement, ++index, it.second.right.second.c_str());
                SQLBasicAccess_LOG(INFO)<<"查询的参数:"<<it.second.right.second.c_str();                

            }
            
        }
        ResultSet_T result = PreparedStatement_executeQuery(_prepareStatement);
        int32_t columnCnt = ResultSet_getColumnCount(result);
        SQLBasicAccess_LOG(INFO)<<"查询结果的数量:"<<columnCnt;
        // bool tableWithBlobField = isBlobType(_table);
        string tableName;
        tableName = boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
        tableName = boost::algorithm::replace_all_copy(_table, "`", "\\`");
        
        while (ResultSet_next(result))
        {
            map<string, string> value;
            for (int32_t index = 1; index <= columnCnt; ++index)
            {
                auto fieldName = ResultSet_getColumnName(result, index);
                SQLBasicAccess_LOG(INFO)<<"查询的fieldname:"<<string((char*)fieldName);
                //根据列名称获取对应的类型名称
                PreparedStatement_T _prepareStatement =
                    Connection_prepareStatement(conn, "select data_type from all_tab_columns where table_name=? and owner='SYSDBA' and column_name=?;");
                    PreparedStatement_setString(_prepareStatement, 1,tableName.c_str());
	                PreparedStatement_setString(_prepareStatement, 2,fieldName);
          
	            ResultSet_T resultson= PreparedStatement_executeQuery(_prepareStatement);
                int32_t columnTypeCnt = ResultSet_getColumnCount(resultson);
                string selectResult;
                while (ResultSet_next(resultson))
                {
                    for (int32_t indexson = 1; indexson <= columnTypeCnt; ++indexson)
                    {
                        // auto fieldNameson = ResultSet_getColumnName(resultson, indexson);
				        selectResult = ResultSet_getString(resultson, indexson);
                        // SQLBasicAccess_LOG(INFO)<<"查看数据fieldNameson:"<<string((char*)fieldNameson)<<" selectResult:"<<selectResult;
                    }
	            }
                if (selectResult=="CLOB")
                {
                    int size;
                    auto bytes = ResultSet_getBlob(result,index, &size);
                    if (bytes)
                    {
                        string res=base64_decode(string((char*)bytes,size),size);
                        if (res!="NULL"){
                            SQLBasicAccess_LOG(INFO)<<"查到的clob数据(base64编码的形式):"<<string((char*)bytes,size);
                            value[fieldName] = res;
                        }
                    }
                }
                else if (selectResult=="BLOB")
                {
                    SQLBasicAccess_LOG(WARNING)<< "获取了Blob信息，还没完成";
                    int size;
                    auto bytes = ResultSet_getBlobByName(result,fieldName , &size);
                    if (bytes)
                    {
                        string res=base64_decode(string((char*)bytes, size),size);
                        if (res!="NULL"){
                            value[fieldName] = res;
                        }else{
                            value[fieldName] = "";
                        }
                         
                    }
                   
                }else{    
                    auto selectResult = ResultSet_getString(result,index);
                    if (selectResult)
                    {
                        value[fieldName] = selectResult;
                        SQLBasicAccess_LOG(INFO)<<"查到的string数据:"<<string((char*)selectResult);
                    }
                  
                }
                
            }
            _values.push_back(move(value));
        }
    }
    Connection_execute(conn,"commit;");
    CATCH(SQLException)
    {
        m_connPool->ReturnConnection(conn);
        // Note: when select exception caused by table doesn't exist and sql error,
        //       here must return 0, in case of the DB is normal but the sql syntax error or the
        //       table will be created later
        SQLBasicAccess_LOG(ERROR) << "select exception for sql error:" << Exception_frame.message;
        return 0;
    }
    END_TRY;
   
    m_connPool->ReturnConnection(conn);
    return 0;
}
//创建查询的sql语句，返回sql查询语句
string SQLBasicAccess::BuildQuerySql(string _table, Condition::Ptr _condition)
{   //将table字符串中所有\替换为\\，`替换为\`
    _table = boost::algorithm::replace_all_copy(_table, "\\", "\\\\");
    _table = boost::algorithm::replace_all_copy(_table, "`", "\\`");
    string sql = "select * from \"SYSDBA\".\"";
    sql.append(_table).append("\"");
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
        // SQLBasicAccess_LOG(INFO)
        //     << "调用BuildQuerySql方法,执行结果:" << sql ;
    return sql;
}
//设置条件查询语句
string SQLBasicAccess::BuildConditionSql(const string& _strPrefix,
    map<string, Condition::Range>::const_iterator& _it, Condition::Ptr _condition)
{
    string strConditionSql = _strPrefix;
    if (_it->second.left.second == _it->second.right.second && _it->second.left.first &&
        _it->second.right.first)
    {
        strConditionSql.append(" \"").append(_it->first).append("\"=").append("?");
    }
    else
    {
        if (_it->second.left.second != _condition->unlimitedField())
        {
            if (_it->second.left.first)
            {
                strConditionSql.append(" \"").append(_it->first).append("\">=").append("?");
            }
            else
            {
                strConditionSql.append(" \"").append(_it->first).append("\">").append("?");
            }
        }
        if (_it->second.right.second != _condition->unlimitedField())
        {
            if (_it->second.right.first)
            {
                strConditionSql.append(" \"").append(_it->first).append("\"<=").append("?");
            }
            else
            {
                strConditionSql.append(" \"").append(_it->first).append("\"<").append("?");
            }
        }
    }
    // SQLBasicAccess_LOG(INFO)
    //         << "调用BuildConditionSql方法,执行结果:" << strConditionSql ;
    return strConditionSql;
}
//根据表名称获取存储类型
SQLFieldType SQLBasicAccess::getFieldType(std::string const& _tableName)
{
    // _sys_hash_2_block_ or _sys_block_2_nonce, the SYS_VALUE field is blob
    if (_tableName == SYS_HASH_2_BLOCK || _tableName == SYS_BLOCK_2_NONCES||_tableName == SYS_HASH_2_BLOCKHEADER )
    {
        // if (g_BCOSConfig.version() >= V2_6_0)
        // {
        //     return SQLFieldType::LongBlobType;
        // }
        return SQLFieldType::ClobType;
    }
    // _sys_hash_2_blockheader_, the SYS_VALUE and SYS_SIG_LIST are blob
    if (_tableName==SYS_CNS||_tableName==SYS_CONFIG||_tableName==SYS_CURRENT_STATE||_tableName==SYS_NUMBER_2_HASH||_tableName==SYS_TX_HASH_2_BLOCK)
    {
        return SQLFieldType::ClobType;
    }
    // contract table, all the fields are blob type
    if (boost::starts_with(_tableName, string("c_")))
    {
        if (g_BCOSConfig.version() >= V2_5_0)
        {
            return SQLFieldType::ClobType;
        }
    }
    // supported_version >= v2.6.0, all the field type of user created table is mediumblob
    if (g_BCOSConfig.version() >= V2_6_0)
    {
        return SQLFieldType::ClobType;
    }
    // supported_version < v2.6.0, the user created table is mediumtext
    return SQLFieldType::ClobType;
}

string SQLBasicAccess::BuildCreateTableSql(
    const string& _tableName, const string& _keyField, const string& _valueField)
{
    stringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS \"SYSDBA\".\"" << _tableName << "\"(\n";
    ss << " \"_id_\" BIGINT IDENTITY(1, 1) NOT NULL,\n";
    ss << " \"_num_\" BIGINT NOT NULL,\n";
    ss << " \"_status_\" INT NOT NULL,\n";
    ss << "\"" << _keyField << "\" varchar(255) default '',\n";
    vector<string> vecSplit;
    boost::split(vecSplit, _valueField, boost::is_any_of(","));
    auto it = vecSplit.begin();

    auto fieldType = getFieldType(_tableName);
    std::string fieldTypeName = SQLFieldTypeName[fieldType];
    for (; it != vecSplit.end(); ++it)
    {
        *it = boost::algorithm::replace_all_copy(*it, "\\", "\\\\");
        *it = boost::algorithm::replace_all_copy(*it, "`", "\\`");
        ss << "\"" << *it << "\" " << fieldTypeName << ",\n";
    }
    ss << " NOT CLUSTER PRIMARY KEY(\"_id_\")) STORAGE(ON \"MAIN\", CLUSTERBTR) ;\n";
    // ss << " KEY(`" << _keyField << "`(191)),\n";
    // ss << " KEY(`_num_`)\n";
    // cout<<"调用BuildCreateTableSql方法:"<<ss.str()<<endl;
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
                _fieldStr.append("\"").append(fieldIt.first).append("\",");
            }
            _valueList.push_back(fieldIt.second);
        }

        // if (g_BCOSConfig.version() <= V2_1_0)
        // {
        //     _valueList.push_back("0");
        // }

        _valueList.push_back(_num);
        _valueList.push_back(boost::lexical_cast<string>(entry->getID()));
        _valueList.push_back(boost::lexical_cast<string>(entry->getStatus()));
        isFirst = false;
    }

    if (!_fieldStr.empty())
    {
        // if (g_BCOSConfig.version() <= V2_1_0)
        // {
        //     _fieldStr.append("`_hash_`,");
        // }
        _fieldStr.append("\"").append(NUM_FIELD).append("\",");
        _fieldStr.append("\"").append(ID_FIELD).append("\",");
        _fieldStr.append("\"").append(STATUS).append("\"");
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
    // SQLBasicAccess_LOG(INFO) << " commit num:" << _num;
    string strNum = to_string(_num);
    if (_datas.size() == 0)
    {
        SQLBasicAccess_LOG(DEBUG) << "empty data just return";
        return 0;
    }
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
                    Connection_execute(conn,"commit;");
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
            vector<string> field2Type;
            this->GetCommitFieldNameAndValue(data->dirtyEntries, strNum, field2Values);
            this->GetCommitFieldNameAndValue(data->newEntries, strNum, field2Values);
            SQLBasicAccess_LOG(DEBUG) << "table:" << tableName << " split to "
                                      << field2Values.size() << " parts to commit";

            tableName = boost::algorithm::replace_all_copy(tableName, "\\", "\\\\");
            tableName = boost::algorithm::replace_all_copy(tableName, "`", "\\`");
            string _sql="set identity_insert \"SYSDBA\".\"";
            _sql.append(tableName).append("\" on;");
            Connection_execute(conn,_sql.c_str());
            for (auto item : field2Values)
            {
                const auto& name = item.first;
                const auto& values = item.second;
                
                vector<SQLPlaceholderItem> sqlPlaceholders =
                    this->BuildCommitSql(tableName, name, values);
                //e.g.name=="key","value","_num_","_id_","_status_"
                vector<string> fieldNames;
                boost::split(fieldNames, name, boost::is_any_of(","));
                //取得列对应的数据类型
                for (const std::string& item : fieldNames) {
               
                    PreparedStatement_T _prepareStatement_select =
                    Connection_prepareStatement(conn, "select data_type from all_tab_columns where table_name=? and owner='SYSDBA' and column_name=?;");
	                auto field = boost::algorithm::replace_all_copy(item, "\"", "");

                    PreparedStatement_setString(_prepareStatement_select, 1,tableName.c_str());
                    PreparedStatement_setString(_prepareStatement_select, 2,field.c_str());
	                ResultSet_T result= PreparedStatement_executeQuery(_prepareStatement_select);
                    int32_t columnCnt = ResultSet_getColumnCount(result);
                    // SQLBasicAccess_LOG(DEBUG)<<columnCnt<<" "<<tableName.c_str()<<" " <<field.c_str()<<"1111111111111111";
                    while (ResultSet_next(result))
                    {
		                for (int32_t index = 1; index <= columnCnt; ++index)
                        {
                            auto fieldName = ResultSet_getColumnName(result, index);
			                auto selectResult = ResultSet_getStringByName(result, fieldName);
			                field2Type.push_back(selectResult);
                        }      
	                }
                }
                uint32_t size1 = field2Type.size();
                vector<string> dealedStr;
            
                for (size_t i = 0; i < values.size(); ++i) {
                    if (field2Type[i%size1]=="CLOB")
                    {
                        string base64=values[i];
                        dealedStr.push_back(base64_encode(base64));

                    }else{
                        dealedStr.push_back(values[i]);
                    }
                }
                string searchSql;
                auto itValue = dealedStr.begin();
                auto itValueSearch = dealedStr.begin();
                // int round=-1;
                for (auto& placeholder : sqlPlaceholders)
                {
                    // round++;
                    PreparedStatement_T preStatement =
                        Connection_prepareStatement(conn, "%s", placeholder.sql.c_str());
                    // SQLBasicAccess_LOG(INFO)
                    //    <<"在commit中执行插入或者更新语句：" << "table:" << tableName << " sql:" << placeholder.sql;
                    uint32_t index = 0;
                    uint32_t size = field2Type.size();
                    if (placeholder.sql.find("insert") != std::string::npos)
                    {
                         for (; itValue != values.end(); ++itValue)
                        {
                            // SQLBasicAccess_LOG(INFO)<<"插入的string数据"<<"c_str():"<< itValue->c_str();
                                PreparedStatement_setString(preStatement, ++index, itValue->c_str());
                                // SQLBasicAccess_LOG(INFO) << "执行插入数据 index:" << index
                                                    //   << " setString的值:" << itValue->c_str();
                            if (index == placeholder.placeholderCnt)
                            {
                                PreparedStatement_execute(preStatement);
                                rowCount += (int32_t)PreparedStatement_rowsChanged(preStatement);
                                ++itValue;
                                break;
                            }
                        }
                   
                    }
                    else
                    {
                        //update的情况
                        bool helpFlag=false;
                        for (; itValue != values.end(); ++itValue)
                        {
                         
                            if (fieldNames[index%size]=="\"_id_\"")
                            {
                                PreparedStatement_setString(preStatement,size,itValue->c_str());
                                    // SQLBasicAccess_LOG(DEBUG)<<"在size大小的位置设置参数:"<<size<<"插入的字符串为:"<<itValue->c_str();
                                helpFlag=true;
                                index++;

                                continue;
                            }
                            if (helpFlag)
                                {
                                    // SQLBasicAccess_LOG(DEBUG)<<"插入的string数据"<<"c_str():"<< itValue->c_str();

                                    PreparedStatement_setString(preStatement, index++, itValue->c_str());
                                    // SQLBasicAccess_LOG(INFO) << "执行更新数据:"<< " setString的值:" << itValue->c_str();
                                    //  SQLBasicAccess_LOG(DEBUG) << " 插入参数（index-1）:" << index-1 << " 实际index:" << index
                                    //                   << " setString:" << itValue->c_str();
                                   
                                

                                }else{
                                    // SQLBasicAccess_LOG(DEBUG)<<"插入的string数据"<<"c_str():"<< itValue->c_str();
                                    PreparedStatement_setString(preStatement, ++index, itValue->c_str());
                                    // SQLBasicAccess_LOG(INFO) << "执行更新数据:"<< " setString的值:" << itValue->c_str();
                                    //  SQLBasicAccess_LOG(DEBUG) << " index:" << index << " num:" << _num
                                                    //   << " setString:" << itValue->c_str();
                                   
                                }
                                if (index == placeholder.placeholderCnt)
                            {
                                PreparedStatement_execute(preStatement);
                                // Connection_execute(conn,"commit;");
                                rowCount += (int32_t)PreparedStatement_rowsChanged(preStatement);
                                // SQLBasicAccess_LOG(DEBUG)<<"执行commit---"<<placeholder.placeholderCnt;
                                
                                ++itValue;
                                break;
                            }

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
    Connection_execute(conn,"commit;");
    m_connPool->ReturnConnection(conn);
    return rowCount;
}
// 	ss<<"merge into SYSDBA.\"_sys_tables_\"\n";
// 	ss<<"using (select ? as _id_,? as _num_,? as _status_,? as table_name,? as key_field,? as value_field from dual\n";
// 	// ss<<"UNION ALL\n";
//   	// ss<<"SELECT ? AS id, ? AS numbers, ? AS age";
// 	ss<<") t\n";
// 	ss<<"on(SYSDBA.\"_sys_tables_\".\"_id_\" = t._id_)\n";
// 	ss<<"when matched then\n";
// 	ss<<"update set SYSDBA.\"_sys_tables_\".\"_num_\"=t._num_, SYSDBA.\"_sys_tables_\".\"_status_\" =t._status_ \n";
// 	ss<<"when not matched then \n";
// 	ss<<"insert (\"_id_\",\"_num_\",\"_status_\") values(t._id_,t._num_,t._status_);";
vector<SQLPlaceholderItem> SQLBasicAccess::BuildCommitSql(
    const string& _table, const string& _fieldStr, const vector<string>& _fieldValues)
{

    SQLBasicAccess_LOG(INFO) << "table name:" << _table << "field str:" << _fieldStr;
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
    Connection_T conn = m_connPool->GetConnection();
    TRY
    {
        uint32_t placeholderCnt = 0;
        uint32_t valueSize = _fieldValues.size();//18
        uint32_t columnSize = fieldNames.size();//6
        string searchSql;
        uint32_t position;
        bool isUpdate=false;        
        for (uint32_t i=0;i<columnSize;i++)
        {
            if (fieldNames[i]=="\"_id_\"")
            {
                position=i;
                break;
            }
        }
        // SQLBasicAccess_LOG(INFO)<<"查看postion的位置："<<position;
        for (uint32_t index = 0; index < (valueSize/columnSize); index++)
        
        {   //建valueSize/columnSize个merge into语句
           
            string sqlHeader;
            searchSql="select count(*) from \"SYSDBA\".\"";
            searchSql.append(_table).append("\" where \"_id_\"= ?;");
            PreparedStatement_T preSearchStatement=Connection_prepareStatement(conn, "%s", searchSql.c_str());
            PreparedStatement_setString(preSearchStatement, 1,_fieldValues[index*columnSize+position].c_str());
            // SQLBasicAccess_LOG(INFO)<<"查看查询数据是否存在的sql语句:"<<searchSql.c_str()<<"---"<<_fieldValues[index*columnSize+position].c_str();
            ResultSet_T searchResult= PreparedStatement_executeQuery(preSearchStatement);
            int32_t columnCnt = ResultSet_getColumnCount(searchResult);     
            while (ResultSet_next(searchResult))
            {
                for (int32_t selectindex = 1; selectindex <= columnCnt; ++selectindex)
                {
			        auto fieldName = ResultSet_getColumnName(searchResult, selectindex);        
				    auto getColumn = ResultSet_getStringByName(searchResult, fieldName);
                    // if (getColumn){
                        // SQLBasicAccess_LOG(INFO)<<"查看查询结果:---"<<string((char*)getColumn)<<"field:"<<fieldName;
                    // }else{
                        // SQLBasicAccess_LOG(INFO)<<"结果为空";
                    // }
                    if ((string((char*)getColumn)=="0"))
                    {
                        isUpdate=false;
                    }else{
                        isUpdate=true;
                    }
                }
	        }
            // SQLBasicAccess_LOG(INFO)<<"检查isupdate"<<isUpdate;
//insert  into \"SYSDBA\".\"_sys_hash_2_block_\"  (\"hash\",\"value\",\"_num_\",\"_id_\",\"_status_\")values (?,?,?,?,?),(?,?,?,?,?);
            if (!isUpdate)
            {
                sqlHeader = "insert into SYSDBA.\"";
                sqlHeader.append(_table).append("\" (");
                for (uint32_t index2= 0; index2 < fieldNames.size(); index2++)
                {
                    if (index2!=(fieldNames.size()-1))
                    {
                        
                        sqlHeader.append(fieldNames[index2]).append(",");
                    }else
                    {
                        sqlHeader.append(fieldNames[index2]).append(")");
                    }
                }
                sqlHeader.append("values (");
                for (uint32_t index3 = 0; index3 < fieldNames.size(); index3++)
                {
                    placeholderCnt++;
                    sqlHeader.append("?,");
                }
                if (!sqlHeader.empty() && sqlHeader.back() == ',') 
                {
                    sqlHeader.pop_back(); // 删除最后一个字符（逗号）
                }
                sqlHeader.append(");");
                
            }else
            {
                // update  SYSDBA.\"_sys_hash_2_block_\" set \"hash\"=? , \"value\"=?, \"_num_\"=?, \"_status_\"=?;
                sqlHeader = "update  SYSDBA.\"";
                sqlHeader.append(_table).append("\" set ");
                 for (uint32_t index2= 0; index2 < fieldNames.size(); index2++)
                {
                    if (fieldNames[index2]=="\"_id_\"")
                    {
                        placeholderCnt++;
                        continue;
                    }else
                    {
                        placeholderCnt++;
                        sqlHeader.append(fieldNames[index2]).append("=? ,");
                    }
                   
                }
                if (!sqlHeader.empty() && sqlHeader.back() == ',') 
                {
                    sqlHeader.pop_back(); // 删除最后一个字符（逗号）
                }
                sqlHeader.append(" where \"_id_\"=?; ");
            }
            if (placeholderCnt > 0)
            {
                sqlHeader = sqlHeader.substr(0, sqlHeader.size() - 1);
                // SQLBasicAccess_LOG(INFO)<<"组成后的插入语句："<<sqlHeader;
                SQLPlaceholderItem item;
                item.sql = sqlHeader;
                item.placeholderCnt = placeholderCnt;
                // SQLBasicAccess_LOG(INFO)<<"查看placeholderCnt的值:"<<placeholderCnt;
                sqlPlaceholders.emplace_back(item);
                placeholderCnt=0;
            }
        }
    }
    CATCH(SQLException)
    {
        SQLBasicAccess_LOG(DEBUG) << "active connections:" << m_connPool->GetActiveConnections()
                                  << " max connetions:" << m_connPool->GetMaxConnections()
                                  << " now connections:" << m_connPool->GetTotalConnections();
        m_connPool->ReturnConnection(conn);
    }
    END_TRY;
    m_connPool->ReturnConnection(conn);
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
std::string base64_decode(const std::string& encoded,int size) {
    BIO *bio, *b64;
    char outbuf[size];
    std::string decoded;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new_mem_buf(encoded.c_str(), static_cast<int>(encoded.length()));
    bio = BIO_push(b64, bio);
    int bytesRead = BIO_read(bio, outbuf, encoded.length());

    decoded.assign(outbuf, bytesRead);

    BIO_free_all(bio);

    return decoded;
}
std::string base64_encode(const std::string& input) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    std::string encoded;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_new(BIO_s_mem());
    BIO_push(b64, bio);
    BIO_write(b64, input.c_str(), static_cast<int>(input.length()));
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bufferPtr);

    encoded.assign(bufferPtr->data, bufferPtr->length);

    BIO_free_all(b64);

    return encoded;
}
