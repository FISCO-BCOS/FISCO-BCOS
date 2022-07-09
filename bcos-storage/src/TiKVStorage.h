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
 * @brief the header of TiKVStorage
 * @file TiKVStorage.h
 * @author: xingqiangbai
 * @date: 2021-04-16
 */

#pragma once

#include <bcos-framework/interfaces/storage/StorageInterface.h>
#include <bcos-utilities/Common.h>

namespace pingcap
{
namespace kv
{
struct Cluster;
struct BCOSTwoPhaseCommitter;
struct Snapshot;
}  // namespace kv
}  // namespace pingcap

namespace bcos
{
namespace storage
{
std::shared_ptr<pingcap::kv::Cluster> newTiKVCluster(
    const std::vector<std::string>& pdAddrs, const std::string& logPath);

class TiKVStorage : public TransactionalStorageInterface
{
public:
    using Ptr = std::shared_ptr<TiKVStorage>;
    explicit TiKVStorage(const std::shared_ptr<pingcap::kv::Cluster>& _cluster)
      : m_cluster(_cluster)
    {}

    virtual ~TiKVStorage() {}

    void asyncGetPrimaryKeys(std::string_view _table,
        const std::optional<Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept
        override;

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) noexcept override;

    void asyncGetRows(std::string_view table,
        const std::variant<const gsl::span<std::string_view const>,
            const gsl::span<std::string const>>& _keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) noexcept
        override;

    void asyncSetRow(std::string_view table, std::string_view key, Entry entry,
        std::function<void(Error::UniquePtr)> callback) noexcept override;

    void asyncPrepare(const bcos::protocol::TwoPCParams& params,
        const TraverseStorageInterface& storage,
        std::function<void(Error::Ptr, uint64_t)> callback) noexcept override;

    void asyncCommit(const bcos::protocol::TwoPCParams& params,
        std::function<void(Error::Ptr, uint64_t)> callback) noexcept override;

    void asyncRollback(const bcos::protocol::TwoPCParams& params,
        std::function<void(Error::Ptr)> callback) noexcept override;

    Error::Ptr setRows(std::string_view table, std::vector<std::string> keys,
        std::vector<std::string> values) noexcept override;

private:
    int32_t m_maxRetry = 10;
    size_t m_coroutineStackSize = 65536;  // macOS default is 128K, linux is 8K, here set 64K
    std::shared_ptr<pingcap::kv::Cluster> m_cluster;
    std::shared_ptr<pingcap::kv::BCOSTwoPhaseCommitter> m_committer;

    mutable RecursiveMutex x_committer;
};
}  // namespace storage
}  // namespace bcos
