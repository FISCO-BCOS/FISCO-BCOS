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
 * @file TestExecuteStateRootRegression.cpp
 * @author: kyonGuo
 * @date 2026/7/22
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-framework/ledger/Features.h"
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
#include <array>
#include <fakeit.hpp>

// Regression test for the zero-stateRoot bug introduced by #5215 (ba902b6bb):
// BaselineScheduler::coExecuteBlock passed view.backendStorageRef() instead of
// the layered view to executeBlock, so transaction state writes bypassed the
// view's mutable layer and landed directly in the backend. finishExecute then
// XOR-folded an empty mutable layer and every consensus-path header carried
// stateRoot == 0x0. The stock MockScheduler never writes through its storage
// argument, so no existing test could see the reroute — the scheduler impl
// here does write, which is the whole point of this file.
//
// Everything lives in an anonymous namespace (unity-build friendly), and the
// storage stack uses its own backend hasher type so this TU's ViewType — and
// therefore its getLedgerConfig tag_invoke stub — cannot collide with the
// stubs in testBaselineScheduler.cpp / FIB101_102_103_104_SchedulerTest.cpp
// when CMake merges the sources into one unity TU.
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

struct SRBackendHash : std::hash<StateKey>
{
};

using SRMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using SRBackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT), SRBackendHash>;
using SRCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, SRBackendStorage>;
using SRMultiLayerStorage = MultiLayerStorage<SRMutableStorage, void, SRCheckpointBackend>;

struct StateRow
{
    std::string_view table;
    std::string_view key;
    std::string_view value;
};
constexpr std::array<StateRow, 2> stateRows{
    StateRow{"/apps/aabbccdd00112233", "balance", "1000000"},
    StateRow{"/apps/eeff445566778899", "code", "0x60806040526004361061"},
};

struct SRMockExecutor
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

// Unlike the stock MockScheduler, this impl writes state rows through the
// storage argument BaselineScheduler hands it — exactly what the production
// SchedulerSerialImpl / SchedulerParallelImpl do. If coExecuteBlock passes the
// backend instead of the layered view, these rows skip the mutable layer and
// the stateRoot XOR collapses to zero.
struct WritingMockScheduler
{
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        for (const auto& row : stateRows)
        {
            storage::Entry entry;
            entry.set(row.value);
            co_await storage2::writeOne(storage, StateKey{row.table, row.key}, std::move(entry));
        }

        auto receipts =
            ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
            ::ranges::views::transform([](size_t index) -> protocol::TransactionReceipt::Ptr {
                auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
                constexpr static std::string_view str = "abc";
                auto& inner = receipt->inner();
                inner.dataHash.assign(str.begin(), str.end());
                inner.data.gasUsed = "100";
                return receipt;
            }) |
            ::ranges::to<std::vector<protocol::TransactionReceipt::Ptr>>();

        co_return receipts;
    }
};

// Storage-level getLedgerConfig stub, found via ADL when the BaselineScheduler
// template is instantiated; [[maybe_unused]] silences clang's -Wunused-function
// false positive on indirect template use.
[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    SRMultiLayerStorage::ViewType& /*storage*/, bcos::ledger::LedgerConfig& /*ledgerConfig*/,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    return {};
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> emptyTxsTaskSR()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}

task::Task<ledger::SystemConfigs> emptySystemConfigsTask()
{
    co_return ledger::SystemConfigs{};
}

task::Task<ledger::Features> emptyFeaturesTask()
{
    co_return ledger::Features{};
}

