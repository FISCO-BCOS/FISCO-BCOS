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
 * @file TestMPTPrunerEngineWiring.cpp
 * @brief End-to-end wiring of the MPT pruner into the ENGINE commit path
 *        (EngineServiceImpl: forkchoiceUpdated -> getPayload -> newPayload), over the same
 *        PRODUCTION persistence stack as TestMPTPrunerWiring (FullChainFixture: real RocksDB,
 *        real Ledger, real prewrite/merge). A live MPTPruner (window N=2) is injected as the
 *        engine service's CommitObserver and blocks are driven through the CL flow; every
 *        block carries one sealed Web3 transaction so buildPayload takes the non-empty branch
 *        and FCWritingScheduler writes the block's planned state rows. Asserts:
 *          (a) the engine path really fires the hooks: the pruner's tracked count grows and
 *              its watermark advances to the head;
 *          (b) past the window the committed "/mpt/" node-row count plateaus (bounded);
 *          (c) roots inside [head-N, head] keep their nodes, older roots are deleted;
 *          (d) no "/sys/mpt_prune_*" metadata row ever lands on the backend.
 *        Deletions land synchronously inside newPayload (coPreparePruneRows' batch rides the
 *        block's WriteBatch), so every assertion runs against the committed state.
 */
#include "FullChainFixture.h"
#include "bcos-ledger/mpt/MPTPruner.h"
#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/test/unit_test.hpp>
#include <magic_enum/magic_enum.hpp>
#include <map>
#include <string>
#include <utility>

namespace
{
using namespace bcos;
using namespace bcos::test::fullchain;
namespace mpt = bcos::ledger::mpt;

constexpr std::string_view c_mptFlagName = "feature_mpt_state_root";
constexpr int64_t c_pruneWindow = 2;

/// The committed-state backend type: MultiLayerStorage::latestBackend() is the checkpoint
/// storage's OPENED handle (RocksDBStorage2) — what the pruner and the production
/// initializer (decltype over the same expression) are parameterized on.
using FCBackend = std::remove_cvref_t<decltype(std::declval<FCMultiLayerStorage&>().latestBackend())>;
using FCPruner = mpt::MPTPruner<FCBackend>;

/// The production init lookup (Initializer.cpp): the committed header's stateRoot, nullopt
/// when the block is not on chain.
FCPruner::StateRootLookup stateRootLookup(std::shared_ptr<bcos::ledger::Ledger> const& ledger)
{
    return [ledger](protocol::BlockNumber number) -> task::Task<std::optional<h256>> {
        auto block = co_await ledger::getBlockData(*ledger, number, ledger::HEADER);
        co_return block ? std::optional<h256>{block->blockHeader()->stateRoot()} : std::nullopt;
    };
}

bool nodeRowInBackend(FCBackend& backend, h256 const& hash)
{
    return task::syncWait(storage2::existsOne(backend, bcos::ledger::mptNodeStateKey(hash)));
}

/// Rows under any "/sys/mpt_prune_*" table — the in-memory pruner must never write one.
size_t pruneMetadataRowCount(FCBackend& backend)
{
    return task::syncWait([](FCBackend& backend) -> task::Task<size_t> {
        size_t count = 0;
        auto iterator = co_await storage2::range(backend);
        while (auto item = co_await iterator.next())
        {
            if (executor_v1::StateKeyView{std::get<0>(*item)}.m_table.find("mpt_prune") !=
                std::string_view::npos)
            {
                ++count;
            }
        }
        co_return count;
    }(backend));
}

/// A SYS_CONFIG row a governance action would write ({value, enableNumber}); the engine's
/// getLedgerConfig reads the executor_version / evmc_revision pair through the same
/// production readFromStorage path as the features.
void writeSysConfig(
    FullChainFixture& fixture, std::string_view key, std::string value, protocol::BlockNumber enableNumber = 0)
{
    storage::Entry entry;
    entry.set(storage::serialize::encode(ledger::SystemConfigEntry{std::move(value), enableNumber}));
    task::syncWait(storage2::writeOne(fixture.m_multiLayerStorage.latestBackend(),
        executor_v1::StateKey{ledger::SYS_CONFIG, key}, std::move(entry)));
}

class TestTransactionImpl : public bcostars::protocol::TransactionImpl
{
public:
    void markClean() { setTainted(false); }
};

/// A Web3-typed transaction with a real EIP-1559 signing payload and a 65-byte signature,
/// shaped exactly like the eth_sendRawTransaction ingress produces: type=Web3Transaction,
/// extraTransactionBytes = 0x02 || rlp(unsigned fields), signature = r(32) || s(32) ||
/// yParity(1) — the only shape buildPayload admits into an OP payload (native Tars
/// transactions have no EIP-2718 wire form and are excluded). The stub scheduler never
/// executes it, so only encodability matters. One fresh nonce per block keeps the tx
/// hashes distinct across blocks.
protocol::Transaction::Ptr makeWeb3Tx(crypto::Hash const& hashImpl, std::string_view senderBytes,
    uint64_t nonce)
{
    bytes body;
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));  // chainId
    bcos::codec::rlp::encode(body, nonce);
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));      // maxPriorityFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(1));      // maxFeePerGas
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(21000));  // gasLimit
    bcos::codec::rlp::encode(body, Address("abcdabcdabcdabcdabcdabcdabcdabcdabcdabcd"));
    bcos::codec::rlp::encode(body, static_cast<uint64_t>(0));  // value
    bcos::codec::rlp::encode(body, bytes{});                   // data
    body.push_back(bcos::codec::rlp::LIST_HEAD_BASE);          // empty accessList
    bytes payload;
    payload.push_back(0x02);
    bcos::codec::rlp::encodeHeader(
        payload, bcos::codec::rlp::Header{.isList = true, .payloadLength = body.size()});
    payload.insert(payload.end(), body.begin(), body.end());

    auto tx = std::make_shared<TestTransactionImpl>();
    tx->mutableInner().type = static_cast<int>(bcos::protocol::TransactionType::Web3Transaction);
    tx->mutableInner().extraTransactionBytes.assign(payload.begin(), payload.end());
    bytes signature(65, 0);
    signature[31] = 0x12;  // r != 0
    signature[63] = 0x34;  // s != 0
    signature[64] = 0x01;  // yParity
    tx->mutableInner().signature.assign(signature.begin(), signature.end());
    tx->setNonce("0x" + std::to_string(nonce));
    tx->forceSender(bytes{reinterpret_cast<const byte*>(senderBytes.data()),
        reinterpret_cast<const byte*>(senderBytes.data()) + senderBytes.size()});
    tx->calculateHash(hashImpl);
    tx->markClean();
    return tx;
}

