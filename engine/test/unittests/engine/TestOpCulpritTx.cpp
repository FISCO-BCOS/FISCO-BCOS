/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file TestOpCulpritTx.cpp
 * @brief Pin the [tx=0x<64 hex>] tag that crosses the executeBlock Error boundary.
 */

#include "bcos-framework/engine/OpCulpritTx.h"
#include <boost/test/unit_test.hpp>

using bcos::engine::appendOpCulpritTag;
using bcos::engine::formatOpCulpritTag;
using bcos::engine::parseOpCulpritHash;

BOOST_AUTO_TEST_SUITE(TestOpCulpritTx)

BOOST_AUTO_TEST_CASE(Roundtrip)
{
    bcos::h256 const hash(std::string(64, 'a'), bcos::h256::FromHex);
    auto const tagged = appendOpCulpritTag("OpScheduler: normal tx validation failed", hash);
    BOOST_CHECK_EQUAL(
        tagged, "OpScheduler: normal tx validation failed [tx=0x" + std::string(64, 'a') + "]");
    auto parsed = parseOpCulpritHash(tagged);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(parsed->hex(), hash.hex());
}

BOOST_AUTO_TEST_CASE(SurvivesDiagnosticPrefix)
{
    bcos::h256 const hash(std::string(64, 'b'), bcos::h256::FromHex);
    auto const inner = appendOpCulpritTag("normal tx validation failed: nonce too low", hash);
    auto const wrapped = "Execute block failed! Dynamic exception type: ... what(): " + inner;
    auto parsed = parseOpCulpritHash(wrapped);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(parsed->hex(), hash.hex());
}

BOOST_AUTO_TEST_CASE(MissingTagIsNullopt)
{
    BOOST_CHECK(!parseOpCulpritHash("OpScheduler: normal tx validation failed: nonce too low"));
    BOOST_CHECK(!parseOpCulpritHash(""));
}

BOOST_AUTO_TEST_CASE(TruncatedHexIsNullopt)
{
    BOOST_CHECK(!parseOpCulpritHash("failed [tx=0xabc]"));
    BOOST_CHECK(!parseOpCulpritHash(std::string("failed [tx=0x") + std::string(63, 'c') + "]"));
}

BOOST_AUTO_TEST_CASE(InvalidHexIsNullopt)
{
    BOOST_CHECK(!parseOpCulpritHash(std::string("failed [tx=0x") + std::string(64, 'z') + "]"));
}

BOOST_AUTO_TEST_CASE(LastTagWins)
{
    bcos::h256 const first(std::string(64, '1'), bcos::h256::FromHex);
    bcos::h256 const second(std::string(64, '2'), bcos::h256::FromHex);
    auto const tagged = appendOpCulpritTag(appendOpCulpritTag("failed", first), second);
    auto parsed = parseOpCulpritHash(tagged);
    BOOST_REQUIRE(parsed.has_value());
    BOOST_CHECK_EQUAL(parsed->hex(), second.hex());
}

BOOST_AUTO_TEST_CASE(FormatTagShape)
{
    bcos::h256 const hash(std::string(64, 'd'), bcos::h256::FromHex);
    auto const tag = formatOpCulpritTag(hash);
    BOOST_CHECK_EQUAL(tag.size(), std::string_view("[tx=0x]").size() + 64);
    BOOST_CHECK(tag.starts_with("[tx=0x"));
    BOOST_CHECK(tag.ends_with("]"));
}

BOOST_AUTO_TEST_SUITE_END()
