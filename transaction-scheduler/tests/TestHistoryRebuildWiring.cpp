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
 * @file TestHistoryRebuildWiring.cpp
 * @brief A restart, end to end: the index a node rebuilds at startup must answer exactly what the
 *        index the commit path built answers (layout spec §1.5, G10).
 *
 * The in-memory index is DERIVED data — it is never written to disk, and a node that restarts has
 * only the shard rows to recompute it from. That makes the rebuild load-bearing in a way an
 * ordinary cache is not: a version the walk misses does not read as "unknown", it reads as "this
 * key never changed", which is today's value presented as block B's (G6). So the property to
 * pin is not "the rebuild produces a plausible index" but "the rebuild produces THE index".
 *
 * The chain is driven through the real BaselineScheduler, so the rows being walked are the rows
 * the commit path actually wrote — not a fixture's idea of them. Then a SECOND MPTHistory is
 * built over the same backend, exactly as Initializer::init does at startup, and the two are
 * compared on the only thing that matters: the answers.
 */

#include "SharedBaselineSchedulerMock.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-ledger/mpt/Classify.h"
#include "bcos-ledger/mpt/Constants.h"
#include "bcos-ledger/mpt/history/HistoryRead.h"
#include "bcos-ledger/mpt/history/MPTHistory.h"
#include "bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-transaction-scheduler/BaselineScheduler-tpp.h"
#include <boost/test/unit_test.hpp>
#include <fakeit.hpp>
#include <map>
#include <memory>
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

using HRRowOp = bcos::test::sharedmock::SharedMockScheduler::RowOp;

task::Task<std::vector<protocol::Transaction::ConstPtr>> hrEmptyTxsTask()
{
    co_return std::vector<protocol::Transaction::ConstPtr>{};
}
task::Task<ledger::SystemConfigs> hrEmptySystemConfigsTask()
{
    co_return ledger::SystemConfigs{};
}
task::Task<ledger::Features> hrEmptyFeaturesTask()
{
    co_return ledger::Features{};
}

class HistoryRebuildFixture
{
public:
    using HRBackend = bcos::test::sharedmock::SharedBackendStorage;

    /// MPT activates here, so the first MPT block — and the first block with any history — is
    /// kActivation + 1.
    static constexpr protocol::BlockNumber kActivation = 300;
    /// Wide enough that nothing expires across the run: this case is about the rebuild, and an
    /// expiry in the middle would make "the two indexes agree" a weaker claim than it looks.
    static constexpr protocol::BlockNumber kDepth = 64;
    static constexpr uint32_t blockVersion = 200;

    HistoryRebuildFixture()
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
        ledger::Features features;
        features.set(ledger::Features::Flag::feature_mpt_state_root);
        features.setActivationBlock(ledger::Features::Flag::feature_mpt_state_root, kActivation);
        bcos::test::sharedmock::g_stubFeatures = features;

        mockScheduler.m_plan = &plan;
        mptHistory = makeHistory();
        baselineScheduler.setMPTHistory(mptHistory);

        fakeit::When(Method(mockLedger, asyncPrewriteBlock))
            .AlwaysDo([](storage::StorageInterface::Ptr, protocol::ConstTransactionsPtr,
                          protocol::Block::ConstPtr,
                          std::function<void(std::string, Error::Ptr&&)> callback, bool,
                          std::optional<ledger::Features>, std::optional<bcos::crypto::HashType>,
                          bool) { callback({}, nullptr); });
        using HashView =
            ::ranges::any_view<h256, ::ranges::category::mask | ::ranges::category::sized>;
        fakeit::When(Method(mockTxPool, getTransactions)).AlwaysDo([](HashView) {
            return hrEmptyTxsTask();
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
            return hrEmptySystemConfigsTask();
        });
        fakeit::When(Method(mockLedger, fetchAllFeatures)).AlwaysDo([](protocol::BlockNumber) {
            return hrEmptyFeaturesTask();
        });