/// Minimal mempool stub: EngineServiceImpl's updateForkchoice calls remove(view) then
/// seal(limit, view, out). Sealing one transaction per FCU makes buildPayload take the
/// non-empty branch, which is what invokes the scheduler's executeBlock (and thereby the
/// block's planned state writes) — an empty payload would change no state.
struct StubMemPool
{
    protocol::Transaction::Ptr m_tx;

    void remove(storage2::ReadableStorage<executor_v1::StateKeyView> auto& /*state*/) {}
    void seal(int64_t /*limit*/,
        storage2::ReadWriteStorage<executor_v1::StateKeyView, executor_v1::StateValue> auto& /*state*/,
        std::output_iterator<protocol::Transaction::Ptr> auto out)
    {
        if (m_tx)
        {
            *out++ = m_tx;
        }
    }
};

using FCEngineService = bcos::engine::EngineServiceImpl<StubMemPool, FCMultiLayerStorage,
    FCExecutor, FCWritingScheduler>;

BOOST_AUTO_TEST_SUITE(MPTPrunerEngineWiringSuite)

BOOST_AUTO_TEST_CASE(prunerWiredIntoEngineCommitPath)
{
    FullChainFixture fixture{"mpt_pruner_engine_wiring"};
    fixture.buildGenesis(FullChainFixture::baseGenesis());
    // Activation at block 1: block 1 itself stays on the legacy XOR root (strictly-greater
    // rule), blocks >= 2 are MPT blocks and fire the pruner — same matrix as the PBFT-path
    // wiring test.
    fixture.enableFeatureFromBlock(c_mptFlagName, 1);
    // The engine derives the header fork era from the chain's on-chain EVM revision and
    // fails closed (UnsupportedFork) without one. executor_version = 2 + CANCUN is the
    // production [op_engine_rpc] configuration; CANCUN pins the V3 method triple
    // (forkchoiceUpdatedV3 / getPayloadV3 / newPayloadV3).
    writeSysConfig(fixture, magic_enum::enum_name(ledger::SystemConfig::executor_version),
        std::to_string(ledger::ETHEREUM_EXECUTOR_VERSION));
    writeSysConfig(
        fixture, ledger::SYSTEM_KEY_EVMC_REVISION, ledger::encodeEVMCRevisionConfig(EVMC_CANCUN, {}));

    auto& backend = fixture.m_multiLayerStorage.latestBackend();
    auto pruner = std::make_shared<FCPruner>(backend, c_pruneWindow);
    // Fresh chain at the genesis block: MPT is not active yet, so init starts empty — the
    // first MPT block's full build seeds the counts through the ordinary delta path.
    task::syncWait(pruner->init(0, stateRootLookup(fixture.m_ledger), /*sweepGarbage=*/false));
    BOOST_CHECK_EQUAL(pruner->trackedCount(), 0U);

    StubMemPool memPool;
    FCEngineService engineService(memPool, fixture.m_multiLayerStorage, fixture.m_executor,
        fixture.m_schedulerImpl, fixture.m_blockFactory, fixture.m_ledger,
        bcos::engine::c_defaultBlockTxCountLimit, /*ledgerConfigState=*/nullptr, pruner);

    auto const genesisHash = task::syncWait(ledger::getBlockHash(*fixture.m_ledger, 0));
    // Whole-second millisecond timestamps, strictly past the genesis header's (the Eth RLP
    // boundary rejects sub-second timestamps).
    auto const baseTimestamp =
        (fixture.headerOnChain(0)->timestamp() / 1000 + 1) * 1000;

    auto const addressA = FullChainFixture::makeAddress(0xA5);
    auto const sender = FullChainFixture::makeAddress(0xB0);
    std::string const senderBytes(
        reinterpret_cast<char const*>(sender.data()), sender.size());

    constexpr protocol::BlockNumber c_head = 8;
    std::map<protocol::BlockNumber, h256> roots;
    std::map<protocol::BlockNumber, size_t> nodeCounts;
    h256 headHash = genesisHash;
    for (protocol::BlockNumber number = 1; number <= c_head; ++number)
    {
        // Every block changes account A's balance, so every MPT block produces a fresh state
        // root and obsoletes the previous one — a new trie version per block. planBlock must
        // run BEFORE the FCU: buildPayload executes (and thereby writes the planned rows)
        // inside updateForkchoice.
        fixture.planBlock(
            number, {FullChainFixture::balanceRow(addressA, std::to_string(number * 100))});
        memPool.m_tx = makeWeb3Tx(*fixture.m_hashImpl, senderBytes, static_cast<uint64_t>(number));

        bcos::engine::ForkchoiceState forkchoiceState{headHash, headHash, headHash};
        bcos::engine::PayloadAttributes payloadAttributes;
        payloadAttributes.timestamp =
            static_cast<std::uint64_t>(baseTimestamp + number * 12000);
        payloadAttributes.prevRandao =
            fixture.m_hashImpl->hash(std::string("randao") + std::to_string(number));
        payloadAttributes.suggestedFeeRecipient =
            Address("1234567890abcdef1234567890abcdef12345678");
        payloadAttributes.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
        payloadAttributes.parentBeaconBlockRoot =
            h256("2222222222222222222222222222222222222222222222222222222222222222");

        auto fcResult = task::syncWait(
            engineService.updateForkchoice(forkchoiceState, &payloadAttributes, /*version=*/3));
        BOOST_REQUIRE_MESSAGE(fcResult.payloadId.has_value(),
            "forkchoiceUpdated returned no payload id for block " + std::to_string(number) +
                (fcResult.payloadStatus.validationError ? *fcResult.payloadStatus.validationError : ""));
        auto payload = task::syncWait(engineService.getPayload(*fcResult.payloadId, /*version=*/3));
        BOOST_REQUIRE(payload);
        BOOST_REQUIRE_EQUAL(payload->executionPayload.blockNumber, number);
        BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 1U);

        bcos::engine::NewPayloadRequest request;
        request.executionPayload = payload->executionPayload;
        request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
        auto status = task::syncWait(engineService.newPayload(request, /*version=*/3));
        BOOST_REQUIRE_MESSAGE(
            status.status == bcos::engine::PayloadValidationStatus::Valid,
            "newPayload rejected block " + std::to_string(number) +
                (status.validationError ? ": " + *status.validationError : ""));
        headHash = payload->executionPayload.blockHash;

        // The committed header as persisted by newPayload's ledger prewrite.
        roots[number] = fixture.headerOnChain(number)->stateRoot();
        nodeCounts[number] = fixture.backendNodeCount();
        if (number >= 2)  // MPT blocks only; block 1 is XOR and fires no observer
        {
            // The in-memory count of the just-committed root: exactly one reference, no
            // deadline — and no metadata row may have landed with any block.
            BOOST_CHECK(pruner->countOf(roots[number]) == std::optional<uint64_t>{1});
            BOOST_CHECK(!pruner->deadlineOf(roots[number]).has_value());
            BOOST_CHECK_EQUAL(pruneMetadataRowCount(backend), 0U);
        }
    }

    // (a) The engine commit path really fired the hooks: the pruner tracks the live window's
    // nodes and its watermark advanced to the head.
    BOOST_CHECK_EQUAL(pruner->watermark(), c_head);
    BOOST_CHECK_GT(pruner->trackedCount(), 0U);

    // (b) Bounded, converged node count: deletions land at the commit of block 2+N+1 = 5;
    // from then on each block adds one trie version and deletes the one that fell out of the
    // window, so the count plateaus.
    BOOST_REQUIRE_EQUAL(nodeCounts[5], nodeCounts[6]);
    BOOST_REQUIRE_EQUAL(nodeCounts[6], nodeCounts[7]);
    BOOST_REQUIRE_EQUAL(nodeCounts[7], nodeCounts[8]);
    BOOST_CHECK_LE(nodeCounts[8], nodeCounts[4]);  // never exceeds the pre-deletion level
    BOOST_CHECK_GT(nodeCounts[8], 0);

    // (c) Window guarantee with N=2 at head 8: roots of blocks 6..8 (head-N .. head) keep
    // their nodes; the root of block r is deleted when block r+1+N commits, so roots 2..5
    // are all gone.
    for (protocol::BlockNumber number = 6; number <= 8; ++number)
    {
        BOOST_CHECK_MESSAGE(nodeRowInBackend(backend, roots[number]),
            "in-window root of block " + std::to_string(number) + " was pruned");
    }
    for (protocol::BlockNumber number = 2; number <= 5; ++number)
    {
        BOOST_CHECK_MESSAGE(!nodeRowInBackend(backend, roots[number]),
            "out-of-window root of block " + std::to_string(number) + " still on disk");
    }
    // The root of block 2 was obsoleted at block 3 and deleted at block 5 — its in-memory
    // entry is erased with the deletion; a still-live root reads count 1, no deadline.
    BOOST_CHECK(!pruner->countOf(roots[2]).has_value());
    BOOST_CHECK(pruner->countOf(roots[8]) == std::optional<uint64_t>{1});

    // (d) The in-memory pruner never wrote a "/sys/mpt_prune_*" metadata row.
    BOOST_CHECK_EQUAL(pruneMetadataRowCount(backend), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace
