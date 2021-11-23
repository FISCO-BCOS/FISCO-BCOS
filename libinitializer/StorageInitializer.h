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
#include <bcos-framework/interfaces/storage/StorageInterface.h>
#include <bcos-storage/RocksDBStorage.h>
#include <rocksdb/write_batch.h>

namespace bcos::initializer
{
class StorageInitializer
{
public:
    static bcos::storage::TransactionalStorageInterface::Ptr build(std::string const& _storagePath)
    {
        boost::filesystem::create_directories(_storagePath);
        rocksdb::DB* db;
        rocksdb::Options options;
        // Optimize RocksDB. This is the easiest way to get RocksDB to perform well
        options.IncreaseParallelism();
        options.OptimizeLevelStyleCompaction();
        // create the DB if it's not already present
        options.create_if_missing = true;

        // open DB
        rocksdb::Status s = rocksdb::DB::Open(options, _storagePath, &db);

        return std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db));
    }
};
}  // namespace bcos::initializer