        baselineScheduler.registerBlockNumberNotifier([](protocol::BlockNumber) {});
        baselineScheduler.registerTransactionNotifier(
            [](protocol::BlockNumber, protocol::TransactionSubmitResultsPtr,
                std::function<void(Error::Ptr)> callback) { callback(nullptr); });
    }

    /// What Initializer::init builds: an MPTHistory at this node's depths over the committed
    /// backend. NOT rebuilt here — the caller decides, because the whole point of one of the
    /// cases below is what an un-rebuilt index answers.
    std::shared_ptr<history::MPTHistory> makeHistory()
    {
        return std::make_shared<history::MPTHistory>(
            history::HistoryDepths{.state = kDepth, .proof = kDepth},
            history::makeHistoryReader(backendStorage));
    }

    static Address account()
    {
        auto address = Address{};
        address.data()[0] = 0xC1;
        return address;
    }

    static std::string slotRowKey(bcos::byte tail)
    {
        std::string key(h256::SIZE, '\0');
        key.back() = static_cast<char>(tail);
        return key;
    }

    /// Two rows per block: the account's balance (rewritten every block, so it accumulates one
    /// version per block) and a per-block slot (touched once, so most blocks never change it
    /// again). Between them the sampled keys below cover both index shapes — a long version
    /// vector and a single version.
    void planBlock(protocol::BlockNumber number)
    {
        auto const table = ledger::mpt::accountTableName(account());
        plan[number] = {{table, "balance", std::to_string(1000 + number)},
            {table, slotRowKey(static_cast<bcos::byte>(number & 0xFFU)),
                std::string(32, static_cast<char>(number & 0xFFU))}};
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

    /// Drive blocks [kActivation, last]. kActivation itself is the activation boundary and still
    /// builds no MPT, so the recorded blocks are kActivation + 1 onwards.
    protocol::BlockNumber runChain(protocol::BlockNumber last)
    {
        for (protocol::BlockNumber number = kActivation; number <= last; ++number)
        {
            planBlock(number);
            commitOneBlock(executeOneBlock(number));
        }
        return last;
    }

    /// One historical answer, rendered so two indexes can be compared as strings — including
    /// which ARM of ReadAtResult answered, because "absent at that block" and "unchanged since
    /// that block" are different facts that must not be conflated by the comparison.
    std::string answerAt(history::MPTHistory& source, std::string_view table,
        std::string_view rowKey, protocol::BlockNumber block, protocol::BlockNumber tip)
    {
        auto result = task::syncWait(history::readStateAt(
            source.state(), *source.backend(), StateKeyView{table, rowKey}, block, tip, kDepth));
        if (auto* bytes = std::get_if<bcos::bytes>(std::addressof(result)))
        {
            return "value:" + std::string(bytes->begin(), bytes->end());
        }
        if (std::holds_alternative<history::HistoryAbsent>(result))
        {
            return "absent";
        }
        return "current";
    }

    HRBackend backendStorage;
    bcos::test::sharedmock::SharedCheckpointBackend checkpointBackend;
    crypto::CryptoSuite::Ptr cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> blockFactory;
    std::shared_ptr<protocol::TransactionSubmitResultFactoryImpl> transactionSubmitResultFactory;
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();

    std::map<protocol::BlockNumber, std::vector<HRRowOp>> plan;
    bcos::test::sharedmock::SharedMockScheduler mockScheduler;
    fakeit::Mock<ledger::LedgerInterface> mockLedger;
    fakeit::Mock<txpool::TxPoolInterface> mockTxPool;
    bcos::test::sharedmock::SharedMultiLayerStorage multiLayerStorage;
    bcos::test::sharedmock::SharedMockExecutor mockExecutor;
    // Resets the shared g_stubFeatures at fixture teardown (SharedBaselineSchedulerMock.h).
    bcos::test::sharedmock::ScopedStubFeatures m_featuresGuard;
    bcos::test::sharedmock::SharedBaselineScheduler baselineScheduler;
    /// Declared after the scheduler so it is destroyed first; the scheduler holds its own copy.
    std::shared_ptr<history::MPTHistory> mptHistory;
};

}  // namespace

BOOST_FIXTURE_TEST_SUITE(HistoryRebuildWiringSuite, HistoryRebuildFixture)

