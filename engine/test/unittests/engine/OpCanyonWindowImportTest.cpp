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
 * @file OpCanyonWindowImportTest.cpp
 * @brief F-B2-1 regression: Canyon..Holocene payloads must land through the newPayload
 * import path. The op-node wire omits withdrawalsRoot on every pre-Isthmus payload (the
 * field is Isthmus-only), while the executor's seal stamps the empty-trie root into the
 * executed header from Canyon on — so the post-execution commitment gate must compare the
 * executed header against the ANNOUNCED header's fork projection (rebuildOpEthHeader),
 * never against the raw wire optional (which collapses "absent" to the zero hash and can
 * never equal the Canyon seal).
 *
 * Deliberately NOT covered here: Holocene. Its 9-byte extraData encodes the EIP-1559
 * params (version byte 0x00 || denominator || elasticity) that a zero-padded shape cannot
 * satisfy, and the Holocene base-fee clock prices from the parent's own extraData — the
 * cell needs the parent/payload pricing pair built separately rather than reusing this
 * file's constants-path fixture shape.
 */

#include "support/OpEngineKarstTestHarness.h"

#include <opstack-executor/OpScheduler.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;
using namespace op_engine_parity_test;

BOOST_AUTO_TEST_SUITE(OpCanyonWindowImportSuite)

namespace
{
/// The canonical Canyon-window schedule: Regolith at 0s, Canyon activating at 1000s.
constexpr std::string_view c_canyonWindowSchedule = "0:regolith,1000:canyon";
/// The V3-window schedule for the Fjord cell: the payload is the Fjord activation block.
constexpr std::string_view c_fjordWindowSchedule = "0:regolith,1000:canyon,2000:ecotone,3000:fjord";
/// The parent (genesis at height 0) sits just below the payload's rung so the payload is
/// the fork's activation block. The fixture default genesis ts (1'699'000'000'000 ms) is
/// far above every rung, hence the re-registration, cf. buildPayloadAt.
constexpr int64_t c_parentTimestampMs = 999'000;
/// Payload timestamps (internal milliseconds): 1000s (Canyon) and 3000s (Fjord).
constexpr std::uint64_t c_canyonPayloadTimestampMs = 1'000'000;
constexpr std::uint64_t c_fjordPayloadTimestampMs = 3'000'000;

/// ImportServiceFixtureT with the custom schedule fed to BOTH the seam (the engine's fork
/// resolver: ctx.forkId, the API profile and the base-fee clock) and the REAL OpScheduler
/// delegate (the executed header's seal config). The harness's WithSchedule tag ctor
/// re-schedules only the seam; its delegate keeps legacy("0:isthmus"), whose seal stamps
/// Isthmus-shaped commitments (MessagePasser storage root + requestsHash) into the
/// executed header — an unrelated mismatch that would mask the F-B2-1 gate. Same member
/// composition as ImportServiceFixtureT, built locally so both components share the
/// Canyon-window schedule.
struct CanyonWindowFixture
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> delegate;
    EngineOpScheduler seamScheduler;
    OpEngine service;

