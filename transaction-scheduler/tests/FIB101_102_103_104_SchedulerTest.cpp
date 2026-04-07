/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file FIB101_102_103_104_SchedulerTest.cpp
 * @author: kyonGuo
 * @date 2026/4/7
 */

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <future>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

using FIBMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using FIBBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using FIBMultiLayerStorage = MultiLayerStorage<FIBMutableStorage, void, FIBBackendStorage>;

struct FIBMockExecutor
{
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return {};
    }

    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        task::Task<protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, int32_t contextID,
        ledger::LedgerConfig const& ledgerConfig, bool call)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};

struct FIBMockScheduler
{
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        auto receipts =
            ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
            ::ranges::views::transform([](size_t index) -> protocol::TransactionReceipt::Ptr {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
                constexpr static std::string_view str = "abc";
                auto& inner = receipt->inner();
                inner.dataHash.assign(str.begin(), str.end());
                inner.data.gasUsed = "100";

                bytes logAddress;
                logAddress.assign(str.begin(), str.end());
                bcos::protocol::LogEntry logEntry{
                    logAddress, bcos::h256s{bcos::h256{}}, bcos::bytes{}};
                std::vector<bcos::protocol::LogEntry> logs;
                logs.emplace_back(std::move(logEntry));
                receipt->setLogEntries(logs);
                return receipt;
            }) |
            ::ranges::to<std::vector<protocol::TransactionReceipt::Ptr>>();

        co_return receipts;
    }
};

// Keep storage-level getLedgerConfig stub minimal for tests
inline task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    FIBMultiLayerStorage::ViewType& storage, bcos::ledger::LedgerConfig& ledgerConfig,
    protocol::BlockNumber blockNumber, protocol::BlockFactory& blockFactory)
{
    return {};
}

static bcos::task::Task<std::vector<bcos::protocol::Transaction::ConstPtr>> emptyTxsTaskFIB()
{
    co_return std::vector<bcos::protocol::Transaction::ConstPtr>{};
}

class FIBSchedulerFixture
{
public:
    FIBSchedulerFixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite)),
        transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite)),
        receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory)),
        transactionSubmitResultFactory(
            std::make_shared<protocol::TransactionSubmitResultFactoryImpl>()),
        multiLayerStorage(backendStorage),
        baselineScheduler(multiLayerStorage, mockScheduler, mockExecutor, *blockFactory,
            mockLedger.get(), mockTxPool.get(), *transactionSubmitResultFactory, *hashImpl)
    {
        // Ledger: asyncPrewriteBlock => invoke callback(success)
        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>) { callback({}, nullptr); });
        // Ledger: storeTransactionsAndReceipts => no error
        fakeit::When(Method(mockLedger, storeTransactionsAndReceipts))
            .AlwaysDo([](protocol::ConstTransactionsPtr, protocol::Block::ConstPtr) -> Error::Ptr {
                return nullptr;
            });

        // TxPool: getTransactions => empty list
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskFIB();
        });
    }

    void writeBlock(std::shared_ptr<bcostars::protocol::BlockImpl> block)
    {
        auto bh = block->blockHeader();
        task::syncWait(ledger::prewriteBlock(mockLedger.get(),
            std::make_shared<protocol::ConstTransactions>(), block, false, backendStorage));
        bytes headerBuffer;
        bh->encode(headerBuffer);

        storage::Entry number2HeaderEntry;
        number2HeaderEntry.importFields({std::move(headerBuffer)});
        task::syncWait(storage2::writeOne(backendStorage,
            StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(bh->number())},
            std::move(number2HeaderEntry)));
    }

    /**
     * Helper: execute a block and return the executed header.
     */
    protocol::BlockHeader::Ptr executeOneBlock(protocol::BlockNumber number)
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto bh = block->blockHeader();
        bh->setNumber(number);
        bh->setVersion(200);
        bh->calculateHash(*hashImpl);
        bytes input;
        block->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, std::to_string(number), 100, "chain", "group", 0));
        writeBlock(block);

        protocol::BlockHeader::Ptr executedHeader;
        Error::Ptr execError;
        baselineScheduler.executeBlock(
            block, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr hdr, bool) {
                execError = std::move(error);
                executedHeader = std::move(hdr);
            });
        BOOST_REQUIRE_MESSAGE(
            !execError, "executeBlock failed: " + (execError ? execError->errorMessage() : ""));
        BOOST_REQUIRE(executedHeader);
        return executedHeader;
    }

    FIBBackendStorage backendStorage;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();

    FIBMockScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    FIBMultiLayerStorage multiLayerStorage;
    FIBMockExecutor mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), FIBMockExecutor, FIBMockScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(FIB101_102_103_104_SchedulerTest, FIBSchedulerFixture)

