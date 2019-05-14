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
/** @file RocksDBStorage.cpp
 *  @author xingqiangbai
 *  @date 20180423
 */

#include "RocksDBStorage.h"
#include "StorageException.h"
#include "Table.h"
#include "boost/archive/binary_iarchive.hpp"
#include "boost/archive/binary_oarchive.hpp"
#include "boost/serialization/map.hpp"
#include "boost/serialization/serialization.hpp"
#include "boost/serialization/vector.hpp"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"
#include "rocksdb/write_batch.h"
#include <libdevcore/Common.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Guards.h>
#include <libdevcore/RLP.h>
#include <libdevcore/easylog.h>
#include <tbb/parallel_for.h>
#include <memory>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::storage;
using namespace rocksdb;

Entries::Ptr RocksDBStorage::select(
    h256, int, TableInfo::Ptr tableInfo, const std::string& key, Condition::Ptr condition)
{
    try
    {
        std::string entryKey = tableInfo->name;
        entryKey.append("_").append(key);

        std::string value;
        auto s = m_db->Get(ReadOptions(), Slice(entryKey), &value);
        if (!s.ok() && !s.IsNotFound())
        {
            STORAGE_LEVELDB_LOG(ERROR)
                << LOG_DESC("Query rocksdb failed") << LOG_KV("status", s.ToString());

            BOOST_THROW_EXCEPTION(StorageException(-1, "Query rocksdb exception:" + s.ToString()));
        }

        Entries::Ptr entries = std::make_shared<Entries>();
        if (!s.IsNotFound())
        {
            std::vector<std::map<std::string, std::string>> res;
            stringstream ss(value);
            boost::archive::binary_iarchive ia(ss);
            ia >> res;

            for (auto it = res.begin(); it != res.end(); ++it)
            {
                Entry::Ptr entry = std::make_shared<Entry>();

                for (auto valueIt = it->begin(); valueIt != it->end(); ++valueIt)
                {
                    if (valueIt->first == ID_FIELD)
                    {
                        entry->setID(valueIt->second);
                    }
                    else
                    {
                        entry->setField(valueIt->first, valueIt->second);
                    }
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
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Query rocksdb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));

        BOOST_THROW_EXCEPTION(e);
    }

    return Entries::Ptr();
}

size_t RocksDBStorage::commit(h256 hash, int64_t num, const std::vector<TableData::Ptr>& datas)
{
    try
    {
        auto start_time = utcTime();

        auto hex = hash.hex();
        WriteBatch batch;
        for (size_t i = 0; i < datas.size(); ++i)
        {
            std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
                key2value = std::make_shared<
                    std::map<std::string, std::vector<std::map<std::string, std::string>>>>();

            auto tableInfo = datas[i]->info;

            processDirtyEntries(hash, num, key2value, tableInfo, datas[i]->dirtyEntries);
            processNewEntries(hash, num, key2value, tableInfo, datas[i]->newEntries);

            for (auto it : *key2value)
            {
                std::string entryKey = tableInfo->name + "_" + it.first;
                stringstream ss;
                boost::archive::binary_oarchive oa(ss);
                oa << it.second;
                batch.Put(Slice(entryKey), Slice(ss.str()));
            }
        }
        auto encode_time_cost = utcTime();

        WriteOptions options;
        options.sync = false;
        m_db->Write(options, &batch);
        auto writeDB_time_cost = utcTime();
        STORAGE_LEVELDB_LOG(DEBUG)
            << LOG_BADGE("Commit") << LOG_DESC("Write to db")
            << LOG_KV("encodeTimeCost", encode_time_cost - start_time)
            << LOG_KV("writeDBTimeCost", writeDB_time_cost - encode_time_cost)
            << LOG_KV("totalTimeCost", utcTime() - start_time);

        return datas.size();
    }
    catch (std::exception& e)
    {
        STORAGE_LEVELDB_LOG(ERROR) << LOG_DESC("Commit rocksdb exception")
                                   << LOG_KV("msg", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(e);
    }

    return 0;
}

bool RocksDBStorage::onlyDirty()
{
    return false;
}

void RocksDBStorage::setDB(std::shared_ptr<rocksdb::DB> db)
{
    m_db = db;
}

void RocksDBStorage::processNewEntries(h256 hash, int64_t num,
    std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
        key2value,
    TableInfo::Ptr tableInfo, Entries::Ptr entries)
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
            auto s = m_db->Get(ReadOptions(), Slice(entryKey), &value);
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
                it = key2value
                         ->insert(
                             std::make_pair(key, std::vector<std::map<std::string, std::string>>()))
                         .first;
            }
            else
            {
                std::vector<std::map<std::string, std::string>> res;
                stringstream ss(value);
                boost::archive::binary_iarchive ia(ss);
                ia >> res;
                it = key2value->emplace(key, res).first;
            }
        }

        std::map<std::string, std::string> value;
        for (auto& fieldIt : *(entry->fields()))
        {
            value[fieldIt.first] = fieldIt.second;
        }
        value["_hash_"] = hash.hex();
        value["_num_"] = boost::lexical_cast<std::string>(num);
        value["_id_"] = boost::lexical_cast<std::string>(entry->getID());

        auto searchIt = std::lower_bound(it->second.begin(), it->second.end(), value,
            [](const std::map<std::string, std::string>& lhs,
                const std::map<std::string, std::string>& rhs) {
                return lhs.at("_id_") < rhs.at("_id_");
            });

        if (searchIt != it->second.end() && (*searchIt)["_id_"] == value["_id_"])
        {
            *searchIt = value;
        }
        else
        {
            it->second.push_back(value);
        }
    }
}

void RocksDBStorage::processDirtyEntries(h256 hash, int64_t num,
    std::shared_ptr<std::map<std::string, std::vector<std::map<std::string, std::string>>>>
        key2value,
    TableInfo::Ptr tableInfo, Entries::Ptr entries)
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

            it =
                key2value
                    ->insert(std::make_pair(key, std::vector<std::map<std::string, std::string>>()))
                    .first;
        }

        std::map<std::string, std::string> value;
        for (auto& fieldIt : *(entry->fields()))
        {
            value[fieldIt.first] = fieldIt.second;
        }
        value["_hash_"] = hash.hex();
        value["_num_"] = boost::lexical_cast<std::string>(num);
        value["_id_"] = boost::lexical_cast<std::string>(entry->getID());

        it->second.push_back(value);
    }
}
