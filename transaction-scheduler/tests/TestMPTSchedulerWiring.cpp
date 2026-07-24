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
 * @file TestMPTSchedulerWiring.cpp
 * @brief End-to-end wiring tests for the execute-time MPT build in BaselineScheduler with
 *        trie nodes as ordinary "/mpt/" state rows on the execute view: scenario-A activation
 *        transition, pipeline node visibility through the view's immutable layer chain (with
 *        a negative control), commit atomicity, CommitObserver timing, parent-root resolution
 *        via the ledger header, the stray-"/mpt/"-row guard, and the no-XOR-fallback policy.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-ledger/mpt/Errors.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include "bcos-ledger/mpt/StorageValueCodec.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-storage/KeyPrefixes.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-scheduler/BaselineScheduler.h"
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Everything in an anonymous namespace (unity-build friendly); all storage-stack types carry
// the MW prefix and their own backend type, so this TU's ViewType — and its getLedgerConfig
// tag_invoke stub — cannot collide with the other scheduler test TUs when CMake merges the
// sources into one unity TU (same pattern as TestExecuteStateRootRegression.cpp).
namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;

using MWMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
// The backend is behaviourally a stock flat MemoryStorage: with trie nodes as ordinary
// "/mpt/" StateKey rows there is nothing MPT-specific left for a backend to implement.
// Still a DISTINCT type rather than an alias — this TU's getLedgerConfig tag_invoke stub is
// found by ADL through the ViewType's template arguments, so the backend type must be
// anchored in this anonymous namespace (the same reasoning as the MW prefix).
struct MWBackendStorage
  : memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>
{
};

using MWCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, MWBackendStorage>;
using MWMultiLayerStorage = MultiLayerStorage<MWMutableStorage, void, MWCheckpointBackend>;

/// Read one trie-node row from the backend under its ordinary StateKey.
std::optional<bytes> backendNode(MWBackendStorage& backend, h256 const& hash)
{
    auto entry = task::syncWait(storage2::readOne(backend, storage2::mptNodeStateKey(hash)));
    if (!entry)
    {
        return std::nullopt;
    }
    auto raw = entry->get();
    return bytes(raw.begin(), raw.end());
}

/// Count the backend rows living in the "/mpt/" table (committed trie nodes).
size_t backendNodeCount(MWBackendStorage& backend)
{
    return task::syncWait([&]() -> task::Task<size_t> {
        size_t count = 0;
        auto iterator = co_await storage2::range(backend);
        while (auto keyValue = co_await iterator.next())
        {
            auto&& [key, value] = *keyValue;
            if (StateKeyView{key}.m_table == storage2::kMPTTable)
            {
                ++count;
            }
        }
        co_return count;
    }());
}

/// One flat-state row operation the mock scheduler performs for a block.
/// value == nullopt means storage-level logical deletion (the removeSome path).
struct MWRowOp
{
    std::string table;
    std::string key;
    std::optional<std::string> value;
};

