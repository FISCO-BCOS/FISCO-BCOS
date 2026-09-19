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
 * @file OpEngineApiVersionsTest.cpp
 * @brief S3 Engine API version windows (Bedrock..Karst): the method-number x
 *        timestamp table, payload shape by (version, fork), FCU V1-V3, and the
 *        two-clock next-block baseFee.
 */

// S3: the Engine method number is chosen by op-node from the payload timestamp, so
// the EL only checks the pair. A mismatch is UnsupportedFork (JSON-RPC -38005); a
// match must not be rejected for being "the wrong version" — whatever the shape
// validation then says about the stub body is a separate concern.

#include "support/OpEngineKarstTestHarness.h"

#include <bcos-framework/engine/Errors.h>
#include <bcos-task/Wait.h>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>

using namespace op_engine_parity_test;
using bcos::engine::ApiVersion;
using bcos::engine::UnsupportedFork;

BOOST_AUTO_TEST_SUITE(OpEngineApiVersionsTest)

namespace
{
std::shared_ptr<bcos::evm::opstack::OpForkSchedule> historical()
{
    using F = bcos::evm::opstack::OpFork;
    using S = bcos::evm::opstack::OpForkSchedule;
    return std::make_shared<S>(
        S{{{F::Regolith, 0}, {F::Canyon, 100}, {F::Ecotone, 200}, {F::Holocene, 300},
              {F::Isthmus, 400}, {F::Jovian, 500}, {F::Karst, 600}},
            S::TestBypass{}});
}

bcos::engine::NewPayloadRequest stubAt(uint64_t tsSec)
{
    bcos::engine::NewPayloadRequest req;
    req.executionPayload.timestamp = tsSec * 1000;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.withdrawals.emplace();
    return req;
}

void expectUnsupportedFork(
    OpEngine& service, bcos::engine::NewPayloadRequest const& req, std::uint32_t version)
{
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(service.newPayload(req, version)), UnsupportedFork,
        [](UnsupportedFork const&) { return true; });
}

/// Payload building goes through the delegate, so a handshake that must return a
/// payloadId needs the harness's recording scheduler: the default null delegate throws
/// at requireDelegate.
struct HistoricalPair
{
    std::shared_ptr<FabricatedRootsStub> delegate{std::make_shared<FabricatedRootsStub>()};
    OpServicePair pair;

    HistoricalPair() : pair(/*allowSynthesized=*/true, delegate, nullptr, historical())
    {
        delegate->failFirst = false;
        delegate->headerFactory = pair.blockFactory->blockHeaderFactory();
    }
};
}  // namespace

// clang-format off
BOOST_AUTO_TEST_CASE(NewPayloadWrongVersionIsUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    // V4 + Canyon time
    expectUnsupportedFork(pair.service, stubAt(100), static_cast<uint32_t>(ApiVersion::V4));
    // V2 + Ecotone time
    expectUnsupportedFork(pair.service, stubAt(200), static_cast<uint32_t>(ApiVersion::V2));
}

// clang-format off
BOOST_AUTO_TEST_CASE(NewPayloadMatchingVersionIsNotUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    // With the pair matched, today's shape validation still rejects or records the
    // stub rather than throwing. "Did not throw" is NOT a claim that a V2 stub is
    // well-formed.
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.newPayload(stubAt(100), static_cast<uint32_t>(ApiVersion::V2)))));
}

// With attrs the method number is part of the pair too: Regolith builds with FCU V1
// and Canyon with V2, so neither may be turned away as an unsupported fork.
// clang-format off
BOOST_AUTO_TEST_CASE(FcuRegolithV1IsNotUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(/*allowSynthesized=*/true, nullptr, nullptr, historical());
    auto attrs = makeRegolithAttrs(0);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.updateForkchoice(fc, &attrs, static_cast<uint32_t>(ApiVersion::V1)))));
}

// clang-format off
BOOST_AUTO_TEST_CASE(FcuCanyonV2IsNotUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(/*allowSynthesized=*/true, nullptr, nullptr, historical());
    auto attrs = makeCanyonAttrs(100'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(pair.storage, hash, 0);
    // Only the gate is under test: a build that fails for want of a parent header
    // comes back Invalid, not an unsupported fork. Getting a payloadId is Task 9.
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.updateForkchoice(fc, &attrs, static_cast<uint32_t>(ApiVersion::V2)))));
}

// clang-format off
BOOST_AUTO_TEST_CASE(FcuV2AtIsthmusIsUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(true, nullptr, nullptr, historical());
    auto attrs = makeCanyonAttrs(400'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(pair.service.updateForkchoice(
                              fc, &attrs, static_cast<uint32_t>(ApiVersion::V2))),
        UnsupportedFork, [](UnsupportedFork const&) { return true; });
}

