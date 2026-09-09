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
 * @file TestHistoryWiring.cpp
 * @brief The commit-time half of the two MPT reverse histories, asserted where it is wired.
 *
 * Two claims live here.
 *
 * The single-WriteBatch claim (G3, pathdb spec §9 and §13): a block's flat rows, its trie-node
 * rows, BOTH histories' meta and shard rows, and the expiry deletes of the blocks leaving the two
 * windows must all reach the backend in ONE merge, or a crash can leave the current state
 * advanced with a block's history missing. The counting backend is TestCommitSingleBatch.cpp's
 * technique — shadow MemoryStorage's variadic `merge`, which is the exact call mergeBackStorage
 * makes on the latest backend — extended to record, per merge, whether each key arrived as a
 * write or as a deletion.
 *
 * The publish-after-persist claim (G9): the in-memory query index must learn about a block only
 * once that merge has landed. Asserted on the two-phase API against the stores the scheduler has
 * been publishing into — stage, drop, and the index is untouched; stage, merge, publish, and the
 * block appears.
 *
 * Everything is in an anonymous namespace with HW-prefixed types so this TU's getLedgerConfig
 * tag_invoke stub cannot collide with the other scheduler test TUs under a unity build.
 */

#include "TrivialCheckpointStorage.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/Classify.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-ledger/mpt/PathKey.h"
#include "bcos-ledger/mpt/history/HistoryCommit.h"
#include "bcos-ledger/mpt/history/HistoryRead.h"
#include "bcos-ledger/mpt/history/HistoryRowCodec.h"
#include "bcos-ledger/mpt/history/HistoryTables.h"
#include "bcos-ledger/mpt/history/MPTHistory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <chrono>
#include <fakeit.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::scheduler_v1;
namespace history = bcos::ledger::mpt::history;

using HWMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

/// One key as it reached the backend in one merge.
struct HWMergedKey
{
    StateKey key;
    bool deleted{};
};

/// Backend that records, per merge call, every key the merge lands and whether it arrived as a
/// deletion. ORDERED so the history's manifest sweep can seek in it; CONCURRENT because
/// MemoryStorage's multi-source `merge` overload is `requires withConcurrent` and
/// mergeBackStorage merges the block's layer PLUS the prewrite buffer; pinned to ONE bucket
/// because a seek positions inside the current bucket only (MemoryStorage.h Iterator::seek), so
/// the default bucket count would make range(RANGE_SEEK, …) answer from bucket 0 alone.
struct HWCountingBackend
  : memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>
{
    using Base = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    HWCountingBackend() : Base(/*buckets*/ 1) {}

    size_t m_mergeCalls = 0;
    std::vector<std::vector<HWMergedKey>> m_mergedKeySets;
    template <class... FromStorages>
    task::AwaitableValue<void> merge(FromStorages&... fromStorages)
    {
        std::vector<HWMergedKey> keys;
        (collectKeys(fromStorages, keys), ...);
        ++m_mergeCalls;
        m_mergedKeySets.push_back(std::move(keys));
        return Base::merge(fromStorages...);
    }

private:
    static void collectKeys(auto& fromStorage, std::vector<HWMergedKey>& out)
    {
        task::syncWait([&]() -> task::Task<void> {
            auto iterator = co_await storage2::range(fromStorage);
            while (auto keyValue = co_await iterator.next())
            {
                auto&& [key, value] = *keyValue;
                out.emplace_back(
                    HWMergedKey{.key = key, .deleted = !std::holds_alternative<StateValue>(value)});
            }
        }());
    }
};

using HWCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, HWCountingBackend>;
using HWBaseMultiLayerStorage = MultiLayerStorage<HWMutableStorage, void, HWCheckpointBackend>;

/// The storage the scheduler commits through, with one switch: `m_failNextMerge` makes the next
/// mergeBackStorage throw instead of landing anything — the storage-layer shape of "this block's
/// WriteBatch did not land", which is the failure G9 is about.
///
/// std::logic_error, not std::runtime_error, and the reason is a macOS link artefact rather than
/// anything about coroutines: this repo propagates wedprcrypto PUBLIC from bcos-crypto to the
/// whole tree, and Apple arm64 libc++ decides typeinfo uniqueness by the name-pointer high bit,
/// so a thrown runtime_error binds only to an exact-type handler and falls straight through
/// coCommitBlock's `catch (std::exception&)`. The logic_error family binds normally, and
/// exact-type handlers (HistoryPruned, HistoryIndexUnavailable) are fine either way because they
/// hit the pointer-equality short circuit. On Linux — CI and production — neither family has the
/// problem. With logic_error the injected failure comes back as the commit Error a real backend
/// failure would produce, which is what this test needs to assert on.
struct HWFailingMultiLayerStorage : HWBaseMultiLayerStorage
{
    using HWBaseMultiLayerStorage::HWBaseMultiLayerStorage;

    bool m_failNextMerge = false;
    /// Run INSIDE the publish window: after the real merge has landed the block's rows and before
    /// mergeBackStorage returns, so the commit path has not published yet. That is the exact
    /// instant at which the disk is ahead of the index, and it is the only place a test can stand
    /// to observe what a query sees there.
    std::function<void()> m_afterMerge;

