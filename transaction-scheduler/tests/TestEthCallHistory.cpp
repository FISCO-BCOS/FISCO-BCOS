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
 * @file TestEthCallHistory.cpp
 * @brief Historical eth_call through the scheduler (M13.2): HistoricalStateBackend resolves
 *        flat-state reads from a past block's MPT root; the View stacked on it gives the
 *        call read-your-writes; BaselineScheduler::callAtBlock wires blockNumber -> header
 *        stateRoot -> historical stack -> executor, with explicit refusals for beyond-latest
 *        blocks and non-scenario-B chains; getExecutable bypasses the global executable
 *        cache for historical storages.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-transaction-executor/vm/HostContext.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include "bcos-transaction-scheduler/HistoricalCallStorage.h"
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Anonymous namespace + HC prefix: this TU is unity-merged with the other scheduler test
// TUs, and the getLedgerConfig tag_invoke stub below is found by ADL through the ViewType's
// template arguments — the storage types must be anchored here (same pattern as
// TestMPTSchedulerWiring.cpp).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

using HCMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
struct HCBackendStorage
  : memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>
{
};
using HCCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, HCBackendStorage>;
using HCMultiLayerStorage = MultiLayerStorage<HCMutableStorage, void, HCCheckpointBackend>;
using HCViewType = HCMultiLayerStorage::ViewType;
using HCHistoricalBackend = HistoricalStateBackend<HCViewType>;
using HCHistoricalView = View<HCMutableStorage, void, HCHistoricalBackend>;

/// The Features the getLedgerConfig stub hands out — set per test.
ledger::Features g_hcFeatures{};