// The OP-specific attrs rules key on the fork's extraData layout, not on a Jovian
// boolean: pre-Holocene carries no 1559 params at all.
// clang-format off
BOOST_AUTO_TEST_CASE(PreHoloceneAttrsRejectEip1559Params, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto attrs = makeOpPayloadAttributes();
    attrs.minBaseFee.reset();
    auto err =
        engine_common::op::validateOpPayloadAttributes(attrs, bcos::engine::OpForkId::Canyon);
    BOOST_REQUIRE(err);
    BOOST_CHECK(err->find("eip1559Params") != std::string::npos);
}

// clang-format off
BOOST_AUTO_TEST_CASE(PreHoloceneAttrsAcceptMissingEip1559Params, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto attrs = makeOpPayloadAttributes();
    attrs.eip1559Params.reset();
    attrs.minBaseFee.reset();
    BOOST_CHECK(
        !engine_common::op::validateOpPayloadAttributes(attrs, bcos::engine::OpForkId::Canyon));
}

// The payload shape follows the (method version, fork) pair: the Ethereum-side fields
// (withdrawals list, beacon root, blob pair, withdrawalsRoot, execution requests)
// arrive with the method's window, while the OP-specific extras key on the fork itself.
// clang-format off
BOOST_AUTO_TEST_CASE(ValidateNewPayloadV2RegolithWithdrawalsMustBeAbsent, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    bcos::engine::NewPayloadRequest req;
    req.executionPayload.timestamp = 0;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.gasLimit = 30'000'000;
    req.executionPayload.gasUsed = 0;
    req.executionPayload.extraData.clear();
    BOOST_CHECK(!engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Regolith, static_cast<uint32_t>(ApiVersion::V2)));

    req.executionPayload.withdrawals.emplace();
    auto err = engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Regolith, static_cast<uint32_t>(ApiVersion::V2));
    BOOST_REQUIRE(err);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ValidateNewPayloadV2CanyonWithdrawalsEmptyArray, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    bcos::engine::NewPayloadRequest req;
    req.executionPayload.timestamp = 100'000;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.gasLimit = 30'000'000;
    req.executionPayload.gasUsed = 0;
    req.executionPayload.withdrawals.emplace();
    req.executionPayload.extraData.clear();
    BOOST_CHECK(!engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Canyon, static_cast<uint32_t>(ApiVersion::V2)));

    req.executionPayload.withdrawals.reset();
    auto err = engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Canyon, static_cast<uint32_t>(ApiVersion::V2));
    BOOST_REQUIRE(err);

    req.executionPayload.withdrawals.emplace();
    req.executionPayload.extraData = {0x00};
    err = engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Canyon, static_cast<uint32_t>(ApiVersion::V2));
    BOOST_REQUIRE(err);
    BOOST_CHECK(err->find("empty") != std::string::npos);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ValidateNewPayloadV3RequiresBeaconNotWithdrawalsRoot, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    bcos::engine::NewPayloadRequest req;
    req.executionPayload.timestamp = 200'000;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.gasLimit = 30'000'000;
    req.executionPayload.gasUsed = 0;
    req.executionPayload.withdrawals.emplace();
    req.executionPayload.blobGasUsed = 0;
    req.executionPayload.excessBlobGas = 0;
    req.parentBeaconBlockRoot = bcos::h256(1);
    auto err = engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Ecotone, static_cast<uint32_t>(ApiVersion::V3));
    BOOST_CHECK(!err);

    req.executionPayload.withdrawalsRoot = bcos::h256(2);
    err = engine_common::op::validateOpNewPayloadRequest(
        req, bcos::engine::OpForkId::Ecotone, static_cast<uint32_t>(ApiVersion::V3));
    BOOST_REQUIRE(err);
}

/// Parent written with PRE-Holocene extraData (empty), the shape the fork's layout
/// requires. registerParentHeader stamps 9-byte Holocene params, which would make a
/// pre-Holocene parent look Holocene and hide the clock under test.
void registerPreHoloceneParent(
    MLS& storage, bcos::protocol::BlockFactory& factory, int64_t number, int64_t timestampMs)
{
    auto header = factory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(number);
    header->setTimestamp(timestampMs);
    header->setGasLimit(30'000'000);
    header->setGasUsed(20'000'000);
    header->setExtraData({});
    header->setBaseFee(bcos::u256(1'000'000'000));
    bcos::bytes encoded;
    header->encode(encoded);
    auto view = storage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(std::move(encoded));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(number)},
        std::move(entry)));
    bcos::task::syncWait(storage.mergeView(std::move(view)));
}

