/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

// OP-Stack extraData encoding (engine::detail::encodeOptimismExtraData) and the attribute
// consistency checks feeding it. Oracle: op-core/eip1559/eip1559.go of
// optimism v1.19.3 — EncodeJovianExtraData / EncodeHoloceneExtraData for the byte
// layout, ValidateJovianExtraData for the version byte (0x01), and
// engine_consolidate.go checkExtraDataParamsMatch for the 0,0 -> Canyon-constants
// translation.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;

namespace
{
/// V3-shaped attributes: eip1559Params and minBaseFee only ever arrive on a
/// forkchoiceUpdatedV3 (op-node/rollup/types.go ForkchoiceUpdatedVersion tops out at
/// FCUV3, and both fields postdate Ecotone), so the validation cases below run at
/// version 3. That means withdrawals and parentBeaconBlockRoot must be present too,
/// or the sibling V3 gates fire before the eip1559 ones and the cases would assert
/// against the wrong error.
PayloadAttributes makeAttributes(
    std::optional<bytes> eip1559Params, std::optional<std::uint64_t> minBaseFee)
{
    PayloadAttributes attributes;
    attributes.timestamp = 1;
    attributes.withdrawals = std::vector<WithdrawalV1>{};
    attributes.parentBeaconBlockRoot =
        h256("2222222222222222222222222222222222222222222222222222222222222222");
    attributes.eip1559Params = std::move(eip1559Params);
    attributes.minBaseFee = minBaseFee;
    return attributes;
}

void checkExtraData(const bytes& actual, std::string_view expectedHex)
{
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(actual), expectedHex);
}

/// A payload that passes every non-extraData gate of validateExecutionPayload at
/// version 3, so the cases below isolate the extraData shape check.
ExecutionPayload makeExecutionPayloadV3(bytes extraData)
{
    ExecutionPayload payload;
    payload.withdrawals = std::vector<WithdrawalV1>{};
    payload.blobGasUsed = 0;
    payload.excessBlobGas = 0;
    payload.extraData = std::move(extraData);
    return payload;
}

ExecutionPayload makePayloadWithTransactions(bytes extraData, std::vector<bytes> rawTransactions)
{
    auto payload = makeExecutionPayloadV3(std::move(extraData));
    for (auto& raw : rawTransactions)
    {
        EngineTransaction transaction;
        transaction.raw = std::move(raw);
        payload.transactions.push_back(std::move(transaction));
    }
    return payload;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(JovianExtraDataTest)

// The round-5 breakpoint vector: op-node sends eip1559Params = 0x0000000000000000 and
// minBaseFee = 0 while the SystemConfig has not set the params, and expects the EL to
// translate 0,0 to the Canyon constants (250, 6). Expected bytes: version 0x01
// (op-core requires 0x01 for Jovian, NOT the 0x00 our genesis fixture carries —
// the genesis header is never re-validated by op-node, blocks >= 1 are), u32 BE
// denominator 250 = 0x000000fa, u32 BE elasticity 6, u64 BE minBaseFee 0.
BOOST_AUTO_TEST_CASE(jovian_zero_params_translate_to_canyon_constants)
{
    auto attributes = makeAttributes(bytes(8, 0), 0);
    auto extraData = engine::detail::encodeOptimismExtraData(attributes);
    BOOST_REQUIRE_EQUAL(extraData.size(), 17);
    checkExtraData(extraData, "0x01000000fa000000060000000000000000");
}

// Non-zero attribute params pass through untranslated, and minBaseFee lands as a big-
// endian u64 in bytes [9, 17) (EncodeJovianExtraData, eip1559.go:152-162).
BOOST_AUTO_TEST_CASE(jovian_explicit_params_and_min_base_fee_encode_big_endian)
{
    auto attributes =
        makeAttributes(fromHexWithPrefix("0x000000fa00000006"), 0x0102030405060708ULL);
    auto extraData = engine::detail::encodeOptimismExtraData(attributes);
    BOOST_REQUIRE_EQUAL(extraData.size(), 17);
    checkExtraData(extraData, "0x01000000fa000000060102030405060708");

    auto second = makeAttributes(fromHexWithPrefix("0x0000005000000004"), 7);
    checkExtraData(
        engine::detail::encodeOptimismExtraData(second), "0x0100000050000000040000000000000007");
}

// Holocene shape: attributes without minBaseFee produce the 9-byte, version-0x00 form
// (EncodeHoloceneExtraData, eip1559.go:74-83); the 0,0 translation applies there too.
BOOST_AUTO_TEST_CASE(holocene_without_min_base_fee_encodes_nine_bytes)
{
    checkExtraData(engine::detail::encodeOptimismExtraData(
                       makeAttributes(fromHexWithPrefix("0x000000fa00000006"), std::nullopt)),
        "0x00000000fa00000006");
    checkExtraData(
        engine::detail::encodeOptimismExtraData(makeAttributes(bytes(8, 0), std::nullopt)),
        "0x00000000fa00000006");
}

// Pre-Holocene: no eip1559Params in the attributes -> extraData must stay empty
// (op-core/eip1559/eip1559.go:27-28).
BOOST_AUTO_TEST_CASE(missing_eip1559_params_keeps_extra_data_empty)
{
    BOOST_CHECK(engine::detail::encodeOptimismExtraData(makeAttributes(std::nullopt, std::nullopt))
            .empty());
}

// minBaseFee without eip1559Params leaves the params half of the 17-byte form
// undefined; the attributes are rejected instead of guessed at.
BOOST_AUTO_TEST_CASE(min_base_fee_without_params_is_rejected)
{
    auto error = engine::detail::validatePayloadAttributes(makeAttributes(std::nullopt, 0), 3);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_NE(error->find("eip1559Params"), std::string::npos);
}

// The 8-byte params precondition is enforced by the validation gate itself, not only
// by the RPC parse layer, so in-process PayloadAttributes producers are covered too.
BOOST_AUTO_TEST_CASE(wrong_length_eip1559_params_are_rejected)
{
    BOOST_CHECK(
        engine::detail::validatePayloadAttributes(makeAttributes(bytes(7, 0), 0), 3).has_value());
    BOOST_CHECK(
        engine::detail::validatePayloadAttributes(makeAttributes(bytes(9, 0), 0), 3).has_value());
}

// ValidateHolocene1559Params (eip1559.go:89-100): a zero denominator with a non-zero
// elasticity (or vice versa) is invalid attribute input.
BOOST_AUTO_TEST_CASE(mixed_zero_eip1559_params_are_rejected)
{
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x0000000000000006"), 0), 3)
            .has_value());
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x000000fa00000000"), 0), 3)
            .has_value());
    // Both zero and both non-zero stay valid.
    BOOST_CHECK(
        !engine::detail::validatePayloadAttributes(makeAttributes(bytes(8, 0), 0), 3).has_value());
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
                    makeAttributes(fromHexWithPrefix("0x000000fa00000006"), 0), 3)
                    .has_value() == false);
}

