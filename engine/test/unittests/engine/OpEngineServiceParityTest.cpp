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
 * @file OpEngineServiceParityTest.cpp
 * @brief OP Engine API parity tests (service-level FCU/getPayload/newPayload vs the legacy impl)
 */
//
// Matrix: S5 — OpEngineService wired on release-3.18.0 (single transactions[i].raw carrier).
// Suite split: the dual-impl cases below pin OpEngineService against EngineServiceImpl
// (FCU ordering/exception consistency); the op-geth reference lives in the vendored t8n
// corpus (opstack-executor/tests/t8n, PROVENANCE.md) — op_golden_vector_rebuild... pins
// this suite's header-rebuild path against the op-geth block hash, and
// OpEngineServiceExecParityTest drives the corpus end-to-end through the production wire
// dialect. This suite covers:
//   - OpEngineService API gates (capabilities, V3 newPayload, gasLimit)
//   - Shared FCU ordering exceptions vs EngineServiceImpl (safe/finalized)
//   - EngineTracker exclusive/shared publish concurrency (op_fast_path)
//   - op-geth golden rebuild pin (vendored corpus)
// NOT import-path coverage: the delegate used here is FabricatedRootsStub, which
// returns invented header roots (kept deliberately for the API-gate and
// fault-injection shapes on this suite). Real import/SetCanonical/commitment-gate
// behaviour is pinned by OpEngineImportFcuTest against the real OpScheduler delegate
// (ChainedImportMatchesCanonicalParentState, BadStateRootIsInvalidAndNotStored,
// CanonicalImportedBlockHasNumberToTxsRow, ThreeImportsThenJumpFcu,
// CanonicalizeRollsBackCacheLayerOnMidChainMergeFailure) and by
// OpNewPayloadRpcE2eSuite/RegolithPayloadBuildsAndImportsAgainstRealScheduler end to
// end — do not cite this suite for it (review F9 resolved by this contract plus those
// real-path suites).

#include "support/GoldenSample.h"
#include "support/OpEngineKarstTestHarness.h"

#include <algorithm>
#include <atomic>
#include <latch>
#include <limits>
#include <thread>

namespace op_engine_parity_test
{

struct SharedForkchoicePair
{
    BackendMemStorage legacyBackend{1};
    BackendMemStorage opBackend{1};
    CheckpointBackend legacyCheckpoint{legacyBackend};
    CheckpointBackend opCheckpoint{opBackend};
    MLS legacyStorage{legacyCheckpoint};
    MLS opStorage{opCheckpoint};
    StubMemPool legacyMemPool;
    StubMemPool opMemPool;
    StubExecutor legacyExecutor;
    StubExecutor opExecutor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    EngineOpScheduler legacyScheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                          bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    EngineOpScheduler opScheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                      bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    EthLegacyEngine legacy;
    OpEngine op;

    SharedForkchoicePair()
      : legacy(legacyMemPool, legacyStorage, legacyExecutor, legacyScheduler, blockFactory),
        op(opMemPool, opStorage, opScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, nullptr)
    {}
};

}  // namespace op_engine_parity_test

BOOST_AUTO_TEST_SUITE(OpEngineServiceParityTest)

using namespace op_engine_parity_test;

BOOST_AUTO_TEST_CASE(op_capabilities_include_op_methods)
{
    // Matrix: S5. The live/dead method matrix is pinned in OpEngineReviewFixTest;
    // this case only checks the endpoint exposes that OP list.
    OpServicePair pair;
    auto caps = bcos::task::syncWait(pair.service.exchangeCapabilities({}));
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_newPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV5") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_forkchoiceUpdatedV3") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV3") != caps.end());
}

BOOST_AUTO_TEST_CASE(op_v3_new_payload_throws_unsupported_fork)
{
    // Matrix: S5 — release carrier is transactions[i].raw (empty list here).
    OpServicePair pair;
    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload.timestamp = 1000;
    request.executionPayload.blockNumber = 1;
    request.executionPayload.transactions = {};
    request.executionPayload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    request.executionPayload.withdrawalsRoot = bcos::h256{};
    request.executionPayload.excessBlobGas = bcos::u256(0);
    request.executionPayload.blobGasUsed = bcos::u256(0);
    request.parentBeaconBlockRoot = bcos::h256{};
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(pair.service.newPayload(request, 3)),
        bcos::engine::UnsupportedFork, [&](bcos::engine::UnsupportedFork const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == c_opNewPayloadVersionMismatchMessage;
        });
}

BOOST_AUTO_TEST_CASE(op_missing_gas_limit_returns_invalid)
{
    // Matrix: S5
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = std::nullopt;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("gasLimit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_gas_limit_above_signed_max_is_invalid)
{
    // Matrix: A1 — FCU INVALID (not -32603), same message as newPayload, no payloadId.
    OpServicePair pair;
    auto attrs = makeOpPayloadAttributes();
    attrs.gasLimit = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + 1;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK_EQUAL(*result.payloadStatus.validationError,
        "gasLimit exceeds the maximum block gas limit (2^63-1)");
}

BOOST_AUTO_TEST_CASE(op_safe_above_head_matches_eth_legacy)
{
    // Matrix: S5 — shared forkchoice ordering vs EngineServiceImpl.
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"),
        bcos::h256("0000000000000000000000000000000000000000000000000000000000000011")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_headOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_safeAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_head_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"),
        bcos::h256("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.headBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveHeadMessage);
}

BOOST_AUTO_TEST_CASE(op_finalized_above_safe_matches_eth_legacy)
{
    SharedForkchoicePair pair;
    bcos::engine::ForkchoiceState forkchoiceState{
        bcos::h256("1212121212121212121212121212121212121212121212121212121212121212"),
        bcos::h256("1313131313131313131313131313131313131313131313131313131313131313"),
        bcos::h256("1414141414141414141414141414141414141414141414141414141414141414")};
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.legacyStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.headBlockHash, c_finalizedOrderingBlockNumber);
    registerVerifiedBlock(pair.opStorage, forkchoiceState.safeBlockHash, c_headOrderingBlockNumber);
    registerVerifiedBlock(
        pair.opStorage, forkchoiceState.finalizedBlockHash, c_safeOrderingBlockNumber);
    checkBothExceptionMessages<bcos::engine::InvalidForkchoiceState>(
        [&] {
            return bcos::task::syncWait(pair.legacy.updateForkchoice(forkchoiceState, nullptr, 3));
        },
        [&] { return bcos::task::syncWait(pair.op.updateForkchoice(forkchoiceState, nullptr, 3)); },
        c_finalizedAboveSafeMessage);
}

BOOST_AUTO_TEST_CASE(op_fast_path_concurrent_with_build_publish)
{
    // Matrix: S5 / S7-adjacent — shared guard blocks exclusive publish.
    bcos::engine::EngineTracker tracker;
    std::unordered_map<bcos::engine::PayloadID, bcos::engine::OpPayloadArtifacts> artifacts;
    auto blockFactory = makeBlockFactory();

    bcos::h256 const targetHash(0x42);
    bcos::engine::PayloadID const targetPayloadId = "0xdeadbeef";
    constexpr bcos::protocol::BlockNumber kTargetNumber = 7;

    {
        auto guard = tracker.lockExclusive();
        auto entry = std::make_shared<bcos::engine::BuiltPayload>();
        entry->executionPayload.blockHash = targetHash;
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(kTargetNumber);
        (void)bcos::engine::publishBuiltPayload(guard, artifacts, targetPayloadId, targetHash,
            entry, bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
    }

    bcos::protocol::BlockHeader::Ptr initialHeader;
    std::atomic<bool> writerFinished{false};
    std::exception_ptr writerError;

    std::latch writerReady{1};
    std::latch permission{1};
    std::latch committed{1};

    std::optional<std::thread> writer;
    {
        auto shared = tracker.lockShared();
        initialHeader = bcos::engine::detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(initialHeader);
        BOOST_CHECK_EQUAL(initialHeader->number(), kTargetNumber);

        writer.emplace([&] {
            try
            {
                writerReady.count_down();
                permission.wait();
                committed.count_down();
                auto guard = tracker.lockExclusive();
                bcos::h256 writerHash(0x99);
                bcos::engine::PayloadID writerPayloadId = "0xcafebabe";
                auto entry = std::make_shared<bcos::engine::BuiltPayload>();
                entry->executionPayload.blockHash = writerHash;
                auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
                header->setNumber(99);
                (void)bcos::engine::publishBuiltPayload(guard, artifacts, writerPayloadId,
                    writerHash, entry, bcos::engine::OpPayloadArtifacts{.canonicalHeader = header});
                writerFinished.store(true, std::memory_order_release);
            }
            catch (...)
            {
                writerError = std::current_exception();
            }
        });

        writerReady.wait();
        permission.count_down();
        committed.wait();
        BOOST_CHECK(!writerFinished.load(std::memory_order_acquire));
    }

    writer->join();
    if (writerError)
    {
        std::rethrow_exception(writerError);
    }
    BOOST_CHECK(writerFinished.load(std::memory_order_acquire));

    {
        auto shared = tracker.lockShared();
        auto stableHeader = bcos::engine::detail::findBuiltHeader(shared, artifacts, targetHash);
        BOOST_REQUIRE(stableHeader);
        BOOST_CHECK_EQUAL(stableHeader->number(), kTargetNumber);
        BOOST_CHECK_EQUAL(stableHeader.get(), initialHeader.get());
    }
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_non_canonical_safe)
{
    // op-geth: HASH_2_NUMBER finds the block, but ReadCanonicalHash(number) differs.
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    auto const canonicalSafe =
        bcos::h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 10);
    registerVerifiedBlock(pair.storage, canonicalSafe, 8);
    registerHashToNumberOnly(pair.storage, forkchoice.safeBlockHash, 8);
    registerVerifiedBlock(pair.storage, forkchoice.finalizedBlockHash, 7);
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [&](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == "Forkchoice safe block not in canonical chain";
        });
}

// N6 regression: a ledger-canonical head above the tip pointer is unreachable in
// production (commitBlock and canonicalize write NUMBER_2_HASH and SYS_CURRENT_STATE in
// the same batch), and the design §4.2 atomicity rule forbids advancing the tip before
// the safe/finalized validation succeeds. This seeds the impossible shape directly: the
// rejected FCU must not leave SYS_CURRENT_STATE advanced to the un-validated head.
BOOST_AUTO_TEST_CASE(op_fcu_rejected_does_not_advance_tip_pointer)
{
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"),
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"),
        bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc")};
    auto const canonicalSafe =
        bcos::h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    registerVerifiedBlock(pair.storage, forkchoice.headBlockHash, 10);
    registerVerifiedBlock(pair.storage, canonicalSafe, 8);
    registerHashToNumberOnly(pair.storage, forkchoice.safeBlockHash, 8);
    registerVerifiedBlock(pair.storage, forkchoice.finalizedBlockHash, 7);
    registerCurrentBlockNumber(pair.storage, 0);

    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [&](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && *comment == "Forkchoice safe block not in canonical chain";
        });

    auto view = pair.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);
}

BOOST_AUTO_TEST_CASE(op_fcu_rejects_empty_txs_when_synthesis_disabled)
{
    // op_engine_rpc / op-geth: do not invent an L1-attributes deposit.
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::nullopt;
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(
        result.payloadStatus.validationError->find("L1 attributes deposit") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_v5_is_unsupported)
{
    // The window is V1-V3; V5 still hits the method-version gate.
    OpServicePair pair;
    bcos::engine::ForkchoiceState state;
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.updateForkchoice(state, nullptr, 5)),
        bcos::engine::UnsupportedEngineApiVersion);
}

