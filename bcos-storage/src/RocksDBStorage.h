/*
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
 * @brief the implement of storage
 * @file Storage.cpp
 * @author: xingqiangbai
 * @date: 2021-04-16
 * @file Storage.cpp
 * @author: ancelmo
 * @date: 2021-08-27
 */
#pragma once

#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-security/bcos-security/DataEncryption.h>
#include <rocksdb/db.h>
#include <tbb/parallel_for.h>

namespace rocksdb
{
class WriteBatch;
}

namespace bcos::storage
{
class RocksDBStorage : public TransactionalStorageInterface
{
public:
    using Ptr = std::shared_ptr<RocksDBStorage>;
    explicit RocksDBStorage(std::unique_ptr<rocksdb::DB>&& db,
        const bcos::security::DataEncryptInterface::Ptr dataEncryption);

    ~RocksDBStorage() {}

    void asyncGetPrimaryKeys(std::string_view _table,
        const std::optional<Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override;

    void asyncGetRows(std::string_view table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback)
        override;

    void asyncSetRow(std::string_view table, std::string_view key, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void asyncPrepare(const bcos::protocol::TwoPCParams& params,
        const TraverseStorageInterface& storage,
        std::function<void(Error::Ptr, uint64_t)> callback) override;

    void asyncCommit(const bcos::protocol::TwoPCParams& params,
        std::function<void(Error::Ptr, uint64_t)> callback) override;

    void asyncRollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(Error::Ptr)> callback) override;
    Error::Ptr setRows(std::string_view table, std::vector<std::string> keys,
        std::vector<std::string> values) noexcept override;

private:
    std::shared_ptr<rocksdb::WriteBatch> m_writeBatch = nullptr;
    tbb::spin_mutex m_writeBatchMutex;
    std::unique_ptr<rocksdb::DB> m_db;

    // Security Storage
    bcos::security::DataEncryptInterface::Ptr m_dataEncryption{nullptr};
};
}  // namespace bcos::storage
