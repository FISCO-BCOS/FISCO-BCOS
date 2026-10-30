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
#include <bcos-ledger/Ledger.h>
#include <bcos-storage/RocksDBStorage.h>
#include <bcos-utilities/IOServicePool.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(LedgerInitializerIOServicePoolTest)

namespace
{
// executor_version defaults to 0. The pool size is the node's thread_pool.io_thread_count.
struct TestLedger
{
    std::filesystem::path dbPath;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcos::protocol::BlockFactory::Ptr blockFactory;
    std::shared_ptr<bcos::storage::RocksDBStorage> storage;
    std::shared_ptr<bcos::IOServicePool> ioServicePool;
    std::shared_ptr<bcos::ledger::Ledger> ledger;

    TestLedger(std::filesystem::path path, size_t threads, std::string poolName)
      : dbPath(std::move(path)),
        cryptoSuite(createNormalCryptoSuite()),
        blockFactory(createBlockFactory(cryptoSuite))
    {
        std::filesystem::remove_all(dbPath);
        rocksdb::DB* db = nullptr;
        rocksdb::Options options;
        options.create_if_missing = true;
        BOOST_REQUIRE(rocksdb::DB::Open(options, dbPath.string(), &db).ok());
        storage = std::make_shared<bcos::storage::RocksDBStorage>(
            std::unique_ptr<rocksdb::DB>(db), nullptr);
        auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>();
        ioServicePool = std::make_shared<bcos::IOServicePool>(threads, std::move(poolName));
        ledger = bcos::initializer::LedgerInitializer::build(
            blockFactory, storage, nodeConfig, nullptr, ioServicePool, std::nullopt);
    }

    ~TestLedger()
    {
        ledger.reset();
        ioServicePool.reset();
        storage.reset();
        std::filesystem::remove_all(dbPath);
    }

    TestLedger(TestLedger const&) = delete;
    TestLedger& operator=(TestLedger const&) = delete;
};

bcos::protocol::Block::Ptr blockWithOneReceipt(
    TestLedger& fixture, bcos::protocol::BlockNumber number)
{
    return fakeAndCheckBlock(fixture.cryptoSuite, fixture.blockFactory, 1, 1, number, true, false);
}

void checkReceiptStored(TestLedger& fixture, bcos::protocol::Block::Ptr const& block)
{
    auto hash = block->transactions()[0]->hash();
    std::string key(reinterpret_cast<char const*>(hash.data()), hash.size());
    auto [readError, entry] = fixture.storage->getRow(bcos::ledger::SYS_HASH_2_RECEIPT, key);
    BOOST_REQUIRE(readError == nullptr);
    BOOST_REQUIRE(entry.has_value());
    bcos::bytes expected;
    block->receipts()[0]->encode(expected);
    std::string_view expectedView;
    if (!expected.empty())
    {
        expectedView =
            std::string_view(reinterpret_cast<char const*>(expected.data()), expected.size());
    }
    BOOST_CHECK(entry->get() == expectedView);
}

// Joining a pool whose only worker is blocked inside storeTransactionsAndReceipts never
// returns. Keep that worker's dependencies alive and fail the case instead.
void failIfWorkerBlocked(
    TestLedger& fixture, std::future<bcos::Error::Ptr>& future, char const* why)
{
    if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready)
    {
        return;
    }
    // ~IOServicePool joins its workers. A worker blocked in this call would never finish.
    auto* keepPool = new auto(fixture.ioServicePool);
    auto* keepLedger = new auto(fixture.ledger);
    auto* keepStorage = new auto(fixture.storage);
    (void)keepPool;
    (void)keepLedger;
    (void)keepStorage;
    BOOST_FAIL(why);
}
}  // namespace

// Ledger::storeTransactionsAndReceipts writes the receipts on the ledger's IOServicePool; the
// executor-version-0 commit path (BlockExecutive::asyncCommit) reaches it for every block, so a
// ledger built without the pool cannot commit.
BOOST_AUTO_TEST_CASE(storeTransactionsAndReceiptsRunsOnTheGivenPool)
{
    TestLedger fixture(
        std::filesystem::temp_directory_path() / "LedgerInitializerIOServicePoolTest.rocksdb", 1,
        "ledger-test");
    auto block = blockWithOneReceipt(fixture, 1);
    BOOST_CHECK(fixture.ledger->storeTransactionsAndReceipts(nullptr, block) == nullptr);
    checkReceiptStored(fixture, block);

    auto empty = fixture.blockFactory->createBlock();
    empty->blockHeader()->setNumber(2);
    BOOST_CHECK(fixture.ledger->storeTransactionsAndReceipts(nullptr, empty) == nullptr);
}

// Reviewer reproduction: call storeTransactionsAndReceipts from a worker of a one-thread
// pool, the thread_pool.io_thread_count=1 configuration of a v0 node. PBFT reaches this
// function the same way — LedgerStorage posts commitStableCheckPoint onto a strand of the
// node IO pool, and that strand runs on the pool.
BOOST_AUTO_TEST_CASE(storeTransactionsAndReceiptsFromPoolWorkerDoesNotDeadlock)
{
    TestLedger fixture(std::filesystem::temp_directory_path() /
                           "LedgerInitializerIOServicePoolTest.worker.rocksdb",
        1, "ledger-v0");
    auto block = blockWithOneReceipt(fixture, 1);
    std::promise<bcos::Error::Ptr> fromWorker;
    fixture.ioServicePool->post([&]() {
        fromWorker.set_value(fixture.ledger->storeTransactionsAndReceipts(nullptr, block));
    });
    auto workerFuture = fromWorker.get_future();
    failIfWorkerBlocked(fixture, workerFuture,
        "storeTransactionsAndReceipts blocked on the one-thread IOServicePool worker");
    BOOST_CHECK(workerFuture.get() == nullptr);
    checkReceiptStored(fixture, block);

    auto strandBlock = blockWithOneReceipt(fixture, 2);
    bcos::Strand strand(fixture.ioServicePool);
    std::promise<bcos::Error::Ptr> fromStrand;
    strand.post([&]() {
        fromStrand.set_value(fixture.ledger->storeTransactionsAndReceipts(nullptr, strandBlock));
    });
    auto strandFuture = fromStrand.get_future();
    failIfWorkerBlocked(
        fixture, strandFuture, "storeTransactionsAndReceipts blocked on the PBFT commit strand");
    BOOST_CHECK(strandFuture.get() == nullptr);
    checkReceiptStored(fixture, strandBlock);
}