BOOST_AUTO_TEST_CASE(op_culprit_hash_is_structured_not_message_text)
{
    // Matrix: T02 T03 — engine reads OpCulpritTxHash; a [tx=0x...] message is ignored.
    bcos::h256 const hash(std::string(64, 'a'));
    auto error = BCOS_ERROR_PTR(-1, "Execute block failed! invalid tx");
    *error << bcos::engine::OpCulpritTxHash(hash);
    auto got = bcos::engine::culpritTxHashFromError(*error);
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK_EQUAL(got->hex(), hash.hex());

    auto plain = BCOS_ERROR_PTR(-1, "Execute block failed! [tx=0x" + hash.hex() + "]");
    BOOST_CHECK(!bcos::engine::culpritTxHashFromError(*plain).has_value());

    bcos::evm::OpConsensusError thrown("op block: invalid non-deposit tx");
    thrown.txHash = hash;
    auto attached = BCOS_ERROR_UNIQUE_PTR(1, thrown.what());
    *attached << bcos::engine::OpCulpritTxHash(*thrown.txHash);
    auto recovered = bcos::engine::culpritTxHashFromError(*attached);
    BOOST_REQUIRE(recovered.has_value());
    BOOST_CHECK_EQUAL(recovered->hex(), hash.hex());

    StubMemPool pool;
    std::array<bcos::crypto::HashType, 1> hashes{*recovered};
    pool.removeByHash(std::span<bcos::crypto::HashType const>(hashes));
    BOOST_REQUIRE_EQUAL(pool.removed.size(), 1);
    BOOST_CHECK_EQUAL(pool.removed.front().hex(), hash.hex());
}

BOOST_AUTO_TEST_CASE(op_newpayload_missing_parent_header_is_invalid)
{
    // BF — parent hash is canonical; SYS_NUMBER_2_BLOCK_HEADER is absent.
    OpServicePair pair;
    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    registerVerifiedBlock(pair.storage, parent, 0);
    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), parent.hex());
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK(status.validationError->find("parent") != std::string::npos);
    BOOST_CHECK(status.validationError->find("header") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_fcu_missing_parent_header_is_invalid)
{
    // BF — FCU build must not default baseFee to 1 gwei when the parent header is gone.
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("parent") != std::string::npos);
    BOOST_CHECK(result.payloadStatus.validationError->find("header") != std::string::npos);
}

static void driveBuildWithCulprit(bool capacityReject)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->rejectAsCapacity = capacityReject;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    delegate->culprit = decoded.tx->hash();
    pair.memPool.pool.push_back(decoded.tx);

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.noTxPool = false;
    // Non-empty forced list skips L1-attributes synthesis; the envelope must still
    // decode in buildOpBlock so the retry loop is reachable.
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_GE(delegate->executeCalls, 2);
    if (capacityReject)
    {
        BOOST_CHECK(pair.memPool.removed.empty());
    }
    else
    {
        BOOST_REQUIRE_EQUAL(pair.memPool.removed.size(), 1);
        BOOST_CHECK_EQUAL(pair.memPool.removed.front().hex(), decoded.tx->hash().hex());
    }
}

BOOST_AUTO_TEST_CASE(op_build_capacity_reject_does_not_evict)
{
    // BH — OpRejectIsCapacity skips this candidate, the tx stays in the pool.
    driveBuildWithCulprit(/*capacityReject=*/true);
}

BOOST_AUTO_TEST_CASE(op_build_culprit_hash_evicts_through_retry_loop)
{
    // BC — structured OpCulpritTxHash on a sealed pool tx is evicted via removeByHash.
    driveBuildWithCulprit(/*capacityReject=*/false);
}

static void driveBuildWithSenderNonceChain(bool capacityReject)
{
    // R3-F1 / R2-F4 — same sender, nonce n then n+1. Excluding n must not evict n+1.
    auto delegate = std::make_shared<NonceChainScheduler>();
    delegate->rejectAsCapacity = capacityReject;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    bcos::crypto::Secp256k1Crypto secp;
    auto keyPair = secp.generateKeyPair();
    auto dummy = makeDecodableWeb3Tx(0);
    auto nonceN = makeDecodableWeb3Tx(1, keyPair.get());
    auto nonceN1 = makeDecodableWeb3Tx(2, keyPair.get());
    delegate->culprit = nonceN.tx->hash();
    delegate->successor = nonceN1.tx->hash();
    delegate->culpritEnvHash = envelopeHashOf(nonceN.tx);
    delegate->successorEnvHash = envelopeHashOf(nonceN1.tx);
    pair.memPool.pool.push_back(nonceN.tx);
    pair.memPool.pool.push_back(nonceN1.tx);

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.noTxPool = false;
    attrs.transactions = std::vector<std::string>{dummy.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_GE(delegate->executeCalls, 2);
    auto const n1Hex = nonceN1.tx->hash().hex();
    if (capacityReject)
    {
        BOOST_CHECK(pair.memPool.removed.empty());
    }
    else
    {
        BOOST_REQUIRE_EQUAL(pair.memPool.removed.size(), 1);
        BOOST_CHECK_EQUAL(pair.memPool.removed.front().hex(), nonceN.tx->hash().hex());
    }
    BOOST_CHECK(std::none_of(pair.memPool.removed.begin(), pair.memPool.removed.end(),
        [&](auto const& removed) { return removed.hex() == n1Hex; }));
}

BOOST_AUTO_TEST_CASE(op_build_capacity_skip_keeps_sender_successor)
{
    driveBuildWithSenderNonceChain(/*capacityReject=*/true);
}

BOOST_AUTO_TEST_CASE(op_build_intrinsic_evict_keeps_sender_successor)
{
    driveBuildWithSenderNonceChain(/*capacityReject=*/false);
}

BOOST_AUTO_TEST_CASE(op_da_skip_drops_higher_nonce_regardless_of_seal_order)
{
    // BU — skipSenderTail must evict by nonce, not sealed-vector position.
    // Seal order is [n+1, n]; only n exceeds maxTxSize. n+1 must still drop.
    // The caps count ESTIMATED DA bytes (Fjord FastLZ estimate), not raw envelope
    // length, so the filler is incompressible (keeps n's estimate above n+1's) and
    // the cap is derived from the estimates themselves.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    auto daCaps = std::make_shared<bcos::engine::DACaps>();
    bcos::crypto::Secp256k1Crypto secp;
    auto keyPair = secp.generateKeyPair();
    auto dummy = makeDecodableWeb3Tx(0);
    bcos::bytes incompressible(200);
    for (std::size_t i = 0; i < incompressible.size(); ++i)
    {
        incompressible[i] = static_cast<bcos::byte>(i * 7 + 1);
    }
    auto nonceN = makeDecodableWeb3Tx(1, keyPair.get(), incompressible);
    auto nonceN1 = makeDecodableWeb3Tx(2, keyPair.get());
    auto nRaw = bcostars::protocol::reassembleWeb3RawTransaction(
        nonceN.tx->extraTransactionBytes(), nonceN.tx->signatureData());
    auto n1Raw = bcostars::protocol::reassembleWeb3RawTransaction(
        nonceN1.tx->extraTransactionBytes(), nonceN1.tx->signatureData());
    auto const nEstimate =
        bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(nRaw.data(), nRaw.size()));
    auto const n1Estimate =
        bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(n1Raw.data(), n1Raw.size()));
    BOOST_REQUIRE_GT(nEstimate, n1Estimate);
    daCaps->maxTxSize.store(n1Estimate, std::memory_order_relaxed);  // inclusive cap

    OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate, daCaps);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
    pair.memPool.pool.push_back(nonceN1.tx);
    pair.memPool.pool.push_back(nonceN.tx);

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.noTxPool = false;
    attrs.transactions = std::vector<std::string>{dummy.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*result.payloadId, 4));
    BOOST_REQUIRE(payload);
    for (auto const& tx : payload->executionPayload.transactions)
    {
        BOOST_CHECK(tx.raw != nRaw);
        BOOST_CHECK(tx.raw != n1Raw);
    }
}

