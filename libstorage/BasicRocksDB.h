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

#pragma once
#include <libdevcore/Common.h>
#include <libdevcrypto/CryptoInterface.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>
#include <tbb/spin_mutex.h>
#include <memory>
#include <string>
#include <vector>

#define ROCKSDB_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("ROCKSDB")
#define DecHookFunction std::function<void(std::string&)>
#define EncHookFunction std::function<void(std::string const&, std::string&)>

namespace dev
{
namespace storage
{
class BasicRocksDB
{
public:
    using Ptr = std::shared_ptr<BasicRocksDB>;
    BasicRocksDB() {}
    virtual ~BasicRocksDB() { closeDB(); }

    // open rocksDB with the given option
    // if rocksDB is opened successfully, return the DB handler
    // if open rocksDB failed, throw exception, and stop the program directly
    virtual void Open(const rocksdb::Options& options, const std::string& dbname);

    // get value from rocksDB according to the given key
    // if query successfully, return query status
    // if query failed, throw exception and exit directly
    virtual rocksdb::Status Get(
        rocksdb::ReadOptions const& options, std::string const& key, std::string& value);

    // common Put interface, put the given (key, value) into batch
    virtual rocksdb::Status Put(
        rocksdb::WriteBatch& batch, std::string const& key, std::string const& value);

    // callback hook function, and put (key, value) into batch later
    // for performance consideration, hook is called without lock, batch put is called with lock
    virtual rocksdb::Status PutWithLock(rocksdb::WriteBatch& batch, std::string const& key,
        std::string const& value, tbb::spin_mutex& mutex);

    virtual rocksdb::Status Write(
        rocksdb::WriteOptions const& options, rocksdb::WriteBatch& updates);

    virtual void setEncryptHandler(EncHookFunction const& encryptHandler)
    {
        m_encryptHandler = encryptHandler;
    }

    virtual void setDecryptHandler(DecHookFunction const& decryptHandler)
    {
        m_decryptHandler = decryptHandler;
    }

    void closeDB();
    void flush();

protected:
    void checkStatus(rocksdb::Status const& status, std::string const& path = "");
    rocksdb::Status BatchPut(
        rocksdb::WriteBatch& batch, std::string const& key, std::string const& value);

    std::unique_ptr<rocksdb::DB> m_db;
    EncHookFunction m_encryptHandler = nullptr;
    DecHookFunction m_decryptHandler = nullptr;
};
rocksdb::Options getRocksDBOptions();
std::function<void(std::string const&, std::string&)> getEncryptHandler(
    const std::vector<uint8_t>& _encryptKey);
std::function<void(std::string&)> getDecryptHandler(const std::vector<uint8_t>& _encryptKey);
}  // namespace storage
}  // namespace dev