/// Scheduler impl that replays a per-block write plan through the storage argument
/// BaselineScheduler hands it — the production write path (view mutable layer).
struct MWWritingScheduler
{
    std::map<protocol::BlockNumber, std::vector<MWRowOp>> const* m_plan{};

    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(auto& storage,
        auto& /*executor*/, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& /*unused*/)
    {
        if (auto it = m_plan->find(blockHeader.number()); it != m_plan->end())
        {
            for (auto const& row : it->second)
            {
                if (row.value)
                {
                    storage::Entry entry;
                    entry.set(*row.value);
                    co_await storage2::writeOne(
                        storage, StateKey{row.table, row.key}, std::move(entry));
                }
                else
                {
                    co_await storage2::removeOne(storage, StateKey{row.table, row.key});
                }
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

struct MWExecutor
{
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(auto& /*storage*/,
        protocol::BlockHeader const& /*blockHeader*/, protocol::Transaction const& /*transaction*/,
        int /*contextID*/, ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
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
    auto createExecuteContext(auto& storage, protocol::BlockHeader const& /*blockHeader*/,
        protocol::Transaction const& /*transaction*/, int32_t /*contextID*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool /*call*/)
        -> task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        co_return {};
    }
};

/// The Features the getLedgerConfig stub hands to every execute — set per test.
ledger::Features g_mwFeatures{};

[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    MWMultiLayerStorage::ViewType& /*storage*/, bcos::ledger::LedgerConfig& ledgerConfig,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    ledgerConfig.setFeatures(g_mwFeatures);
    return {};
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> emptyTxsTaskMW()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> emptySystemConfigsTaskMW()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> emptyFeaturesTaskMW()
{
    co_return ledger::Features{};
}

/// Probe observer: records the callback's view of the world at onCommit time, including
/// whether every delta node was already readable from the backend under its ordinary
/// "/mpt/" StateKey (the timing contract).
struct MWProbeObserver : public bcos::ledger::mpt::CommitObserver
{
    struct Record
    {
        protocol::BlockNumber blockNumber{};
        h256 stateRoot;
        size_t newNodeCount{};
        bool allNodesInBackendAtCallback{};
        bcos::ledger::mpt::MPTDeltaLayer delta;  // copy for post-hoc assertions
    };
    MWBackendStorage* m_backend{};
    std::vector<Record> m_records;

    void onCommit(
        protocol::BlockNumber blockNumber, bcos::ledger::mpt::MPTDeltaLayer const& delta) override
    {
        bool allVisible = true;
        for (auto const& [hash, rlp] : delta.newNodes)
        {
            auto stored = backendNode(*m_backend, hash);
            if (!stored || *stored != rlp)
            {
                allVisible = false;
                break;
            }
        }
        m_records.push_back({.blockNumber = blockNumber,
            .stateRoot = delta.stateRoot,
            .newNodeCount = delta.newNodes.size(),
            .allNodesInBackendAtCallback = allVisible,
            .delta = delta});
    }
};

class MPTWiringFixture
{
public:
    MPTWiringFixture()
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
        g_mwFeatures = ledger::Features{};
        mockScheduler.m_plan = &plan;

        observer = std::make_shared<MWProbeObserver>();
        observer->m_backend = &backendStorage;
        baselineScheduler.setMPTCommitObserver(observer);

        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>) { callback({}, nullptr); });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskMW();
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
            return emptySystemConfigsTaskMW();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return emptyFeaturesTaskMW();
        });

        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    static void useScenarioA(protocol::BlockNumber activationBlock)
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_mpt_state_root);
        features.setActivationBlock(
            ledger::Features::Flag::feature_mpt_state_root, activationBlock);
        g_mwFeatures = features;
    }
    static void useScenarioB()
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_l2_ethereum_compat);
        g_mwFeatures = features;
    }

    std::tuple<Error::Ptr, protocol::BlockHeader::Ptr> executeBlockRaw(protocol::BlockNumber number)
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
        return {std::move(execError), std::move(executedHeader)};
    }

    protocol::BlockHeader::Ptr executeOneBlock(protocol::BlockNumber number)
    {
        auto [error, header] = executeBlockRaw(number);
        BOOST_REQUIRE_MESSAGE(
            !error, "executeBlock failed: " + (error ? error->errorMessage() : ""));
        BOOST_REQUIRE(header);
        return header;
    }

    void commitOneBlock(protocol::BlockHeader::Ptr const& header)
    {
        Error::Ptr commitError;
        baselineScheduler.commitBlock(header,
            [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
        BOOST_REQUIRE_MESSAGE(!commitError,
            "commitBlock failed: " + (commitError ? commitError->errorMessage() : ""));
        // Production persists the consensus-signed header (with its final stateRoot) via
        // prewriteBlockToBuffer; the ledger mock here only invokes the callback, so mirror
        // that persistence for the parent-root-from-header resolution path.
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

    protocol::BlockHeader::Ptr makeExecutedShapeHeader(protocol::BlockNumber number, h256 stateRoot)
    {
        auto header = blockHeaderFactory->createBlockHeader();
        header->setNumber(number);
        header->setVersion(blockVersion);
        header->setStateRoot(stateRoot);
        header->calculateHash(*hashImpl);
        return header;
    }

    // --- oracles -----------------------------------------------------------

    /// Legacy XOR oracle over a block's row plan (same per-entry hash call shape as
    /// calculateStateRoot).
    h256 xorOracle(std::vector<MWRowOp> const& rows) const
    {
        const std::optional<ledger::Features> features{g_mwFeatures};
        h256 expected;
        for (auto const& row : rows)
        {
            BOOST_REQUIRE(row.value);
            storage::Entry entry;
            entry.set(*row.value);
            expected ^= entry.hash(row.table, row.key, *hashImpl, blockVersion, features);
        }
        return expected;
    }

    /// Independent MPT oracle: commitTrie (bcos-ledger's stateless trie build) over explicitly
    /// constructed account leaves, from the empty root. Tries are history-independent, so the
    /// expected root of any block equals one from-scratch build over the expected FINAL state
    /// of every account the MPT has absorbed so far.
    static h256 mptOracle(std::map<Address, bcos::ledger::mpt::Account> const& accounts)
    {
        namespace mpt = bcos::ledger::mpt;
        std::map<h256, std::optional<bytes>> accountChanges;
        for (auto const& [address, account] : accounts)
        {
            accountChanges[mpt::accountKeyHash(address)] = account.encode();
        }
        memory_storage::MemoryStorage<h256, bytes, memory_storage::ORDERED> scratch;
        auto merged =
            task::syncWait(mpt::commitTrie(scratch, mpt::emptyRootHash(), accountChanges));
        return merged.root;
    }

    static h256 storageTrieOracle(std::map<h256, bytes> const& slots)  // slot -> 32-byte raw value
    {
        namespace mpt = bcos::ledger::mpt;
        std::map<h256, std::optional<bytes>> changes;
        for (auto const& [slot, rawValue] : slots)
        {
            changes[mpt::slotKeyHash(slot)] =
                mpt::encodeStorageValue(bytesConstRef(rawValue.data(), rawValue.size()));
        }
        memory_storage::MemoryStorage<h256, bytes, memory_storage::ORDERED> scratch;
        auto merged = task::syncWait(mpt::commitTrie(scratch, mpt::emptyRootHash(), changes));
        return merged.root;
    }

    static Address makeAddress(uint8_t firstByte)
    {
        Address address{};
        address.data()[0] = firstByte;
        return address;
    }

    static constexpr uint32_t blockVersion = 200;

    MWBackendStorage backendStorage;
    MWCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    std::map<protocol::BlockNumber, std::vector<MWRowOp>> plan;
    MWWritingScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    MWMultiLayerStorage multiLayerStorage;
    MWExecutor mockExecutor;
    std::shared_ptr<MWProbeObserver> observer;
    BaselineScheduler<decltype(multiLayerStorage), MWExecutor, MWWritingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestMPTSchedulerWiring, MPTWiringFixture)

// (a) Scenario-A transition: with feature_mpt_state_root activated at block 500, block 500
// itself still commits the legacy XOR root (strictly-greater boundary), block 501 commits an
// MPT root pinned against the commitTrie oracle, and block 502 — whose parent root resolves
// via the committed header in the ledger (the restart path) — extends the trie incrementally,
// including a storage-slot write that exercises the two-level trie build.
BOOST_AUTO_TEST_CASE(scenarioATransition)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto const addressA = makeAddress(0xAA);
    auto const addressB = makeAddress(0xBB);
    auto const tableA = mpt::accountTableName(addressA);
    auto const tableB = mpt::accountTableName(addressB);

    // Block 500 (activation block, XOR): A gets balance + nonce.
    plan[500] = {{tableA, "balance", "1000000"}, {tableA, "nonce", "1"}};
    // Block 501 (first MPT block): B first-touch, balance + nonce.
    plan[501] = {{tableB, "balance", "7"}, {tableB, "nonce", "3"}};
    // Block 502: A first-touch in the MPT — balance overwritten, nonce inherited from the
    // FLAT state (written back in the XOR era), plus one storage slot.
    h256 slot{};
    slot.data()[31] = 0x01;
    std::string slotValueRaw(32, '\0');
    slotValueRaw[31] = 0x2A;
    plan[502] = {{tableA, "balance", "42"},
        {tableA, std::string(reinterpret_cast<char const*>(slot.data()), h256::SIZE),
            slotValueRaw}};

    // --- block 500: XOR root, no MPT delta, observer silent.
    auto header500 = executeOneBlock(500);
    BOOST_CHECK_NE(header500->stateRoot(), h256{});
    BOOST_CHECK_EQUAL(header500->stateRoot(), xorOracle(plan[500]));
    commitOneBlock(header500);
    BOOST_CHECK_EQUAL(backendNodeCount(backendStorage), 0);
    BOOST_CHECK(observer->m_records.empty());

    // --- block 501: MPT root over {B}.
    auto header501 = executeOneBlock(501);
    mpt::Account accountB;
    accountB.balance = 7;
    accountB.nonce = 3;
    BOOST_CHECK_EQUAL(header501->stateRoot(), mptOracle({{addressB, accountB}}));
    commitOneBlock(header501);
    BOOST_CHECK_GT(backendNodeCount(backendStorage), 0);

    // --- block 502: parent root read from block 501's committed header (m_results is empty
    // after the commit, so the pipeline path cannot serve it), trie extended to {A, B}.
    auto header502 = executeOneBlock(502);
    mpt::Account accountA;
    accountA.balance = 42;
    accountA.nonce = 1;  // from the flat rows block 500 wrote — first-touch metadata read
    accountA.storageRoot =
        storageTrieOracle({{slot, bytes(slotValueRaw.begin(), slotValueRaw.end())}});
    BOOST_CHECK_EQUAL(
        header502->stateRoot(), mptOracle({{addressA, accountA}, {addressB, accountB}}));
    commitOneBlock(header502);
}

// (c) Pipeline visibility: two MPT blocks execute back-to-back with NO commit in between.
// Block 502's incremental rebuild must resolve block 501's not-yet-committed trie nodes
// through its forked view's immutable layer chain (fork() carries 501's mutable layer, node
// rows included) — the backend holds zero node rows throughout.
BOOST_AUTO_TEST_CASE(pipelineVisibility)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto const addressA = makeAddress(0xA1);
    auto const addressB = makeAddress(0xB2);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "5"}};
    plan[501] = {{mpt::accountTableName(addressA), "balance", "11"}};
    plan[502] = {{mpt::accountTableName(addressB), "balance", "13"}};

    auto header500 = executeOneBlock(500);  // XOR
    commitOneBlock(header500);

    auto header501 = executeOneBlock(501);  // MPT, stays pending
    auto header502 = executeOneBlock(502);  // MPT, must read 501's pending nodes

    // Nothing committed: 502's reads came from 501's layer in the view chain.
    BOOST_CHECK_EQUAL(backendNodeCount(backendStorage), 0);

    mpt::Account accountA;
    accountA.balance = 11;
    mpt::Account accountB;
    accountB.balance = 13;
    BOOST_CHECK_EQUAL(header501->stateRoot(), mptOracle({{addressA, accountA}}));
    BOOST_CHECK_EQUAL(
        header502->stateRoot(), mptOracle({{addressA, accountA}, {addressB, accountB}}));

    // Both commit cleanly afterwards.
    commitOneBlock(header501);
    commitOneBlock(header502);
    BOOST_CHECK_GT(backendNodeCount(backendStorage), 0);
}