    explicit CanyonWindowFixture(std::string_view scheduleCanonical)
      : delegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(
            makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
            std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                bcos::evm::opstack::OpForkSchedule::parse(scheduleCanonical)),
            blockFactory, storage, /*ledger=*/nullptr, ioServicePool)),
        seamScheduler(std::make_shared<const bcos::evm::opstack::OpForkSchedule>(
                          bcos::evm::opstack::OpForkSchedule::parse(scheduleCanonical)),
            {}),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        auto const g = fixtureHeadHash();
        registerVerifiedBlock(storage, g, 0);
        // The parent header must sit BELOW the payload's fork rung (the fixture default
        // genesis ts is above every rung) — the re-registration pattern of buildPayloadAt.
        registerParentHeader(storage, *blockFactory, 0, c_parentTimestampMs);
        registerEmptyCanonicalTxRow(storage, *blockFactory, 0);
        seedCommittedGenesis(storage, makeCryptoSuite()->hashImpl());
        // FCU to genesis: tracker head = G@0 (no attrs, no delegate involvement).
        bcos::engine::ForkchoiceState forkchoice{g, g, g};
        auto built = bcos::task::syncWait(service.updateForkchoice(forkchoice, nullptr, 3));
        BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    }

    /// The stored parent header the engine itself prices from
    /// (SYS_NUMBER_2_BLOCK_HEADER[0] — the same row runOpNewPayloadSteps reads).
    bcos::protocol::BlockHeader::Ptr storedParentHeader()
    {
        auto view = storage.fork();
        auto entry = bcos::task::syncWait(bcos::storage2::readOne(
            view, bcos::executor_v1::StateKeyView{
                      bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(0)}));
        BOOST_REQUIRE(entry.has_value());
        auto const storedHeader = entry->get();
        bcos::bytes parentHeaderBytes(storedHeader.begin(), storedHeader.end());
        return blockFactory->blockHeaderFactory()->createBlockHeader(std::move(parentHeaderBytes));
    }

    /// Fork-aware sibling of ImportServiceFixtureT::fillCommitmentsFromProbe (which
    /// hardcodes OpForkId::Isthmus — an announced-header shape the Canyon-window forks
    /// must NOT carry: it would stamp a requestsHash/blob pair into the hash oracle).
    /// Import the identical block once at scheduler level, copy the true execution
    /// commitments into the payload, then re-hash through the payload's OWN fork. The
    /// executed withdrawalsRoot is deliberately NOT copied back: pre-Isthmus the wire
    /// field is absent and the validator rejects it, while rebuildOpEthHeader projects
    /// the empty-trie root for the announced header on both sides.
    void fillCommitmentsFromProbe(
        bcos::engine::NewPayloadRequest& request, bcos::engine::OpForkId forkId)
    {
        auto& payload = request.executionPayload;
        auto const txRoot =
            EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
        auto header =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory->blockHeaderFactory(),
                payload, txRoot, request.parentBeaconBlockRoot, forkId);

        // Probe: buildOpBlock-equivalent block (envelopes -> tars transactions), executed
        // once against the parent's plane (the canonical tip's committed genesis here).
        auto probeBlock = blockFactory->createBlock();
        probeBlock->setBlockHeader(header);
        {
            auto& hashImpl = *blockFactory->cryptoSuite()->hashImpl();
            for (auto const& env : bcos::engine::detail::rawEnvelopes(payload))
            {
                auto const txHash = hashImpl.hash(env);
                auto tarsTx = bcos::engine::engine_common::op::opEnvelopeToTars(
                    env, txHash, /*allowDeposit=*/true);
                BOOST_REQUIRE(tarsTx.has_value());
                tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
                auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [inner = std::move(*tarsTx)]() mutable { return &inner; });
                probeBlock->appendTransaction(std::move(tx));
            }
        }
        BOOST_CHECK_EQUAL(probeBlock->transactionsSize(), payload.transactions.size());
        bcos::protocol::BlockHeader::Ptr executed;
        delegate->importExecute(probeBlock, {}, /*parentFlat=*/nullptr,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr done,
                std::shared_ptr<void>, std::shared_ptr<void>) {
                if (error)
                {
                    BOOST_FAIL(std::string("probe import failed: ") + error->errorMessage());
                }
                executed = std::move(done);
            });
        BOOST_REQUIRE(executed != nullptr);
        payload.stateRoot = executed->stateRoot();
        payload.receiptsRoot = executed->receiptsRoot();
        payload.gasUsed = executed->gasUsed();
        auto const bloom = executed->logsBloom();
        std::memcpy(payload.logsBloom.data(), bloom.data(),
            std::min(bloom.size(), payload.logsBloom.size()));

        auto const filledTxRoot =
            EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
        auto filledHeader =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory->blockHeaderFactory(),
                payload, filledTxRoot, request.parentBeaconBlockRoot, forkId);
        payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*filledHeader);
    }
};

/// The cell-1/3 request shape: a pre-Isthmus wire payload (withdrawalsRoot ABSENT — the
/// Isthmus-only field), deposit-only tx list, baseFee priced from the ACTUAL parent header.
bcos::engine::NewPayloadRequest makeAbsentRootRequest(CanyonWindowFixture& fixture,
    bcos::engine::OpForkId forkId, std::uint64_t timestampMs, bool hasBlobFields)
{
    auto request = makeNewPayloadAt(*fixture.blockFactory, fixtureHeadHash(), /*blockNumber=*/1,
        forkId, /*hasWithdrawals=*/true, hasBlobFields, /*extraDataLen=*/0, timestampMs);
    // makeNewPayloadAt pre-sets the Isthmus/ECotone wire fields; pre-Isthmus V2/V3 requests
    // must carry them ABSENT again (validateOpPayloadWindowFields' <V4 arm).
    request.executionRequests.reset();
    if (forkId < bcos::engine::OpForkId::Ecotone)
    {
        request.parentBeaconBlockRoot.reset();
    }
    request.executionPayload.withdrawalsRoot.reset();
    // OP blocks always carry the L1 attributes deposit (a zero-tx block is a consensus
    // reject: "missing L1 attributes deposit").
    bcos::engine::EngineTransaction depositTx;
    depositTx.raw = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
    request.executionPayload.transactions.push_back(std::move(depositTx));
    // A literal baseFee is rejected; the engine prices from the stored parent header
    // (pre-Holocene parent -> constants clock: denominator 250 / elasticity 6, exactly
    // what the parent's own 9-byte extraData encodes, so calcOpBaseFee agrees).
    request.executionPayload.baseFeePerGas =
        bcos::engine::calcOpBaseFee(*fixture.storedParentHeader(), /*parentIsJovian=*/false);
    return request;
}
}  // namespace

