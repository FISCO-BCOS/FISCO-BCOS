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
#include "bcos-storage/RocksDBStorage.h"
#include <rocksdb/statistics.h>
#ifdef WITH_TIKV
#include "bcos-storage/TiKVStorage.h"
#endif
#include "rocksdb/convenience.h"
#include "rocksdb/filter_policy.h"
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-framework/storage/StorageInterface.h>

namespace bcos::initializer
{

struct RocksDBOption
{
    int maxWriteBufferNumber = 3;
    int maxBackgroundJobs = 4;
    size_t writeBufferSize = 64 << 20;  // 64MB
    int minWriteBufferNumberToMerge = 1;
    size_t blockCacheSize = 128 << 20;  // 128MB
    bool optimizeLevelStyleCompaction = false;
    bool enable_blob_files = false;
};

class StorageInitializer
{
public:
    static auto createRocksDB(const std::string& _path, RocksDBOption& rocksDBOption,
        bool _enableDBStatistics = false, [[maybe_unused]] size_t keyPageSize = 0)
    {
        boost::filesystem::create_directories(_path);
        rocksdb::DB* db = nullptr;
        rocksdb::Options options;
        if (rocksDBOption.optimizeLevelStyleCompaction)
        {
            // Note: This option will increase much memory
            options.IncreaseParallelism();
            // Note: This option will increase much memory
            options.OptimizeLevelStyleCompaction();
        }

        // create the DB if it's not already present
        options.create_if_missing = true;
        // to mitigate write stalls
        options.max_background_jobs = rocksDBOption.maxBackgroundJobs;
        options.max_write_buffer_number = rocksDBOption.maxWriteBufferNumber;
        // FIXME: enable blob support when space amplification is acceptable
        // options.enable_blob_files = keyPageSize > 1 ? true : false;
        options.enable_blob_files = rocksDBOption.enable_blob_files;
        options.bytes_per_sync = 1 << 20;  // 1MB
        // options.level_compaction_dynamic_level_bytes = true;
        // options.compaction_pri = rocksdb::kMinOverlappingRatio;
        options.compression = rocksdb::kZSTD;
        options.bottommost_compression = rocksdb::kZSTD;  // last level compression
        options.max_open_files = 256;
        options.write_buffer_size =
            rocksDBOption.writeBufferSize;  // default is 64MB, set 256MB here
        options.min_write_buffer_number_to_merge =
            rocksDBOption.minWriteBufferNumberToMerge;  // default is 1
        options.enable_pipelined_write = true;
        // options.min_blob_size = 1024;
        options.max_bytes_for_level_base = 512 << 20;  // 512MB
        options.target_file_size_base = 128 << 20;     // 128MB

        if (_enableDBStatistics)
        {
            options.statistics = rocksdb::CreateDBStatistics();
        }
        // block cache 128MB
        std::shared_ptr<rocksdb::Cache> cache = rocksdb::NewLRUCache(rocksDBOption.blockCacheSize);
        rocksdb::BlockBasedTableOptions table_options;
        table_options.block_cache = cache;
        // use bloom filter to optimize point lookup, i.e. get
        table_options.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10, false));
        table_options.optimize_filters_for_memory = true;
        table_options.block_size = 64 * 1024;
        // table_options.cache_index_and_filter_blocks = true; // this will increase memory and
        // lower performance
        options.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
        if (boost::filesystem::space(_path).available < 1 << 30)
        {
            BCOS_LOG(INFO) << "available disk space is less than 1GB";
            throw std::runtime_error("available disk space is less than 1GB");
        }

        // open DB
        rocksdb::Status status = rocksdb::DB::Open(options, _path, &db);
        if (!status.ok())
        {
            BCOS_LOG(INFO) << LOG_DESC("open rocksDB failed")
                           << LOG_KV("message", status.ToString());
            throw std::runtime_error("open rocksDB failed, msg:" + status.ToString());
        }
        return std::unique_ptr<rocksdb::DB, std::function<void(rocksdb::DB*)>>(
            db, [](rocksdb::DB* rocksDB) {
                CancelAllBackgroundWork(rocksDB, true);
                rocksDB->Close();
                delete rocksDB;
            });
    }
    static bcos::storage::TransactionalStorageInterface::Ptr build(
        auto&& rocksDB, const bcos::security::DataEncryptInterface::Ptr& _dataEncrypt)
    {
        return std::make_shared<bcos::storage::RocksDBStorage>(
            std::forward<decltype(rocksDB)>(rocksDB), _dataEncrypt);
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