// Ported from engine-cutover-on-prereqs: maxBlockSize cumulative-budget contract,
// re-expressed on the shared Karst harness (FabricatedRootsStub / getPayload V4).
BOOST_AUTO_TEST_CASE(op_da_block_budget_admits_at_cap_then_drops_and_keeps_forced)
{
    // BU — maxBlockSize is a cumulative estimated-DA budget: the forced (undroppable)
    // envelope preloads it and a sealed tx is admitted only while it fits the remaining
    // budget. At exactly the remaining budget it lands; one estimated byte over it is
    // dropped while the forced envelope still lands. (op_da_skip covers maxTxSize; this
    // is the maxBlockSize/Budget engine path, previously tested only in isolation.)
    bcos::crypto::Secp256k1Crypto secp;
    auto key = secp.generateKeyPair();
    bcos::bytes incompressible(200);
    for (std::size_t i = 0; i < incompressible.size(); ++i)
    {
        incompressible[i] = static_cast<bcos::byte>(i * 7 + 1);
    }
    auto sealed = makeDecodableWeb3Tx(1, key.get(), incompressible);
    auto const sealedRaw = bcostars::protocol::reassembleWeb3RawTransaction(
        sealed.tx->extraTransactionBytes(), sealed.tx->signatureData());
    auto const sealedEst =
        bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(sealedRaw.data(), sealedRaw.size()));

    // Forced envelope carried through payloadAttributes.transactions (the same shape the
    // txFits test uses); its estimate is what preloads the budget.
    auto forced = makeDecodableWeb3Tx(0);
    auto const forcedRaw = bcos::fromHex(forced.rawHex);
    auto const forcedEst =
        bcos::evm::opstack::estimatedDaSize(evmc::bytes_view(forcedRaw.data(), forcedRaw.size()));

    auto buildWithBudget = [&](std::uint64_t maxBlockSize) {
        auto delegate = std::make_shared<FabricatedRootsStub>();
        delegate->failFirst = false;
        auto daCaps = std::make_shared<bcos::engine::DACaps>();
        daCaps->maxBlockSize.store(maxBlockSize, std::memory_order_relaxed);
        OpServicePair pair(/*allowSynthesizedL1Attributes=*/true, delegate, daCaps);
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
        pair.memPool.pool.push_back(sealed.tx);

        auto attrs = makeOpPayloadAttributes();
        attrs.minBaseFee = std::nullopt;
        attrs.noTxPool = false;
        attrs.transactions = std::vector<std::string>{forced.rawHex};
        auto const hash =
            bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
        registerVerifiedBlock(pair.storage, hash, 0);
        registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

        auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
        BOOST_REQUIRE_EQUAL(static_cast<int>(result.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        BOOST_REQUIRE(result.payloadId.has_value());
        auto payload = bcos::task::syncWait(pair.service.getPayload(*result.payloadId, 4));
        BOOST_REQUIRE(payload);
        std::vector<bcos::bytes> raws;
        raws.reserve(payload->executionPayload.transactions.size());
        for (auto const& tx : payload->executionPayload.transactions)
        {
            raws.push_back(tx.raw);
        }
        return raws;
    };
    auto contains = [](std::vector<bcos::bytes> const& raws, bcos::bytes const& needle) {
        for (auto const& raw : raws)
        {
            if (raw == needle)
            {
                return true;
            }
        }
        return false;
    };

    // Exactly at the remaining budget: the sealed tx is admitted alongside forced.
    auto const atCap = buildWithBudget(forcedEst + sealedEst);
    BOOST_CHECK_MESSAGE(contains(atCap, forcedRaw), "forced envelope must always be present");
    BOOST_CHECK_MESSAGE(contains(atCap, sealedRaw), "sealed tx must fit exactly at the budget");

    // One estimated byte over: the sealed tx is dropped, the forced envelope still lands.
    auto const overCap = buildWithBudget(forcedEst + sealedEst - 1);
    BOOST_CHECK_MESSAGE(
        contains(overCap, forcedRaw), "forced envelope must survive an over-budget sealed tx");
    BOOST_CHECK_MESSAGE(
        !contains(overCap, sealedRaw), "sealed tx exceeding the block budget must be dropped");
}

BOOST_AUTO_TEST_CASE(op_fcu_unresolved_head_is_syncing)
{
    // BS — missing HASH_2_NUMBER for head is SYNCING (not an exception), no payloadId.
    OpServicePair pair;
    auto const unknownHead =
        bcos::h256("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd");
    bcos::engine::ForkchoiceState forkchoice{unknownHead, {}, {}};
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_CHECK(!result.payloadStatus.latestValidHash.has_value());
    BOOST_CHECK(!result.payloadStatus.validationError.has_value());
}

BOOST_AUTO_TEST_CASE(op_fcu_unknown_nonzero_safe_is_invalid_forkchoice)
{
    // BJ — zero hash stays unset; a non-zero unresolved safe is InvalidForkchoiceState.
    OpServicePair pair;
    auto const head =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto const unknownSafe =
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    bcos::engine::ForkchoiceState forkchoice{head, unknownSafe, {}};
    registerVerifiedBlock(pair.storage, head, 0);
    BOOST_CHECK_EXCEPTION(
        bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3)),
        bcos::engine::InvalidForkchoiceState, [](bcos::engine::InvalidForkchoiceState const& e) {
            auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
            return comment != nullptr && comment->find("unknown") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(op_fcu_undecodable_envelope_is_invalid_not_internal_error)
{
    // AM — FCU build must answer INVALID, not throw OpExecutionInternalError (-32603).
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{"0xdeadbeef"};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("undecodable") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(op_newpayload_undecodable_envelope_is_short_invalid)
{
    // Finding CG — newPayload must return the same stable phrase as FCU, without
    // appending boost::diagnostic_information to validationError.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    registerVerifiedBlock(pair.storage, parent, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto parentHeader = pair.blockFactory->blockHeaderFactory()->createBlockHeader();
    parentHeader->setNumber(0);
    parentHeader->setTimestamp(1'699'000'000'000);
    parentHeader->setGasLimit(30'000'000);
    parentHeader->setGasUsed(0);
    parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
    parentHeader->setBaseFee(bcos::u256(1'000'000'000));

    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    bcos::engine::EngineTransaction garbage;
    garbage.raw = bcos::bytes{0xde, 0xad, 0xbe, 0xef};
    request.executionPayload.transactions.push_back(std::move(garbage));
    request.executionPayload.baseFeePerGas = bcos::engine::calcOpBaseFee(*parentHeader, false);
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK_EQUAL(*status.validationError, "undecodable payload transaction envelope");
}

BOOST_AUTO_TEST_CASE(op_fcu_getpayload_newpayload_roundtrip)
{
    // AR — service-level FCU → getPayload → newPayload with a real delegate.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);
    BOOST_REQUIRE_EQUAL(bcos::toHex(payload->executionPayload.extraData), "00000000fa00000006");

    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(pair.service.lastExecutedHeader());
}

BOOST_AUTO_TEST_CASE(op_newpayload_failure_keeps_last_executed_header)
{
    // runOpNewPayloadSteps must not reset m_lastExecutedHeader on entry.
    // A duplicate newPayload arriving while another one is mid-flight used to clear a
    // header the concurrent success had just published; with assign-only-on-success
    // the previous payload's header survives any failed run.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto const published = pair.service.lastExecutedHeader();
    BOOST_REQUIRE(published);

    // A failing submission (tampered blockHash) must leave the published header intact.
    bcos::engine::NewPayloadRequest bad = request;
    bad.executionPayload.blockHash =
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto badStatus = bcos::task::syncWait(pair.service.newPayload(bad, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(badStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(badStatus.validationError.has_value());
    BOOST_CHECK(badStatus.validationError->find("blockHash does not match") != std::string::npos);

    auto after = pair.service.lastExecutedHeader();
    BOOST_REQUIRE(after);
    BOOST_CHECK(after.get() == published.get());
}

/// FCU -> getPayload -> serialize (struct -> JSON) -> parseNewPayloadRequest (JSON -> struct)
/// -> newPayload, all through the production EngineHelper dialect: the strict-compared
/// field set must survive the wire round trip and the parsed request must execute.
BOOST_AUTO_TEST_CASE(op_newpayload_wire_roundtrip_survives_engine_helper_v4)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    auto const& original = payload->executionPayload;
    auto epJson = bcos::rpc::serializeExecutionPayload(original, bcos::engine::ApiVersion::V4);
    Json::Value params(Json::arrayValue);
    params.append(epJson);
    params.append(Json::Value(Json::arrayValue));  // expectedBlobVersionedHashes = []
    params.append("0x" + payload->parentBeaconBlockRoot->hex());
    params.append(Json::Value(Json::arrayValue));  // executionRequests = []
    auto parsed = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    checkSameExecutionPayload(original, parsed.executionPayload);
    BOOST_REQUIRE(parsed.parentBeaconBlockRoot.has_value());
    BOOST_CHECK_EQUAL(parsed.parentBeaconBlockRoot->hex(), payload->parentBeaconBlockRoot->hex());

    // The wire-parsed request executes: the CL's JSON shape is what the service accepts.
    auto status = bcos::task::syncWait(pair.service.newPayload(parsed, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(pair.service.lastExecutedHeader());
}

/// Honest-retry twin of the legacy new_payload_honest_retry_does_not_recommit: after the
/// durable commit the same honest request answers VALID without a second commit; the
/// committed payload's artifacts stay servable.
BOOST_AUTO_TEST_CASE(op_newpayload_honest_retry_does_not_recommit)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto first = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(first.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);
    // The stub commit persists the hash row exactly as the real ledger write would.
    registerVerifiedBlock(pair.storage, payload->executionPayload.blockHash, 1);

    auto retry = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(retry.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);  // ledger-known idempotency, no re-commit
}

/// A transient commit failure must not strand the payload: the first newPayload THROWS,
/// the retained artifacts survive, and the retry re-attempts the commit and completes it.
BOOST_AUTO_TEST_CASE(op_newpayload_retry_after_failed_commit_recommits)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failCommit = true;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.newPayload(request, 4)),
        bcos::engine::OpExecutionInternalError);
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);

    delegate->failCommit = false;
    auto retry = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(retry.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->commitCalls, 2);  // the retry re-attempted and completed
    BOOST_REQUIRE(pair.service.lastExecutedHeader());
}

/// getPayloadV4/V5 (the advertised capability set) serve the built payload through the
/// service, not just V3: the V4+ shape gate requires withdrawalsRoot and the response
/// embeds the full V3 field set unchanged.
/// mapDelegateError's routing is the load-bearing half of the delegate-concurrency
/// rationale: a commitBlock whose pending was dropped by a concurrent reset
/// reports SchedulerError::UnknownError ("Unexpected empty results!", OpScheduler.h) — that
/// must surface as -32603 (OpExecutionInternalError), NEVER as a consensus INVALID for a
/// valid payload. OpConsensusRejected is the ONLY code the service may answer INVALID for;
/// pin both routes so a future change to either side cannot silently flip them.
BOOST_AUTO_TEST_CASE(op_commit_error_routing_unknown_error_is_never_invalid)
{
    auto makeRequest = [](OpServicePair& pair, FabricatedRootsStub& delegate) {
        auto decoded = makeDecodableWeb3Tx(1);
        auto attrs = makeOpPayloadAttributes();
        attrs.minBaseFee = std::nullopt;
        attrs.transactions = std::vector<std::string>{decoded.rawHex};
        auto const hash =
            bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
        registerVerifiedBlock(pair.storage, hash, 0);
        registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
        auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
        BOOST_REQUIRE(built.payloadId.has_value());
        auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
        BOOST_REQUIRE(payload);
        bcos::engine::NewPayloadRequest request;
        request.executionRequests =
            std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
        request.executionPayload = payload->executionPayload;
        request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
        request.expectedBlobVersionedHashes = {};
        return request;
    };

    // Dropped-pending shape: UnknownError → internal error (-32603), never INVALID.
    {
        auto delegate = std::make_shared<FabricatedRootsStub>();
        delegate->failFirst = false;
        delegate->failCommit = true;
        delegate->commitErrorCode = static_cast<int>(bcos::scheduler::SchedulerError::UnknownError);
        OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
        auto request = makeRequest(pair, *delegate);
        BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.newPayload(request, 4)),
            bcos::engine::OpExecutionInternalError);
    }

    // The one code that may answer INVALID: OpConsensusRejected → Invalid status.
    {
        auto delegate = std::make_shared<FabricatedRootsStub>();
        delegate->failFirst = false;
        delegate->failCommit = true;
        delegate->commitErrorCode =
            static_cast<int>(bcos::scheduler::SchedulerError::OpConsensusRejected);
        OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
        auto request = makeRequest(pair, *delegate);
        auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
        BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    }
}

/// A failed reset must fail closed: the build model assumes reset executed
/// its documented effect before executeBlock runs, so a reset error is an internal error,
/// never a silently-ignored proceed.
BOOST_AUTO_TEST_CASE(op_reset_failure_is_internal_error)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failReset = true;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3)),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(op_getpayload_v4_v5_serve_the_built_payload)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto v4 = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(v4);
    BOOST_REQUIRE(v4->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(
        v4->executionPayload.withdrawalsRoot->hex(), delegate->executedWithdrawalsRoot.hex());
    // Jovian payload timestamp: getPayload is V4. V5 is Karst-only (-38005).
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 5)),
        bcos::engine::UnsupportedFork);
}