// (c) negative control: the SAME build succeeds through a view forked AFTER block 501's
// pushView (its immutable chain carries 501's node rows) and fails loudly with the
// missing-node invariant through a view forked BEFORE it (no immutable chain to resolve the
// parent trie from) — proving the pipeline visibility above really comes from the view's
// layer chain, not from some other channel.
BOOST_AUTO_TEST_CASE(pipelineVisibilityNegativeControl)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto const addressA = makeAddress(0xA7);
    auto const addressB = makeAddress(0xB8);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "5"}};
    plan[501] = {{mpt::accountTableName(addressA), "balance", "11"}};

    auto header500 = executeOneBlock(500);  // XOR
    commitOneBlock(header500);

    // Forked BEFORE block 501 executes: fork() snapshots the layer list at call time, so
    // this view will never see 501's mutable layer.
    auto staleView = multiLayerStorage.fork();
    staleView.newMutable();

    auto header501 = executeOneBlock(501);  // MPT, stays pending — nodes only in its layer
    BOOST_CHECK_EQUAL(backendNodeCount(backendStorage), 0);

    // Forked AFTER pushView: carries 501's layer.
    auto freshView = multiLayerStorage.fork();
    freshView.newMutable();

    // The candidate next-block delta, written identically into both views' mutable layers.
    auto writeDelta = [&](auto& view) {
        storage::Entry entry;
        entry.set("13");
        task::syncWait(storage2::writeOne(mutableStorage(view),
            StateKey{mpt::accountTableName(addressB), "balance"}, std::move(entry)));
    };
    writeDelta(freshView);
    writeDelta(staleView);

    auto const parentRoot = header501->stateRoot();

    // GREEN: the view whose immutable chain carries 501's node rows resolves the parent trie.
    {
        ViewNodeStorage<MWMultiLayerStorage::ViewType> nodeStorage(freshView);
        auto delta =
            task::syncWait(mpt::buildAndCollect(nodeStorage, parentRoot, freshView, false));
        mpt::Account accountA;
        accountA.balance = 11;
        mpt::Account accountB;
        accountB.balance = 13;
        BOOST_CHECK_EQUAL(delta.stateRoot, mptOracle({{addressA, accountA}, {addressB, accountB}}));
    }

    // RED: the same build over the stale view — 501's nodes are unreachable, so the trie
    // walk must fail with the missing-node invariant, never silently rebuild from empty.
    {
        ViewNodeStorage<MWMultiLayerStorage::ViewType> nodeStorage(staleView);
        BOOST_CHECK_THROW(
            task::syncWait(mpt::buildAndCollect(nodeStorage, parentRoot, staleView, false)),
            mpt::MPTInvariantViolation);
    }
}

