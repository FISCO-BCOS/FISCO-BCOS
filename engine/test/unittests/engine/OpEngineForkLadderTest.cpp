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
 * @file OpEngineForkLadderTest.cpp
 * @brief WI-16: a continuous newPayload+FCU chain across all nine OP fork activations.
 */
// Step-1 probe answer (chaining): NO manual registration is needed. Rung 1 (Regolith@+1s:
// newPayloadV2 + FCU V1) goes Valid, and rung 2 (Canyon@1000s) chains onto it with no
// registerVerifiedBlock/registerParentHeader in between — parent resolution, the
// strictly-increasing timestamp gate, parent-priced baseFee and FULL EXECUTION all succeed
// across the fork boundary; the imported/canonicalized rung-1 block is found by the service
// on its own (seedCanonicalChainABC's mechanism holds across a fork switch).
//
// F-B2-1: the same probe proved the Canyon..Holocene window (V2/V3) was UNSATISFIABLE — the
// spec-shaped payload (withdrawals present-and-empty, no withdrawalsRoot field) was rejected
// by the post-execution commitment gate (OpEngineService.inl:1190) because the seal stamps
// the empty trie root from Canyon on (OpBlockExecute.cpp:497-502) while the raw payload
// optional is absent; and carrying the root instead was rejected pre-execution by the
// validator (OpEngineService.cpp:142-144). Fixed in commit 6fdc294fb (the gate now compares
// the executed header against the ANNOUNCED header's fork projection), with the focused
// regression cells in OpCanyonWindowImportTest.cpp. Both halves of the fixture below must
// carry the ladder schedule: importServiceFixtureT's WithSchedule tag ctor re-schedules only
// the seam, and a legacy-scheduled delegate would execute every rung as Isthmus.
// This file's ladder is the cross-fork driver that keeps all nine rungs pinned together.

#include "support/OpEngineKarstTestHarness.h"

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/OpForkId.h>
#include <opstack-executor/OpSchedulerSeam.h>  // bcos::evm::engine::detail::tryEngineForkId

#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>

using namespace op_engine_parity_test;

namespace
{

/// The 9-rung ladder, one row per fork activation (the plan's rung table). The fork column
/// is cross-pinned against the schedule at runtime and against extraDataLayoutFor at
/// compile time; extraData CONTENT is derived from the layout enum via extraDataFor below,
/// never hand-written per fork (OpForkId.h:113-131 pins the layout table itself).
struct Rung
{
    bcos::engine::OpForkId engineFork;
    std::uint64_t tsSeconds;
    bool hasWithdrawals;
    bool hasBlobFields;
    bcos::engine::OpExtraDataLayout layout;
};

static constexpr Rung c_rungs[] = {
    {bcos::engine::OpForkId::Regolith, 0, false, false, bcos::engine::OpExtraDataLayout::Empty},
    {bcos::engine::OpForkId::Canyon, 1000, true, false, bcos::engine::OpExtraDataLayout::Empty},
    {bcos::engine::OpForkId::Ecotone, 2000, true, true, bcos::engine::OpExtraDataLayout::Empty},
    {bcos::engine::OpForkId::Fjord, 3000, true, true, bcos::engine::OpExtraDataLayout::Empty},
    {bcos::engine::OpForkId::Granite, 4000, true, true, bcos::engine::OpExtraDataLayout::Empty},
    {bcos::engine::OpForkId::Holocene, 5000, true, true,
        bcos::engine::OpExtraDataLayout::Holocene9},
    {bcos::engine::OpForkId::Isthmus, 6000, true, true, bcos::engine::OpExtraDataLayout::Holocene9},
    {bcos::engine::OpForkId::Jovian, 7000, true, true, bcos::engine::OpExtraDataLayout::Jovian17},
    {bcos::engine::OpForkId::Karst, 8000, true, true, bcos::engine::OpExtraDataLayout::Jovian17},
};

// The shape columns must agree with the production tables they claim to mirror: the layout
// column against extraDataLayoutFor (OpForkId.h:113-144), the withdrawals column against
// Canyon's Shanghai window, the blob column against Ecotone's V3 window.
static_assert([]() constexpr {
    for (auto const& rung : c_rungs)
    {
        if (rung.layout != bcos::engine::extraDataLayoutFor(rung.engineFork))
        {
            return false;
        }
        if (rung.hasWithdrawals != (rung.engineFork >= bcos::engine::OpForkId::Canyon))
        {
            return false;
        }
        if (rung.hasBlobFields != (rung.engineFork >= bcos::engine::OpForkId::Ecotone))
        {
            return false;
        }
    }
    return true;
}());

/// extraData content for a layout, built from the production constants the layout
/// validator enforces (OpBaseFee.h): version byte first, then the non-zero 1559 pair
/// (denominator 250 / elasticity 6), then the Jovian minBaseFee word. The LENGTH falls out
/// of the layout (0/9/17) — no per-fork literal exists anywhere in this file.
inline bcos::bytes extraDataFor(bcos::engine::OpExtraDataLayout layout)
{
    if (layout == bcos::engine::OpExtraDataLayout::Empty)
    {
        return {};
    }
    bcos::bytes out{layout == bcos::engine::OpExtraDataLayout::Jovian17 ?
                        bcos::engine::c_jovianExtraDataVersion :
                        bcos::engine::c_holoceneExtraDataVersion};
    auto const appendU32 = [&out](std::uint32_t value) {
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            out.push_back(static_cast<bcos::byte>(value >> shift));
        }
    };
    appendU32(bcos::engine::kLegacyOpEip1559Params.denominatorCanyon);
    appendU32(bcos::engine::kLegacyOpEip1559Params.elasticity);
    if (layout == bcos::engine::OpExtraDataLayout::Jovian17)
    {
        out.insert(out.end(), 8, bcos::byte{0});  // minBaseFee floor 0
    }
    return out;
}