/// The OP lane's FCU window is exactly V1-V3 (the caps list advertises no
/// forkchoiceUpdatedV4 and upstream has no FCU V4 on this fork): a V4 FCU must answer
/// -38005 (UnsupportedEngineApiVersion), and a V3 FCU with the same attributes still
/// builds a payload — the advertised window and the acceptance gate are the same list,
/// not two divergent sources.
BOOST_AUTO_TEST_CASE(op_fcu_v4_is_outside_the_advertised_window)
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
}

/// The full getPayload-response JSON shape as the CL sees it (Karst pairing
/// FCU V3 -> getPayload V5 -> newPayload V4): combineGetPayloadResponse must
/// wrap the payload with blockValue/blobsBundle/shouldOverrideBuilder/
/// executionRequests/parentBeaconBlockRoot, and the embedded executionPayload
/// must re-parse to the built payload.
BOOST_AUTO_TEST_CASE(op_getpayload_v5_response_json_shape)
{
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    auto payload = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5));
    BOOST_REQUIRE(payload);

    Json::Value response;
    bcos::rpc::combineGetPayloadResponse(response, payload, bcos::engine::ApiVersion::V5);
    BOOST_REQUIRE(response.isMember("executionPayload"));
    BOOST_CHECK_EQUAL(response["blockValue"].asString(), "0x0");
    BOOST_REQUIRE(response["blobsBundle"].isObject());
    BOOST_CHECK_EQUAL(response["blobsBundle"]["commitments"].size(), 0U);
    BOOST_CHECK_EQUAL(response["blobsBundle"]["proofs"].size(), 0U);
    BOOST_CHECK_EQUAL(response["blobsBundle"]["blobs"].size(), 0U);
    BOOST_CHECK_EQUAL(response["shouldOverrideBuilder"].asBool(), false);
    BOOST_REQUIRE(response["executionRequests"].isArray());
    BOOST_CHECK_EQUAL(response["executionRequests"].size(), 0U);
    BOOST_REQUIRE(response.isMember("parentBeaconBlockRoot"));
    BOOST_CHECK_EQUAL(
        response["parentBeaconBlockRoot"].asString(), "0x" + payload->parentBeaconBlockRoot->hex());

    // The embedded executionPayload must re-parse to the built payload (wire round trip
    // through the response's own JSON).
    Json::Value params(Json::arrayValue);
    params.append(response["executionPayload"]);
    params.append(Json::Value(Json::arrayValue));
    params.append(response["parentBeaconBlockRoot"]);
    params.append(Json::Value(Json::arrayValue));
    auto parsed = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    checkSameExecutionPayload(payload->executionPayload, parsed.executionPayload);

    auto status = bcos::task::syncWait(fixture.pair.service.newPayload(parsed, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

BOOST_AUTO_TEST_CASE(op_newpayload_occupied_nontip_height_is_syncing)
{
    // S5 revision of matrix A2: height N already has canonical hash A, payload is
    // hash B, tip is past N. The parent's post-state plane is NOT the committed tip
    // and this stub fixture holds no parent flat — the engine answers SYNCING
    // (honest: never execute on a wrong plane; CL retries). The VALID ancestor-
    // sibling path is pinned by AncestorSiblingWhileTipStillAhead with the real
    // scheduler. Must not throw OpExecutionInternalError (-32603).
    OpServicePair pair;
    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    auto const occupied =
        bcos::h256("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    auto const tip = bcos::h256("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    registerVerifiedBlock(pair.storage, parent, 0);
    registerVerifiedBlock(pair.storage, occupied, 1);
    registerVerifiedBlock(pair.storage, tip, 2);
    registerCurrentBlockNumber(pair.storage, 2);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto parentHeader = pair.blockFactory->blockHeaderFactory()->createBlockHeader();
    parentHeader->setNumber(0);
    parentHeader->setTimestamp(1'699'000'000'000);
    parentHeader->setGasLimit(30'000'000);
    parentHeader->setGasUsed(0);
    parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
    parentHeader->setBaseFee(bcos::u256(1'000'000'000));

    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    request.executionPayload.baseFeePerGas = bcos::engine::calcOpBaseFee(*parentHeader, false);
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    BOOST_CHECK_NE(request.executionPayload.blockHash.hex(), occupied.hex());

    bcos::engine::PayloadStatus status;
    BOOST_CHECK_NO_THROW(status = bcos::task::syncWait(pair.service.newPayload(request, 4)));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!status.latestValidHash.has_value());
    BOOST_CHECK(!status.validationError.has_value());
}

BOOST_AUTO_TEST_CASE(op_fcu_getpayload_newpayload_roundtrip_messagepasser_root)
{
    // FCU stamps the executed MessagePasser storage root; newPayload must accept it.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->executedWithdrawalsRoot = bcos::h256(1);
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId.has_value());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);
    BOOST_REQUIRE(payload->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(
        payload->executionPayload.withdrawalsRoot->hex(), delegate->executedWithdrawalsRoot.hex());

    bcos::engine::NewPayloadRequest request;
    request.executionRequests =
        std::vector<bcos::bytes>{};  // present-but-empty: the Isthmus wire contract
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(pair.service.lastExecutedHeader());
}

BOOST_AUTO_TEST_CASE(op_newpayload_rejects_executed_withdrawals_root_mismatch)
{
    // Announced empty root hashes; execute returns a different MessagePasser root.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->executedWithdrawalsRoot = bcos::h256(42);
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto const parent =
        bcos::h256("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    registerVerifiedBlock(pair.storage, parent, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);

    auto parentHeader = pair.blockFactory->blockHeaderFactory()->createBlockHeader();
    parentHeader->setNumber(0);
    parentHeader->setTimestamp(1'699'000'000'000);
    parentHeader->setGasLimit(30'000'000);
    parentHeader->setGasUsed(0);
    parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
    parentHeader->setBaseFee(bcos::u256(1'000'000'000));

    auto request = makeValidIsthmusNewPayload(*pair.blockFactory, parent, 1);
    request.executionPayload.baseFeePerGas = bcos::engine::calcOpBaseFee(*parentHeader, false);
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);
    BOOST_REQUIRE(request.executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK_EQUAL(
        request.executionPayload.withdrawalsRoot->hex(), bcos::ledger::mpt::emptyRootHash().hex());

    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK(status.validationError->find("withdrawalsRoot") != std::string::npos);
}

/// The op-geth oracle for the rebuild path: the vendored corpus vector's payload (parsed
/// through the production wire dialect) must re-hash to the op-geth golden block hash —
/// rebuildOpEthHeader's field layout and the RLP hash match op-geth byte-for-byte.
BOOST_AUTO_TEST_CASE(op_golden_vector_rebuild_matches_op_geth_block_hash)
{
    auto sample = w6test::loadVectorSample("isthmus_deposit_only");
    auto params = w6test::makeParamsJson(sample);
    auto request = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
    auto blockFactory = makeBlockFactory();
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        *request.parentBeaconBlockRoot, bcos::engine::OpForkId::Isthmus);
    auto const rebuilt = bcos::protocol::EthBlockHeader::computeHash(*header);
    auto const golden = bcos::h256(sample.golden["blockHash"].asString());
    BOOST_CHECK_EQUAL(rebuilt.hex(), golden.hex());
}

// ── Ported upstream parity cases (fisco/release-3.18.0 OpEngineServiceParityTest.cpp) ──
// Each is adapted to this tree's harness: the delegate is FabricatedRootsStub (not the
// upstream RecordingScheduler) and newPayload's fall-through lands on the S5 import plane
// (importExecute), not a second executeBlock+commitBlock — so the fall-through pins assert
// the execute counter, not upstream's commitCalls==2.

BOOST_AUTO_TEST_CASE(op_fcu_zero_head_is_invalid)
{
    // op-geth answers STATUS_INVALID for a zero head, not SYNCING. (Ported verbatim.)
    OpServicePair pair;
    bcos::engine::ForkchoiceState forkchoice{{}, {}, {}};
    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("zero hash") != std::string::npos);
    BOOST_CHECK(!result.payloadId.has_value());
}

BOOST_AUTO_TEST_CASE(op_newpayload_built_header_commit_failure_falls_through_to_execute)
{
    // A replaced pending must not strand the payload on -32603. Adapted: commitCalls is
    // pinned at 1 (the failed built-header commit) and the retry is observed through the
    // execute counter (S5 import), where upstream saw a second commitBlock.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failCommitRemaining = 1;
    // "Replaced pending" carries the OpPendingDropped tag; that is what the fall-through
    // is scoped to.
    delegate->tagPendingDropped = true;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    // The S5 import commitment gate compares the fabricated header's txRoot/requestsHash
    // against the payload-rebuilt header, so stamp the values this payload defines (the
    // fabrication is otherwise only used for the commit-routing pin).
    delegate->txsRootToReturn = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(payload->executionPayload));
    delegate->requestsHashToReturn = bcos::engine::engine_common::c_emptyRequestsHash;

    int const executesAfterBuild = delegate->executeCalls;

    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);                 // the failed built-header commit
    BOOST_CHECK_GT(delegate->executeCalls, executesAfterBuild);  // it did re-execute
}

BOOST_AUTO_TEST_CASE(op_newpayload_missing_executed_withdrawals_root_is_internal_error)
{
    // Missing withdrawalsRoot on the executed header is a node-internal fault
    // (-32603), not a consensus INVALID. Fork level: OpServicePair's default schedule is
    // OpForkSchedule::legacy(false) == "0:isthmus", so the payload resolves to Isthmus —
    // >= Canyon, the fork whose header field set includes withdrawalsRoot (the same gate
    // rebuildOpEthHeader uses) and where this guard's presence check fires. FCU stamps a
    // valid payload; the newPayload import omits the field. Fail the built-header commit
    // once with the dropped-pending tag so control reaches the import-path check.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);

    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};
    delegate->stampWithdrawalsRoot = false;
    delegate->failCommitRemaining = 1;
    // Dropped-pending code: only that fault falls through to the import path where the
    // missing withdrawalsRoot is detected.
    delegate->tagPendingDropped = true;
    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.newPayload(request, 4)),
        bcos::engine::OpExecutionInternalError);
}