// (b) + (e) Commit atomicity and observer timing: after commit, every delta node is readable
// from the backend under its ordinary "/mpt/" StateKey byte-for-byte (asserted twice: inside
// the observer AT callback time — the timing contract — and post-hoc); before commit, nothing
// reaches the backend. XOR blocks never fire the observer.
BOOST_AUTO_TEST_CASE(commitAtomicityAndObserverTiming)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto const addressA = makeAddress(0xC3);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "9"}};
    plan[501] = {{mpt::accountTableName(addressA), "nonce", "2"}};

    auto header500 = executeOneBlock(500);  // XOR
    commitOneBlock(header500);
    BOOST_CHECK(observer->m_records.empty());  // XOR block: observer NOT fired

    auto header501 = executeOneBlock(501);  // MPT
    // (amended d) Executed but uncommitted: the node rows live only in the block's pending
    // mutable layer; zero "/mpt/" rows have reached the backend. NOTE: BaselineScheduler
    // exposes no proposal-drop / view-change clearing of m_results today (reset() is a no-op,
    // m_results only shrinks via commit-side pop_back), so "rollback" coverage here is
    // exactly this: nothing reaches disk before commit.
    BOOST_CHECK_EQUAL(backendNodeCount(backendStorage), 0);
    BOOST_CHECK(observer->m_records.empty());

    commitOneBlock(header501);

    BOOST_REQUIRE_EQUAL(observer->m_records.size(), 1);
    auto const& record = observer->m_records.front();
    BOOST_CHECK_EQUAL(record.blockNumber, 501);
    BOOST_CHECK_EQUAL(record.stateRoot, header501->stateRoot());
    BOOST_CHECK_GT(record.newNodeCount, 0);
    BOOST_CHECK(record.allNodesInBackendAtCallback);  // fired AFTER the nodes landed

    // Post-hoc: every delta node readable from the backend under its digest, bytes equal.
    for (auto const& [hash, rlp] : record.delta.newNodes)
    {
        auto stored = backendNode(backendStorage, hash);
        BOOST_REQUIRE_MESSAGE(stored.has_value(), "node missing from backend: " + hash.hex());
        BOOST_CHECK(*stored == rlp);
    }
}

