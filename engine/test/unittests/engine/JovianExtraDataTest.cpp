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
PayloadAttributes makeAttributes(
    std::optional<bytes> eip1559Params, std::optional<std::uint64_t> minBaseFee)
{
    PayloadAttributes attributes;
    attributes.timestamp = 1;
    attributes.eip1559Params = std::move(eip1559Params);
    attributes.minBaseFee = minBaseFee;
    return attributes;
}

void checkExtraData(const bytes& actual, std::string_view expectedHex)
{
    BOOST_CHECK_EQUAL(toHexStringWithPrefix(actual), expectedHex);
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
    auto error = engine::detail::validatePayloadAttributes(makeAttributes(std::nullopt, 0), 1);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_NE(error->find("eip1559Params"), std::string::npos);
}

// ValidateHolocene1559Params (eip1559.go:89-100): a zero denominator with a non-zero
// elasticity (or vice versa) is invalid attribute input.
BOOST_AUTO_TEST_CASE(mixed_zero_eip1559_params_are_rejected)
{
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x0000000000000006"), 0), 1)
            .has_value());
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
        makeAttributes(fromHexWithPrefix("0x000000fa00000000"), 0), 1)
            .has_value());
    // Both zero and both non-zero stay valid.
    BOOST_CHECK(
        !engine::detail::validatePayloadAttributes(makeAttributes(bytes(8, 0), 0), 1).has_value());
    BOOST_CHECK(engine::detail::validatePayloadAttributes(
                    makeAttributes(fromHexWithPrefix("0x000000fa00000006"), 0), 1)
                    .has_value() == false);
}

BOOST_AUTO_TEST_SUITE_END()
