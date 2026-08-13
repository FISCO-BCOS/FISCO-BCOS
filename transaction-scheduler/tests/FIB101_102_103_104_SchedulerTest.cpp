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

#include "TrivialCheckpointStorage.h"
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

// Wrap the entire fixture / mock surface in an anonymous namespace so the
// `using namespace bcos::*` directives below do not leak into other unity-build
// translation units. This keeps the file unity-build-friendly and lets the
// tests live alongside testBaselineScheduler.cpp without symbol conflicts.
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

using FIBMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using FIBBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using FIBCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, FIBBackendStorage>;
using FIBMultiLayerStorage = MultiLayerStorage<FIBMutableStorage, void, FIBCheckpointBackend>;

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
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
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

// Storage-level getLedgerConfig stub for tests. Found via ADL when the
// BaselineScheduler template is instantiated; clang's -Wunused-function can't
// see indirect template uses, so [[maybe_unused]] silences a false positive.
[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    FIBMultiLayerStorage::ViewType& /*storage*/, bcos::ledger::LedgerConfig& /*ledgerConfig*/,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    return {};
}

bcos::task::Task<std::vector<bcos::protocol::Transaction::ConstPtr>> emptyTxsTaskFIB()
{
    co_return std::vector<bcos::protocol::Transaction::ConstPtr>{};
}

class FIBSchedulerFixture
{
public:
    FIBSchedulerFixture()
      : checkpointBackend(backendStorage),
        cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
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
        multiLayerStorage(checkpointBackend),
        baselineScheduler(multiLayerStorage, mockScheduler, mockExecutor, *blockFactory,
            mockLedger.get(), mockTxPool.get(), *transactionSubmitResultFactory, *hashImpl)
    {
        // Ledger: asyncPrewriteBlock => invoke callback(success)
        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([this](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>,
                          bool) {
                if (prewriteBlockFails)
                {
                    callback({},
                        BCOS_ERROR_PTR(scheduler::SchedulerError::UnknownError, prewriteFailure));
                    return;
                }
                callback({}, nullptr);
            });
        // FIB-104: storeTransactionsAndReceipts is no longer called by coCommitBlock
        // (the unified prewriteBlockToBuffer path writes txs/receipts directly into
        // the prewrite buffer). The mock is intentionally not registered.

        // TxPool: getTransactions => empty list
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskFIB();
        });

        fakeit::When(Method(mockLedger, asyncGetBlockNumber))
            .AlwaysDo([this](std::function<void(Error::Ptr, protocol::BlockNumber)> callback) {
                callback(nullptr, ledgerBlockNumber);
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
        number2HeaderEntry.set(std::move(headerBuffer));
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
    FIBCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    protocol::BlockNumber ledgerBlockNumber = -1;
    bool prewriteBlockFails = false;
    std::string prewriteFailure = "injected prewrite failure";

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

// FIB-102: Two consecutive executes succeed under the mutex-owned counter
// regime — exercises the m_executeMutex-protected read/write of
// m_lastExecutedBlockNumber on the discontinuity-check fast path.
BOOST_AUTO_TEST_CASE(consecutiveExecutesAdvanceCounter)
{
    auto executedHeader = executeOneBlock(200);
    BOOST_CHECK(executedHeader);
    BOOST_CHECK_EQUAL(executedHeader->number(), 200);

    auto executedHeader2 = executeOneBlock(201);
    BOOST_CHECK(executedHeader2);
    BOOST_CHECK_EQUAL(executedHeader2->number(), 201);
}

// FIB-102: Verify the discontinuity check fires when the input skips a number.
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

BOOST_AUTO_TEST_CASE(maxPendingRejectionDoesNotAdvanceExecutionState)
{
    for (int i = 0; i < 16; ++i)
    {
        auto header = executeOneBlock(600 + i);
        BOOST_CHECK(header);
    }

    auto rejectedBlock = std::make_shared<bcostars::protocol::BlockImpl>();
    auto rejectedHeader = rejectedBlock->blockHeader();
    rejectedHeader->setNumber(616);
    rejectedHeader->setVersion(200);
    rejectedHeader->calculateHash(*hashImpl);
    bytes input;
    rejectedBlock->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "616", 100, "chain", "group", 0));
    writeBlock(rejectedBlock);

    Error::Ptr rejectedError;
    baselineScheduler.executeBlock(
        rejectedBlock, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr, bool) {
            rejectedError = std::move(error);
        });
    BOOST_REQUIRE(rejectedError);
    BOOST_CHECK_EQUAL(rejectedError->errorCode(), scheduler::SchedulerError::InvalidStatus);

    auto skippedBlock = std::make_shared<bcostars::protocol::BlockImpl>();
    auto skippedHeader = skippedBlock->blockHeader();
    skippedHeader->setNumber(617);
    skippedHeader->setVersion(200);
    skippedHeader->calculateHash(*hashImpl);
    skippedBlock->appendTransaction(
        transactionFactory->createTransaction(0, "to", input, "617", 100, "chain", "group", 0));
    writeBlock(skippedBlock);

    Error::Ptr skippedError;
    baselineScheduler.executeBlock(
        skippedBlock, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr, bool) {
            skippedError = std::move(error);
        });
    BOOST_REQUIRE(skippedError);
    BOOST_CHECK_EQUAL(skippedError->errorCode(), scheduler::SchedulerError::InvalidBlockNumber);
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