// Both fields reach the EL only on a forkchoiceUpdatedV3: op-node's
// Config::ForkchoiceUpdatedVersion (op-node/rollup/types.go:727-745, v1.19.3) answers
// FCUV3 from Ecotone onwards and there is no FCUV4 constant, while Holocene (which
// introduces eip1559Params) and Jovian (minBaseFee) both activate after Ecotone. A
// V1/V2 forkchoiceUpdated carrying them must be rejected rather than stamp
// Holocene/Jovian extraData on a pre-Holocene build.
BOOST_AUTO_TEST_CASE(eip1559_fields_are_rejected_below_version_three)
{
    // The probes must otherwise be valid for the version under test, or a sibling gate
    // (withdrawals on V1, parentBeaconBlockRoot on V1/V2) reports first and the case
    // would pass without exercising the new gate at all.
    auto attributesForVersion = [](std::uint32_t version) {
        auto attributes = makeAttributes(std::nullopt, std::nullopt);
        if (version == 1)
        {
            attributes.withdrawals = std::nullopt;
        }
        if (version <= 2)
        {
            attributes.parentBeaconBlockRoot = std::nullopt;
        }
        return attributes;
    };

    for (std::uint32_t version : {1U, 2U})
    {
        // Baseline: without the two fields these attributes are accepted at this
        // version, so any error below is attributable to the new gate.
        BOOST_REQUIRE(
            !engine::detail::validatePayloadAttributes(attributesForVersion(version), version)
                .has_value());

        auto holocene = attributesForVersion(version);
        holocene.eip1559Params = fromHexWithPrefix("0x000000fa00000006");
        auto holoceneError = engine::detail::validatePayloadAttributes(holocene, version);
        BOOST_REQUIRE(holoceneError.has_value());
        BOOST_CHECK_NE(holoceneError->find("eip1559Params"), std::string::npos);

        auto jovian = holocene;
        jovian.minBaseFee = 7;
        BOOST_CHECK(engine::detail::validatePayloadAttributes(jovian, version).has_value());

        // minBaseFee is version-gated in its own right, before the pairing rule.
        auto minBaseFeeOnly = attributesForVersion(version);
        minBaseFeeOnly.minBaseFee = 7;
        auto minBaseFeeError = engine::detail::validatePayloadAttributes(minBaseFeeOnly, version);
        BOOST_REQUIRE(minBaseFeeError.has_value());
        BOOST_CHECK_NE(minBaseFeeError->find("minBaseFee"), std::string::npos);
    }

    // V3 is the version op-node actually uses, so both forms stay valid there.
    BOOST_CHECK(!engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x000000fa00000006"), std::nullopt), 3)
            .has_value());
    BOOST_CHECK(!engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x000000fa00000006"), 7), 3)
            .has_value());
}

