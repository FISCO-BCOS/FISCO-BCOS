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
 * (c) 2016-2018 fisco-dev contributors.
 */
/** @file SealerPrecompiled.h
 *  @author ancelmo
 *  @date 20180921
 */

#include "LevelDBStorage2.h"
#include "Table.h"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
#include <libdevcore/BasicLevelDB.h>
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
#include <tbb/parallel_for.h>
#include <memory>
#include <thread>

using namespace dev;
using namespace dev::storage;

Entries::Ptr LevelDBStorage2::select(
    h256, int, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    try
    {
        std::string entryKey = tableInfo->name;
        entryKey.append("_").append(key);

        std::string value;
        // ReadGuard l(m_remoteDBMutex);
        auto s = m_db->Get(leveldb::ReadOptions(), leveldb::Slice(entryKey), &value);
        // l.unlock();
        if (!s.ok() && !s.IsNotFound())
        {
            STORAGE_LEVELDB_LOG(ERROR)
                << LOG_DESC("Query leveldb failed") << LOG_KV("status", s.ToString());

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

            Json::Value values = valueJson["values"];
            for (auto it = values.begin(); it != values.end(); ++it)
            {
                Entry::Ptr entry = std::make_shared<Entry>();

                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {
                    entry->setField(valueIt.key().asString(), valueIt->asString());
                }

                if (entry->getStatus() == Entry::Status::NORMAL && condition->process(entry))
                {
                    entry->setDirty(false);
                    entries->addEntry(entry);
                }
            }
        }

        return entries;
    }
    catch (std::exception& e)
    {
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Query leveldb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(e);
    }

    return Entries::Ptr();
}

size_t LevelDBStorage2::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    try
    {
        auto hex = hash.hex();

        std::shared_ptr<dev::db::LevelDBWriteBatch> batch = m_db->createWriteBatch();
        for (size_t i = 0; i < datas.size(); ++i)
        {
            std::shared_ptr<std::map<std::string, Json::Value> > key2value =
                std::make_shared<std::map<std::string, Json::Value> >();

            auto tableInfo = datas[i]->info;

            processEntries(hash, num, key2value, tableInfo, datas[i]->dirtyEntries);
            processEntries(hash, num, key2value, tableInfo, datas[i]->newEntries);

            for (auto it : *key2value)
            {
                std::string entryKey = tableInfo->name + "_" + it.first;
                std::stringstream ssOut;
                ssOut << it.second;

                batch->insertSlice(leveldb::Slice(entryKey), leveldb::Slice(ssOut.str()));
            }
        }

        m_db->Write(leveldb::WriteOptions(), &batch->writeBatch());
        return datas.size();
    }
    catch (std::exception& e)
    {
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Commit leveldb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            StorageException(-1, "Commit leveldb exception:" + boost::diagnostic_information(e)));
    }

    return 0;
}

bool LevelDBStorage2::onlyDirty()
{
    return false;
}

void LevelDBStorage2::setDB(std::shared_ptr<dev::db::BasicLevelDB> db)
{
    m_db = db;
}

void LevelDBStorage2::processEntries(h256 hash, int64_t num,
    std::shared_ptr<std::map<std::string, Json::Value> > key2value, TableInfo::Ptr tableInfo,
    Entries::Ptr entries)
{
    for (size_t j = 0; j < entries->size(); ++j)
    {
        auto entry = entries->get(j);
        auto key = entry->getField(tableInfo->key);

        auto it = key2value->find(key);
        if (it == key2value->end())
        {
            std::string entryKey = tableInfo->name;
            entryKey.append("_").append(key);

            std::string value;
            auto s = m_db->Get(leveldb::ReadOptions(), leveldb::Slice(entryKey), &value);
            // l.unlock();
            if (!s.ok() && !s.IsNotFound())
            {
                STORAGE_LEVELDB_LOG(ERROR)
                    << LOG_DESC("Query leveldb failed") << LOG_KV("status", s.ToString());

                BOOST_THROW_EXCEPTION(
                    StorageException(-1, "Query leveldb exception:" + s.ToString()));
            }

            if (s.IsNotFound())
            {
                it = key2value->insert(std::make_pair(key, Json::Value())).first;
            }
            else
            {
                std::stringstream ssIn;
                ssIn << value;

                Json::Value valueJson;
                ssIn >> valueJson;

                it = key2value->emplace(key, valueJson).first;
            }
        }

        Json::Value value;
        for (auto& fieldIt : *(entry->fields()))
        {
            value[fieldIt.first] = fieldIt.second;
        }
        value["_hash_"] = hash.hex();
        value["_num_"] = boost::lexical_cast<std::string>(num);

        auto searchIt = std::lower_bound(it->second["values"].begin(), it->second["values"].end(),
            value, [](const Json::Value& lhs, const Json::Value& rhs) {
                // LOG(ERROR) << "lhs: " << lhs.toStyledString() << "rhs: " <<
                // rhs.toStyledString();
                return boost::lexical_cast<size_t>(lhs["_id_"].asString()) <
                       boost::lexical_cast<size_t>(rhs["_id_"].asString());
                return false;
            });

        if (searchIt != it->second["values"].end() && (*searchIt)["_id_"] == value["_id_"])
        {
            *searchIt = value;
        }
        else
        {
            it->second["values"].append(value);
        }
    }
}
