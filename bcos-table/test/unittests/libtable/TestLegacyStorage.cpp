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

struct LegacyStorageTestFixture
{
    bcos::storage2::memory_storage::MemoryStorage<bcos::transaction_executor::StateKey,
        bcos::storage::Entry,
        bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                                  bcos::storage2::memory_storage::LOGICAL_DELETION)>
        storage;
};

BOOST_FIXTURE_TEST_SUITE(LegacyStorageTest, LegacyStorageTestFixture)

BOOST_AUTO_TEST_CASE(getPrimaryKeys)
{
    bcos::task::syncWait([this]() -> bcos::task::Task<void> {
        co_await bcos::storage2::writeSome(storage,
            RANGES::views::iota(0, 10) | RANGES::views::transform([](int i) {
                return bcos::transaction_executor::StateKey("t_test", std::to_string(i));
            }),
            RANGES::views::repeat(bcos::storage::Entry("t_test")));

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

        co_await bcos::storage2::removeOne(
            storage, bcos::transaction_executor::StateKeyView("t_test", "5"));

        legacyStorage.asyncGetPrimaryKeys(
            "t_test", condition, [](bcos::Error::UniquePtr error, std::vector<std::string> keys) {
                BOOST_CHECK(!error);
                BOOST_CHECK(!keys.empty());
                BOOST_CHECK_EQUAL(keys.size(), 4);
                auto expectedKeys = std::vector<std::string>{"6", "7", "8", "9"};
                BOOST_CHECK_EQUAL_COLLECTIONS(
                    keys.begin(), keys.end(), expectedKeys.begin(), expectedKeys.end());
            });
    }());
}

BOOST_AUTO_TEST_SUITE_END()