BOOST_AUTO_TEST_CASE(op_commit_error_routing_dropped_pending_reexecutes)
{
    // The intended fall-through: a commit whose built pending was dropped by a concurrent
    // reset (OpPendingDropped tag) is retried once and answers VALID when the retry
    // commits, instead of -32603 on every retry of a still-valid payload. Adapted: the
    // retry is the S5 import (no second commitBlock), so it is pinned via executeCalls.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failCommitRemaining = 1;
    delegate->tagPendingDropped = true;
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);
    // Same commitment-consistency stamp as the built-header fall-through case above: the
    // fabricated import header must match the payload-rebuilt one to land Valid.
    delegate->txsRootToReturn = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(payload->executionPayload));
    delegate->requestsHashToReturn = bcos::engine::engine_common::c_emptyRequestsHash;
    int const executesAfterBuild = delegate->executeCalls;

    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};

    auto status = bcos::task::syncWait(pair.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);                 // failed once on the built header
    BOOST_CHECK_GT(delegate->executeCalls, executesAfterBuild);  // the retry re-executed
}

BOOST_AUTO_TEST_CASE(op_commit_error_routing_non_dropped_pending_does_not_fall_through)
{
    // Only the dropped-pending commit fault may fall through to execute+commit. A first
    // commit failing with any other code must keep its routing and must NOT be retried by
    // a silent re-execute: the second commit would succeed here, so without the gate this
    // answers VALID and hides the real failure. (Ported; assertion unchanged.)
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failCommitRemaining = 1;  // first commit fails, a retry would succeed
    delegate->commitErrorCode = static_cast<int>(bcos::scheduler::SchedulerError::CommitError);
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);
    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};

    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.newPayload(request, 4)),
        bcos::engine::OpExecutionInternalError);
    // The gate must stop at the first failure: no second execute/commit attempt.
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);
}