    task::Task<std::shared_ptr<MutableStorage>> mergeBackStorage(auto&... extraStorages)
    {
        if (m_failNextMerge)
        {
            m_failNextMerge = false;
            BOOST_THROW_EXCEPTION(std::logic_error("injected merge failure"));
        }
        auto merged = co_await HWBaseMultiLayerStorage::mergeBackStorage(extraStorages...);
        if (m_afterMerge)
        {
            auto const hook = std::exchange(m_afterMerge, {});
            hook();
        }
        co_return merged;
    }
};
using HWMultiLayerStorage = HWFailingMultiLayerStorage;

size_t countInTable(std::vector<HWMergedKey> const& keys, std::string_view table, bool deleted)
{
    return static_cast<size_t>(std::ranges::count_if(keys, [&](HWMergedKey const& merged) {
        return StateKeyView{merged.key}.m_table == table && merged.deleted == deleted;
    }));
}

/// Rows of one table left in the BACKEND (after merges), by row key.
std::vector<std::string> backendRowsOfTable(HWCountingBackend& backend, std::string_view table)
{
    std::vector<std::string> rows;
    task::syncWait([&]() -> task::Task<void> {
        auto iterator = co_await storage2::range(backend);
        while (auto keyValue = co_await iterator.next())
        {
            auto&& [key, value] = *keyValue;
            if (!std::holds_alternative<StateValue>(value))
            {
                continue;
            }
            StateKeyView const view{key};
            if (view.m_table == table)
            {
                rows.emplace_back(view.m_key);
            }
        }
    }());
    return rows;
}

/// Every row of one block in a shard table — its meta row (8-byte key) and its shard rows
/// (10-byte keys), which all start with the same big-endian block number (HistoryRowCodec.h).
size_t countHistoryRowsOfBlock(
    HWCountingBackend& backend, std::string_view table, protocol::BlockNumber block)
{
    auto const prefix = history::metaRowKey(block);
    auto const rows = backendRowsOfTable(backend, table);
    return static_cast<size_t>(std::ranges::count_if(
        rows, [&](std::string const& rowKey) { return rowKey.starts_with(prefix); }));
}

/// One flat-state row the mock scheduler writes for a block.
struct HWRow
{
    std::string table;
    std::string key;
    std::string value;
};

/// Scheduler impl that replays a per-block write plan through the storage BaselineScheduler
/// hands it — the production write path (the view's mutable layer).
struct HWWritingScheduler
{
    std::map<protocol::BlockNumber, std::vector<HWRow>> const* m_plan{};

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

struct HWExecutor
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

/// Records each MPT block's delta, so a case can compare what the builder produced against what
/// the history recorded instead of re-deriving one from the other.
struct HWDeltaRecorder : ledger::mpt::CommitObserver
{
    std::map<protocol::BlockNumber, size_t> m_preimageCounts;
    /// Throw once, for this block. The commit path calls observers AFTER the merge and BEFORE
    /// the committed block number advances, deliberately without a try/catch, so this is the
    /// simplest faithful way to make a commit fail at that exact point — which is the window the
    /// publish must not sit inside (G9, and the retry hazard the ordering comment describes).
    ///
    /// std::logic_error, not std::runtime_error: on Apple arm64 libc++ this repo's link graph
    /// (wedprcrypto propagated PUBLIC from bcos-crypto) makes the runtime_error family's typeinfo
    /// non-unique, so a thrown runtime_error binds only to an exact-type handler and falls
    /// through `catch (std::exception&)`. The logic_error family is unaffected. The distinction
    /// is a macOS link artefact, absent on Linux, and it decides only whether the failure reaches
    /// the test as an Error or as a throw.
    protocol::BlockNumber m_throwOnBlock{-1};

