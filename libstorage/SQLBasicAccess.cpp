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

using namespace dev::storage;
using namespace std;

int SQLBasicAccess::Select(h256 hash, int num, 
                const std::string& table, const std::string& key,
                Condition::Ptr condition,Json::Value &respJson)
{
    std::string    strSql = this->BuildQuerySql(table,condition);
    LOG(DEBUG) << "hash:"<<hash.hex()<<" num:"<<num<<" table:"
                    <<table<<" key:"<<key<<" query sql:"<<strSql;
    Connection_T    oConn = m_oConnPool.GetConnection();
    PreparedStatement_T oPreSatement = Connection_prepareStatement(oConn,"%s",strSql.c_str());
    uint32_t    dwIndex = 0;
    if (condition)
    {
        for (auto it : *(condition->getConditions()))
        {
            PreparedStatement_setString(oPreSatement,++dwIndex,it.second.second.c_str());
            LOG(DEBUG) << "hash:"<<hash.hex()<<" num:"<<num<<" table:"
                    <<table<<" key:"<<key<<" index:"<<dwIndex<<" value:"<<it.second.second;
        }
    }

    TRY
    {
        ResultSet_T oResult = PreparedStatement_executeQuery(oPreSatement);
        string  strColumnName;
        int32_t iColumnCnt = ResultSet_getColumnCount(oResult);
        for(int32_t iIndex =1;iIndex <= iColumnCnt;++iIndex)
        {
            strColumnName = ResultSet_getColumnName(oResult, iIndex);
            respJson["result"]["columns"].append(strColumnName);
        }

        while(ResultSet_next(oResult))
        {
            Json::Value oValueJson;
            for(int32_t iIndex =1;iIndex <= iColumnCnt;++iIndex)
            {
                oValueJson.append(ResultSet_getString(oResult,iIndex));
            }
            respJson["result"]["data"].append(oValueJson);
        }
    }
    CATCH (SQLException)
    {
        LOG(ERROR)<<"exception:";
        fprintf(stderr,"exception:%u",__LINE__);
	m_oConnPool.ReturnConnection(oConn);
        return 1;
    }
    END_TRY;
    m_oConnPool.ReturnConnection(oConn);
    return 0;
}

std::string SQLBasicAccess::BuildQuerySql(
        const std::string& table, Condition::Ptr condition)
{
    std::string strSql = "select * from ";
    strSql.append(table);
    uint32_t    dwIndex = 0;
    if (condition)
    {
        auto conditionmap = *(condition->getConditions());
        auto it = conditionmap.begin();
        for (;it != conditionmap.end();++it)
        {
            if(dwIndex == 0)
            {
                ++dwIndex;
                strSql.append(GenerateConditionSql(" where",it));
            }
            else
            {
                strSql.append(GenerateConditionSql(" and",it));
            }
        }
    }

    return strSql;
}

std::string SQLBasicAccess::GenerateConditionSql(const std::string &strPrefix,
                std::map<std::string, std::pair<Condition::Op, std::string>>::iterator &it)
{
    
    string  strConditionSql = strPrefix;
    if(it->second.first == Condition::Op::eq)
    {
        strConditionSql.append(" `").append(it->first).append("`=").append("?");
    }
    else if(it->second.first ==  Condition::Op::ne)
    {
        strConditionSql.append(" `").append(it->first).append("`!=").append("?");
    }
    else if(it->second.first ==  Condition::Op::gt)
    {
        strConditionSql.append(" `").append(it->first).append("`>").append("?");
    }
    else if(it->second.first ==  Condition::Op::ge)
    {
        strConditionSql.append(" `").append(it->first).append("`>=").append("?");
    }
    else if(it->second.first ==  Condition::Op::lt)
    {
        strConditionSql.append(" `").append(it->first).append("`<").append("?");
    }
    else if(it->second.first ==  Condition::Op::le)
    {
        strConditionSql.append(" `").append(it->first).append("`<=").append("?");
    }
    else
    {
        LOG(ERROR) << "error op code:"<<it->second.first;
        return "";
    }
    return strConditionSql;
}