// The pre-Holocene price comes from the chain constants (elasticity 6, denominator
// 250 at Canyon), not from the parent's (empty) extraData.
// clang-format off
BOOST_AUTO_TEST_CASE(NewPayloadPreHoloceneBaseFeeMismatchIsInvalid, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    auto hash = fixtureHeadHash();
    registerVerifiedBlock(pair.storage, hash, 0);
    registerPreHoloceneParent(pair.storage, *pair.blockFactory, 0, 99'000);
    auto req = stubAt(100);
    req.executionPayload.parentHash = hash;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.withdrawals.emplace();
    req.executionPayload.baseFeePerGas = 1;
    // The header-hash check runs before the price check, so the announced hash must be
    // the real one or the payload never reaches the clock under test.
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(req.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), req.executionPayload, txRoot,
        req.parentBeaconBlockRoot, bcos::engine::OpForkId::Canyon);
    req.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(
        pair.service.newPayload(req, static_cast<std::uint32_t>(ApiVersion::V2)));
    BOOST_CHECK(status.status == bcos::engine::PayloadValidationStatus::Invalid);
    BOOST_REQUIRE(status.validationError);
    BOOST_CHECK_MESSAGE(status.validationError->find("baseFeePerGas") != std::string::npos,
        "got: " << *status.validationError);
}

