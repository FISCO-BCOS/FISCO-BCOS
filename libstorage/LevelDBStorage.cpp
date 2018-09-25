/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file MinerPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */
#include "LevelDBStorage.h"

#include "LevelDBStorage.h"
#include "Table.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <libdevcore/easylog.h>

using namespace dev;
using namespace dev::storage;

TableInfo::Ptr LevelDBStorage::info(const std::string& table)
{
    TableInfo::Ptr tableInfo = std::make_shared<TableInfo>();
    tableInfo->name = table;

    return tableInfo;
}

Entries::Ptr LevelDBStorage::select(
    h256 hash, int num, const std::string& table, const std::string& key)
{
    try
    {
        LOG(DEBUG) << "Query leveldb data";

        std::string entryKey = table + "_" + key;
        std::string value;
        auto s = m_db->Get(leveldb::ReadOptions(), leveldb::Slice(entryKey), &value);
        if (!s.ok() && !s.IsNotFound())
        {
            LOG(ERROR) << "Query leveldb failed:" + s.ToString();

            BOOST_THROW_EXCEPTION(StorageException(-1, "Query leveldb exception:" + s.ToString()));
        }

        Entries::Ptr entries = std::make_shared<Entries>();
        if (!s.IsNotFound())
        {
            // parse json
            std::stringstream ssIn;
            ssIn << value;

            Json::Value valueJson;
            ssIn >> valueJson;

            std::string blockHash = valueJson["blockHash"].asString();
            int num = valueJson["num"].asInt();

            Json::Value values = valueJson["values"];
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                Entry::Ptr entry = std::make_shared<Entry>();

                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {
                    entry->setField(valueIt.key().asString(), valueIt->asString());
                }

                entry->setDirty(false);
                entries->addEntry(entry);
            }
        }

        return entries;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Query leveldb exception:" << boost::diagnostic_information(e);

        BOOST_THROW_EXCEPTION(e);
    }

    return Entries::Ptr();
}

size_t LevelDBStorage::commit(
    h256 hash, int num, const std::vector<TableData::Ptr>& datas, h256 blockHash)
{
    try
    {
        leveldb::WriteBatch batch;

        size_t total = 0;
        for (auto it : datas)
        {
            for (auto dataIt : it->data)
            {
                std::string entryKey = it->tableName + "_" + dataIt.first;

                Json::Value entry;
                entry["blockHash"] = hash.hex();
                entry["num"] = num;

                for (size_t i = 0; i < dataIt.second->size(); ++i)
                {
                    if (dataIt.second->get(i)->dirty())
                    {
                        Json::Value value;
                        for (auto fieldIt : *(dataIt.second->get(i)->fields()))
                        {
                            value[fieldIt.first] = fieldIt.second;
                        }
                        entry["values"].append(value);
                    }
                }

                std::stringstream ssOut;
                ssOut << entry;

                batch.Put(leveldb::Slice(entryKey), leveldb::Slice(ssOut.str()));
                ++total;
            }
        }

        leveldb::WriteOptions writeOptions;
        writeOptions.sync = false;

        auto s = m_db->Write(writeOptions, &batch);
        if (!s.ok())
        {
            LOG(ERROR) << "Commit leveldb failed: " << s.ToString();

            BOOST_THROW_EXCEPTION(StorageException(-1, "Commit leveldb exception:" + s.ToString()));
        }

        return total;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "Commit leveldb exception" << e.what();

        BOOST_THROW_EXCEPTION(e);
    }

    return 0;
}

bool LevelDBStorage::onlyDirty()
{
    return false;
}

void LevelDBStorage::setDB(std::shared_ptr<leveldb::DB> db)
{
    m_db = db;
}