BOOST_AUTO_TEST_CASE(op_commit_error_routing_unknown_error_does_not_fall_through)
{
    // The catch-all code: OpScheduler::classifyException reports every unclassified commit
    // fault as SchedulerError::UnknownError, and a code-keyed gate would treat that as a
    // dropped pending. Only the tag may fall through, so an untagged UnknownError must
    // route immediately. Ported; the retry would succeed, which is what makes Valid a
    // false pass.
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    delegate->failCommitRemaining = 1;  // first commit fails, a retry would succeed
    delegate->commitErrorCode = static_cast<int>(bcos::scheduler::SchedulerError::UnknownError);
    BOOST_REQUIRE(!delegate->tagPendingDropped);  // a real fault, not a dropped pending
    OpServicePair pair(/*allowSynthesizedL1Attributes=*/false, delegate);
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee = std::nullopt;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(pair.storage, *pair.blockFactory, 0, 1'699'000'000'000);
    auto built = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_REQUIRE(built.payloadId.has_value());
    auto payload = bcos::task::syncWait(pair.service.getPayload(*built.payloadId, 4));
    BOOST_REQUIRE(payload);
    bcos::engine::NewPayloadRequest request;
    request.executionRequests = std::vector<bcos::bytes>{};
    request.executionPayload = payload->executionPayload;
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.expectedBlobVersionedHashes = {};

    BOOST_CHECK_THROW(bcos::task::syncWait(pair.service.newPayload(request, 4)),
        bcos::engine::OpExecutionInternalError);
    // Not VALID: the retry must never have run.
    BOOST_CHECK_EQUAL(delegate->commitCalls, 1);
}

BOOST_AUTO_TEST_SUITE_END()