[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/, HCViewType& /*storage*/,
    bcos::ledger::LedgerConfig& ledgerConfig, protocol::BlockNumber /*blockNumber*/,
    protocol::BlockFactory& /*blockFactory*/)
{
    ledgerConfig.setFeatures(g_hcFeatures);
    return {};
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> hcEmptyTxsTask()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> hcEmptySystemConfigsTask()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> hcEmptyFeaturesTask()
{
    co_return ledger::Features{};
}

// --- the probed account -----------------------------------------------------------------

Address hcAddress()
{
    Address address{};
    address.data()[0] = 0xAA;
    return address;
}

evmc_address hcEvmcAddress()
{
    evmc_address address{};
    address.bytes[0] = 0xAA;
    return address;
}

h256 hcSlot()
{
    h256 slot{};
    slot.data()[31] = 0x01;
    return slot;
}

std::string hcSlotRowKey()
{
    auto slot = hcSlot();
    return {reinterpret_cast<char const*>(slot.data()), h256::SIZE};
}

std::string hcRawValue(uint8_t lastByte)
{
    std::string raw(32, '\0');
    raw[31] = static_cast<char>(lastByte);
    return raw;
}

evmc_bytes32 hcEvmcValue(uint8_t lastByte)
{
    evmc_bytes32 value{};
    value.bytes[31] = lastByte;
    return value;
}

bytes outputOf(protocol::TransactionReceipt::Ptr const& receipt)
{
    auto view = receipt->output();
    return {view.begin(), view.end()};
}

bytes toBytes(evmc_bytes32 const& value)
{
    return {value.bytes, value.bytes + sizeof(value.bytes)};
}

// --- block-driving mocks (same shapes as TestMPTSchedulerWiring) ------------------------

struct HCRowOp
{
    std::string table;
    std::string key;
    std::string value;
};

struct HCWritingScheduler
{
    std::map<protocol::BlockNumber, std::vector<HCRowOp>> const* m_plan{};

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        if (auto it = m_plan->find(blockHeader.number()); it != m_plan->end())
        {
            for (auto const& row : it->second)
            {
                storage::Entry entry;
                entry.set(row.value);
                co_await storage2::writeOne(
                    storage, StateKey{row.table, row.key}, std::move(entry));
            }
        }
        auto receipts = ::ranges::iota_view<size_t, size_t>(0, ::ranges::size(transactions)) |
                        ::ranges::views::transform([](size_t) -> protocol::TransactionReceipt::Ptr {
                            auto receipt =
                                std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
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

/// The "EVM" of these tests: reads the probed account's slot through whatever storage the
/// scheduler hands it — EVMAccount over the historical stack, exactly the account type the
/// production HostContext uses — and returns the 32-byte value as the receipt output.
/// WriteThenReadSlot first SSTOREs m_writeValue, so the output proves (or disproves)
/// read-your-writes on the stack.
struct HCProbeExecutor
{
    enum class Mode : uint8_t
    {
        ReadSlot,
        WriteThenReadSlot,
    };
    Mode m_mode{Mode::ReadSlot};
    evmc_bytes32 m_writeValue{};
    protocol::BlockNumber m_lastExecutedHeaderNumber{-1};

    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& /*transaction*/,
        int /*contextID*/, ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
    {
        m_lastExecutedHeaderNumber = blockHeader.number();
        ledger::account::EVMAccount account(storage, hcEvmcAddress(), false);
        if (m_mode == Mode::WriteThenReadSlot)
        {
            co_await account.setStorage(ledger::account::toEvmcBytes32(hcSlot()), m_writeValue);
        }
        auto value = co_await account.storage(ledger::account::toEvmcBytes32(hcSlot()));
        auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
        auto& inner = receipt->inner();
        inner.data.output.assign(value.bytes, value.bytes + sizeof(value.bytes));
        co_return receipt;
    }

    template <class Storage>
    struct ExecuteContext
    {
        task::Task<void> prepare() { co_return; }
        task::Task<void> execute() { co_return; }
        task::Task<protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
    };
    auto createExecuteContext(auto& storage, protocol::BlockHeader const& /*blockHeader*/,
        protocol::Transaction const& /*transaction*/, int32_t /*contextID*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};

class EthCallHistoryFixture
{
public:
    EthCallHistoryFixture()
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
        baselineScheduler(multiLayerStorage, mockScheduler, probeExecutor, *blockFactory,
            mockLedger.get(), mockTxPool.get(), *transactionSubmitResultFactory, *hashImpl)
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
        g_hcFeatures = features;
        mockScheduler.m_plan = &plan;

        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>,
                          bool) { callback({}, nullptr); });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return hcEmptyTxsTask();
        });
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
            return hcEmptySystemConfigsTask();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return hcEmptyFeaturesTask();
        });

        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
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

        Error::Ptr execError;
        protocol::BlockHeader::Ptr executedHeader;
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

    void commitOneBlock(protocol::BlockHeader::Ptr const& header)
    {
        Error::Ptr commitError;
        baselineScheduler.commitBlock(header,
            [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
        BOOST_REQUIRE_MESSAGE(!commitError,
            "commitBlock failed: " + (commitError ? commitError->errorMessage() : ""));
        // Mirror production: the ledger persists the signed header row (the mock only
        // invokes the callback).
        writeHeaderToBackend(header);
    }

    void writeHeaderToBackend(protocol::BlockHeader::Ptr const& header)
    {
        bytes headerBuffer;
        header->encode(headerBuffer);
        storage::Entry entry;
        entry.set(std::move(headerBuffer));
        task::syncWait(storage2::writeOne(backendStorage,
            StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(header->number())},
            std::move(entry)));
    }

    protocol::BlockHeader::Ptr makeHeader(protocol::BlockNumber number, h256 stateRoot)
    {
        auto header = blockHeaderFactory->createBlockHeader();
        header->setNumber(number);
        header->setVersion(blockVersion);
        header->setStateRoot(stateRoot);
        header->calculateHash(*hashImpl);
        return header;
    }

    void writeCurrentNumberToBackend(protocol::BlockNumber number)
    {
        storage::Entry entry;
        entry.set(std::to_string(number));
        task::syncWait(storage2::writeOne(backendStorage,
            StateKey{ledger::SYS_CURRENT_STATE, ledger::SYS_KEY_CURRENT_NUMBER}, std::move(entry)));
    }

    /// Run the canonical 3-block chain: slot=0x01 at block 1, slot=0x02 at block 2,
    /// balance bump (slot untouched) at block 3. Returns the three executed headers.
    std::vector<protocol::BlockHeader::Ptr> runCanonicalChain()
    {
        // Seed the genesis header (Ledger::buildGenesisBlock's job in production): scenario-B
        // block 1 resolves its parent root from it, and an empty-alloc L2 genesis commits the
        // empty trie.
        writeHeaderToBackend(makeHeader(0, ledger::mpt::emptyRootHash()));

        auto const table = ledger::mpt::accountTableName(hcAddress());
        plan[1] = {{table, "balance", "1000"}, {table, "nonce", "5"},
            {table, hcSlotRowKey(), hcRawValue(0x01)}};
        plan[2] = {{table, hcSlotRowKey(), hcRawValue(0x02)}};
        plan[3] = {{table, "balance", "2000"}};

        std::vector<protocol::BlockHeader::Ptr> headers;
        for (protocol::BlockNumber number = 1; number <= 3; ++number)
        {
            auto header = executeOneBlock(number);
            commitOneBlock(header);
            headers.push_back(std::move(header));
        }
        writeCurrentNumberToBackend(3);
        return headers;
    }

    std::tuple<Error::Ptr, protocol::TransactionReceipt::Ptr> callAt(protocol::BlockNumber number)
    {
        auto transaction = transactionFactory->createTransaction(
            0, "to", bytes{}, "callNonce", 100, "chain", "group", 0);
        Error::Ptr callError;
        protocol::TransactionReceipt::Ptr receipt;
        baselineScheduler.callAtBlock(std::move(transaction), number,
            [&](Error::Ptr error, protocol::TransactionReceipt::Ptr result) {
                callError = std::move(error);
                receipt = std::move(result);
            });
        return {std::move(callError), std::move(receipt)};
    }

    static constexpr uint32_t blockVersion = 200;

    HCBackendStorage backendStorage;
    HCCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    std::map<protocol::BlockNumber, std::vector<HCRowOp>> plan;
    HCWritingScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    HCMultiLayerStorage multiLayerStorage;
    HCProbeExecutor probeExecutor;
    BaselineScheduler<decltype(multiLayerStorage), HCProbeExecutor, HCWritingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

// The trait getExecutable consults: the marker propagates through View and Rollbackable,
// and plain storages stay non-historical (negative control).
static_assert(hostcontext::isHistoricalStorage<HCHistoricalBackend>());
static_assert(hostcontext::isHistoricalStorage<HCHistoricalView>());
static_assert(hostcontext::isHistoricalStorage<Rollbackable<HCHistoricalView>>());
static_assert(!hostcontext::isHistoricalStorage<HCViewType>());
static_assert(!hostcontext::isHistoricalStorage<Rollbackable<HCViewType>>());
static_assert(!hostcontext::isHistoricalStorage<HCMutableStorage>());

BOOST_FIXTURE_TEST_SUITE(TestEthCallHistory, EthCallHistoryFixture)

// Storage-level: HistoricalStateBackend answers account rows from the MPT at the pinned
// root — the block-1 root serves block-1 values after later blocks overwrote them — and
// absent accounts/slots read as absent. Pass-through rows (the header table) stay readable.
BOOST_AUTO_TEST_CASE(historicalBackendResolvesAccountRows)
{
    auto headers = runCanonicalChain();
    auto const table = ledger::mpt::accountTableName(hcAddress());

    auto latestView = multiLayerStorage.fork();
    HCHistoricalBackend backendAt1(latestView, headers[0]->stateRoot());

    // Slot: the block-1 value, not the block-2 overwrite.
    auto slotEntry =
        task::syncWait(storage2::readOne(backendAt1, StateKeyView{table, hcSlotRowKey()}));
    BOOST_REQUIRE(slotEntry);
    BOOST_CHECK(slotEntry->get() == hcRawValue(0x01));

    // Balance and nonce in the exact flat representations EVMAccount reads.
    auto balanceEntry =
        task::syncWait(storage2::readOne(backendAt1, StateKeyView{table, "balance"}));
    BOOST_REQUIRE(balanceEntry);
    BOOST_CHECK_EQUAL(balanceEntry->get(), "1000");
    auto nonceEntry = task::syncWait(storage2::readOne(backendAt1, StateKeyView{table, "nonce"}));
    BOOST_REQUIRE(nonceEntry);
    BOOST_CHECK_EQUAL(nonceEntry->get(), "5");

    // SYS_TABLES row of the account table: leaf presence.
    auto existsEntry =
        task::syncWait(storage2::readOne(backendAt1, StateKeyView{ledger::SYS_TABLES, table}));
    BOOST_CHECK(existsEntry.has_value());

    // A root later in the chain serves the overwrite and the balance bump.
    HCHistoricalBackend backendAt3(latestView, headers[2]->stateRoot());
    auto slotAt3 =
        task::syncWait(storage2::readOne(backendAt3, StateKeyView{table, hcSlotRowKey()}));
    BOOST_REQUIRE(slotAt3);
    BOOST_CHECK(slotAt3->get() == hcRawValue(0x02));
    auto balanceAt3 = task::syncWait(storage2::readOne(backendAt3, StateKeyView{table, "balance"}));
    BOOST_REQUIRE(balanceAt3);
    BOOST_CHECK_EQUAL(balanceAt3->get(), "2000");

    // An account with no leaf at the root reads as absent, all four row kinds.
    Address stranger{};
    stranger.data()[0] = 0xBB;
    auto strangerTable = ledger::mpt::accountTableName(stranger);
    BOOST_CHECK(
        !task::syncWait(storage2::readOne(backendAt1, StateKeyView{strangerTable, "balance"})));
    BOOST_CHECK(!task::syncWait(
        storage2::readOne(backendAt1, StateKeyView{strangerTable, hcSlotRowKey()})));
    BOOST_CHECK(!task::syncWait(
        storage2::readOne(backendAt1, StateKeyView{ledger::SYS_TABLES, strangerTable})));

    // Pass-through: a header row read through the historical backend is served verbatim
    // from the latest view.
    auto headerRow = task::syncWait(
        storage2::readOne(backendAt1, StateKeyView{ledger::SYS_NUMBER_2_BLOCK_HEADER, "2"}));
    BOOST_CHECK(headerRow.has_value());
}

// Stack-level: a mutable layer over the historical backend gives read-your-writes without
// disturbing the committed state.
BOOST_AUTO_TEST_CASE(historicalViewReadYourWrites)
{
    auto headers = runCanonicalChain();
    auto const table = ledger::mpt::accountTableName(hcAddress());

    auto latestView = multiLayerStorage.fork();
    HCHistoricalBackend historicalBackend(latestView, headers[0]->stateRoot());
    HCHistoricalView historicalView(std::addressof(historicalBackend));
    historicalView.newMutable();

    // Miss resolves historically...
    auto before =
        task::syncWait(storage2::readOne(historicalView, StateKeyView{table, hcSlotRowKey()}));
    BOOST_REQUIRE(before);
    BOOST_CHECK(before->get() == hcRawValue(0x01));

    // ...a write lands in the mutable layer and reads back...
    storage::Entry updated;
    updated.set(hcRawValue(0x99));
    task::syncWait(
        storage2::writeOne(historicalView, StateKey{table, hcSlotRowKey()}, std::move(updated)));
    auto after =
        task::syncWait(storage2::readOne(historicalView, StateKeyView{table, hcSlotRowKey()}));
    BOOST_REQUIRE(after);
    BOOST_CHECK(after->get() == hcRawValue(0x99));

    // ...and the backend below never saw it.
    auto backendValue =
        task::syncWait(storage2::readOne(historicalBackend, StateKeyView{table, hcSlotRowKey()}));
    BOOST_REQUIRE(backendValue);
    BOOST_CHECK(backendValue->get() == hcRawValue(0x01));
}

// Scheduler-level: callAtBlock(N) executes against block N's state — each height serves its
// own slot value, the executor sees block N's header, and the latest heights keep working
// through both entry points.
BOOST_AUTO_TEST_CASE(callAtBlockServesEachHeight)
{
    runCanonicalChain();

    auto [error1, receipt1] = callAt(1);
    BOOST_REQUIRE_MESSAGE(!error1, (error1 ? error1->errorMessage() : std::string{}));
    BOOST_REQUIRE(receipt1);
    BOOST_CHECK(outputOf(receipt1) == toBytes(hcEvmcValue(0x01)));
    BOOST_CHECK_EQUAL(probeExecutor.m_lastExecutedHeaderNumber, 1);

    auto [error2, receipt2] = callAt(2);
    BOOST_REQUIRE_MESSAGE(!error2, (error2 ? error2->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(receipt2) == toBytes(hcEvmcValue(0x02)));
    BOOST_CHECK_EQUAL(probeExecutor.m_lastExecutedHeaderNumber, 2);

    // Block 3 IS the latest: callAtBlock delegates to the latest path, which reads the
    // same (unchanged since block 2) slot.
    auto [error3, receipt3] = callAt(3);
    BOOST_REQUIRE_MESSAGE(!error3, (error3 ? error3->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(receipt3) == toBytes(hcEvmcValue(0x02)));
    BOOST_CHECK_EQUAL(probeExecutor.m_lastExecutedHeaderNumber, 3);

    // The plain latest-state call() still works.
    auto transaction = transactionFactory->createTransaction(
        0, "to", bytes{}, "latestNonce", 100, "chain", "group", 0);
    Error::Ptr latestError;
    protocol::TransactionReceipt::Ptr latestReceipt;
    baselineScheduler.call(
        std::move(transaction), [&](Error::Ptr error, protocol::TransactionReceipt::Ptr result) {
            latestError = std::move(error);
            latestReceipt = std::move(result);
        });
    BOOST_REQUIRE_MESSAGE(
        !latestError, (latestError ? latestError->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(latestReceipt) == toBytes(hcEvmcValue(0x02)));
}

// The PR #5324 review blocker, pinned green on the real stack: an SSTORE inside a
// historical call reads back within that call, leaks into no other call and never reaches
// the committed state.
BOOST_AUTO_TEST_CASE(writeThenReadInsideHistoricalCall)
{
    runCanonicalChain();

    probeExecutor.m_mode = HCProbeExecutor::Mode::WriteThenReadSlot;
    probeExecutor.m_writeValue = hcEvmcValue(0x99);
    auto [writeError, writeReceipt] = callAt(1);
    BOOST_REQUIRE_MESSAGE(!writeError, (writeError ? writeError->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(writeReceipt) == toBytes(hcEvmcValue(0x99)));

    // A fresh call at the same height starts from the committed state again.
    probeExecutor.m_mode = HCProbeExecutor::Mode::ReadSlot;
    auto [readError, readReceipt] = callAt(1);
    BOOST_REQUIRE_MESSAGE(!readError, (readError ? readError->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(readReceipt) == toBytes(hcEvmcValue(0x01)));

    // And the live chain state is untouched.
    auto latestView = multiLayerStorage.fork();
    auto liveValue = task::syncWait(storage2::readOne(
        latestView, StateKeyView{ledger::mpt::accountTableName(hcAddress()), hcSlotRowKey()}));
    BOOST_REQUIRE(liveValue);
    BOOST_CHECK(liveValue->get() == hcRawValue(0x02));
}

// The refusals: a block beyond the latest height, a chain without scenario-B MPT, and a
// block whose header is unreachable all answer with explicit errors, never a latest-state
// result dressed up as a historical one.
BOOST_AUTO_TEST_CASE(callAtBlockRefusalPaths)
{
    runCanonicalChain();

    auto [beyondError, beyondReceipt] = callAt(99);
    BOOST_REQUIRE(beyondError);
    BOOST_CHECK(beyondError->errorMessage().find("does not exist") != std::string::npos);
    BOOST_CHECK(!beyondReceipt);

    // Scenario A (mid-chain MPT activation) is refused: the trie there is not the complete
    // state, so a historical call could silently mis-read dormant accounts.
    ledger::Features scenarioA;
    scenarioA.set(ledger::Features::Flag::feature_mpt_state_root);
    scenarioA.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, 0);
    g_hcFeatures = scenarioA;
    auto [scenarioAError, scenarioAReceipt] = callAt(1);
    BOOST_REQUIRE(scenarioAError);
    BOOST_CHECK(
        scenarioAError->errorMessage().find("feature_l2_ethereum_compat") != std::string::npos);
    BOOST_CHECK(!scenarioAReceipt);

    // Back to scenario B: the empty-root genesis header serves an EMPTY state — the slot
    // reads as zero, which is Ethereum's answer for "before anything was written".
    ledger::Features scenarioB;
    scenarioB.set(ledger::Features::Flag::feature_l2_ethereum_compat);
    g_hcFeatures = scenarioB;
    auto [genesisError, genesisReceipt] = callAt(0);
    BOOST_REQUIRE_MESSAGE(
        !genesisError, (genesisError ? genesisError->errorMessage() : std::string{}));
    BOOST_CHECK(outputOf(genesisReceipt) == toBytes(hcEvmcValue(0x00)));

    // A header that records NO state root at all (h256{} zero) is refused loudly rather
    // than misread as a real root.
    writeHeaderToBackend(makeHeader(0, h256{}));
    auto [zeroRootError, zeroRootReceipt] = callAt(0);
    BOOST_REQUIRE(zeroRootError);
    BOOST_CHECK(zeroRootError->errorMessage().find("no MPT state root") != std::string::npos);
    BOOST_CHECK(!zeroRootReceipt);
}

// getExecutable must not serve a historical call from the global address-keyed executable
// cache, nor fill that cache from one: the cache is shared with latest-state execution and
// code at an address can differ between the pinned block and today.
BOOST_AUTO_TEST_CASE(executableCacheBypassForHistoricalStorage)
{
    auto headers = runCanonicalChain();

    // A unique address so this test owns its global-cache entry.
    evmc_address cachedAddress{};
    cachedAddress.bytes[0] = 0xCE;
    cachedAddress.bytes[19] = 0x01;

    // Poison the global cache with an executable for that address.
    bytes fakeCode{0x60, 0x00, 0x60, 0x00, 0xF3};  // trivial EVM bytes
    auto poisoned =
        std::make_shared<hostcontext::Executable>(bytesConstRef{fakeCode.data(), fakeCode.size()});
    task::syncWait(storage2::writeOne(hostcontext::getCacheExecutables(), cachedAddress, poisoned));

    auto latestView = multiLayerStorage.fork();
    HCHistoricalBackend historicalBackend(latestView, headers[0]->stateRoot());
    HCHistoricalView historicalView(std::addressof(historicalBackend));
    historicalView.newMutable();
    Rollbackable<HCHistoricalView> rollbackable(historicalView);

    // Historical storage: the poisoned cache entry must NOT be served. The address has no
    // leaf at the block-1 root, so its code resolves to nothing — "no executable".
    auto historicalResult = task::syncWait(hostcontext::getExecutable(
        rollbackable, cachedAddress, EVMC_CANCUN, /*binaryAddress*/ false));
    BOOST_CHECK(!historicalResult);

    // Positive control: a latest-state storage IS served from the cache for the same
    // address, proving the bypass above is what made the difference.
    auto plainView = multiLayerStorage.fork();
    plainView.newMutable();
    Rollbackable<decltype(plainView)> plainRollbackable(plainView);
    auto latestResult = task::syncWait(hostcontext::getExecutable(
        plainRollbackable, cachedAddress, EVMC_CANCUN, /*binaryAddress*/ false));
    BOOST_CHECK_EQUAL(latestResult.get(), poisoned.get());

    // Clean up the global cache so no other test sees this entry.
    task::syncWait(storage2::removeOne(hostcontext::getCacheExecutables(), cachedAddress));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace
