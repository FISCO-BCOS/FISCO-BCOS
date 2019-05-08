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

#include "ZdbStorage.h"
#include "Common.h"
#include "StorageException.h"
#include "Table.h"
#include <libdevcore/easylog.h>


using namespace dev;
using namespace dev::storage;

ZdbStorage::ZdbStorage() {}

Entries::Ptr ZdbStorage::select(h256 _hash, int _num, TableInfo::Ptr _tableInfo,
    const std::string& _key, Condition::Ptr _condition)
{
    Json::Value responseJson;
    int iRet = m_sqlBasicAcc.Select(_hash, _num, _tableInfo->name, _key, _condition, responseJson);
    if (iRet < 0)
    {
        LOG(ERROR) << "Remote database return error:" << iRet;
        throw StorageException(
            -1, "Remote database return error:" + boost::lexical_cast<std::string>(iRet));
    }


    LOG(DEBUG) << "select resp:" << responseJson.toStyledString();
    std::vector<std::string> columns;
    for (Json::ArrayIndex i = 0; i < responseJson["result"]["columns"].size(); ++i)
    {
        columns.push_back(responseJson["result"]["columns"].get(i, "").asString());
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

size_t ZdbStorage::commit(h256 _hash, int64_t _num, const std::vector<TableData::Ptr>& _datas)
{
    for (auto it : _datas)
    {
        for (size_t i = 0; i < it->dirtyEntries->size(); ++i)
        {
            Entry::Ptr entry = it->dirtyEntries->get(i);
            for (auto fieldIt : *entry->fields())
            {
                if (fieldIt.first == "_num_" || fieldIt.first == "_hash_")
                {
                    continue;
                }
                LOG(DEBUG) << "new entry key:" << fieldIt.first << " value:" << fieldIt.second;
            }
        }
        for (size_t i = 0; i < it->newEntries->size(); ++i)
        {
            Entry::Ptr entry = it->newEntries->get(i);
            for (auto fieldIt : *entry->fields())
            {
                if (fieldIt.first == "_num_" || fieldIt.first == "_hash_")
                {
                    continue;
                }
                LOG(DEBUG) << "new entry key:" << fieldIt.first << " value:" << fieldIt.second;
            }
        }
    }

    int32_t dwRowCount = m_sqlBasicAcc.Commit(_hash, (int32_t)_num, _datas);
    if (dwRowCount < 0)
    {
        LOG(ERROR) << "database return error:" << dwRowCount;
        throw StorageException(
            -1, "database return error:" + boost::lexical_cast<std::string>(dwRowCount));
    }
    return dwRowCount;
}

bool ZdbStorage::onlyDirty()
{
    return true;
}

void ZdbStorage::initSqlAccess(const storage::ZDBConfig& _dbConfig)
{
    m_sqlBasicAcc.initConnPool(_dbConfig);
}
