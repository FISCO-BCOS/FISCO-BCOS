/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief initializer for the storage
 * @file StorageInitializer.h
 * @author: yujiechen
 * @date 2021-06-11
 * @brief initializer for the storage
 * @file StorageInitializer.h
 * @author: ancelmo
 * @date 2021-10-14
 */
#pragma once
#include "bcos-storage/bcos-storage/RocksDBStorage.h"
#include "bcos-storage/bcos-storage/TiKVStorage.h"
#include "boost/filesystem.hpp"
#include "rocksdb/convenience.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/write_batch.h"
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-framework/storage/StorageInterface.h>

namespace bcos::initializer
{
class StorageInitializer
{
public:
    static auto createRocksDB(const std::string& _path, int _max_write_buffer_number = 3,
        bool _enableDBStatistics = false, int _max_background_jobs = 3)
    {
        boost::filesystem::create_directories(_path);
        rocksdb::DB* db;
        rocksdb::Options options;
        // Note: This option will increase much memory
        // options.IncreaseParallelism();
        // Note: This option will increase much memory
        // options.OptimizeLevelStyleCompaction();
        // create the DB if it's not already present
        options.create_if_missing = true;
        // to mitigate write stalls
        options.max_background_jobs = _max_background_jobs;
        options.max_write_buffer_number = _max_write_buffer_number;
        // FIXME: enable blob support when space amplification is acceptable
        // options.enable_blob_files = keyPageSize > 1 ? true : false;
        options.compression = rocksdb::kZSTD;
        options.bottommost_compression = rocksdb::kZSTD;  // last level compression
        options.max_open_files = 256;
        options.write_buffer_size = 64 << 20;  // default is 64MB
        // options.min_blob_size = 1024;

        if (_enableDBStatistics)
        {
            options.statistics = rocksdb::CreateDBStatistics();
        }
        // block cache 128MB
        std::shared_ptr<rocksdb::Cache> cache = rocksdb::NewLRUCache(128 << 20);
        rocksdb::BlockBasedTableOptions table_options;
        table_options.block_cache = cache;
        // use bloom filter to optimize point lookup, i.e. get
        table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
        table_options.optimize_filters_for_memory = true;
        options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

        if (boost::filesystem::space(_path).available < 1024 * 1024 * 500)
        {
            BCOS_LOG(INFO) << "available disk space is less than 500MB";
            throw std::runtime_error("available disk space is less than 500MB");
        }

        // open DB
        rocksdb::Status status = rocksdb::DB::Open(options, _path, &db);
        if (!status.ok())
        {
            BCOS_LOG(INFO) << LOG_DESC("open rocksDB failed") << LOG_KV("error", status.ToString());
            throw std::runtime_error("open rocksDB failed, err:" + status.ToString());
        }
        return std::unique_ptr<rocksdb::DB, std::function<void(rocksdb::DB*)>>(
            db, [](rocksdb::DB* db) {
                CancelAllBackgroundWork(db, true);
                db->Close();
                delete db;
            });
    }
    static bcos::storage::TransactionalStorageInterface::Ptr build(const std::string& _storagePath,
        const bcos::security::DataEncryptInterface::Ptr& _dataEncrypt,
        [[maybe_unused]] size_t keyPageSize = 0, int _max_write_buffer_number = 3,
        bool _enableDBStatistics = false, int _max_background_jobs = 3)
    {
        auto unique_db = createRocksDB(
            _storagePath, _max_write_buffer_number, _enableDBStatistics, _max_background_jobs);
        return std::make_shared<bcos::storage::RocksDBStorage>(std::move(unique_db), _dataEncrypt);
    }

#ifdef WITH_TIKV
    static bcos::storage::TransactionalStorageInterface::Ptr build(
        const std::vector<std::string>& _pdAddrs, const std::string& _logPath,
        const std::string& caPath = std::string(""), const std::string& certPath = std::string(""),
        const std::string& keyPath = std::string(""))
    {
        boost::filesystem::create_directories(_logPath);
        static std::shared_ptr<tikv_client::TransactionClient> cluster =
            storage::newTiKVClient(_pdAddrs, _logPath, caPath, certPath, keyPath);
        return std::make_shared<bcos::storage::TiKVStorage>(cluster);
    }
#endif
};
}  // namespace bcos::initializer