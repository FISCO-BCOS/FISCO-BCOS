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
/** @file StashStorage.cpp
 *  @author wesleywang
 *  @date 2021-5-10
 */

#include "StashStorage.h"
#include "StashSQLBasicAccess.h"
#include <libdevcore/FixedHash.h>
#include <libstorage/Common.h>
#include <libstorage/StorageException.h>
#include <libstorage/Table.h>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::storage;

StashStorage::StashStorage() {}

void StashStorage::SetStashSqlAccess(StashSQLBasicAccess::Ptr _sqlBasicAcc)
{
    m_sqlBasicAcc = _sqlBasicAcc;
}
void StashStorage::setStashConnPool(std::shared_ptr<SQLConnectionPool>& _connPool)
{
    m_sqlBasicAcc->setConnPool(_connPool);
}

TableData::Ptr StashStorage::selectTableDataByNum(
    int64_t num, const TableInfo::Ptr& _tableInfo, uint64_t start, uint32_t counts)
{
    try
    {
        std::vector<std::map<std::string, std::string> > values;
        int ret = 0, i = 0;
        for (i = 0; i < m_maxRetry; ++i)
        {
            ret = m_sqlBasicAcc->SelectTableDataByNum(num, _tableInfo, start, counts, values);
            if (ret < 0)
            {
                StashStorage_LOG(ERROR) << "Remote select datdbase return error:" << ret
                                        << " table:" << _tableInfo->name << LOG_KV("retry", i + 1);
                this_thread::sleep_for(chrono::milliseconds(1000));
                continue;
            }
            else
            {
                break;
            }
        }
        if (i == m_maxRetry && ret < 0)
        {
            StashStorage_LOG(ERROR)
                << "MySQL select return error: " << ret << LOG_KV("table", _tableInfo->name)
                << LOG_KV("retry", m_maxRetry);
            auto e = StorageException(
                -1, "MySQL select return error:" + to_string(ret) + " table:" + _tableInfo->name);
            m_fatalHandler(e);
            BOOST_THROW_EXCEPTION(e);
        }
        TableData::Ptr tableData = std::make_shared<TableData>();
        _tableInfo->fields.emplace_back(_tableInfo->key);
        _tableInfo->fields.emplace_back(STATUS);
        _tableInfo->fields.emplace_back(NUM_FIELD);
        _tableInfo->fields.emplace_back(ID_FIELD);
        _tableInfo->fields.emplace_back("_hash_");
        tableData->info = _tableInfo;
        tableData->newEntries = std::make_shared<Entries>();

        for (const auto& it : values)
        {
            Entry::Ptr entry = std::make_shared<Entry>();
            for (const auto& it2 : it)
            {
                if (it2.first == ID_FIELD)
                {
                    entry->setID(it2.second);
                }
                else if (it2.first == NUM_FIELD)
                {
                    entry->setNum(it2.second);
                }
                else if (it2.first == STATUS)
                {
                    entry->setStatus(it2.second);
                }
                else
                {
                    entry->setField(it2.first, it2.second);
                }
            }
            if (entry->getStatus() == 0)
            {
                entry->setDirty(false);
                tableData->newEntries->addEntry(entry);
            }
        }
        tableData->dirtyEntries = std::make_shared<Entries>();
        return tableData;
    }
    catch (std::exception& e)
    {
        STORAGE_EXTERNAL_LOG(ERROR) << "Query database error:" << e.what();
        BOOST_THROW_EXCEPTION(
            StorageException(-1, std::string("Query database error:") + e.what()));
    }
    return TableData::Ptr();
}
