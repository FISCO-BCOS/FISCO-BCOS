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
 * @file TestCommitSingleBatch.cpp
 * @brief Tripwire for the single-WriteBatch commit invariant.
 *
 * BaselineScheduler::coCommitBlock funnels EVERYTHING a block commit persists
 * through exactly one backend merge: prewriteBlockToBuffer collects the ledger
 * rows (header, hash mappings, nonces, tx metadata) into prewriteStorage, and
 * mergeBackStorage(prewriteStorage) (BaselineScheduler.h) folds the block's
 * mutable layer PLUS that buffer into the backend via a single
 * storage2::merge(m_latestBackend, backStorage, extraStorages...)
 * (MultiLayerStorage.h, non-cache arm). MPT trie-node rows need no extra path:
 * they were written into the same mutable layer at execute time as ordinary
 * "/mpt/" state rows (MPTNodeStorage.h).
 *
 * Transitive closure with the physical layer: TestRocksDBStorage2 (bcos-storage,
 * cases `merge` / `mergeAppliesSourcesInArgumentOrder`) pins that one
 * RocksDBStorage2::merge call = one rocksdb::WriteBatch handed to a single
 * rocksdb::DB::Write (RocksDBStorage2.h). This file pins the logical layer:
 * one commit = exactly one backend merge whose key set is complete (header row,
 * number<->hash index rows, transaction-touched state rows, and — under
 * feature_mpt_state_root — the "/mpt/" trie-node rows). Together: one commit,
 * one WriteBatch, one Write. If a refactor ever splits the commit into several
 * backend writes (reintroducing the half-commit window the retired checker
 * #5333 hunted for), the exactly-once assertions here go red.
 *
 * The counting backend intercepts the merge by shadowing MemoryStorage's
 * variadic `merge` member — storage2::merge is a member-function-first CPO
 * (Storage.h `Merge`), so the derived overload is what mergeBackStorage hits.
 *
 * Everything lives in an anonymous namespace with SB-prefixed storage types so
 * this TU's getLedgerConfig tag_invoke stub cannot collide with the other
 * scheduler test TUs under a unity build (same pattern as
 * TestExecuteStateRootRegression.cpp / TestMPTSchedulerWiring.cpp).
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
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-storage/KeyPrefixes.h"
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

using SBMutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

/// Backend that counts merge calls and records, per call, every key the merge
/// lands — the block's mutable layer and all extra storages (prewriteStorage).
/// storage2::merge dispatches to the member function (Storage.h `Merge` CPO), so
/// shadowing MemoryStorage's variadic merge intercepts exactly the call
/// MultiLayerStorage::mergeBackStorage makes on m_latestBackend; all other
/// storage2 CPOs keep hitting the inherited MemoryStorage members. A distinct
/// type (not an alias) also anchors this TU's getLedgerConfig tag_invoke stub
/// for ADL through the ViewType — same reasoning as TestMPTSchedulerWiring.
struct SBCountingBackend
  : memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>
{
    using Base = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    size_t m_mergeCalls = 0;
    std::vector<std::vector<StateKey>> m_mergedKeySets;  // one key list per merge call

    template <class... FromStorages>
    task::AwaitableValue<void> merge(FromStorages&... fromStorages)
    {
        std::vector<StateKey> keys;
        (collectKeys(fromStorages, keys), ...);
        ++m_mergeCalls;
        m_mergedKeySets.push_back(std::move(keys));
        return Base::merge(fromStorages...);
    }

private:
    static void collectKeys(auto& fromStorage, std::vector<StateKey>& out)
    {
        task::syncWait([&]() -> task::Task<void> {
            auto iterator = co_await storage2::range(fromStorage);
            while (auto keyValue = co_await iterator.next())
            {
                auto&& [key, value] = *keyValue;
                out.emplace_back(key);
            }
        }());
    }
};

using SBCheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, SBCountingBackend>;
using SBMultiLayerStorage = MultiLayerStorage<SBMutableStorage, void, SBCheckpointBackend>;

bool keySetContains(std::vector<StateKey> const& keys, std::string_view table, std::string_view key)
{
    return std::ranges::any_of(keys, [&](StateKey const& stateKey) {
        return StateKeyView{stateKey}.get() == std::tuple{table, key};
    });
}

size_t mptKeyCount(std::vector<StateKey> const& keys)
{
    return static_cast<size_t>(std::ranges::count_if(keys, [](StateKey const& stateKey) {
        return StateKeyView{stateKey}.m_table == storage2::kMPTTable;
    }));
}

/// One flat-state row the mock scheduler writes for a block.
struct SBRow
{
    std::string table;
    std::string key;
    std::string value;
};

/// Scheduler impl that replays a per-block write plan through the storage
/// argument BaselineScheduler hands it — the production write path (the view's
/// mutable layer), which commit later merges into the backend.
struct SBWritingScheduler
{
    std::map<protocol::BlockNumber, std::vector<SBRow>> const* m_plan{};

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

struct SBExecutor
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

/// The Features the getLedgerConfig stub hands to every execute — set per test.
ledger::Features g_sbFeatures{};

[[maybe_unused]] task::AwaitableValue<void> tag_invoke(
    ledger::tag_t<bcos::ledger::getLedgerConfig> /*unused*/,
    SBMultiLayerStorage::ViewType& /*storage*/, bcos::ledger::LedgerConfig& ledgerConfig,
    protocol::BlockNumber /*blockNumber*/, protocol::BlockFactory& /*blockFactory*/)
{
    ledgerConfig.setFeatures(g_sbFeatures);
    return {};
}

task::Task<std::vector<protocol::Transaction::ConstPtr>> emptyTxsTaskSB()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> emptySystemConfigsTaskSB()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> emptyFeaturesTaskSB()
{
    co_return ledger::Features{};
}

class CommitSingleBatchFixture
{
public:
    CommitSingleBatchFixture()
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
        g_sbFeatures = ledger::Features{};
        mockScheduler.m_plan = &plan;

        // Unlike the no-op stubs in the sibling fixtures, this asyncPrewriteBlock
        // writes the same three ledger index rows the production LedgerImpl
        // writes (Ledger.cpp asyncPrewriteBlock: number->hash, hash->number,
        // number->header) through the storage it is handed. coCommitBlock hands
        // it prewriteStorage (via prewriteBlockToBuffer's LegacyStorageWrapper),
        // so these rows MUST travel in the same single backend merge as the
        // block's state rows — which is exactly what the key-set assertions pin.
        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo(
                [](storage::StorageInterface::Ptr storage, protocol::ConstTransactionsPtr,
                    protocol::Block::ConstPtr block,
                    std::function<void(std::string, Error::Ptr&&)> callback, bool,
                    std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>, bool) {
                    auto header = block->blockHeader();
                    auto blockNumberStr = boost::lexical_cast<std::string>(header->number());
                    auto hash = header->hash();
                    auto hashView =
                        std::string_view(reinterpret_cast<const char*>(hash.data()), hash.SIZE);

                    storage::Entry hashEntry;
                    hashEntry.set(hash.asBytes());
                    storage->asyncSetRow(ledger::SYS_NUMBER_2_HASH, blockNumberStr,
                        std::move(hashEntry), [](Error::UniquePtr) {});

                    storage::Entry hash2NumberEntry;
                    hash2NumberEntry.set(blockNumberStr);
                    storage->asyncSetRow(ledger::SYS_HASH_2_NUMBER, hashView,
                        std::move(hash2NumberEntry), [](Error::UniquePtr) {});

                    bytes headerBuffer;
                    header->encode(headerBuffer);
                    storage::Entry number2HeaderEntry;
                    number2HeaderEntry.set(std::move(headerBuffer));
                    storage->asyncSetRow(ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr,
                        std::move(number2HeaderEntry), [](Error::UniquePtr) {});

                    callback({}, nullptr);
                });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return emptyTxsTaskSB();
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
            return emptySystemConfigsTaskSB();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return emptyFeaturesTaskSB();
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
        g_sbFeatures = features;
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

    std::optional<storage::Entry> backendRow(std::string_view table, std::string_view key)
    {
        return task::syncWait(storage2::readOne(backendStorage, StateKey{table, key}));
    }

    /// Assert the invariant contract for one committed block against merge call
    /// @p mergeIndex : the block's header / number->hash / hash->number rows and
    /// every planned state row all travelled in that single merge.
    void checkCommitKeySet(size_t mergeIndex, protocol::BlockHeader::Ptr const& header)
    {
        BOOST_REQUIRE_GT(backendStorage.m_mergedKeySets.size(), mergeIndex);
        auto const& keys = backendStorage.m_mergedKeySets[mergeIndex];
        auto blockNumberStr = boost::lexical_cast<std::string>(header->number());
        auto hash = header->hash();
        auto hashView = std::string_view(reinterpret_cast<const char*>(hash.data()), hash.SIZE);

        BOOST_CHECK_MESSAGE(keySetContains(keys, ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr),
            "header row missing from the commit merge for block " + blockNumberStr);
        BOOST_CHECK_MESSAGE(keySetContains(keys, ledger::SYS_NUMBER_2_HASH, blockNumberStr),
            "number->hash row missing from the commit merge for block " + blockNumberStr);
        BOOST_CHECK_MESSAGE(keySetContains(keys, ledger::SYS_HASH_2_NUMBER, hashView),
            "hash->number row missing from the commit merge for block " + blockNumberStr);
        for (auto const& row : plan.at(header->number()))
        {
            BOOST_CHECK_MESSAGE(keySetContains(keys, row.table, row.key),
                "state row missing from the commit merge: " + row.table + ":" + row.key);
        }
    }

    static constexpr uint32_t blockVersion = 200;

    SBCountingBackend backendStorage;
    SBCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;

    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    std::map<protocol::BlockNumber, std::vector<SBRow>> plan;
    SBWritingScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    SBMultiLayerStorage multiLayerStorage;
    SBExecutor mockExecutor;
    BaselineScheduler<decltype(multiLayerStorage), SBExecutor, SBWritingScheduler,
        ledger::LedgerInterface>
        baselineScheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestCommitSingleBatch, CommitSingleBatchFixture)

// (a)+(b)+(d) Flat (XOR-era) block: commit lands the header row, both hash index
// rows, and every transaction-touched state row through EXACTLY ONE backend
// merge; before the commit the backend has seen zero merges and none of those
// rows. A second block's commit drives the counter to exactly 2 — proving the
// counter is live (the exactly-once assertion is not vacuously green) and that
// the invariant is per-commit. Negative controls: a never-written row is absent
// from the recorded key set, and a flat commit carries no "/mpt/" keys.
BOOST_AUTO_TEST_CASE(flatCommitIsExactlyOneMergeWithFullKeySet)
{
    plan[100] = {{"/apps/aabbccdd00112233", "balance", "1000000"},
        {"/apps/eeff445566778899", "code", "0x60806040526004361061"}};
    plan[101] = {{"/apps/aabbccdd00112233", "balance", "999999"}};

    auto header100 = executeOneBlock(100);

    // (d) Executed but not committed: zero merges, zero rows in the backend.
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, 0);
    BOOST_CHECK(!backendRow("/apps/aabbccdd00112233", "balance"));
    BOOST_CHECK(!backendRow(ledger::SYS_NUMBER_2_BLOCK_HEADER, "100"));

    commitOneBlock(header100);

    // (a) Exactly one backend merge for the whole commit.
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 1);
    // (b) ...and that one merge carried the complete key set.
    checkCommitKeySet(0, header100);
    // Negative control for (b): the recorded key set is discriminating.
    BOOST_CHECK(!keySetContains(
        backendStorage.m_mergedKeySets[0], "/apps/aabbccdd00112233", "never_written"));
    // Negative control for the MPT case below: no trie-node rows in a flat commit.
    BOOST_CHECK_EQUAL(mptKeyCount(backendStorage.m_mergedKeySets[0]), 0);

    // The merged rows are really in the backend now (the wrapper forwarded).
    auto balance = backendRow("/apps/aabbccdd00112233", "balance");
    BOOST_REQUIRE(balance);
    BOOST_CHECK_EQUAL(std::string(balance->get()), "1000000");
    BOOST_CHECK(backendRow(ledger::SYS_NUMBER_2_BLOCK_HEADER, "100"));

    // Counter sensitivity: the next commit is again exactly one merge.
    auto header101 = executeOneBlock(101);
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, 1);
    commitOneBlock(header101);
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 2);
    checkCommitKeySet(1, header101);
}