// Scenario B (L2): every block is MPT-rooted; the first executed block resolves its parent
// root from the previous block's SIGNED HEADER in the ledger. Part 1: a parent header carrying
// emptyRootHash() chains cleanly. Part 2 (separate case below): a parent header carrying a
// root whose nodes do not exist must fail the execute loudly — proving the header value is
// actually consumed, not silently defaulted.
BOOST_AUTO_TEST_CASE(scenarioBParentFromLedgerHeader)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioB();

    writeHeaderToBackend(makeExecutedShapeHeader(499, mpt::emptyRootHash()));

    auto const addressA = makeAddress(0xD4);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "21"}};

    auto header500 = executeOneBlock(500);
    mpt::Account accountA;
    accountA.balance = 21;
    BOOST_CHECK_EQUAL(header500->stateRoot(), mptOracle({{addressA, accountA}}));
}

BOOST_AUTO_TEST_CASE(scenarioBBogusParentRootFailsLoud)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioB();

    h256 bogusRoot;
    bogusRoot.data()[0] = 0xDE;
    bogusRoot.data()[1] = 0xAD;
    writeHeaderToBackend(makeExecutedShapeHeader(499, bogusRoot));

    auto const addressA = makeAddress(0xE5);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "1"}};

    auto [error, header] = executeBlockRaw(500);
    BOOST_REQUIRE(error);
    BOOST_CHECK(!header);
}

