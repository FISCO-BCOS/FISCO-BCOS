/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * @brief Regression test for issue #5353: the RLP integer decoder must reject non-canonical
 *        integer payloads (leading zero bytes, and the single byte 0x00 for zero), matching
 *        go-ethereum's ErrCanonInt. Length prefixes were already checked; values were not.
 */
#include "bcos-codec/rlp/RLPDecode.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(RLPNonCanonicalIntegerTest)

namespace
{
template <typename T>
int32_t decodeError(std::string_view hex)
{
    bcos::bytes bytes = fromHex(hex);
    auto ref = bcos::ref(bytes);
    T value{};
    auto error = bcos::codec::rlp::decode(ref, value);
    return error ? error->errorCode() : -1;
}
template <typename T>
T decodeOk(std::string_view hex)
{
    bcos::bytes bytes = fromHex(hex);
    auto ref = bcos::ref(bytes);
    T value{};
    auto error = bcos::codec::rlp::decode(ref, value);
    BOOST_REQUIRE(!error);
    return value;
}
}  // namespace

BOOST_AUTO_TEST_CASE(leadingZeroPayloadRejected)
{
    // 0x820001: two-byte string 00 01 == value 1 written non-minimally
    BOOST_CHECK_EQUAL(decodeError<uint64_t>("820001"), NonCanonicalSize);
    BOOST_CHECK_EQUAL(decodeError<u256>("820001"), NonCanonicalSize);
    // three-byte with leading zero
    BOOST_CHECK_EQUAL(decodeError<uint64_t>("83000102"), NonCanonicalSize);
    // 0x00 alone: zero must be the empty string 0x80, not a single zero byte
    BOOST_CHECK_EQUAL(decodeError<uint64_t>("00"), NonCanonicalSize);
    BOOST_CHECK_EQUAL(decodeError<u256>("00"), NonCanonicalSize);
}

BOOST_AUTO_TEST_CASE(canonicalPayloadsStillDecode)
{
    BOOST_CHECK_EQUAL(decodeOk<uint64_t>("80"), 0U);
    BOOST_CHECK_EQUAL(decodeOk<uint64_t>("01"), 1U);
    BOOST_CHECK_EQUAL(decodeOk<uint64_t>("8180"), 0x80U);
    BOOST_CHECK_EQUAL(decodeOk<uint64_t>("820100"), 0x100U);  // trailing zero is fine
    BOOST_CHECK_EQUAL(decodeOk<u256>("820100"), u256(0x100));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
