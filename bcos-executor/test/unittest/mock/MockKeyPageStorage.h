/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file MockKeyPageStorage.h
 * @author: kyonGuo
 * @date 2022/6/28
 */

#pragma once
#include "../../src/Common.h"
#include "MockTransactionalStorage.h"
#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-table/src/KeyPageStorage.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <memory>


using namespace bcos::protocol;

namespace bcos::test
{
class MockKeyPageStorage : public bcos::storage::TransactionalStorageInterface
{
public:
    MockKeyPageStorage(bcos::crypto::Hash::Ptr hashImpl) : m_hashImpl(hashImpl)
    {
        auto pre = std::make_shared<MockTransactionalStorage>(hashImpl);
        m_inner = std::make_shared<bcos::storage::KeyPageStorage>(std::move(pre), false);
    }

    MockKeyPageStorage(bcos::crypto::Hash::Ptr hashImpl, uint32_t version,
        std::shared_ptr<std::set<std::string, std::less<>>> _ignoreTables = nullptr)
      : m_hashImpl(hashImpl)
    {
        auto pre = std::make_shared<MockTransactionalStorage>(hashImpl);
        m_inner = std::make_shared<bcos::storage::KeyPageStorage>(
            std::move(pre), false, 10240, version, _ignoreTables);
    }

    void asyncGetPrimaryKeys(std::string_view table,
        const std::optional<storage::Condition const>& _condition,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> _callback) noexcept override
    {
        m_inner->asyncGetPrimaryKeys(table, _condition, std::move(_callback));
    }

    void asyncGetRow(std::string_view table, std::string_view _key,
        std::function<void(Error::UniquePtr, std::optional<storage::Entry>)> _callback) noexcept
        override
    {
        m_inner->asyncGetRow(table, _key, std::move(_callback));
    }

    void asyncGetRows(std::string_view table,
        RANGES::any_view<std::string_view,
            RANGES::category::input | RANGES::category::random_access | RANGES::category::sized>
            keys,
        std::function<void(Error::UniquePtr, std::vector<std::optional<storage::Entry>>)>
            _callback) noexcept override
    {
        m_inner->asyncGetRows(table, keys, std::move(_callback));
    }

    void asyncSetRow(std::string_view table, std::string_view key, storage::Entry entry,
        std::function<void(Error::UniquePtr)> callback) noexcept override
    {
        m_inner->asyncSetRow(table, key, std::move(entry), std::move(callback));
    }

    void asyncOpenTable(std::string_view tableName,
        std::function<void(Error::UniquePtr, std::optional<storage::Table>)> callback) noexcept
        override
    {
        m_inner->asyncOpenTable(tableName, std::move(callback));
    }

    void asyncPrepare(const TwoPCParams& params,
        const bcos::storage::TraverseStorageInterface& storage,
        std::function<void(Error::Ptr, uint64_t, const std::string&)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);

        std::mutex mutex;
        storage.parallelTraverse(
            true, [&](const std::string_view& table, const std::string_view& key,
                      const storage::Entry& entry) {
                std::unique_lock<std::mutex> lock(mutex);

                auto keyHex = boost::algorithm::hex_lower(std::string(key));
                EXECUTOR_LOG(TRACE) << "Merge data" << LOG_KV("table", table) << LOG_KV("key", key)
                                    << LOG_KV("fields", entry.get());

                auto myTable = m_inner->openTable(table);
                if (!myTable)
                {
                    m_inner->createTable(std::string(table), std::string(executor::STORAGE_VALUE));
                    myTable = m_inner->openTable(std::string(table));
                }
                myTable->setRow(key, entry);

                return true;
            });

        callback(nullptr, 0, "");
    }

    void asyncCommit(const TwoPCParams& params,
        std::function<void(Error::Ptr, uint64_t)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);
        callback(nullptr, 0);
    }

    void asyncRollback(
        const TwoPCParams& params, std::function<void(Error::Ptr)> callback) noexcept override
    {
        BOOST_CHECK_GT(params.number, 0);
        callback(nullptr);
    }

    std::pair<size_t, Error::Ptr> count(const std::string_view& table)
    {
        return m_inner->count(table);
    }

    Error::Ptr setRows(std::string_view tableName,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            keys,
        RANGES::any_view<std::string_view,
            RANGES::category::random_access | RANGES::category::sized>
            values) noexcept override
    {
        std::promise<bool> p;
        std::atomic_int64_t count = 0;
        std::atomic_int64_t c = keys.size();
        auto collector = [&]() {
            c--;
            count++;
            if (c == 0)
            {
                p.set_value(true);
            }
        };
        for (size_t i = 0; i < keys.size(); ++i)
        {
            storage::Entry e;
            e.set(std::string(values[i]));
            asyncSetRow(
                tableName, keys[i], std::move(e), [&collector](Error::UniquePtr) { collector(); });
        }
        p.get_future().get();
        std::cout << "setRows: " << count << std::endl;
        return nullptr;
    }

    bcos::storage::KeyPageStorage::Ptr m_inner;
    bcos::crypto::Hash::Ptr m_hashImpl;
};
}  // namespace bcos::test