int SQLBasicAccess::Commit(h256 hash, int num,
            const std::vector<TableData::Ptr>& datas,
            h256 const& blockHash)
{
    LOG(DEBUG)<<" commit hash:"<<hash.hex()<<" num:"<<num<<" blockhash:"<<blockHash.hex();
    char cNum[16] = {0};
    snprintf(cNum,sizeof(cNum),"%u",num);
    if (datas.size() == 0)
    {
        LOG(DEBUG) << "Empty data just return";
        return 0;
    }
    int32_t dwRowCount = 0;
    Connection_T    oConn = m_oConnPool.GetConnection();
    m_oConnPool.BeginTransaction(oConn);
    TRY
    {
        for (auto it : datas)
        {
            auto tableInfo = it->info;
            std::string strTableName = tableInfo->name;
            std::vector<std::string> oVecFieldName;
            std::vector<std::string> oVecFieldValue;
            /*different rows*/
            for (size_t i = 0; i < it->entries->size(); ++i)
            {
                auto entry = it->entries->get(i);
                /*different fields*/
                for (auto fieldIt : *entry->fields())
                {
                    if(i == 0)
                    {
                        oVecFieldName.push_back(fieldIt.first);
                    }
                    oVecFieldValue.push_back(fieldIt.second);
                }
                oVecFieldValue.push_back(hash.hex());
                oVecFieldValue.push_back(cNum);

            }
            oVecFieldName.push_back("_hash_");
            oVecFieldName.push_back("_num_");
            /*build commit sql*/
            string  strSql = this->BuildCommitSql(strTableName,oVecFieldName,oVecFieldValue);
            LOG(DEBUG)<<" commit hash:"<<hash.hex()<<" num:"
                <<num<<" blockhash:"<<blockHash.hex()<<" commit sql:"<<strSql;
            PreparedStatement_T oPreSatement = Connection_prepareStatement(oConn,"%s",strSql.c_str());
            int32_t    dwIndex = 0;
            auto itValue = oVecFieldValue.begin();
            for(;itValue != oVecFieldValue.end();++itValue)
            {
                PreparedStatement_setString(oPreSatement,++dwIndex,itValue->c_str());
            }
            PreparedStatement_execute(oPreSatement);

             dwRowCount += (int32_t)PreparedStatement_rowsChanged(oPreSatement);
        }
    }
    CATCH (SQLException)
    {
        LOG(ERROR)<<"exception:";
        m_oConnPool.RollBack(oConn);
        m_oConnPool.ReturnConnection(oConn);
        return 1;
    }
    END_TRY;
    m_oConnPool.Commit(oConn);
    m_oConnPool.ReturnConnection(oConn);
    return dwRowCount;
}


 std::string SQLBasicAccess::BuildCommitSql(
        const std::string& table,
        const std::vector<std::string> &oVecFieldName,
        const std::vector<std::string> &oVecFieldValue)
{
    if(oVecFieldName.size()==0 || oVecFieldName.size() ==0 
        || (oVecFieldValue.size()%oVecFieldName.size()))
    {
        /*throw execption*/
        LOG(ERROR)<<"field size:"<<oVecFieldName.size()<<" value size:"<<oVecFieldValue.size();
    }
    uint32_t    dwColumnSize = oVecFieldName.size();
    std::string strSql = "insert into ";
    strSql.append(table).append("(");
    auto it = oVecFieldName.begin();
    for(;it != oVecFieldName.end();++it)
    {
        strSql.append("`").append(*it).append("`").append(",");
    }
    strSql = strSql.substr(0,strSql.size()-1);
    strSql.append(") values");
    
    LOG(DEBUG)<<"field size:"<<oVecFieldName.size()<<" value size:"<<oVecFieldValue.size();

    uint32_t    dwSize = oVecFieldValue.size();
    for(uint32_t dwIndex = 0;dwIndex <dwSize;++dwIndex)
    {
        if(dwIndex % dwColumnSize ==0)
        {
            strSql.append("(?,");
        }
        else
        {
             strSql.append("?,");
        }
        if(dwIndex % dwColumnSize == (dwColumnSize-1))
        {
            strSql = strSql.substr(0,strSql.size()-1);
            strSql.append("),");
        }
        LOG(DEBUG)<<"commit sql:"<<strSql;
    }
    strSql = strSql.substr(0,strSql.size()-1);
    return strSql;
}


void SQLBasicAccess::initConnPool( const std::string &dbtype,
        const std::string &dbip,
        uint32_t    dbport,
        const std::string &dbusername,
        const std::string &dbpasswd,
        const std::string &dbname,
        const std::string &dbcharset,
        uint32_t    initconnections,
        uint32_t    maxconnections)
{
    m_oConnPool.InitConnectionPool(
        dbtype,dbip,
        dbport,dbusername,
        dbpasswd,dbname,
        dbcharset,
        initconnections,maxconnections);
    return;
}