    void onCommit(protocol::BlockNumber blockNumber, ledger::mpt::PathDiff const& diff) override
    {
        m_preimageCounts[blockNumber] = diff.preimages.size();
        if (blockNumber == m_throwOnBlock)
        {
            m_throwOnBlock = -1;
            throw std::logic_error("injected commit-observer failure");
        }
    }
};

ledger::Features g_hwFeatures{};

[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    HWMultiLayerStorage::ViewType& /*storage*/, bcos::ledger::LedgerConfig& ledgerConfig,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    ledgerConfig.setFeatures(g_hwFeatures);
    return {};
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> emptyTxsTaskHW()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> emptySystemConfigsTaskHW()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> emptyFeaturesTaskHW()
{
    co_return ledger::Features{};
}

class HistoryWiringFixture
{
public:
    /// MPT activates at this block (scenario A), so the first MPT block is kActivation + 1.
    static constexpr protocol::BlockNumber kActivation = 500;
    /// Deliberately tiny, so a four-block run crosses the window and exercises expiry.
    static constexpr protocol::BlockNumber kDepth = 2;
    static constexpr uint32_t blockVersion = 200;

    HistoryWiringFixture()
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
        deltaRecorder(std::make_shared<HWDeltaRecorder>()),
        baselineScheduler(multiLayerStorage, mockScheduler, mockExecutor, *blockFactory,
            mockLedger.get(), mockTxPool.get(), *transactionSubmitResultFactory, *hashImpl)
    {
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_mpt_state_root);
        features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, kActivation);
        g_hwFeatures = features;

        mockScheduler.m_plan = &plan;
        useDepths(kDepth, kDepth);
        baselineScheduler.setMPTCommitObserver(deltaRecorder);

        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>,
                          bool) { callback({}, nullptr); });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskHW();
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
            return emptySystemConfigsTaskHW();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return emptyFeaturesTaskHW();
        });

        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    /// Give the scheduler a fresh MPTHistory at the requested depths, rebuilt from whatever is
    /// already on disk — the shape a restart with a changed nodeConfig [storage] has, which is
    /// the only way depths ever change in production (they are read once, at wiring time).
    void useDepths(protocol::BlockNumber state, protocol::BlockNumber proof)
    {
        mptHistory = std::make_shared<history::MPTHistory>(
            history::HistoryDepths{.state = state, .proof = proof},
            history::makeHistoryReader(backendStorage));
        task::syncWait(mptHistory->rebuild(backendStorage));
        baselineScheduler.setMPTHistory(mptHistory);
    }

    /// How many records the state history's index holds for one block, i.e. what the block's
    /// meta row declares — asserted through the INDEX rather than by re-decoding the shard, so a
    /// case that checks the count also checks that publishing happened.
    std::optional<uint32_t> recordedStateCount(protocol::BlockNumber block) const
    {
        auto const meta = mptHistory->state().index().blockMeta(block);
        return meta ? std::optional<uint32_t>{meta->recordCount} : std::nullopt;
    }
    std::optional<uint32_t> recordedTrieCount(protocol::BlockNumber block) const
    {
        auto const meta = mptHistory->trie().index().blockMeta(block);
        return meta ? std::optional<uint32_t>{meta->recordCount} : std::nullopt;
    }

    /// One flat write per block, to a per-block slot of the same account, so every block has a
    /// non-empty diff and consecutive blocks touch different keys.
    void planBlock(protocol::BlockNumber number)
    {
        auto const table = ledger::mpt::accountTableName(hwAccount());
        plan[number] = {{table, "balance", std::to_string(1000 + number)},
            {table, slotRowKey(static_cast<byte>(number & 0xFFU)),
                std::string(32, static_cast<char>(number & 0xFFU))}};
    }

    /// The one account every planned block writes to.
    static Address hwAccount()
    {
        auto address = Address{};
        address.data()[0] = 0xAB;
        return address;
    }

    static std::string slotRowKey(byte tail)
    {
        std::string key(h256::SIZE, '\0');
        key.back() = static_cast<char>(tail);
        return key;
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
    }

    /// Drive blocks [kActivation, last]. kActivation itself is still an XOR block (the
    /// activation boundary), so the MPT blocks are kActivation + 1 onwards.
    void runChain(protocol::BlockNumber last)
    {
        for (protocol::BlockNumber number = kActivation; number <= last; ++number)
        {
            planBlock(number);
            commitOneBlock(executeOneBlock(number));
        }
    }

    /// A store's retention boundary as it stands in the backend after the merges so far.
    std::optional<protocol::BlockNumber> boundaryOf(history::HistoryTables const& tables)
    {
        auto row = task::syncWait(storage2::readOne(backendStorage,
            StateKey{tables.boundary, std::string(history::kRetentionBoundaryRowKey)}));
        if (!row)
        {
            return std::nullopt;
        }
        return history::decodeRetentionBoundary(row->get());
    }

    HWCountingBackend backendStorage;
    HWCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    std::map<protocol::BlockNumber, std::vector<HWRow>> plan;
    HWWritingScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    HWMultiLayerStorage multiLayerStorage;
    HWExecutor mockExecutor;
    std::shared_ptr<HWDeltaRecorder> deltaRecorder;
    BaselineScheduler<decltype(multiLayerStorage), HWExecutor, HWWritingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
    /// Declared after the scheduler so it outlives it: the scheduler holds a shared_ptr copy, and
    /// this member is the one the cases read the index through.
    std::shared_ptr<history::MPTHistory> mptHistory;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(HistoryWiringSuite, HistoryWiringFixture)

/// G3: an MPT block's history rides the block's own single backend merge, together with its flat
/// rows and its trie-node rows. The activation block right before it is the in-test negative
/// control — it builds no MPT, so it writes no history at all.
BOOST_AUTO_TEST_CASE(historyRowsRideTheBlocksSingleMerge)
{
    runChain(kActivation);  // XOR block: no MPT, no history
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 1);
    auto const& xorMerge = backendStorage.m_mergedKeySets[0];
    BOOST_CHECK_EQUAL(countInTable(xorMerge, history::kStateHistory.shard, false), 0);
    BOOST_CHECK_EQUAL(countInTable(xorMerge, history::kTrieHistory.shard, false), 0);

    planBlock(kActivation + 1);
    commitOneBlock(executeOneBlock(kActivation + 1));

    // Exactly one more merge — the whole commit, history included, is one WriteBatch.
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 2);
    auto const& keys = backendStorage.m_mergedKeySets[1];

    // Both histories are present in that one merge, each as a meta row plus its one shard: this
    // block's diff is far under the 64 KiB shard cap, so the whole of it is one row. Two rows per
    // store, whatever the key count — which is the layout change PR-B made.
    BOOST_CHECK_EQUAL(countInTable(keys, history::kStateHistory.shard, false), 2U);
    BOOST_CHECK_EQUAL(countInTable(keys, history::kTrieHistory.shard, false), 2U);
    // The first recorded block also seeds each store's retention boundary, in the same batch.
    BOOST_CHECK_EQUAL(countInTable(keys, history::kStateHistory.boundary, false), 1U);
    BOOST_CHECK_EQUAL(countInTable(keys, history::kTrieHistory.boundary, false), 1U);
    // ...alongside the trie-node rows and the flat state rows that block wrote.
    BOOST_CHECK_GT(countInTable(keys, ledger::mpt::kMPTAccountTable, false), 0U);
    auto address = Address{};
    address.data()[0] = 0xAB;
    auto const table = ledger::mpt::accountTableName(address);
    BOOST_CHECK_EQUAL(countInTable(keys, table, false), 2U);

    // The state history recorded exactly the flat rows the block changed, no more — read back
    // from the published index, which also proves publishBlockHistory ran.
    BOOST_REQUIRE(recordedStateCount(kActivation + 1).has_value());
    BOOST_CHECK_EQUAL(*recordedStateCount(kActivation + 1), 2U);
    BOOST_CHECK(mptHistory->state().recordedBlock(kActivation + 1));
    BOOST_CHECK(mptHistory->trie().recordedBlock(kActivation + 1));
}

/// G9 at the commit path: a merge that fails publishes nothing, and re-driving the same height
/// publishes it exactly once.
///
/// The injection is a throwing `mergeBackStorage`, which is what a backend failure looks like
/// from coCommitBlock: the commit comes back as an Error, the block's rows never reach the
/// backend, and the stage is destroyed on the way out. The index must be byte-identical to what
/// it was, and the retry — the same header, driven again, as PBFT's failure handler does — must
/// leave it holding block N once.
BOOST_AUTO_TEST_CASE(aFailedMergePublishesNothingAndTheRetryPublishesOnce)
{
    auto const first = kActivation + 1;  // first MPT block
    runChain(first);                     // the activation block plus the first MPT block

    BOOST_REQUIRE(mptHistory->state().recordedBlock(first));
    BOOST_REQUIRE(!mptHistory->state().recordedBlock(first + 1));
    auto const versionsBefore = mptHistory->state().index().versionCount();
    auto const blocksBefore = mptHistory->state().index().blockCount();
    auto const mergesBefore = backendStorage.m_mergeCalls;

    planBlock(first + 1);
    auto const header = executeOneBlock(first + 1);

    multiLayerStorage.m_failNextMerge = true;
    Error::Ptr commitError;
    baselineScheduler.commitBlock(header,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
    BOOST_REQUIRE_MESSAGE(commitError, "the injected merge failure must fail the commit");

    // Nothing landed and nothing was published.
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, mergesBefore);
    BOOST_CHECK(!mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(!mptHistory->trie().recordedBlock(first + 1));
    BOOST_CHECK_EQUAL(mptHistory->state().index().versionCount(), versionsBefore);
    BOOST_CHECK_EQUAL(mptHistory->state().index().blockCount(), blocksBefore);
    BOOST_CHECK(mptHistory->state().index().state() == ledger::mpt::history::IndexState::Ready);
    BOOST_CHECK(!history::historyCoversBlock(mptHistory->state(), first, first + 1));

    // The retry commits the same block for real, and only now does the index hold it — once.
    commitOneBlock(header);
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, mergesBefore + 1);
    BOOST_CHECK(mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(mptHistory->trie().recordedBlock(first + 1));
    BOOST_CHECK(mptHistory->state().index().state() == ledger::mpt::history::IndexState::Ready);
    BOOST_CHECK_EQUAL(mptHistory->state().index().blockCount(), blocksBefore + 1);
    BOOST_CHECK_EQUAL(mptHistory->state().index().versionCount(), versionsBefore + 2);
    BOOST_CHECK(history::historyCoversBlock(mptHistory->state(), first, first + 1));
}

/// The publish must be the LAST fallible step before the committed block number advances, and
/// this is the case that pins it.
///
/// The commit observer runs after the merge and before that advance, and its contract says it
/// must not throw — but if it does (or if anything else in that stretch does), the commit returns
/// an error WITHOUT advancing the tip, and PBFT re-drives the same height
/// (LedgerStorage.cpp -> onStableCheckPointCommitFailed -> clearExceptionProposalState). A
/// publish sitting above that point would already have run for block N, and the re-drive would
/// run it again; HistoryIndex refuses the second as out-of-order, latches itself Unavailable and
/// rethrows, and from there every historical read on the node is refused until restart AND the
/// height can never commit, because each retry throws at the same place.
///
/// So the property to pin is the one that makes the second publish impossible: a commit that
/// fails ANYWHERE after the merge must leave the index exactly as it was. With the publish above
/// the observer this case goes red on every assertion below.
///
/// The retry half — "and the next attempt publishes it exactly once" — is asserted by the
/// merge-failure case above rather than here, because this fixture cannot re-commit after a
/// SUCCESSFUL merge: mergeBackStorage consumes the queued view, so a bare second commitBlock
/// fails with NotExistsImmutableStorageError. Production does not re-commit bare either; it
/// re-executes first, which pushes the view back.
BOOST_AUTO_TEST_CASE(aCommitFailingAfterTheMergePublishesNothing)
{
    auto const first = kActivation + 1;
    runChain(first);

    auto const versionsBefore = mptHistory->state().index().versionCount();
    auto const blocksBefore = mptHistory->state().index().blockCount();
    auto const mergesBefore = backendStorage.m_mergeCalls;

    planBlock(first + 1);
    auto const header = executeOneBlock(first + 1);

    // Throws once, for this block, from inside the commit path's observer call — which sits
    // after the merge and before the committed block number advances.
    deltaRecorder->m_throwOnBlock = first + 1;
    Error::Ptr commitError;
    baselineScheduler.commitBlock(header,
        [&](Error::Ptr error, ledger::LedgerConfig::Ptr) { commitError = std::move(error); });
    BOOST_REQUIRE_MESSAGE(commitError, "a throwing commit observer must fail the commit");
    BOOST_REQUIRE_EQUAL(deltaRecorder->m_throwOnBlock, protocol::BlockNumber{-1});

    // The merge DID land — this is a failure after it, not instead of it.
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, mergesBefore + 1);
    BOOST_REQUIRE_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kStateHistory.shard, first + 1), 2U);

    // ...and the index still knows nothing about the block, so the height stays re-drivable.
    BOOST_CHECK(mptHistory->state().index().state() == ledger::mpt::history::IndexState::Ready);
    BOOST_CHECK(!mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(!mptHistory->trie().recordedBlock(first + 1));
    BOOST_CHECK_EQUAL(mptHistory->state().index().blockCount(), blocksBefore);
    BOOST_CHECK_EQUAL(mptHistory->state().index().versionCount(), versionsBefore);
    BOOST_CHECK(!history::historyCoversBlock(mptHistory->state(), first, first + 1));
}

