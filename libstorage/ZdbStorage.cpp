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
/** @file ZdbStorage.cpp
 *  @author darrenyin
 *  @date 2019-04-24
 */

#include "StorageException.h"
#include <libdevcore/easylog.h>
#include "Common.h"
#include "ZdbStorage.h"
#include "Table.h"


using namespace dev;
using namespace dev::storage;

ZdbStorage::ZdbStorage() {}

Entries::Ptr ZdbStorage::select(
    h256 hash, int num, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    Json::Value responseJson;
    int iRet = m_oSqlBasicAcc.Select(hash,num,tableInfo->name,key,condition,responseJson);
    if(iRet < 0)
    {
        LOG(ERROR) << "Remote database return error:" << iRet;
        throw StorageException(-1, 
            "Remote database return error:" + boost::lexical_cast<std::string>(iRet));
    }


    LOG(DEBUG)<<"select resp:"<<responseJson.toStyledString();
    std::vector<std::string> columns;
    for (Json::ArrayIndex i = 0; i < responseJson["result"]["columns"].size(); ++i)
    {
        std::string fieldName = responseJson["result"]["columns"].get(i, "").asString();
        columns.push_back(fieldName);
    }

    Entries::Ptr entries = std::make_shared<Entries>();
    for (Json::ArrayIndex i = 0; i < responseJson["result"]["data"].size(); ++i)
    {
        Json::Value line = responseJson["result"]["data"].get(i, "");
        Entry::Ptr entry = std::make_shared<Entry>();

        for (Json::ArrayIndex j = 0; j < line.size(); ++j)
        {
            std::string fieldValue = line.get(j, "").asString();
            entry->setField(columns[j], fieldValue);
        }

        if (entry->getStatus() == 0)
        {
            entry->setDirty(false);
            entries->addEntry(entry);
        }
        }
        entries->setDirty(false);
        return entries;
}

size_t ZdbStorage::commit(
    h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
   
    for (auto it : datas)
    {
        for (size_t i = 0; i < it->dirtyEntries->size(); ++i)
        {
            Entry::Ptr entry = it->dirtyEntries->get(i);
            for (auto fieldIt : *entry->fields())
            {
                if(fieldIt.first == "_num_" || fieldIt.first == "_hash_")
                {
                    continue;
                }
            LOG(DEBUG)<<"new entry key:"<<fieldIt.first<<" value:"<<fieldIt.second;
            }
        }
        for (size_t i = 0; i < it->newEntries->size(); ++i)
        {
            Entry::Ptr entry = it->newEntries->get(i);
            for (auto fieldIt : *entry->fields())
            {
                if(fieldIt.first == "_num_" || fieldIt.first == "_hash_")
                {
                continue;
                }
                LOG(DEBUG)<<"new entry key:"<<fieldIt.first<<" value:"<<fieldIt.second;
            }
        }
    }    

    int32_t dwRowCount = m_oSqlBasicAcc.Commit(hash,(int32_t)num,datas);
    if(dwRowCount < 0)
    {
        LOG(ERROR) << "database return error:" << dwRowCount;
        throw StorageException(-1, 
            "database return error:" + boost::lexical_cast<std::string>(dwRowCount));
    }
    return dwRowCount;
}

bool ZdbStorage::onlyDirty()
{
    return true;
}


void ZdbStorage::initSqlAccess(
        const std::string &dbtype,
        const std::string &dbip,
        uint32_t    dbport,
        const std::string &dbusername,
        const std::string &dbpasswd,
        const std::string &dbname,
        const std::string &dbcharset,
        uint32_t    initconnections,
        uint32_t    maxconnections
    )
{
    m_oSqlBasicAcc.initConnPool(dbtype,dbip,dbport,
        dbusername,dbpasswd,dbname,dbcharset,initconnections,maxconnections);
}
