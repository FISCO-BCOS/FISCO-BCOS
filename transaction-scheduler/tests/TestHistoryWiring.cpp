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
#include <fakeit.hpp>
#include <map>
#include <optional>
#include <string>
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
using HWMultiLayerStorage = MultiLayerStorage<HWMutableStorage, void, HWCheckpointBackend>;

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

    void onCommit(protocol::BlockNumber blockNumber, ledger::mpt::PathDiff const& diff) override
    {
        m_preimageCounts[blockNumber] = diff.preimages.size();
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
        auto address = Address{};
        address.data()[0] = 0xAB;
        auto const table = ledger::mpt::accountTableName(address);
        plan[number] = {{table, "balance", std::to_string(1000 + number)},
            {table, slotRowKey(static_cast<byte>(number & 0xFFU)),
                std::string(32, static_cast<char>(number & 0xFFU))}};
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
/// The failure is modelled by dropping the stage rather than by making the backend's merge throw.
/// That injection was tried and abandoned: an exception raised under a `co_await` does not reach
/// coCommitBlock's `catch (std::exception&)` on this toolchain, the coroutine frame is never
/// destroyed, its commit lock is never released, and the process dies at fixture teardown. The
/// finding is reported separately; what it cannot do is carry this assertion.
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