// encodeOptimismExtraData is an exported detail:: entry point reachable from
// in-process PayloadAttributes producers that never went through
// validatePayloadAttributes. Its decode does span arithmetic whose precondition is
// 8 bytes ([span.sub] makes a shorter span undefined behaviour, not an exception),
// so the entry point enforces the length itself instead of trusting the caller.
BOOST_AUTO_TEST_CASE(encode_rejects_params_shorter_than_eight_bytes)
{
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{7}, std::size_t{9}})
    {
        BOOST_CHECK_THROW(
            engine::detail::encodeOptimismExtraData(makeAttributes(bytes(size, 0), 0)),
            std::invalid_argument);
    }
}

// Commit-side shape check. op-geth validates header extraData on every block it
// imports (consensus/beacon/consensus.go:240-243 -> eip1559.ValidateOptimismExtraData);
// this service has no fork schedule, so it accepts the three legal shapes and rejects
// everything else. Since extraData now feeds the block hash, an unchecked extraData is
// an unchecked block-hash input.
BOOST_AUTO_TEST_CASE(execution_payload_extra_data_shape_is_validated)
{
    auto accepted = [](bytes extraData) {
        return !engine::detail::validateExecutionPayload(
            makeExecutionPayloadV3(std::move(extraData)), 3)
                    .has_value();
    };

    // Empty (pre-Holocene), 9-byte Holocene and 17-byte Jovian forms.
    BOOST_CHECK(accepted({}));
    BOOST_CHECK(accepted(fromHexWithPrefix("0x00000000fa00000006")));
    BOOST_CHECK(accepted(fromHexWithPrefix("0x01000000fa000000060000000000000000")));

    // Wrong lengths.
    BOOST_CHECK(!accepted(bytes(8, 0)));
    BOOST_CHECK(!accepted(bytes(16, 0)));
    BOOST_CHECK(!accepted(bytes(32, 0)));
    // Wrong version byte for the length: Jovian must be 0x01, Holocene 0x00.
    BOOST_CHECK(!accepted(fromHexWithPrefix("0x00000000fa000000060000000000000000")));
    BOOST_CHECK(!accepted(fromHexWithPrefix("0x01000000fa00000006")));
    // A header, unlike attributes, may not encode a zero denominator or elasticity
    // (validateHoloceneExtraDataPart, op-core/eip1559/eip1559.go:105-113).
    BOOST_CHECK(!accepted(fromHexWithPrefix("0x000000000000000006")));
    BOOST_CHECK(!accepted(fromHexWithPrefix("0x000000000000000000")));
    BOOST_CHECK(!accepted(fromHexWithPrefix("0x00000000fa00000000")));
}

// A CL returning a payload under a blockHash this node minted must return the same
// extraData it was handed, since that is a block-hash input from this change onwards.
BOOST_AUTO_TEST_CASE(compare_with_built_payload_catches_altered_extra_data)
{
    auto built = makePayloadWithTransactions(
        fromHexWithPrefix("0x01000000fa000000060000000000000000"), {{0x7e, 0x01}, {0x02, 0x03}});

    BOOST_CHECK(!engine::detail::compareWithBuiltPayload(built, built).has_value());

    auto alteredExtraData = built;
    alteredExtraData.extraData = fromHexWithPrefix("0x01000000fa000000060000000000000001");
    auto extraDataError = engine::detail::compareWithBuiltPayload(alteredExtraData, built);
    BOOST_REQUIRE(extraDataError.has_value());
    BOOST_CHECK_NE(extraDataError->find("extraData"), std::string::npos);

    auto strippedExtraData = built;
    strippedExtraData.extraData.clear();
    BOOST_CHECK(engine::detail::compareWithBuiltPayload(strippedExtraData, built).has_value());

    // Documented non-goals of this comparison, asserted so the boundary is visible.
    // withdrawalsRoot is dropped by the V3 wire dialect on the way back
    // (get_payload_v5_rejects_a_v3_committed_entry_without_withdrawals_root in
    // EngineServiceTest relies on that staying VALID), and the transaction list is
    // left to #5468, which is what makes payload bodies verifiable at all.
    auto withoutWithdrawalsRoot = built;
    withoutWithdrawalsRoot.withdrawalsRoot = std::nullopt;
    BOOST_CHECK(
        !engine::detail::compareWithBuiltPayload(withoutWithdrawalsRoot, built).has_value());

    auto alteredTransaction = built;
    alteredTransaction.transactions[1].raw = bytes{0x02, 0x04};
    BOOST_CHECK(!engine::detail::compareWithBuiltPayload(alteredTransaction, built).has_value());
}

BOOST_AUTO_TEST_SUITE_END()
