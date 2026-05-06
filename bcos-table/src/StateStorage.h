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
 * @brief interface of Table
 * @file Table.h
 * @author: xingqiangbai
 * @date: 2021-04-07
 * @brief interface of Table
 * @file StateStorage.h
 * @author: ancelmo
 * @date: 2021-09-01
 */
#pragma once

#include "StateStorageInterface.h"
#include "fmt/format.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Error.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/core/ignore_unused.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/key.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <mutex>

namespace bcos::storage
{
template <bool enableLRU = false>
class BaseStorage : public virtual storage::StateStorageInterface,
                    public virtual storage::MergeableStorageInterface
{
public:
    using Ptr = std::shared_ptr<BaseStorage<enableLRU>>;

        BaseStorage(std::shared_ptr<StorageInterface> prev, bool setRowWithDirtyFlag);

    BaseStorage(const BaseStorage&) = delete;
    BaseStorage& operator=(const BaseStorage&) = delete;

    BaseStorage(BaseStorage&&) = delete;
    BaseStorage& operator=(BaseStorage&&) = delete;

    ~BaseStorage() override;

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) override;

    void asyncGetRow(std::string_view tableView, std::string_view keyView,
        std::function<void(Error::UniquePtr, std::optional<Entry>)> _callback) override;

    void asyncGetRows(std::string_view tableView,
        ::ranges::any_view<std::string_view,
            ::ranges::category::input | ::ranges::category::random_access |
                ::ranges::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<Entry>>)> _callback) override;

    void asyncSetRow(std::string_view tableView, std::string_view keyView, Entry entry,
        std::function<void(Error::UniquePtr)> callback) override;

    void parallelTraverse(bool onlyDirty, std::function<bool(const std::string_view& table,
                                              const std::string_view& key, const Entry& entry)>
                                              callback) const override;

    void merge(bool onlyDirty, const TraverseStorageInterface& source) override;

    crypto::HashType hash(
        const bcos::crypto::Hash::Ptr& hashImpl, const ledger::Features& features) const override;


    void rollback(const Recoder& recoder) override;

    void setEnableTraverse(bool enableTraverse);
    void setMaxCapacity(ssize_t capacity);

private:
    Entry importExistingEntry(std::string_view table, std::string_view key, Entry entry);

    std::shared_ptr<StorageInterface> getPrev();

    bool m_enableTraverse = false;

    constexpr static int64_t DEFAULT_CAPACITY = 32L * 1024 * 1024;
    int64_t m_maxCapacity = DEFAULT_CAPACITY;

    struct Data
    {
        std::string table;
        std::string key;
        Entry entry;

        std::tuple<std::string_view, std::string_view> view() const
        {
            return std::make_tuple(std::string_view(table), std::string_view(key));
        }
    };

    using HashContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<boost::multi_index::ordered_unique<boost::multi_index::
                const_mem_fun<Data, std::tuple<std::string_view, std::string_view>, &Data::view>>>>;
    using LRUHashContainer = boost::multi_index_container<Data,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::const_mem_fun<Data,
                std::tuple<std::string_view, std::string_view>, &Data::view>>,
            boost::multi_index::sequenced<>>>;
    using Container = std::conditional_t<enableLRU, LRUHashContainer, HashContainer>;

    struct Bucket
    {
        Container container;
        std::mutex mutex;
        ssize_t capacity = 0;
    };
    uint32_t m_blockVersion = 0;
    std::vector<Bucket> m_buckets;
    bool m_setRowWithDirtyFlag = false;

    std::tuple<Bucket*, std::unique_lock<std::mutex>> getBucket(
        const std::string_view& table, const std::string_view& key);

    void updateMRUAndCheck(Bucket& bucket, typename Container::iterator it);
};

using StateStorage = BaseStorage<false>;
using LRUStateStorage = BaseStorage<true>;

extern template class BaseStorage<false>;
extern template class BaseStorage<true>;

}  // namespace bcos::storage