/// MF1: inside the publish window — after a block's rows have landed on disk and before the index
/// learns about them — a historical read must NOT hand back that block's value.
///
/// This is the hole two-phase commit leaves and locking cannot close: the wrong value would come
/// from the committed plane, which the index does not own. The case stands exactly there, in a
/// hook that runs inside mergeBackStorage after the real merge, and asks for a key that the
/// committing block N is the FIRST to touch, at a height B well below N. `locate` must miss (no
/// version was ever recorded for that key), so without the generation guard the query would fall
/// through to the current value — which by then is N's.
///
/// The negative control is in the same hook: a raw readOne of the same row DOES return N's value.
/// The wrong answer is available; the guard is what stops the query taking it.
BOOST_AUTO_TEST_CASE(aReadInsideThePublishWindowRefusesRatherThanSeeingTheNewBlock)
{
    useDepths(128, 128);  // nothing expires here; the subject is the window, not the boundary
    // The production budget is two seconds of WAITING for the window to close (HistoryIndex.h).
    // This case is about what happens when it never does, so shorten it — otherwise the case
    // spends two seconds proving a timeout it could prove in twenty milliseconds.
    mptHistory->state().setPublishWindowWaitBudget(std::chrono::milliseconds{20});
    auto const first = kActivation + 1;
    runChain(first + 1);  // blocks first and first+1 recorded; tip is first+1

    auto const committing = first + 2;  // block N, whose rows land inside the hook
    auto const queried = first;         // block B, two below N
    auto const table = ledger::mpt::accountTableName(hwAccount());
    auto const freshSlot = slotRowKey(static_cast<byte>(committing & 0xFFU));

    planBlock(committing);
    auto const header = executeOneBlock(committing);

    bool hookRan = false;
    bool generationWasOdd = false;
    bool rawReadSawTheNewBlock = false;
    bool guardedReadRefused = false;
    std::optional<std::string> guardedAnswer;

    multiLayerStorage.m_afterMerge = [&]() {
        hookRan = true;
        generationWasOdd = (mptHistory->state().index().generation() % 2) != 0;

        // The wrong answer is right there on the committed plane.
        auto const raw =
            task::syncWait(storage2::readOne(backendStorage, StateKey{table, freshSlot}));
        rawReadSawTheNewBlock = raw.has_value();

        // ...and the guarded read must not take it. tip is still first+1: the commit has not
        // advanced it yet, which is precisely why B is admissible and the window matters.
        try
        {
            auto const value = task::syncWait(history::readStateAtOrCurrent(mptHistory->state(),
                backendStorage, StateKeyView{table, freshSlot}, queried, first + 1, 128));
            if (value)
            {
                guardedAnswer = std::string(value->get());
            }
        }
        catch (history::HistoryIndexUnavailable const&)
        {
            guardedReadRefused = true;
        }
    };

    commitOneBlock(header);

    BOOST_REQUIRE_MESSAGE(hookRan, "the in-window hook must have run");
    BOOST_CHECK_MESSAGE(
        generationWasOdd, "the publish window must be open between the merge and the publish");
    BOOST_REQUIRE_MESSAGE(rawReadSawTheNewBlock,
        "the committed plane must already hold block N's row inside the window, or this case is "
        "not testing anything");
    BOOST_CHECK_MESSAGE(
        guardedReadRefused, "a read inside the publish window must refuse, got: "
                                << (guardedAnswer ? "a value" : "no value but no refusal either"));
    BOOST_CHECK_MESSAGE(!guardedAnswer.has_value(),
        "a read inside the publish window must never return the committing block's value");

    // And once the window has closed, the same question is answered — correctly. Block N is the
    // first to touch this slot, so at B it did not exist: absent, not N's value.
    BOOST_CHECK(mptHistory->state().recordedBlock(committing));
    BOOST_CHECK_EQUAL(mptHistory->state().index().generation() % 2, 0U);
    auto const afterPublish = task::syncWait(history::readStateAtOrCurrent(mptHistory->state(),
        backendStorage, StateKeyView{table, freshSlot}, queried, committing, 128));
    BOOST_CHECK_MESSAGE(!afterPublish.has_value(),
        "after the publish, the slot block N created must read as absent at an earlier height");

    // The row block N CHANGED (rather than created) reads back as its pre-image, so the index is
    // genuinely answering rather than refusing everything.
    auto const balanceAtB = task::syncWait(history::readStateAtOrCurrent(mptHistory->state(),
        backendStorage, StateKeyView{table, "balance"}, queried, committing, 128));
    BOOST_REQUIRE(balanceAtB.has_value());
    BOOST_CHECK_EQUAL(std::string(balanceAtB->get()), std::to_string(1000 + queried));
}

