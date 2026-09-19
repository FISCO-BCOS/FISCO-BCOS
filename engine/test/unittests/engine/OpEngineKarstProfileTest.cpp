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
 * @file OpEngineKarstProfileTest.cpp
 * @brief getPayload profile is keyed on payload/attrs timestamp (A9/A13).
 *
 * Karst (attrs.timestamp = 1000s as 1000_000 ms) must reject getPayload V4 with
 * UnsupportedFork (-38005) and accept V5. Jovian (999_000 ms) is the inverse.
 */
#include "support/OpEngineKarstTestHarness.h"

#include <boost/exception/get_error_info.hpp>
#include <boost/test/tree/decorator.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <string_view>

using namespace op_engine_parity_test;

namespace
{
constexpr std::string_view kGetPayloadProfileMismatch =
    "getPayload version does not match the OP Engine API profile at payload timestamp";

bool isGetPayloadProfileMismatch(bcos::engine::UnsupportedFork const& e)
{
    auto const* comment = boost::get_error_info<bcos::errinfo_comment>(e);
    return comment != nullptr && *comment == kGetPayloadProfileMismatch;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineKarstProfileSuite)

// clang-format off
BOOST_AUTO_TEST_CASE(KarstPayloadTimestampRejectsGetPayloadV4, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    BOOST_CHECK_EQUAL(
        bcos::engine::unixSecondsFromInternalMillis(c_karstPayloadTimestampMs), 1000U);
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4)),
        bcos::engine::UnsupportedFork, isGetPayloadProfileMismatch);
}

// clang-format off
BOOST_AUTO_TEST_CASE(KarstPayloadTimestampAcceptsGetPayloadV5, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    auto result = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5));
    BOOST_REQUIRE(result);
    BOOST_REQUIRE(result->executionRequests.has_value());
    BOOST_CHECK(result->executionRequests->empty());
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianPayloadTimestampRejectsGetPayloadV5, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_jovianPayloadTimestampMs, /*parent*/ 998'000);
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5)),
        bcos::engine::UnsupportedFork, isGetPayloadProfileMismatch);
}

// clang-format off
BOOST_AUTO_TEST_CASE(JovianPayloadTimestampAcceptsGetPayloadV4, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // #5550 regression: Jovian payload timestamp still serves getPayload V4.
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_jovianPayloadTimestampMs, /*parent*/ 998'000);
    auto result = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4));
    BOOST_REQUIRE(result);
}

// clang-format off
BOOST_AUTO_TEST_CASE(ActivationBlockFcuUsesAttrTimestampNotHead, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    // Head is still Jovian (999_000 ms); attrs.timestamp is Karst (1000_000 ms).
    // Profile must follow the payload timestamp, never the head.
    KarstProfilePair fixture;
    auto const payloadId =
        buildPayloadAt(fixture.pair, c_karstPayloadTimestampMs, c_jovianPayloadTimestampMs);
    BOOST_CHECK_EXCEPTION(bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 4)),
        bcos::engine::UnsupportedFork, isGetPayloadProfileMismatch);
    auto v5 = bcos::task::syncWait(fixture.pair.service.getPayload(payloadId, 5));
    BOOST_REQUIRE(v5);
}

// clang-format off
BOOST_AUTO_TEST_CASE(CapabilitiesAlwaysAdvertiseV4AndV5, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    KarstProfilePair fixture;
    auto caps = bcos::task::syncWait(fixture.pair.service.exchangeCapabilities({}));
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV4") != caps.end());
    BOOST_CHECK(std::find(caps.begin(), caps.end(), "engine_getPayloadV5") != caps.end());
}

static void seedKarstActivationHead(OpServicePair& pair)
{
    auto const hash = fixtureHeadHash();
    registerVerifiedBlock(pair.storage, hash, 0);
    registerParentHeader(
        pair.storage, *pair.blockFactory, 0, static_cast<int64_t>(c_jovianPayloadTimestampMs));
}

// F17: attrs that already contain a user tx on a Jovian/Karst activation timestamp
// must FCU-INVALID before execute. Never OpExecutionInternalError (-32603).
// clang-format off
BOOST_AUTO_TEST_CASE(ActivationFcuInvalidatesNonDepositAttrs, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(
        /*allowSynthesizedL1Attributes=*/true, delegate, nullptr, makeKarstProfileSchedule());
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    auto attrs = makeOpPayloadAttributesAt(c_karstPayloadTimestampMs);
    attrs.noTxPool = false;
    attrs.transactions = std::vector<std::string>{decoded.rawHex};
    seedKarstActivationHead(pair);
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};

    bcos::engine::ForkchoiceUpdatedResult result;
    BOOST_REQUIRE_NO_THROW(
        result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3)));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_CHECK(!result.payloadId.has_value());
    BOOST_REQUIRE(result.payloadStatus.validationError.has_value());
    BOOST_CHECK(result.payloadStatus.validationError->find("activation") != std::string::npos);
    BOOST_CHECK_EQUAL(delegate->executeCalls, 0);
}

// F17: activation + noTxPool=false must not seal mempool user txs onto a
// deposits-only attrs list (empty attrs → synthesized L1 deposit).
// clang-format off
BOOST_AUTO_TEST_CASE(ActivationFcuSkipsMempoolUserTxs, * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto delegate = std::make_shared<FabricatedRootsStub>();
    delegate->failFirst = false;
    OpServicePair pair(
        /*allowSynthesizedL1Attributes=*/true, delegate, nullptr, makeKarstProfileSchedule());
    delegate->headerFactory = pair.blockFactory->blockHeaderFactory();

    auto decoded = makeDecodableWeb3Tx(1);
    pair.memPool.pool.push_back(decoded.tx);

    auto attrs = makeOpPayloadAttributesAt(c_karstPayloadTimestampMs);
    attrs.noTxPool = false;
    attrs.transactions.reset();
    seedKarstActivationHead(pair);
    auto const hash = fixtureHeadHash();
    bcos::engine::ForkchoiceState forkchoice{hash, hash, hash};

    auto result = bcos::task::syncWait(pair.service.updateForkchoice(forkchoice, &attrs, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(result.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(result.payloadId.has_value());
    BOOST_CHECK_GE(delegate->executeCalls, 1);
    BOOST_CHECK(pair.memPool.removed.empty());

    auto payload = bcos::task::syncWait(pair.service.getPayload(*result.payloadId, 5));
    BOOST_REQUIRE(payload);
    BOOST_REQUIRE_EQUAL(payload->executionPayload.transactions.size(), 1);
    BOOST_REQUIRE(!payload->executionPayload.transactions[0].raw.empty());
    BOOST_CHECK_EQUAL(payload->executionPayload.transactions[0].raw[0], 0x7e);
}

BOOST_AUTO_TEST_SUITE_END()