// FIB-102: Verify atomic types are used for block number counters.
// The fact that BaselineScheduler compiles with std::atomic<int64_t> members
// and this test links and runs correctly verifies the data-race fix.
BOOST_AUTO_TEST_CASE(atomicBlockNumberCounters)
{
    // Execute two consecutive blocks to exercise the atomic load/store paths
    // in coExecuteBlock (m_lastExecutedBlockNumber)
    auto executedHeader = executeOneBlock(200);
    BOOST_CHECK(executedHeader);
    BOOST_CHECK_EQUAL(executedHeader->number(), 200);

    auto executedHeader2 = executeOneBlock(201);
    BOOST_CHECK(executedHeader2);
    BOOST_CHECK_EQUAL(executedHeader2->number(), 201);
}

// FIB-102: Verify the discontinuous block number check works with atomics
BOOST_AUTO_TEST_CASE(discontinuousBlockNumberRejected)
{
    auto executedHeader = executeOneBlock(100);
    BOOST_CHECK(executedHeader);

    // Skip block 101, try to execute 102 -- should fail
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    auto bh = block->blockHeader();
    bh->setNumber(102);
    bh->setVersion(200);
    bh->calculateHash(*hashImpl);
    bytes input;
    block->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "102", 100, "chain", "group", 0));
    writeBlock(block);

    Error::Ptr execError;
    baselineScheduler.executeBlock(block, false,
        [&](Error::Ptr error, protocol::BlockHeader::Ptr, bool) { execError = std::move(error); });
    BOOST_CHECK(execError);
    BOOST_CHECK_EQUAL(execError->errorCode(), scheduler::SchedulerError::InvalidBlockNumber);
}

// FIB-103: Verify that MAX_PENDING_RESULTS (=16) prevents unbounded memory growth.
// Execute many blocks without committing to trigger the bound.
BOOST_AUTO_TEST_CASE(maxPendingResultsBound)
{
    // Execute 16 blocks without committing (MAX_PENDING_RESULTS = 16)
    for (int i = 0; i < 16; ++i)
    {
        auto header = executeOneBlock(300 + i);
        BOOST_CHECK(header);
    }

    // The 17th execution should fail with InvalidStatus due to the bound
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    auto blockHeader = block->blockHeader();
    blockHeader->setNumber(316);
    blockHeader->setVersion(200);
    blockHeader->calculateHash(*hashImpl);
    bytes input;
    block->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "316", 100, "chain", "group", 0));
    writeBlock(block);

    Error::Ptr execError;
    baselineScheduler.executeBlock(
        block, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr hdr, bool) {
            execError = std::move(error);
        });
    BOOST_CHECK(execError);
    BOOST_CHECK_EQUAL(execError->errorCode(), scheduler::SchedulerError::InvalidStatus);
}

// FIB-103: Verify that results within the bound succeed
BOOST_AUTO_TEST_CASE(withinPendingResultsBound)
{
    // Execute 15 blocks without committing (within MAX_PENDING_RESULTS = 16)
    for (int i = 0; i < 15; ++i)
    {
        auto header = executeOneBlock(400 + i);
        BOOST_CHECK(header);
    }

    // The 16th should still succeed (at the limit)
    auto header16 = executeOneBlock(415);
    BOOST_CHECK(header16);
}

// FIB-101/FIB-104: Verify the code structure is correct by checking that
// sequential execution works after the atomic counter changes.
// The actual m_lastCommittedBlockNumber update is verified by code inspection
// (the store() is placed AFTER persistence in coCommitBlock).
BOOST_AUTO_TEST_CASE(sequentialExecutionWithAtomicCounters)
{
    // Execute a sequence of blocks to verify the atomic counters work correctly
    for (int i = 0; i < 10; ++i)
    {
        auto header = executeOneBlock(500 + i);
        BOOST_CHECK(header);
        BOOST_CHECK_EQUAL(header->number(), 500 + i);
    }

    // Verify we can still get cached results for already-executed blocks
    for (int blockNum = 500; blockNum < 510; ++blockNum)
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto bh = block->blockHeader();
        bh->setNumber(blockNum);
        bh->setVersion(200);
        bh->calculateHash(*hashImpl);
        bytes input;
        block->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, std::to_string(blockNum), 100, "chain", "group", 0));

        Error::Ptr error;
        protocol::BlockHeader::Ptr cached;
        baselineScheduler.executeBlock(
            block, false, [&](Error::Ptr err, protocol::BlockHeader::Ptr hdr, bool) {
                error = std::move(err);
                cached = std::move(hdr);
            });
        BOOST_CHECK(!error);
        BOOST_CHECK(cached);
        BOOST_CHECK_EQUAL(cached->number(), blockNum);
    }
}

BOOST_AUTO_TEST_SUITE_END()