/// The other half of the window contract: a read that overlaps an ordinary commit must WAIT it
/// out and then answer, not refuse.
///
/// The refusal case above proves the gate holds; on its own it would also pass an implementation
/// that refused every overlapping query, which on a healthy node would break `eth_getBalance` at
/// a numeric head every time it raced a commit. So: open a window by hand, have another thread
/// close it after ~50 ms, and ask — on the reader's thread — for a row whose correct answer does
/// not depend on the block being committed. The read must block, wake, and return the recorded
/// pre-image.
///
/// The key is `balance`, which block first+1 changed: its answer at `first` is what block first
/// wrote, and it is settled by versions the index already holds. That is deliberate — the case
/// is about the WAIT, so its expected answer must not depend on what the other thread did.
BOOST_AUTO_TEST_CASE(aReadWaitsOutThePublishWindowAndThenAnswers)
{
    useDepths(128, 128);
    auto const first = kActivation + 1;
    runChain(first + 1);  // first and first+1 recorded

    auto const table = ledger::mpt::accountTableName(hwAccount());
    BOOST_REQUIRE(mptHistory->state().recordedBlock(first + 1));

    // A window, as the commit path would have opened it before its merge.
    mptHistory->state().openPublishWindow();
    BOOST_REQUIRE_EQUAL(mptHistory->state().index().generation() % 2, 1U);

    constexpr auto kHold = std::chrono::milliseconds{50};
    std::thread closer([&]() {
        std::this_thread::sleep_for(kHold);
        mptHistory->state().closePublishWindow();
    });

    auto const started = std::chrono::steady_clock::now();
    std::optional<executor_v1::StateValue> value;
    bool refused = false;
    try
    {
        value = task::syncWait(history::readStateAtOrCurrent(mptHistory->state(), backendStorage,
            StateKeyView{table, "balance"}, first, first + 1, 128));
    }
    catch (history::HistoryIndexUnavailable const&)
    {
        // Recorded rather than propagated, so the closer thread below is always joined — an
        // escaping exception would destroy a joinable std::thread and abort the process, turning
        // a readable failure into a SIGABRT.
        refused = true;
    }
    auto const waited = std::chrono::steady_clock::now() - started;
    closer.join();

    // It answered, and it answered correctly.
    BOOST_REQUIRE_MESSAGE(!refused, "the read must wait the window out, not refuse");
    BOOST_REQUIRE_MESSAGE(value.has_value(), "the read must succeed once the window closes");
    BOOST_CHECK_EQUAL(std::string(value->get()), std::to_string(1000 + first));

    // And it really waited rather than racing through before the window was seen: the answer
    // cannot have been produced before the closer thread ran.
    BOOST_CHECK_MESSAGE(waited >= kHold,
        "the read must have blocked until the window closed, waited "
            << std::chrono::duration_cast<std::chrono::milliseconds>(waited).count() << "ms");
    BOOST_CHECK_EQUAL(mptHistory->state().index().generation() % 2, 0U);
}