/// The ladder needs BOTH halves on the ladder schedule: the SEAM decides fork identity,
/// API profiles and request validation, while the real delegate derives each block's
/// execution config from ITS OWN schedule (OpScheduler.h:1280, m_schedule->configAt) — a
/// legacy-scheduled delegate would execute every rung as Isthmus and reject pre-Ecotone
/// headers outright (requireEcotoneHeaderFields). ImportServiceFixtureT's ctors can swap
/// only one half (WithSchedule pins the delegate to legacy, DelegateFromFactory keeps the
/// seam legacy), so this fixture composes the same members directly from harness
/// primitives, with identical seeding.
struct LadderFixture
{
    LadderFixture()
    {
        auto const g = fixtureHeadHash();
        registerVerifiedBlock(storage, g, 0);
        // Genesis parent header at timestamp 0: registerParentHeader's field set with the
        // ladder-compatible clock. The fixture default (1'699'000'000 ms) sits ABOVE every
        // rung and would trip newPayload's strictly-increasing timestamp gate
        // (OpEngineService.inl:1041) at rung 1. Explicit test-side seeding, the
        // buildPayloadAt precedent.
        registerParentHeader(storage, *blockFactory, 0, 0);
        registerEmptyCanonicalTxRow(storage, *blockFactory, 0);
        seedCommittedGenesis(storage, makeCryptoSuite()->hashImpl());
        // FCU to genesis: tracker head = G@0 (no attrs, no delegate involvement).
        bcos::engine::ForkchoiceState forkchoice{g, g, g};
        auto built = bcos::task::syncWait(service.updateForkchoice(forkchoice, nullptr, 3));
        BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    }