// SYS_HASH_2_TX fails after the receipt write has been handed to the pool. The function
// returns without waiting; the receipt task must still observe the encoded bytes.
class TxWriteFailsStorage : public bcos::storage::StorageInterface
{
public:
    std::atomic<bool> callerReturned{false};
    std::promise<void> receiptWriteFinished;
    std::string receiptValue;

    void asyncGetPrimaryKeys(std::string_view, std::optional<bcos::storage::Condition const> const&,
        std::function<void(Error::UniquePtr, std::vector<std::string>)> callback) override
    {
        callback(nullptr, {});
    }

    void asyncGetRow(std::string_view, std::string_view,
        std::function<void(Error::UniquePtr, std::optional<bcos::storage::Entry>)> callback)
        override
    {
        callback(nullptr, std::nullopt);
    }

    void asyncGetRows(std::string_view,
        ::ranges::any_view<std::string_view, ::ranges::category::input |
                                                 ::ranges::category::random_access |
                                                 ::ranges::category::sized>,
        std::function<void(Error::UniquePtr, std::vector<std::optional<bcos::storage::Entry>>)>
            callback) override
    {
        callback(nullptr, {});
    }

    void asyncSetRow(std::string_view, std::string_view, bcos::storage::Entry,
        std::function<void(Error::UniquePtr)> callback) override
    {
        callback(nullptr);
    }

    Error::Ptr setRows(std::string_view tableName,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>,
        ::ranges::any_view<std::string_view,
            ::ranges::category::random_access | ::ranges::category::sized>
            values) override
    {
        if (tableName == bcos::ledger::SYS_HASH_2_TX)
        {
            return BCOS_ERROR_PTR(bcos::ledger::LedgerError::ErrorCommitBlock, "tx write failed");
        }
        if (tableName == bcos::ledger::SYS_HASH_2_RECEIPT)
        {
            auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!callerReturned.load(std::memory_order_acquire))
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    receiptWriteFinished.set_value();
                    return BCOS_ERROR_PTR(bcos::ledger::LedgerError::UnknownError,
                        "receipt task timed out before the caller returned");
                }
                std::this_thread::yield();
            }
            for (auto value : values)
            {
                receiptValue = std::string(value);
                break;
            }
            receiptWriteFinished.set_value();
        }
        return nullptr;
    }
};

BOOST_AUTO_TEST_CASE(storeTransactionsAndReceiptsKeepsReceiptBytesAliveWhenTxWriteFails)
{
    auto cryptoSuite = createNormalCryptoSuite();
    auto blockFactory = createBlockFactory(cryptoSuite);
    auto block = fakeAndCheckBlock(cryptoSuite, blockFactory, 1, 1, 1, true, false);
    bcos::bytes expected;
    block->receipts()[0]->encode(expected);

    auto storage = std::make_shared<TxWriteFailsStorage>();
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "ledger-txfail");
    auto ledger = std::make_shared<bcos::ledger::Ledger>(blockFactory, storage, 0, nullptr,
        bcos::ledger::Ledger::c_defaultMerkleTreeCacheSize, ioServicePool);
    auto receiptFinished = storage->receiptWriteFinished.get_future();

    // Unblock a receipt task still waiting inside setRows before the pool is joined.
    struct ReleaseReceiptTask
    {
        TxWriteFailsStorage& storage;
        std::future<void>& finished;
        ~ReleaseReceiptTask()
        {
            storage.callerReturned.store(true, std::memory_order_release);
            if (finished.valid())
            {
                finished.wait_for(std::chrono::seconds(5));
            }
        }
    } release{*storage, receiptFinished};
    (void)release;

    auto err = ledger->storeTransactionsAndReceipts(nullptr, block);
    // Reuse the freed receipt buffer if the task does not own it. The fill stays
    // alive until the task has copied, so a dangling view observes the sentinel.
    std::vector<bcos::bytes> poison;
    poison.reserve(64);
    for (int i = 0; i < 64; ++i)
    {
        poison.emplace_back(expected.size(), static_cast<bcos::byte>(0xA5));
    }
    storage->callerReturned.store(true, std::memory_order_release);

    BOOST_REQUIRE(receiptFinished.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    BOOST_REQUIRE(err);
    BOOST_CHECK_EQUAL(
        err->errorCode(), static_cast<int64_t>(bcos::ledger::LedgerError::ErrorCommitBlock));
    // Touch the sentinel buffers so the fill cannot be deleted as a dead store.
    std::size_t sentinel = 0;
    for (auto const& buf : poison)
    {
        sentinel += buf.empty() ? 0 : buf.front();
    }
    std::string expectedText;
    if (!expected.empty())
    {
        BOOST_CHECK_EQUAL(sentinel, poison.size() * static_cast<std::size_t>(0xA5));
        expectedText.assign(reinterpret_cast<char const*>(expected.data()), expected.size());
    }
    BOOST_CHECK(storage->receiptValue == expectedText);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