/// G9: the index learns about a block exactly when publishBlockHistory runs — not when the rows
/// are written, and never when the write did not land.
///
/// Asserted on the two-phase API directly, against the SAME stores the scheduler has been
/// publishing into, so the "before" state is a real chain's index rather than a fixture's:
///
///  1. stage block first + 1 into a batch of its own. That is byte-for-byte what coCommitBlock
///     does before the merge, and it is the state a FAILED merge leaves behind: rows in a batch
///     that never reaches the backend, and a HistoryCommitStage nobody publishes;
///  2. drop the stage, as a scope exit after a throwing merge does. The index must be untouched —
///     the block absent, a query for it refused, and a query at the height below it answering
///     exactly as before;
///  3. re-stage the same block (the retry), merge the batch, and only then publish. Both flip.
///
/// This case proves the API CONTRACT, not the commit path: moving publishBlockHistory to before
/// the merge in either scheduler would leave it green. The commit path's own ordering is pinned
/// by the two cases above, which drive it through a real failure.
BOOST_AUTO_TEST_CASE(publishingIsWhatMakesAStagedBlockVisible)
{
    auto const first = kActivation + 1;  // first MPT block
    runChain(first);                     // the activation block plus the first MPT block

    // Baseline: the state history covers `first`'s parent, and block first + 1 is not in it.
    BOOST_REQUIRE(mptHistory->state().recordedBlock(first));
    BOOST_REQUIRE(!mptHistory->state().recordedBlock(first + 1));
    auto const versionsBefore = mptHistory->state().index().versionCount();
    auto const blocksBefore = mptHistory->state().index().blockCount();

    std::map<ledger::mpt::PathKey, std::optional<bcos::bytes>> const noTrieChanges;

    // 1 + 2: staged, then dropped. An empty diff is enough — the block still gets
    // Meta{shardCount = 1, recordCount = 0} in each store, which is what recordedBlock and
    // historyCoversBlock answer from.
    {
        HWMutableStorage abandonedBatch;
        auto abandoned = task::syncWait(history::stageBlockHistory(backendStorage, abandonedBatch,
            first + 1, bcos::h256{}, std::span<StateKey const>{}, noTrieChanges, *mptHistory));
        BOOST_REQUIRE(abandoned.state.staged.has_value());
        BOOST_REQUIRE(abandoned.trie.staged.has_value());
    }  // the batch and the stage go out of scope together, exactly as a failed merge leaves them

    BOOST_CHECK(!mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(!mptHistory->trie().recordedBlock(first + 1));
    BOOST_CHECK_EQUAL(mptHistory->state().index().versionCount(), versionsBefore);
    BOOST_CHECK_EQUAL(mptHistory->state().index().blockCount(), blocksBefore);
    BOOST_CHECK(!history::historyCoversBlock(mptHistory->state(), first, first + 1));
    BOOST_CHECK(history::historyCoversBlock(mptHistory->state(), first - 1, first));
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kStateHistory.shard, first + 1), 0U);

    // 3: the retry. Same block, fresh batch, merge, then publish — the order coCommitBlock uses.
    HWMutableStorage batch;
    auto stage = task::syncWait(history::stageBlockHistory(backendStorage, batch, first + 1,
        bcos::h256{}, std::span<StateKey const>{}, noTrieChanges, *mptHistory));
    BOOST_CHECK(!mptHistory->state().recordedBlock(first + 1));
    task::syncWait(storage2::merge(backendStorage, batch));
    history::publishBlockHistory(*mptHistory, std::move(stage));

    BOOST_CHECK(mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(mptHistory->trie().recordedBlock(first + 1));
    BOOST_CHECK(history::historyCoversBlock(mptHistory->state(), first, first + 1));
    // And what it published is on disk: the block's meta row plus its one shard, per store.
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kStateHistory.shard, first + 1), 2U);
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kTrieHistory.shard, first + 1), 2U);
}

/// A depth of 0 is "not retained", and it has to mean NOTHING is written — not "written and
/// immediately expired". The block itself must still commit normally.
BOOST_AUTO_TEST_CASE(zeroDepthWritesNoHistory)
{
    useDepths(0, 0);
    runChain(kActivation + 1);

    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 2);
    for (auto const& keys : backendStorage.m_mergedKeySets)
    {
        BOOST_CHECK_EQUAL(countInTable(keys, history::kStateHistory.shard, false), 0U);
        BOOST_CHECK_EQUAL(countInTable(keys, history::kStateHistory.boundary, false), 0U);
        BOOST_CHECK_EQUAL(countInTable(keys, history::kTrieHistory.shard, false), 0U);
        BOOST_CHECK_EQUAL(countInTable(keys, history::kTrieHistory.boundary, false), 0U);
    }
    // The MPT block still committed its trie nodes — the depth governs the history, nothing else.
    BOOST_CHECK_GT(
        countInTable(backendStorage.m_mergedKeySets[1], ledger::mpt::kMPTAccountTable, false), 0U);
    BOOST_CHECK(backendRowsOfTable(backendStorage, history::kTrieHistory.shard).empty());
    // And nothing was published either: a store at depth 0 is never rebuilt, so its index stays
    // Empty and every historical read is refused rather than answered from the current state.
    BOOST_CHECK(!mptHistory->state().recordedBlock(kActivation + 1));
    BOOST_CHECK(mptHistory->state().index().state() == ledger::mpt::history::IndexState::Empty);
    BOOST_CHECK(!history::historyCoversBlock(mptHistory->state(), kActivation, kActivation + 1));
}

/// The trie history records one entry per position the block touched — PathDiff::preimages,
/// which spec A.7 requires to cover every upsert and every delete. Asserted against the delta
/// the builder actually produced (captured by the commit observer), not against a re-derivation.
BOOST_AUTO_TEST_CASE(trieHistoryEntryCountMatchesThePathDiff)
{
    runChain(kActivation + 1);

    auto const block = kActivation + 1;
    auto const recorded = deltaRecorder->m_preimageCounts.find(block);
    BOOST_REQUIRE(recorded != deltaRecorder->m_preimageCounts.end());
    BOOST_REQUIRE_GT(recorded->second, 0U);
    BOOST_REQUIRE(recordedTrieCount(block).has_value());
    BOOST_CHECK_EQUAL(*recordedTrieCount(block), recorded->second);
}

