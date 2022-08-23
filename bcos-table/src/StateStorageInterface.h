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
 * @brief interface of StateStorage, TableFactory in FISCO BCOS 2.0
 * @file Table.h
 * @author: xingqiangbai
 * @date: 2022-04-19
 */
#pragma once

#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage/Table.h"
#include "tbb/enumerable_thread_specific.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Error.h>
#include <boost/throw_exception.hpp>
#include <future>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <type_traits>

namespace bcos::storage
{
class Recoder
{
public:
    using Ptr = std::shared_ptr<Recoder>;
    using ConstPtr = std::shared_ptr<Recoder>;

    struct Change
    {
        Change(std::string _table, std::string _key, std::optional<Entry> _entry)
          : table(std::move(_table)), key(std::move(_key)), entry(std::move(_entry))
        {}
        Change(const Change&) = delete;
        Change& operator=(const Change&) = delete;
        Change(Change&&) noexcept = default;
        Change& operator=(Change&&) noexcept = default;

        std::string table;
        std::string key;
        std::optional<Entry> entry;
    };

    void log(Change&& change) { m_changes.emplace_front(std::move(change)); }
    auto begin() const { return m_changes.cbegin(); }
    auto end() const { return m_changes.cend(); }
    void clear() { m_changes.clear(); }

private:
    std::list<Change> m_changes;
};

class StateStorageInterface : public virtual storage::TraverseStorageInterface

{
public:
    using Ptr = std::shared_ptr<StateStorageInterface>;
    StateStorageInterface(std::shared_ptr<StorageInterface> prev)
      : storage::TraverseStorageInterface(), m_prev(std::move(prev)){};
    virtual std::optional<Table> openTable(const std::string_view& tableView)
    {
        std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> openPromise;
        asyncOpenTable(tableView, [&](auto&& error, auto&& table) {
            openPromise.set_value({std::move(error), std::move(table)});
        });

        auto [error, table] = openPromise.get_future().get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
        return table;
    }

    virtual std::optional<Table> createTable(std::string _tableName, std::string _valueFields)
    {
        std::promise<std::tuple<Error::UniquePtr, std::optional<Table>>> createPromise;
        asyncCreateTable(
            _tableName, _valueFields, [&](Error::UniquePtr&& error, std::optional<Table>&& table) {
                createPromise.set_value({std::move(error), std::move(table)});
            });
        auto [error, table] = createPromise.get_future().get();
        if (error)
        {
            BOOST_THROW_EXCEPTION(*error);
        }
        return table;
    }

    virtual crypto::HashType hash(const bcos::crypto::Hash::Ptr& hashImpl) const = 0;
    virtual void setPrev(std::shared_ptr<StorageInterface> prev)
    {
        std::unique_lock<std::shared_mutex> lock(m_prevMutex);
        m_prev = std::move(prev);
    }
    virtual void rollback(const Recoder& recoder) = 0;
    virtual void setRecoder(typename Recoder::Ptr recoder) { m_recoder.local().swap(recoder); }
    virtual void setReadOnly(bool readOnly) { m_readOnly = readOnly; }

protected:
    bool m_readOnly = false;
    tbb::enumerable_thread_specific<typename Recoder::Ptr> m_recoder;
    std::shared_ptr<StorageInterface> m_prev;
    std::shared_mutex m_prevMutex;
};


}  // namespace bcos::storage