// Guard: a stray row under the reserved "/mpt/" table in the block's TOP delta layer must be
// skipped by the build's account scan — parseAccountTable only accepts "/apps/<40-hex>"
// tables — never classified as an account, and must not perturb the state root.
BOOST_AUTO_TEST_CASE(strayMptRowIsNotAnAccount)
{
    namespace mpt = bcos::ledger::mpt;
    BOOST_CHECK(!mpt::parseAccountTable(storage2::kMPTTable).has_value());

    useScenarioA(500);
    auto const addressA = makeAddress(0xC9);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "9"}};

    h256 strayHash;
    strayHash.data()[0] = 0x5A;
    std::string const strayKey(reinterpret_cast<char const*>(strayHash.data()), h256::SIZE);
    plan[501] = {{mpt::accountTableName(addressA), "balance", "10"},
        {std::string(storage2::kMPTTable), strayKey, "not-a-real-node"}};

    auto header500 = executeOneBlock(500);  // XOR
    commitOneBlock(header500);

    // Must not throw UnknownAccountRowField, and the root reflects ONLY the account row.
    auto header501 = executeOneBlock(501);
    mpt::Account accountA;
    accountA.balance = 10;
    BOOST_CHECK_EQUAL(header501->stateRoot(), mptOracle({{addressA, accountA}}));
    commitOneBlock(header501);
}

// (f) No-fallback policy: a delta shape buildAndCollect rejects (a core-field row logically
// deleted outside a tombstone) surfaces as an execute ERROR — never as a block that silently
// fell back to the XOR root.
BOOST_AUTO_TEST_CASE(buildFailureNoXorFallback)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto const addressA = makeAddress(0xF6);
    plan[500] = {{mpt::accountTableName(addressA), "balance", "8"}};
    // Lone deletion of a core-field row: not a tombstone (nonce/codeHash not deleted).
    plan[501] = {{mpt::accountTableName(addressA), "balance", std::nullopt}};

    auto header500 = executeOneBlock(500);  // XOR
    commitOneBlock(header500);

    auto [error, header] = executeBlockRaw(501);
    BOOST_REQUIRE(error);
    BOOST_CHECK(!header);  // no header => no XOR-rooted block escaped
    BOOST_CHECK(error->errorMessage().find("tombstone") != std::string::npos);
    BOOST_CHECK_EQUAL(backendNodeCount(backendStorage), 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
