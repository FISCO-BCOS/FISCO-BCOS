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
 * @brief Unit tests for the Table
 * @file Table.cpp
 */

#include "bcos-framework/storage/Common.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/Wait.h"
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <tuple>

struct LegacyStorageTestFixture
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey, bcos::storage::Entry,
        bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                                  bcos::storage2::memory_storage::LOGICAL_DELETION)>
        storage;
};

BOOST_FIXTURE_TEST_SUITE(LegacyStorageTest, LegacyStorageTestFixture)

BOOST_AUTO_TEST_CASE(getPrimaryKeys)
{
    bcos::task::syncWait([this]() -> bcos::task::Task<void> {
        co_await bcos::storage2::writeSome(
            storage, ::ranges::views::iota(0, 10) | ::ranges::views::transform([](int i) {
                auto key = bcos::executor_v1::StateKey("t_test", std::to_string(i));
                return std::make_tuple(key, bcos::storage::Entry("t_test"));
            }));

        bcos::storage::LegacyStorageWrapper legacyStorage(storage);

        bcos::storage::Condition condition;
        condition.GT("5");
        legacyStorage.asyncGetPrimaryKeys(
            "t_test", condition, [](bcos::Error::UniquePtr error, std::vector<std::string> keys) {
                BOOST_CHECK(!error);
                BOOST_CHECK(!keys.empty());
                BOOST_CHECK_EQUAL(keys.size(), 4);
                auto expectedKeys = std::vector<std::string>{"6", "7", "8", "9"};
                BOOST_CHECK_EQUAL_COLLECTIONS(
                    keys.begin(), keys.end(), expectedKeys.begin(), expectedKeys.end());
            });

        bcos::storage::Entry deletedEntry;
        deletedEntry.setStatus(bcos::storage::Entry::DELETED);
        legacyStorage.asyncSetRow(
            "t_test", "9", deletedEntry, [](bcos::Error::UniquePtr error) { BOOST_CHECK(!error); });

        legacyStorage.asyncGetPrimaryKeys(
            "t_test", {}, [](bcos::Error::UniquePtr error, std::vector<std::string> keys) {
                BOOST_CHECK(!error);
                BOOST_CHECK(!keys.empty());
                BOOST_CHECK_EQUAL(keys.size(), 9);
                auto expectedKeys = std::vector<std::string>{
                    "0",
                    "1",
                    "2",
                    "3",
                    "4",
                    "5",
                    "6",
                    "7",
                    "8",
                };
                BOOST_CHECK_EQUAL_COLLECTIONS(
                    keys.begin(), keys.end(), expectedKeys.begin(), expectedKeys.end());
            });
    }());
}

BOOST_AUTO_TEST_CASE(balanceCase)
{
    bcos::task::syncWait([this]() -> bcos::task::Task<void> {
        constexpr static auto keys = std::to_array<std::string_view>(
            {"00000000000000000000000000000195cb2ccc38", "57e8f689daed377a69915c0ddce26da288f3aa65",
                "893de00e32b0765e03366ce8b3bfcd0404b24995",
                "d24180cc0fef2f3e545de4f9aafc09345cd08903"});

        auto otherKeys = ::ranges::views::iota(0, 496) | ::ranges::views::transform([](int i) {
            return fmt::format("{:#040x}", i);
        });

        auto totalKeys =
            ::ranges::views::concat(::ranges::views::transform(keys,
                                        [](std::string_view view) { return std::string(view); }),
                otherKeys) |
            ::ranges::to<std::vector>();
        ::ranges::sort(totalKeys);

        using namespace std::string_literals;
        co_await bcos::storage2::writeSome(
            storage, totalKeys | ::ranges::views::transform([](std::string keyView) {
                auto key = bcos::executor_v1::StateKey("s_balance_caller"s, std::move(keyView));
                return std::make_tuple(key, bcos::storage::Entry("1"));
            }));

        bcos::storage::LegacyStorageWrapper legacyStorage(storage);

        legacyStorage.asyncGetPrimaryKeys("s_balance_caller", {},
            [&](bcos::Error::UniquePtr error, std::vector<std::string> gotKeys) {
                BOOST_CHECK(!error);
                BOOST_CHECK(!gotKeys.empty());
                BOOST_CHECK_EQUAL(gotKeys.size(), 500);
                ::ranges::sort(gotKeys);
                BOOST_CHECK_EQUAL_COLLECTIONS(
                    gotKeys.begin(), gotKeys.end(), totalKeys.begin(), totalKeys.end());
            });

        co_await bcos::storage2::removeSome(
            storage, ::ranges::views::transform(otherKeys, [](std::string view) {
                return bcos::executor_v1::StateKey("s_balance_caller", view);
            }));

        bcos::storage::Entry deletedEntry;
        deletedEntry.setStatus(bcos::storage::Entry::DELETED);
        legacyStorage.asyncSetRow("s_balance_caller", "d24180cc0fef2f3e545de4f9aafc09345cd08903",
            deletedEntry, [](bcos::Error::UniquePtr error) { BOOST_CHECK(!error); });

        legacyStorage.asyncGetPrimaryKeys("s_balance_caller", {},
            [](bcos::Error::UniquePtr error, std::vector<std::string> gotKeys) {
                BOOST_CHECK(!error);
                BOOST_CHECK(!gotKeys.empty());
                BOOST_CHECK_EQUAL(gotKeys.size(), 3);
                auto expectedKeys =
                    std::vector<std::string>{"00000000000000000000000000000195cb2ccc38",
                        "57e8f689daed377a69915c0ddce26da288f3aa65",
                        "893de00e32b0765e03366ce8b3bfcd0404b24995"};
                BOOST_CHECK_EQUAL_COLLECTIONS(
                    gotKeys.begin(), gotKeys.end(), expectedKeys.begin(), expectedKeys.end());
            });
    }());
}

BOOST_AUTO_TEST_SUITE_END()