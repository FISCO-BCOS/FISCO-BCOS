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
/**
 * @brief : implement basic interface to access RocksDB
 * @author: yujiechen
 * @date: 2019-06-22
 */

#include "BasicRocksDB.h"
#include <libdevcore/Exceptions.h>
using namespace dev;
using namespace rocksdb;

namespace dev
{
namespace db
{
/**
 * @brief: open rocksDB
 *
 * @param options: options used to open the rocksDB
 * @param dbname: db name
 * @return std::shared_ptr<rocksdb::DB>:
 * 1. open successfully: return the DB handler
 * 2. open failed: throw exception(OpenDBFailed)
 */
std::shared_ptr<rocksdb::DB> BasicRocksDB::Open(const Options& options, const std::string& dbname)
{
    ROCKSDB_LOG(INFO) << LOG_DESC("open rocksDB handler");
    DB* db = nullptr;
    auto status = DB::Open(options, dbname, &db);
    if (!status.ok() || !db)
    {
        std::stringstream exitInfo;
        exitInfo << "Database open error, dbname = " << dbname
                 << ", status = " << status.ToString();
        ROCKSDB_LOG(ERROR) << LOG_DESC(exitInfo.str());
        errorExit(exitInfo, OpenDBFailed());
    }
    m_db.reset(db);
    return m_db;
}

Status BasicRocksDB::Get(ReadOptions const& options, std::string const& key, std::string& value)
{
    assert(m_db);
    auto status = m_db->Get(options, Slice(std::move(key)), &value);
    if (!status.ok() && !status.IsNotFound())
    {
        std::stringstream exitInfo;
        exitInfo << "Query rocksDB failed, status:" << status.ToString();
        ROCKSDB_LOG(ERROR) << LOG_DESC(exitInfo.str());
        errorExit(exitInfo, QueryDBFailed());
    }
    // decrypt value
    if (m_decryptHandler && !value.empty())
    {
        m_decryptHandler(value);
    }
    return status;
}

Status BasicRocksDB::BatchPut(WriteBatch& batch, std::string const& key, std::string const& value)
{
    auto status = batch.Put(Slice(std::move(key)), Slice(value));
    if (!status.ok() && !status.IsNotFound())
    {
        std::stringstream exitInfo;
        exitInfo << "Put key = " << key << " into rocksDB failed, status:" << status.ToString();
        ROCKSDB_LOG(ERROR) << LOG_DESC(exitInfo.str());
        errorExit(exitInfo, PutBatchFailed());
    }
    return status;
}

Status BasicRocksDB::PutWithLock(
    WriteBatch& batch, std::string const& key, std::string& value, tbb::spin_mutex& mutex)
{
    // encrypt value
    if (m_encryptHandler)
    {
        m_encryptHandler(value);
    }

    // put handled value into the batch
    tbb::spin_mutex::scoped_lock lock(mutex);
    return BatchPut(batch, key, value);
}

Status BasicRocksDB::Put(WriteBatch& batch, std::string const& key, std::string& value)
{
    // encrypt value
    if (m_encryptHandler)
    {
        m_encryptHandler(value);
    }
    // put handled value into the batch
    return BatchPut(batch, key, value);
}

Status BasicRocksDB::PutValue(
    const WriteOptions& options, std::string const& key, std::string& value)
{
    if (m_encryptHandler)
    {
        m_encryptHandler(value);
    }
    auto status = m_db->Put(options, rocksdb::Slice(key), rocksdb::Slice(value));
    if (!status.ok())
    {
        std::stringstream exitInfo;
        exitInfo << "Put Data into db failed , status:" << status.ToString();
        ROCKSDB_LOG(ERROR) << LOG_DESC(exitInfo.str());
        errorExit(exitInfo, WriteDBFailed());
    }
    return status;
}

Status BasicRocksDB::Write(WriteOptions const& options, WriteBatch& batch)
{
    auto status = m_db->Write(options, &batch);
    if (!status.ok())
    {
        std::stringstream exitInfo;
        exitInfo << "WriteDB failed , status:" << status.ToString();
        ROCKSDB_LOG(ERROR) << LOG_DESC(exitInfo.str());
        errorExit(exitInfo, WriteDBFailed());
    }
    return status;
}

}  // namespace db
}  // namespace dev
