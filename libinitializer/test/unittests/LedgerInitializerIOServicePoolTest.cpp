/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file LedgerInitializerIOServicePoolTest.cpp
 * @brief A ledger built by LedgerInitializer commits receipts through the pool it is given.
 */
#include "libinitializer/LedgerInitializer.h"
#include <bcos-framework/testutils/faker/FakeBlock.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-utilities/IOServicePool.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include <memory>
#include <optional>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(LedgerInitializerIOServicePoolTest)

// Ledger::storeTransactionsAndReceipts writes the receipts on the ledger's IOServicePool; the
// executor-version-0 commit path (BlockExecutive::asyncCommit) reaches it for every block, so a
// ledger built without the pool cannot commit.
BOOST_AUTO_TEST_CASE(storeTransactionsAndReceiptsRunsOnTheGivenPool)
{
    auto blockFactory = createBlockFactory(createNormalCryptoSuite());
    auto dbPath =
        std::filesystem::temp_directory_path() / "LedgerInitializerIOServicePoolTest.rocksdb";
    std::filesystem::remove_all(dbPath);
    rocksdb::DB* db = nullptr;
    rocksdb::Options options;
    options.create_if_missing = true;
    BOOST_REQUIRE(rocksdb::DB::Open(options, dbPath.string(), &db).ok());
    auto storage =
        std::make_shared<bcos::storage::RocksDBStorage>(std::unique_ptr<rocksdb::DB>(db), nullptr);
    auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "ledger-test");

    auto ledger = bcos::initializer::LedgerInitializer::build(
        blockFactory, storage, nodeConfig, nullptr, ioServicePool, std::nullopt);

    auto block = blockFactory->createBlock();
    block->blockHeader()->setNumber(1);
    BOOST_CHECK(ledger->storeTransactionsAndReceipts(nullptr, block) == nullptr);

    ledger.reset();
    storage.reset();
    std::filesystem::remove_all(dbPath);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