    /// Field-for-field the row registerParentHeader wrote above (number 0, ts 0) — the
    /// header rung 1 is priced against.
    bcos::protocol::BlockHeader::Ptr genesisParentHeader() const
    {
        auto header = blockFactory->blockHeaderFactory()->createBlockHeader();
        header->setNumber(0);
        header->setTimestamp(0);
        header->setGasLimit(30'000'000);
        header->setGasUsed(0);
        header->setExtraData(bcos::fromHex("00000000fa00000006"));
        header->setBaseFee(bcos::u256(1'000'000'000));
        header->setBlobGasUsed(0);
        return header;
    }

    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    std::unique_ptr<MLS> storagePtr{std::make_unique<MLS>(checkpoint)};
    MLS& storage{*storagePtr};
    StubMemPool memPool;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::evm::opstack::OpForkSchedule> ladder{
        std::make_shared<bcos::evm::opstack::OpForkSchedule>(
            bcos::evm::opstack::OpForkSchedule::parse(c_forkLadderCanonical))};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> delegate{
        std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(makeImportReceiptFactory(),
            makeCryptoSuite()->hashImpl(), /*chainId=*/8453, ladder, blockFactory, storage,
            /*ledger=*/nullptr, ioServicePool)};
    EngineOpScheduler seamScheduler{ladder, {}};
    OpEngine service{memPool, storage, seamScheduler, blockFactory,
        bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false};
};

/// The baseFee the service will demand for a child of @p pricedParent: mirror of its
/// compose (OpEngineService.inl:1048-1060 via baseFeeClockFor:104-115) from public
/// primitives — the clock keys on the PARENT's fork (by parent timestamp through the same
/// ladder), the denominator on the CHILD's.
inline bcos::u256 expectedNextBaseFee(bcos::evm::opstack::OpForkSchedule const& ladder,
    bcos::protocol::BlockHeader const& pricedParent, bcos::engine::OpForkId childFork)
{
    auto const parentTsSec = bcos::engine::unixSecondsFromInternalMillis(
        static_cast<std::uint64_t>(pricedParent.timestamp()));
    auto const parentFork = bcos::evm::engine::detail::tryEngineForkId(ladder.forkAt(parentTsSec));
    BOOST_REQUIRE(parentFork.has_value());
    auto const parentLayout = bcos::engine::extraDataLayoutFor(*parentFork);
    bcos::engine::OpBaseFeeClock const clock{
        .parentIsHolocene = parentLayout != bcos::engine::OpExtraDataLayout::Empty,
        .parentIsJovian = parentLayout == bcos::engine::OpExtraDataLayout::Jovian17,
        .newBlockIsCanyon = childFork != bcos::engine::OpForkId::Regolith};
    return bcos::engine::calcOpNextBlockBaseFee(pricedParent, clock);
}

/// Drives rung requests against a LadderFixture. announcedByNumber tracks the FILLED
/// (announced-content) headers — the headers the service itself prices chained imports
/// against, because importedStore stores the payload-rebuilt header.
struct LadderDriver
{
    explicit LadderDriver(LadderFixture& fixtureIn) : fixture(fixtureIn) {}
    LadderFixture& fixture;
    std::map<std::int64_t, bcos::protocol::BlockHeader::Ptr> announcedByNumber;

    /// Parent header the service will price @p number against: the seeded genesis row for
    /// height 1, the previous rung's announced header above.
    [[nodiscard]] bcos::protocol::BlockHeader::Ptr pricedParentFor(std::int64_t number) const
    {
        if (number == 1)
        {
            return fixture.genesisParentHeader();
        }
        return announcedByNumber.at(number - 1);
    }

    /// Rung-shaped request skeleton: the rung table's columns become the request's shape;
    /// commitments are learned afterwards by fillRungCommitments. @p extraData overrides
    /// the rung's own layout (the foreign-layout negative control differs ONLY here).
    [[nodiscard]] bcos::engine::NewPayloadRequest makeRungRequest(
        bcos::protocol::BlockHeader const& pricedParent, bcos::h256 const& parentHash,
        std::int64_t number, Rung const& rung, bcos::bytes extraData) const
    {
        bcos::engine::NewPayloadRequest request;
        auto& payload = request.executionPayload;
        payload.parentHash = parentHash;
        payload.blockNumber = number;
        // Whole seconds only (EthBlockHeader rejects sub-second ms) and strictly greater
        // than the parent's. Rungs 2-9 land ON their activation second; rung 1 lands one
        // second after its activation because the genesis itself occupies second 0
        // (Regolith activates at 0) and the timestamp gate is strict.
        payload.timestamp = rung.tsSeconds * 1000ULL + (number == 1 ? 1000ULL : 0ULL);
        payload.gasLimit = 30'000'000;
        payload.gasUsed = 0;  // filled from execution below
        // OP blocks always carry the L1-attributes deposit: a zero-tx block is a consensus
        // reject ("missing L1 attributes deposit (empty block)", preBlockOpSteps). The
        // Isthmus-length attributes form is the uniform choice: pre-Jovian the
        // DA-footprint lane is off, and on Jovian+ it is the activation form —
        // deposits-only, DA footprint 0, matching blobGasUsed 0.
        bcos::engine::EngineTransaction depositTx;
        depositTx.raw =
            bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(/*jovianActive=*/false);
        payload.transactions.push_back(std::move(depositTx));
        if (rung.hasWithdrawals)
        {
            payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
            // withdrawalsRoot is a payload field only from Isthmus (V4) on; below it the
            // header DERIVES the root from the withdrawals list
            // (rebuildOpEthHeader/validateOpPayloadWindowFields).
            if (rung.engineFork >= bcos::engine::OpForkId::Isthmus)
            {
                payload.withdrawalsRoot = bcos::ledger::mpt::emptyRootHash();
            }
        }
        if (rung.hasBlobFields)
        {
            payload.excessBlobGas = bcos::u256(0);
            payload.blobGasUsed = bcos::u256(0);
        }
        // The beacon root exists only from Ecotone (newPayloadV3+); below it the field must
        // be absent entirely (validateOpPayloadWindowFields).
        if (rung.engineFork >= bcos::engine::OpForkId::Ecotone)
        {
            request.parentBeaconBlockRoot = bcos::h256{};
        }
        // Same window rule for the execution-requests list: present-but-empty only from
        // Isthmus (V4) on, absent before.
        if (rung.engineFork >= bcos::engine::OpForkId::Isthmus)
        {
            request.executionRequests = std::vector<bcos::bytes>{};
        }
        payload.extraData = std::move(extraData);
        // Priced from the ACTUAL parent header — the service recomputes and enforces it.
        payload.baseFeePerGas = expectedNextBaseFee(*fixture.ladder, pricedParent, rung.engineFork);
        return request;
    }

    /// Learn the true execution commitments for @p request by importing the identical block
    /// once at scheduler level (fillCommitmentsFromProbe's mechanism, parameterized by
    /// fork), copy them into the payload, and re-hash. The parent is ALWAYS the canonical
    /// tip here — the fixture seeds genesis canonical and each rung's FCU canonicalizes its
    /// own height before the next rung builds — so the probe plane is the committed plane
    /// (empty flat, no parent headers), the same plane the service's own import runs on.
    /// Returns the FILLED announced header for the next rung's pricing.
    bcos::protocol::BlockHeader::Ptr fillRungCommitments(
        bcos::engine::NewPayloadRequest& request, Rung const& rung)
    {
        auto& blockFactory = *fixture.blockFactory;
        auto& payload = request.executionPayload;
        auto const txRoot =
            EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
        auto header =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory.blockHeaderFactory(),
                payload, txRoot, request.parentBeaconBlockRoot, rung.engineFork);

        auto probeBlock = blockFactory.createBlock();
        probeBlock->setBlockHeader(header);
        {
            // buildOpBlock equivalent: envelopes -> tars transactions.
            auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
            for (auto const& env : bcos::engine::detail::rawEnvelopes(payload))
            {
                auto const txHash = hashImpl.hash(env);
                auto tarsTx = bcos::engine::engine_common::op::opEnvelopeToTars(
                    env, txHash, /*allowDeposit=*/true);
                BOOST_REQUIRE(tarsTx.has_value());
                tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
                auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [tars = std::move(*tarsTx)]() mutable { return &tars; });
                probeBlock->appendTransaction(std::move(tx));
            }
        }
        bcos::Error::Ptr probeError;
        bcos::protocol::BlockHeader::Ptr executed;
        fixture.delegate->importExecute(probeBlock, {}, /*parentFlat=*/nullptr,
            [&](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr done,
                std::shared_ptr<void> /*delta*/, std::shared_ptr<void> /*flat*/) {
                probeError = std::move(error);
                executed = std::move(done);
            });
        BOOST_REQUIRE(!probeError);
        BOOST_REQUIRE(executed != nullptr);

        payload.stateRoot = executed->stateRoot();
        payload.receiptsRoot = executed->receiptsRoot();
        payload.gasUsed = executed->gasUsed();
        if (payload.withdrawalsRoot.has_value())
        {
            payload.withdrawalsRoot = executed->withdrawalsRoot();
        }
        auto const bloom = executed->logsBloom();
        std::memcpy(payload.logsBloom.data(), bloom.data(),
            std::min(bloom.size(), payload.logsBloom.size()));

        auto const filledTxRoot =
            EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(payload));
        auto filledHeader =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory.blockHeaderFactory(),
                payload, filledTxRoot, request.parentBeaconBlockRoot, rung.engineFork);
        payload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*filledHeader);
        return filledHeader;
    }
};

}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineForkLadderSuite)