BOOST_AUTO_TEST_CASE(failedCommitRetainsExecutionResultForRetry)
{
    auto executedHeader = executeOneBlock(700);
    BOOST_REQUIRE(executedHeader);

    prewriteBlockFails = true;

    Error::Ptr firstError;
    baselineScheduler.commitBlock(executedHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { firstError = std::move(error); });
    BOOST_REQUIRE(firstError);
    BOOST_CHECK(firstError->errorMessage().find(prewriteFailure) != std::string::npos);

    Error::Ptr retryError;
    baselineScheduler.commitBlock(executedHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { retryError = std::move(error); });
    BOOST_REQUIRE(retryError);
    BOOST_CHECK(retryError->errorMessage().find(prewriteFailure) != std::string::npos);
}

BOOST_AUTO_TEST_CASE(commitHeaderMustMatchOldestExecutionResult)
{
    auto executedHeader = executeOneBlock(720);
    BOOST_REQUIRE(executedHeader);

    auto wrongHeader = blockHeaderFactory->createBlockHeader();
    wrongHeader->setNumber(719);
    wrongHeader->setVersion(200);
    wrongHeader->calculateHash(*hashImpl);

    Error::Ptr commitError;
    baselineScheduler.commitBlock(wrongHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
    BOOST_REQUIRE(commitError);
    BOOST_CHECK_EQUAL(commitError->errorCode(), scheduler::SchedulerError::InvalidBlockNumber);

    prewriteBlockFails = true;
    Error::Ptr retryError;
    baselineScheduler.commitBlock(executedHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { retryError = std::move(error); });
    BOOST_REQUIRE(retryError);
    BOOST_CHECK(retryError->errorMessage().find(prewriteFailure) != std::string::npos);
}

// FIB-101 / FIB-104: Sequential execution and cached-duplicate execution still
// work after tightening the block-number counters (now mutex-owned) and the
// pending-result bookkeeping.
BOOST_AUTO_TEST_CASE(sequentialExecutionAndDuplicateCacheLookup)
{
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

// FIB-101 regression: the genesis system-contract deploy block (number 0) is
// committed through the scheduler during initSysContract() at node startup.
// buildGenesisBlock() has already written current-number=0, so a freshly
// started node reports ledgerBlockNumber=0 here. The commit-time bootstrap +
// already-committed gate must NOT treat block 0 as "already committed"
// (0 <= 0) or "discontinuous" (0 - 0 != 1), otherwise commitBlock fails and the
// node never starts. With the bug present this returns InvalidBlockNumber; with
// the fix block 0 bypasses the gate and falls through to the empty-results
// guard (UnknownError) instead — either way, NOT InvalidBlockNumber.
BOOST_AUTO_TEST_CASE(genesisBlockCommitBypassesAlreadyCommittedGate)
{
    ledgerBlockNumber = 0;  // buildGenesisBlock wrote SYS_KEY_CURRENT_NUMBER=0

    auto genesisHeader = blockHeaderFactory->createBlockHeader();
    genesisHeader->setNumber(0);
    genesisHeader->setVersion(200);
    genesisHeader->calculateHash(*hashImpl);

    Error::Ptr commitError;
    baselineScheduler.commitBlock(genesisHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });

    BOOST_REQUIRE(commitError);  // no execute → fails at the empty-results guard
    BOOST_CHECK_MESSAGE(commitError->errorCode() != scheduler::SchedulerError::InvalidBlockNumber,
        "genesis block 0 must not be rejected by the already-committed / continuity gate");
}

// FIB-101 guard: the fix above is narrow — for a normal block (number > 0) the
// already-committed gate must still reject a height at or below the ledger's
// current number. Here the ledger is at 10 and we try to commit 5.
BOOST_AUTO_TEST_CASE(alreadyCommittedNonGenesisBlockStillRejected)
{
    ledgerBlockNumber = 10;

    auto staleHeader = blockHeaderFactory->createBlockHeader();
    staleHeader->setNumber(5);
    staleHeader->setVersion(200);
    staleHeader->calculateHash(*hashImpl);

    Error::Ptr commitError;
    baselineScheduler.commitBlock(staleHeader,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });

    BOOST_REQUIRE(commitError);
    BOOST_CHECK_EQUAL(commitError->errorCode(), scheduler::SchedulerError::InvalidBlockNumber);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