/// spec §12: the block leaving each window is expired IN THE COMMITTING BLOCK'S BATCH, so an
/// index version can never outlive the shard that holds it, crash or no crash. With a depth of 2,
/// committing block N drops block N-2 — and the index drops it in the same publish.
BOOST_AUTO_TEST_CASE(expiryDeletesRideTheSameBatch)
{
    auto const first = kActivation + 1;  // first MPT block
    runChain(first + 2);                 // first, first+1, first+2 — plus the XOR activation block

    // Block first+2's commit expires block first (first+2 - depth). Its deletes are in THAT
    // merge, not in one of their own: the block's meta row and its one shard row, per store.
    auto const& lastMerge = backendStorage.m_mergedKeySets.back();
    BOOST_CHECK_EQUAL(countInTable(lastMerge, history::kStateHistory.shard, true), 2U);
    BOOST_CHECK_EQUAL(countInTable(lastMerge, history::kTrieHistory.shard, true), 2U);
    // Still one merge per block: expiry added deletes, not a second write path.
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, 4U);

    // And the effect landed on disk: block `first` has no rows left, the younger blocks do.
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kStateHistory.shard, first), 0U);
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kTrieHistory.shard, first), 0U);
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kStateHistory.shard, first + 1), 2U);
    BOOST_CHECK_EQUAL(
        countHistoryRowsOfBlock(backendStorage, history::kTrieHistory.shard, first + 2), 2U);

    // ...and in memory, in the same publish: the retired block is gone from the index too, so no
    // lookup can locate a version whose shard row was just deleted.
    BOOST_CHECK(!mptHistory->state().recordedBlock(first));
    BOOST_CHECK(!mptHistory->trie().recordedBlock(first));
    BOOST_CHECK(mptHistory->state().recordedBlock(first + 1));
    BOOST_CHECK(mptHistory->state().recordedBlock(first + 2));
}

/// spec §13's retention boundary as the commit path actually writes it, in the two shapes where
/// "the block leaving the window" is NOT the oldest recorded block.
///
/// (i) Mid-chain activation. History starts at the first MPT block N0, so N0 - 1 is the oldest
/// height it can answer for — but the same commit also expires N0 - H, which for a window wider
/// than the chain is far below that and records nothing real. The boundary must be the seed.
///
/// (ii) A raised depth. Once the chain is deep enough for expiry to bite, the boundary tracks
/// N - H upward; raising H across a restart makes the next commit's N - H jump BACKWARDS. The
/// boundary must not follow it down, because the blocks in between are already deleted.
///
/// Each assertion is made against the DISK row and against the in-memory index, which must agree:
/// the index applies exactly what the batch wrote.
BOOST_AUTO_TEST_CASE(retentionBoundaryTracksTheOldestAnswerableBlock)
{
    auto const first = kActivation + 1;  // the first MPT block, and the first recorded one

    // (i) A window narrower than the chain height, so the first MPT block's own commit ALSO
    // expires block first - depth. That block is far below anything this store ever recorded,
    // and the boundary must stay at the seed rather than follow it down.
    useDepths(128, 128);
    runChain(first);
    auto const seeded = boundaryOf(history::kStateHistory);
    BOOST_REQUIRE_MESSAGE(seeded.has_value(), "the first recorded block must seed the boundary");
    BOOST_CHECK_EQUAL(*seeded, first - 1);
    BOOST_CHECK_EQUAL(boundaryOf(history::kTrieHistory).value_or(-1), first - 1);
    BOOST_CHECK_EQUAL(mptHistory->state().index().boundary().value_or(-1), first - 1);

    // Two more blocks under a NARROW window: now expiry bites and the boundary climbs with it.
    useDepths(1, 1);
    planBlock(first + 1);
    commitOneBlock(executeOneBlock(first + 1));
    planBlock(first + 2);
    commitOneBlock(executeOneBlock(first + 2));
    auto const climbed = boundaryOf(history::kStateHistory);
    BOOST_REQUIRE(climbed.has_value());
    BOOST_CHECK_EQUAL(*climbed, first + 1);  // block first+2 expired first+1
    BOOST_CHECK_EQUAL(mptHistory->state().index().boundary().value_or(-1), first + 1);

    // (ii) The operator raises the depth. block - depth is now well below the boundary, and the
    // boundary must hold: the blocks in between are gone, and claiming them intact would make
    // every query for them answer from the current state.
    useDepths(200, 200);
    planBlock(first + 3);
    commitOneBlock(executeOneBlock(first + 3));
    auto const held = boundaryOf(history::kStateHistory);
    BOOST_REQUIRE(held.has_value());
    BOOST_CHECK_MESSAGE(*held == first + 1,
        "raising the depth must not walk the boundary back down, got " << *held);
    BOOST_CHECK_EQUAL(mptHistory->state().index().boundary().value_or(-1), first + 1);
    // A query below the boundary is refused rather than answered from the current state (G6).
    BOOST_CHECK_THROW(
        std::ignore = history::historyCoversBlock(mptHistory->state(), first, first + 3),
        history::HistoryPruned);
}

BOOST_AUTO_TEST_SUITE_END()