/// THE discriminating cell: this exact shape is INVALID before the F-B2-1 fix. The
/// executed header carries the Canyon empty-trie seal (0x56e81f...), the wire root is
/// absent, and the pre-fix gate collapsed the absent optional to zero — mismatch. The fix
/// projects the announced header (empty-trie root at Canyon) and the payload lands.
BOOST_AUTO_TEST_CASE(CanyonAbsentWithdrawalsRootImports)
{
    CanyonWindowFixture f(c_canyonWindowSchedule);
    auto request = makeAbsentRootRequest(
        f, bcos::engine::OpForkId::Canyon, c_canyonPayloadTimestampMs, /*hasBlobFields=*/false);
    f.fillCommitmentsFromProbe(request, bcos::engine::OpForkId::Canyon);

    auto status = bcos::task::syncWait(f.service.newPayload(request,
        static_cast<std::uint32_t>(
            bcos::engine::engineApiProfileFor(bcos::engine::OpForkId::Canyon).newPayload)));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    if (status.validationError.has_value())
    {
        BOOST_TEST_MESSAGE("Canyon absent-root import error: " << *status.validationError);
    }
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), request.executionPayload.blockHash.hex());
}

/// The validator absence rule must stay intact: a payload carrying the Isthmus-only
/// withdrawalsRoot on the pre-Isthmus wire is rejected by validateOpPayloadWindowFields
/// (OpEngineService.cpp:142-144), before any execution. The blockHash is recomputed after
/// setting the root exactly as makeNewPayloadAt does internally (at Canyon the announced
/// projection derives the root from the withdrawals list, so the hash is unchanged — the
/// recompute documents intent rather than moving bytes).
BOOST_AUTO_TEST_CASE(CanyonPresentWithdrawalsRootRejectedByValidator)
{
    CanyonWindowFixture f(c_canyonWindowSchedule);
    auto request = makeAbsentRootRequest(
        f, bcos::engine::OpForkId::Canyon, c_canyonPayloadTimestampMs, /*hasBlobFields=*/false);
    request.executionPayload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        f.blockFactory->blockHeaderFactory(), request.executionPayload, txRoot,
        request.parentBeaconBlockRoot, bcos::engine::OpForkId::Canyon);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(f.service.newPayload(request,
        static_cast<std::uint32_t>(
            bcos::engine::engineApiProfileFor(bcos::engine::OpForkId::Canyon).newPayload)));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.validationError.has_value());
    BOOST_CHECK(status.validationError->find(
                    "withdrawalsRoot must be absent before the Isthmus fork") != std::string::npos);
}

/// The fix covers the V3 side of the same gate: Fjord activation payload, absent wire
/// root, full real-scheduler import. (Holocene is deliberately skipped — see the file
/// header: its 9-byte extraData encodes eip1559 params a zero-padded form cannot satisfy.)
BOOST_AUTO_TEST_CASE(FjordAbsentWithdrawalsRootImports)
{
    CanyonWindowFixture f(c_fjordWindowSchedule);
    auto request = makeAbsentRootRequest(
        f, bcos::engine::OpForkId::Fjord, c_fjordPayloadTimestampMs, /*hasBlobFields=*/true);
    f.fillCommitmentsFromProbe(request, bcos::engine::OpForkId::Fjord);

    auto status = bcos::task::syncWait(f.service.newPayload(
        request, static_cast<std::uint32_t>(
                     bcos::engine::engineApiProfileFor(bcos::engine::OpForkId::Fjord).newPayload)));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    if (status.validationError.has_value())
    {
        BOOST_TEST_MESSAGE("Fjord absent-root import error: " << *status.validationError);
    }
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), request.executionPayload.blockHash.hex());
}

BOOST_AUTO_TEST_SUITE_END()
