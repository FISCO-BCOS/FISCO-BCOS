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
#include "boost/filesystem.hpp"
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-storage/src/RocksDBStorage.h>
#include <bcos-storage/src/TiKVStorage.h>
#include <rocksdb/write_batch.h>

namespace bcos::initializer
{
class StorageInitializer
{
public:
    static bcos::storage::TransactionalStorageInterface::Ptr build(const std::string& _storagePath,
        const bcos::security::DataEncryptInterface::Ptr _dataEncrypt, size_t keyPageSize = 0)
    {
        // FIXME: enable compress
        boost::filesystem::create_directories(_storagePath);
        rocksdb::DB* db;
        rocksdb::Options options;
        // Note: This option will increase much memory
        // options.IncreaseParallelism();
        // Note: This option will increase much memory
        // options.OptimizeLevelStyleCompaction();
        // create the DB if it's not already present
        options.create_if_missing = true;
        options.enable_blob_files = keyPageSize > 1 ? true : false;
        options.compression = rocksdb::kZSTD;
        // options.min_blob_size = 1024;

        if (boost::filesystem::space(_storagePath).available < 1024 * 1024 * 100)
        {
            BCOS_LOG(INFO) << "available disk space is less than 100MB";
            throw std::runtime_error("available disk space is less than 100MB");
        }

        // open DB
        rocksdb::Status s = rocksdb::DB::Open(options, _storagePath, &db);
        if (!s.ok())
        {
            BCOS_LOG(INFO) << LOG_DESC("open rocksDB failed") << LOG_KV("error", s.ToString());
            throw std::runtime_error("open rocksDB failed, err:" + s.ToString());
        }
        return std::make_shared<bcos::storage::RocksDBStorage>(
            std::unique_ptr<rocksdb::DB>(db), _dataEncrypt);
    }

#ifdef WITH_TIKV
    static bcos::storage::TransactionalStorageInterface::Ptr build(
        const std::vector<std::string>& _pdAddrs, const std::string& _logPath)
    {
        boost::filesystem::create_directories(_logPath);
        auto cluster = storage::newTiKVCluster(_pdAddrs, _logPath);

        return std::make_shared<bcos::storage::TiKVStorage>(cluster);
    }
#endif
};
}  // namespace bcos::initializer