/// A restart reproduces the index the commit path built: same answer for every sampled key at
/// every retained height, and the same number of versions overall.
///
/// The version count is checked as well as the answers because the answers alone cannot catch a
/// DUPLICATED version — a key whose vector gained a second copy of the same block still answers
/// correctly today and breaks the moment that block is expired.
BOOST_AUTO_TEST_CASE(aRestartRebuildsTheSameAnswers)
{
    auto const first = kActivation + 1;  // the first recorded block
    auto const tip = runChain(first + 5);

    auto const table = ledger::mpt::accountTableName(account());
    BOOST_REQUIRE_GT(mptHistory->state().index().versionCount(), 0U);

    // The startup path, on the same backend: a fresh MPTHistory, rebuilt synchronously.
    auto rebuilt = makeHistory();
    task::syncWait(rebuilt->rebuild(backendStorage));

    BOOST_CHECK(rebuilt->state().index().state() == history::IndexState::Ready);
    BOOST_CHECK(rebuilt->trie().index().state() == history::IndexState::Ready);
    BOOST_CHECK_EQUAL(
        rebuilt->state().index().versionCount(), mptHistory->state().index().versionCount());
    BOOST_CHECK_EQUAL(
        rebuilt->trie().index().versionCount(), mptHistory->trie().index().versionCount());
    BOOST_CHECK_EQUAL(rebuilt->state().index().keyCount(), mptHistory->state().index().keyCount());
    BOOST_CHECK_EQUAL(
        rebuilt->state().index().blockCount(), mptHistory->state().index().blockCount());
    BOOST_CHECK_EQUAL(rebuilt->state().index().boundary().value_or(-1),
        mptHistory->state().index().boundary().value_or(-1));

    // The sampled key set: the row rewritten every block (a long version vector), one row touched
    // by exactly one block (a single version), one touched by a different block, and a row this
    // chain never wrote at all — the case a broken rebuild answers WRONG rather than not at all,
    // because a missing version and an untouched key look the same from the outside.
    std::vector<std::string> const sampledRows{"balance",
        slotRowKey(static_cast<bcos::byte>(first & 0xFFU)),
        slotRowKey(static_cast<bcos::byte>((first + 3) & 0xFFU)),
        slotRowKey(static_cast<bcos::byte>(0xEEU))};

    std::size_t comparisons = 0;
    for (auto const& rowKey : sampledRows)
    {
        for (protocol::BlockNumber block = first - 1; block <= tip; ++block)
        {
            auto const before = answerAt(*mptHistory, table, rowKey, block, tip);
            auto const after = answerAt(*rebuilt, table, rowKey, block, tip);
            BOOST_CHECK_MESSAGE(before == after, "rebuilt index disagrees at block "
                                                     << block << " for row '" << rowKey << "': '"
                                                     << before << "' vs '" << after << "'");
            ++comparisons;
        }
    }
    // Guard against the comparison loop silently degenerating to nothing.
    BOOST_CHECK_EQUAL(comparisons, sampledRows.size() * static_cast<std::size_t>(tip - first + 2));

    // The answers are not all "current" — a rebuild that produced an EMPTY index would satisfy
    // every equality above if the live index were empty too, so at least one recorded pre-image
    // has to be observed coming back.
    BOOST_CHECK_EQUAL(
        answerAt(*rebuilt, table, "balance", first, tip), "value:" + std::to_string(1000 + first));
}

/// The negative control the case above needs: an MPTHistory that was NEVER rebuilt refuses every
/// query instead of answering "unchanged since then" (G10). Without this, "the rebuilt index
/// agrees" would also hold for a startup path that forgot to rebuild at all.
BOOST_AUTO_TEST_CASE(anUnrebuiltHistoryRefusesEveryQuery)
{
    auto const first = kActivation + 1;
    auto const tip = runChain(first + 2);

    auto const table = ledger::mpt::accountTableName(account());
    auto fresh = makeHistory();
    BOOST_REQUIRE(fresh->state().index().state() == history::IndexState::Empty);

    BOOST_CHECK_THROW(task::syncWait(history::readStateAt(fresh->state(), *fresh->backend(),
                          StateKeyView{table, "balance"}, first, tip, kDepth)),
        history::HistoryIndexUnavailable);
    BOOST_CHECK(!history::historyCoversBlock(fresh->state(), first, tip));

    // ...and the same store, once rebuilt, answers.
    task::syncWait(fresh->rebuild(backendStorage));
    BOOST_CHECK_EQUAL(
        answerAt(*fresh, table, "balance", first, tip), "value:" + std::to_string(1000 + first));
    BOOST_CHECK(history::historyCoversBlock(fresh->state(), first, tip));
}

BOOST_AUTO_TEST_SUITE_END()