class StateRootRegressionFixture
{
public:
    StateRootRegressionFixture()
      : checkpointBackend(backendStorage),
        cryptoSuite(std::make_shared<crypto::CryptoSuite>(
            std::make_shared<crypto::Keccak256>(), nullptr, nullptr)),
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
        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>,
                          bool) { callback({}, nullptr); });

        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskSR();
        });

        // The stubs below let coCommitBlock run to completion (its trailing
        // ledger::getLedgerConfig(m_ledger.get()) touches all of them).
        fakeit::When(Method(mockLedger, asyncGetBlockNumber))
            .AlwaysDo([](std::function<void(Error::Ptr, protocol::BlockNumber)> callback) {
                callback(nullptr, -1);
            });
        fakeit::When(Method(mockLedger, asyncGetNodeListByType))
            .AlwaysDo([](std::string_view const&,
                          std::function<void(Error::Ptr, consensus::ConsensusNodeList)> callback) {
                callback(nullptr, {});
            });
        fakeit::When(Method(mockLedger, asyncGetBlockDataByNumber))
            .AlwaysDo([](protocol::BlockNumber, int32_t,
                          std::function<void(Error::Ptr, protocol::Block::Ptr)> callback) {
                callback(nullptr, nullptr);
            });
        fakeit::When(Method(mockLedger, asyncGetBlockHashByNumber))
            .AlwaysDo([](protocol::BlockNumber,
                          std::function<void(Error::Ptr, crypto::HashType)> callback) {
                callback(nullptr, crypto::HashType{});
            });
        fakeit::When(Method(mockLedger, fetchAllSystemConfigs)).AlwaysDo([](protocol::BlockNumber) {
            return emptySystemConfigsTask();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return emptyFeaturesTask();
        });

        // A successful commit invokes both notifiers on m_asyncGroup; leaving
        // them unset would throw std::bad_function_call inside a TBB task.
        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    void writeBlock(const std::shared_ptr<bcostars::protocol::BlockImpl>& block)
    {
        auto blockHeader = block->blockHeader();
        task::syncWait(ledger::prewriteBlock(mockLedger.get(),
            std::make_shared<protocol::ConstTransactions>(), block, false, backendStorage));
        bytes headerBuffer;
        blockHeader->encode(headerBuffer);

        storage::Entry number2HeaderEntry;
        number2HeaderEntry.set(std::move(headerBuffer));
        task::syncWait(storage2::writeOne(backendStorage,
            StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(blockHeader->number())},
            std::move(number2HeaderEntry)));
    }

    protocol::BlockHeader::Ptr executeOneBlock(protocol::BlockNumber number)
    {
        auto block = std::make_shared<bcostars::protocol::BlockImpl>();
        auto blockHeader = block->blockHeader();
        blockHeader->setNumber(number);
        blockHeader->setVersion(blockVersion);
        blockHeader->calculateHash(*hashImpl);
        bytes input;
        block->appendTransaction(transactionFactory->createTransaction(
            0, "to", input, std::to_string(number), 100, "chain", "group", 0));
        writeBlock(block);

        protocol::BlockHeader::Ptr executedHeader;
        Error::Ptr execError;
        baselineScheduler.executeBlock(
            block, false, [&](Error::Ptr error, protocol::BlockHeader::Ptr header, bool) {
                execError = std::move(error);
                executedHeader = std::move(header);
            });
        BOOST_REQUIRE_MESSAGE(
            !execError, "executeBlock failed: " + (execError ? execError->errorMessage() : ""));
        BOOST_REQUIRE(executedHeader);
        return executedHeader;
    }

    // Independent oracle: same per-entry hash call shape as calculateStateRoot
    // (BaselineScheduler.h), XOR-folded over the rows WritingMockScheduler wrote.
    h256 expectedStateRoot() const
    {
        const std::optional<ledger::Features> features{ledger::Features{}};
        h256 expected;
        for (const auto& row : stateRows)
        {
            storage::Entry entry;
            entry.set(row.value);
            expected ^= entry.hash(row.table, row.key, *hashImpl, blockVersion, features);
        }
        return expected;
    }

    static constexpr uint32_t blockVersion = 200;

    SRBackendStorage backendStorage;
    SRCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    WritingMockScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    SRMultiLayerStorage multiLayerStorage;
    SRMockExecutor mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), SRMockExecutor, WritingMockScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestExecuteStateRootRegression, StateRootRegressionFixture)

// The regression pin: a block whose execution writes state must produce a
// non-zero header stateRoot, equal to the independently computed XOR oracle.
// With the #5215 reroute in place this fails with stateRoot == 0x0.
BOOST_AUTO_TEST_CASE(executedStateRootMatchesXorOracle)
{
    auto executedHeader = executeOneBlock(500);

    BOOST_CHECK_NE(executedHeader->stateRoot(), h256{});
    BOOST_CHECK_EQUAL(executedHeader->stateRoot(), expectedStateRoot());
}

// The layering pin: execute writes stay in the view's mutable layer until
// commit merges them into the backend. With the #5215 reroute, the rows are
// visible in the backend right after execute — i.e. a discarded proposal
// would already have dirtied the backend.
BOOST_AUTO_TEST_CASE(executeWritesStayInViewUntilCommit)
{
    auto executedHeader = executeOneBlock(600);

    // Before commit: rows are NOT in the backend...
    for (const auto& row : stateRows)
    {
        auto backendValue =
            task::syncWait(storage2::readOne(backendStorage, StateKey{row.table, row.key}));
        BOOST_CHECK_MESSAGE(!backendValue,
            std::string("row leaked into backend before commit: ").append(row.table));
    }
    // ...but ARE readable through a fork of the multi-layer storage (they live
    // in the pushed execution layer).
    task::syncWait([&]() -> task::Task<void> {
        auto view = multiLayerStorage.fork();
        for (const auto& row : stateRows)
        {
            auto viewValue = co_await storage2::readOne(view, StateKey{row.table, row.key});
            BOOST_REQUIRE_MESSAGE(viewValue,
                std::string("row missing from layered view after execute: ").append(row.table));
            BOOST_CHECK_EQUAL(std::string(viewValue->get()), std::string(row.value));
        }
    }());

    Error::Ptr commitError;
    ledger::LedgerConfig::Ptr ledgerConfig;
    baselineScheduler.commitBlock(
        executedHeader, [&](Error::Ptr error, ledger::LedgerConfig::Ptr config) {
            commitError = std::move(error);
            ledgerConfig = std::move(config);
        });
    BOOST_REQUIRE_MESSAGE(
        !commitError, "commitBlock failed: " + (commitError ? commitError->errorMessage() : ""));

    // After commit: the merge moved the rows into the backend.
    for (const auto& row : stateRows)
    {
        auto backendValue =
            task::syncWait(storage2::readOne(backendStorage, StateKey{row.table, row.key}));
        BOOST_REQUIRE_MESSAGE(
            backendValue, std::string("row missing from backend after commit: ").append(row.table));
        BOOST_CHECK_EQUAL(std::string(backendValue->get()), std::string(row.value));
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