/// WI-16: one chain that walks all nine fork activations, one rung (block) per fork, driven
/// by newPayload + FCU. Every rung is checked against the schedule it must resolve to, and
/// each rung additionally carries a foreign-layout NEGATIVE CONTROL (same height and parent,
/// only the extraData layout differs) that must NOT be accepted — otherwise "Valid" alone
/// would not prove the service really classified the rung as that fork.
BOOST_AUTO_TEST_CASE(NineForkLadderIsContinuous)
{
    LadderFixture fixture;
    LadderDriver driver{fixture};
    auto const ladder = bcos::evm::opstack::OpForkSchedule::parse(c_forkLadderCanonical);

    bcos::h256 headHash = fixtureHeadHash();
    std::size_t rungsChecked = 0;
    std::size_t negativeControls = 0;
    for (auto const& rung : c_rungs)
    {
        auto const number = static_cast<std::int64_t>(rungsChecked + 1);
        BOOST_TEST_INFO_SCOPE("rung " << rungsChecked << " ts=" << rung.tsSeconds
                                      << " fork=" << static_cast<int>(rung.engineFork));

        // The schedule itself is the oracle for the rung's identity (independent of the
        // service's internals): OpFork -> OpForkId via the same mapping the seam uses.
        auto const scheduled =
            bcos::evm::engine::detail::tryEngineForkId(ladder.forkAt(rung.tsSeconds));
        BOOST_REQUIRE(scheduled.has_value());
        BOOST_CHECK_EQUAL(static_cast<int>(*scheduled), static_cast<int>(rung.engineFork));

        auto const profile = bcos::engine::engineApiProfileFor(rung.engineFork);
        auto const version = static_cast<std::uint32_t>(profile.newPayload);

        // Negative control FIRST, at the same height and parent as the rung itself, so the
        // rejection can only come from the layout (not from an occupied height or a missing
        // ancestor). The three layouts are mutually exclusive: Empty / Holocene9 / Jovian17.
        {
            auto const ownLayout = bcos::engine::extraDataLayoutFor(rung.engineFork);
            auto const wrongLayout = ownLayout == bcos::engine::OpExtraDataLayout::Empty ?
                                         bcos::engine::OpExtraDataLayout::Holocene9 :
                                         bcos::engine::OpExtraDataLayout::Empty;
            auto wrong = driver.makeRungRequest(
                *driver.pricedParentFor(number), headHash, number, rung, extraDataFor(wrongLayout));
            // Complete the control exactly like the accepted rung (probe-filled commitments +
            // announced-header hash) so the ONLY difference is the extraData layout. Without
            // this the request carried no blockHash and was rejected for THAT reason instead,
            // so the control passed vacuously — found by the V-LADDER-layoutGuard mutation
            // (defeating the layout guard left the ladder green).
            (void)driver.fillRungCommitments(wrong, rung);
            auto const wrongStatus =
                bcos::task::syncWait(fixture.service.newPayload(wrong, version));
            BOOST_CHECK_MESSAGE(wrongStatus.status != bcos::engine::PayloadValidationStatus::Valid,
                "rung " << rungsChecked << " (fork " << static_cast<int>(rung.engineFork)
                        << "): a foreign extraData layout must not be accepted");
            ++negativeControls;
        }

        auto request = driver.makeRungRequest(
            *driver.pricedParentFor(number), headHash, number, rung, extraDataFor(rung.layout));
        driver.announcedByNumber[number] = driver.fillRungCommitments(request, rung);
        auto status = bcos::task::syncWait(fixture.service.newPayload(request, version));
        BOOST_REQUIRE_MESSAGE(status.status == bcos::engine::PayloadValidationStatus::Valid,
            "rung " << rungsChecked << " (fork " << static_cast<int>(rung.engineFork) << ", ts "
                    << rung.tsSeconds
                    << ") newPayload rejected: " << status.validationError.value_or("(no error)"));
        BOOST_REQUIRE(status.latestValidHash.has_value());
        BOOST_CHECK_EQUAL(status.latestValidHash->hex(), request.executionPayload.blockHash.hex());

        headHash = request.executionPayload.blockHash;
        bcos::engine::ForkchoiceState fc{headHash, headHash, headHash};
        auto fcu = bcos::task::syncWait(fixture.service.updateForkchoice(
            fc, nullptr, static_cast<std::uint32_t>(profile.forkchoiceUpdated)));
        BOOST_REQUIRE_EQUAL(static_cast<int>(fcu.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
        ++rungsChecked;
    }
    // Proof of run: nine rungs and nine attributed negative controls (0-cell green guard).
    BOOST_CHECK_EQUAL(rungsChecked, std::size_t{9});
    BOOST_CHECK_EQUAL(negativeControls, std::size_t{9});
}

BOOST_AUTO_TEST_SUITE_END()