// The Holocene activation block carries its own 9-byte extraData, but its PARENT is
// pre-Holocene, so the constants still price it. Pricing it from its own fork would
// decode an empty parent extraData (op-reth's #13060 regression).
// clang-format off
BOOST_AUTO_TEST_CASE(HoloceneActivationUsesConstantBaseFee, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair(false, nullptr, nullptr, historical());
    auto hash = fixtureHeadHash();
    registerVerifiedBlock(pair.storage, hash, 0);
    // 299s is Ecotone: pre-Holocene, so its extraData is empty.
    registerPreHoloceneParent(pair.storage, *pair.blockFactory, 0, 299'000);

    auto parent = pair.blockFactory->blockHeaderFactory()->createBlockHeader();
    parent->setGasLimit(30'000'000);
    parent->setGasUsed(20'000'000);
    parent->setBaseFee(bcos::u256(1'000'000'000));
    parent->setExtraData({});
    auto const expected = bcos::engine::calcOpNextBlockBaseFee(
        *parent, {.parentIsHolocene = false, .parentIsJovian = false, .newBlockIsCanyon = true});
    BOOST_CHECK_EQUAL(expected, bcos::u256(1'012'000'000));

    // Feed that price to a V3 payload at the Holocene activation timestamp: whatever
    // the outcome (the stub body may still be INVALID / SYNCING), it must not be the
    // base-fee or extraData shape that rejects it — those are what this task owns.
    auto req = stubAt(300);
    req.executionPayload.parentHash = hash;
    req.executionPayload.blockNumber = 1;
    req.executionPayload.withdrawals.emplace();
    req.executionPayload.blobGasUsed = 0;
    req.executionPayload.excessBlobGas = 0;
    req.executionPayload.extraData = {0x00, 0x00, 0x00, 0x00, 0xfa, 0x00, 0x00, 0x00, 0x06};
    req.executionPayload.baseFeePerGas = expected;
    req.parentBeaconBlockRoot = bcos::h256(1);
    auto const txRoot =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(req.executionPayload));
    auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), req.executionPayload, txRoot,
        req.parentBeaconBlockRoot, bcos::engine::OpForkId::Holocene);
    req.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    // Positive: priced with the constants, the clock accepts it. A rejected clock is a
    // returned status, so any throw here comes from a later stage — this pair has no
    // delegate for the import to run against. The parent is well-formed (extraData and
    // baseFee both set), so the price path itself cannot be the thrower.
    std::string error;
    try
    {
        auto accepted = bcos::task::syncWait(
            pair.service.newPayload(req, static_cast<std::uint32_t>(ApiVersion::V3)));
        error = accepted.validationError.value_or(std::string{});
    }
    catch (std::exception const& e)
    {
        error = e.what();
    }
    BOOST_CHECK_MESSAGE(error.find("baseFeePerGas") == std::string::npos, error);
    BOOST_CHECK_MESSAGE(error.find("extraData") == std::string::npos, error);

    // Negative control: the same payload one wei off must be rejected by the price
    // check, which proves the positive case above actually reached it. The hash covers
    // baseFeePerGas, so it is recomputed — otherwise the hash check would fire first.
    req.executionPayload.baseFeePerGas = expected + 1;
    auto const txRootOff =
        EngineOpScheduler::computeTxRoot(bcos::engine::detail::rawEnvelopes(req.executionPayload));
    auto headerOff = bcos::engine::engine_common::op::rebuildOpEthHeader(
        pair.blockFactory->blockHeaderFactory(), req.executionPayload, txRootOff,
        req.parentBeaconBlockRoot, bcos::engine::OpForkId::Holocene);
    req.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*headerOff);
    auto const rejected = bcos::task::syncWait(
        pair.service.newPayload(req, static_cast<std::uint32_t>(ApiVersion::V3)));
    BOOST_REQUIRE(rejected.validationError);
    BOOST_CHECK_MESSAGE(rejected.validationError->find("baseFeePerGas") != std::string::npos,
        "got: " << *rejected.validationError);
}

// Handshake matrix from Bedrock onward: op-node picks the method from the payload
// timestamp, so every window must build with its own FCU version and answer getPayload
// with the matching one.
// clang-format off
BOOST_AUTO_TEST_CASE(FcuV1RegolithReturnsPayloadIdThenGetPayloadV2, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    HistoricalPair h;
    auto attrs = makeRegolithAttrs(1'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(h.pair.storage, hash, 0);
    registerPreHoloceneParent(h.pair.storage, *h.pair.blockFactory, 0, 0);
    auto built = bcos::task::syncWait(
        h.pair.service.updateForkchoice(fc, &attrs, static_cast<std::uint32_t>(ApiVersion::V1)));
    BOOST_REQUIRE(built.payloadId);
    // The envelope is slimmed to the method's shape: a V2 response carries neither the
    // beacon root (Cancun) nor execution requests (Prague).
    auto got = bcos::task::syncWait(
        h.pair.service.getPayload(*built.payloadId, static_cast<std::uint32_t>(ApiVersion::V2)));
    BOOST_REQUIRE(got);
    BOOST_CHECK(!got->executionRequests.has_value());
    BOOST_CHECK(!got->parentBeaconBlockRoot.has_value());
    // The response executionPayload is shaped like the Regolith block itself, not like the
    // builder's carrier: pre-Shanghai has no withdrawals list or root, pre-Cancun no blob pair.
    BOOST_CHECK(!got->executionPayload.withdrawals.has_value());
    BOOST_CHECK(!got->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK(!got->executionPayload.blobGasUsed.has_value());
    BOOST_CHECK(!got->executionPayload.excessBlobGas.has_value());
}

// clang-format off
BOOST_AUTO_TEST_CASE(FcuV2CanyonReturnsPayloadIdThenGetPayloadV3IsUnsupportedFork, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    HistoricalPair h;
    auto attrs = makeCanyonAttrs(100'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(h.pair.storage, hash, 0);
    registerPreHoloceneParent(h.pair.storage, *h.pair.blockFactory, 0, 99'000);
    auto built = bcos::task::syncWait(
        h.pair.service.updateForkchoice(fc, &attrs, static_cast<std::uint32_t>(ApiVersion::V2)));
    BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(built.payloadId);
    auto got = bcos::task::syncWait(
        h.pair.service.getPayload(*built.payloadId, static_cast<std::uint32_t>(ApiVersion::V2)));
    BOOST_REQUIRE(got);
    // Canyon is Shanghai: withdrawals are present (empty) and the root is present, but the
    // Cancun blob pair must still be absent.
    BOOST_CHECK(got->executionPayload.withdrawals.has_value());
    BOOST_CHECK(got->executionPayload.withdrawals->empty());
    BOOST_CHECK(got->executionPayload.withdrawalsRoot.has_value());
    BOOST_CHECK(!got->executionPayload.blobGasUsed.has_value());
    BOOST_CHECK(!got->executionPayload.excessBlobGas.has_value());
    // Canyon's live getPayload is V2, so V3 is an unsupported fork, not an unknown id.
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(h.pair.service.getPayload(
                              *built.payloadId, static_cast<std::uint32_t>(ApiVersion::V3))),
        UnsupportedFork, [](UnsupportedFork const&) { return true; });
}

// clang-format off
BOOST_AUTO_TEST_CASE(FcuV3EcotoneReturnsPayloadId, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    HistoricalPair h;
    auto attrs = makeEcotoneAttrs(200'000);
    auto hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState fc{hash, hash, hash};
    registerVerifiedBlock(h.pair.storage, hash, 0);
    registerPreHoloceneParent(h.pair.storage, *h.pair.blockFactory, 0, 199'000);
    auto built = bcos::task::syncWait(
        h.pair.service.updateForkchoice(fc, &attrs, static_cast<std::uint32_t>(ApiVersion::V3)));
    BOOST_REQUIRE(built.payloadId);
}

// The schedule production runs today resolves every timestamp to Isthmus+, so it must
// behave exactly as before this change: V4 only.
// clang-format off
BOOST_AUTO_TEST_CASE(IsthmusOnlyScheduleStillV4, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon") * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    OpServicePair pair;
    expectUnsupportedFork(pair.service, stubAt(1), static_cast<std::uint32_t>(ApiVersion::V3));
    BOOST_CHECK_NO_THROW(static_cast<void>(bcos::task::syncWait(
        pair.service.newPayload(stubAt(1), static_cast<std::uint32_t>(ApiVersion::V4)))));
}

BOOST_AUTO_TEST_SUITE_END()