// (c) MPT block (feature_mpt_state_root active): the execute-time trie build
// writes its node rows into the block's mutable layer as ordinary "/mpt/" state
// keys (MPTNodeStorage.h), so the SAME single commit merge must carry them
// alongside the flat state rows and the ledger index rows. Before the commit no
// "/mpt/" row exists in the backend; the activation-block (XOR) commit right
// before is the in-test negative control (merge #0 carries none).
BOOST_AUTO_TEST_CASE(mptCommitSingleMergeIncludesTrieNodeRows)
{
    namespace mpt = bcos::ledger::mpt;
    useScenarioA(500);

    auto address = Address{};
    address.data()[0] = 0xAB;
    auto const table = mpt::accountTableName(address);
    plan[500] = {{table, "balance", "1000000"}};  // activation block: still XOR
    plan[501] = {{table, "balance", "42"}};       // first MPT block

    auto header500 = executeOneBlock(500);
    commitOneBlock(header500);
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 1);
    checkCommitKeySet(0, header500);
    BOOST_CHECK_EQUAL(mptKeyCount(backendStorage.m_mergedKeySets[0]), 0);  // XOR: no nodes

    auto header501 = executeOneBlock(501);
    // (d) MPT nodes built at execute time stay in the pending layer: still only
    // the one merge from block 500, and no "/mpt/" row reached the backend.
    BOOST_CHECK_EQUAL(backendStorage.m_mergeCalls, 1);

    commitOneBlock(header501);

    // (a) The MPT block's commit is still exactly one backend merge...
    BOOST_REQUIRE_EQUAL(backendStorage.m_mergeCalls, 2);
    // (b) ...with the full flat/ledger key set...
    checkCommitKeySet(1, header501);
    // (c) ...AND the trie-node rows riding in the same merge.
    auto const mptRows = mptKeyCount(backendStorage.m_mergedKeySets[1]);
    BOOST_CHECK_GT(mptRows, 0);

    // The node rows really landed (spot-check one recorded "/mpt/" key).
    auto const& keys = backendStorage.m_mergedKeySets[1];
    auto nodeKey = std::ranges::find_if(keys, [](StateKey const& stateKey) {
        return StateKeyView{stateKey}.m_table == storage2::kMPTTable;
    });
    BOOST_REQUIRE(nodeKey != keys.end());
    auto nodeView = StateKeyView{*nodeKey};
    BOOST_CHECK(backendRow(nodeView.m_table, nodeView.m_key));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
