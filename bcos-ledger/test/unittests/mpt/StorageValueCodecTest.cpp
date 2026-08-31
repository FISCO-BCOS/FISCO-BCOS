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
 * @file StorageValueCodecTest.cpp
 * @brief Round-trip and malformed-input tests for the storage-slot value codec (spec §5.3)
 */

#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/StorageValueCodec.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/test/unit_test.hpp>

#include "bcos-ledger/test/unittests/ExceptionCheck.h"

namespace bcos::ledger::mpt::test
{
using bcos::test::errinfoContains;

BOOST_AUTO_TEST_SUITE(StorageValueCodecSuite)

// encode then decode returns the original value, across the byte-length boundaries RLP treats
// differently (single small byte, one byte >= 0x80, multi-byte, full 32 bytes).
BOOST_AUTO_TEST_CASE(RoundTrip)
{
    for (auto const& value : {bcos::u256{1}, bcos::u256{0x7f}, bcos::u256{0x80}, bcos::u256{0x1234},
             bcos::u256{0xdeadbeef},
             bcos::u256{"0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20"}})
    {
        auto const encoded = encodeStorageValue(bcos::h256(value).ref());
        BOOST_REQUIRE(!encoded.empty());
        BOOST_CHECK_EQUAL(decodeStorageValue(bcos::ref(encoded)), value);
    }
}

// A zero value is not part of the storage trie: the encoder returns an empty buffer, which is
// the caller's signal to delete the slot rather than write a leaf.
BOOST_AUTO_TEST_CASE(ZeroEncodesToEmpty)
{
    BOOST_CHECK(encodeStorageValue(bcos::h256{}.ref()).empty());
}

// Malformed leaves are rejected, not silently truncated into a plausible value.
BOOST_AUTO_TEST_CASE(MalformedLeavesThrow)
{
    // Truncated: a 4-byte string header with only 2 bytes of payload.
    bcos::bytes const truncated{0x84, 0x01, 0x02};
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(truncated)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "bad RLP value"); });

    // Empty input: no RLP item at all.
    bcos::bytes const empty;
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(empty)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "bad RLP value"); });

    // Trailing bytes after a well-formed value — a corrupted leaf that would otherwise decode
    // to the first item and silently drop the rest.
    auto trailing = encodeStorageValue(bcos::h256(bcos::u256{0x1234}).ref());
    trailing.push_back(0x01);
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(trailing)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "trailing bytes after the RLP value"); });

    // A list where a string is expected.
    bcos::bytes const list{0xc2, 0x01, 0x02};
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(list)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "bad RLP value"); });
}

// Ethereum storage values are minimal big-endian byte strings of at most 32 bytes. Decoding
// straight to u256 would accept both of these: fromBigEndian shifts an over-long payload's high
// bytes out and hands back a plausible wrong value mod 2^256, and a leading zero decodes as if
// it were canonical.
BOOST_AUTO_TEST_CASE(NonCanonicalPayloadsThrow)
{
    // 33-byte payload, short-string form (0x80 + 33). Its first byte would be shifted out
    // entirely. It must be 0xa1 and not 0xb8|33: the long-string form requires a payload of at
    // least 56 bytes (RLPDecode.h:65-84), so 0xb8|33 is rejected by the header parser before the
    // length check below ever runs.
    bcos::bytes overlong{0xa1};
    overlong.push_back(0xff);
    overlong.insert(overlong.end(), 32, 0x11);
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(overlong)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "over the 32-byte max"); });

    // The long-string form itself, canonically encoded: 0xb8 + length 56 + 56 bytes.
    bcos::bytes longForm{0xb8, 56};
    longForm.insert(longForm.end(), 56, 0x11);
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(longForm)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "over the 32-byte max"); });

    // Exactly 32 bytes is the boundary and must still decode.
    bcos::bytes maxWidth{0xa0};
    maxWidth.insert(maxWidth.end(), 32, 0x11);
    BOOST_CHECK_NO_THROW(decodeStorageValue(bcos::ref(maxWidth)));

    // Leading zero: 0x82 0x00 0x01 encodes the same value as 0x01, non-minimally.
    bcos::bytes const leadingZero{0x82, 0x00, 0x01};
    BOOST_CHECK_EXCEPTION(decodeStorageValue(bcos::ref(leadingZero)), MPTDecodeError,
        [](auto const& e) { return errinfoContains(e, "leading zero byte"); });

    // The encoder never produces either form, so nothing valid is rejected.
    BOOST_CHECK_EQUAL(decodeStorageValue(bcos::ref(encodeStorageValue(
                          bcos::h256(bcos::u256{"0x00ff00000000000000000000000000000000000000000000"
                                                "0000000000000001"})
                              .ref()))),
        bcos::u256{"0x00ff000000000000000000000000000000000000000000000000000000000001"});
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::ledger::mpt::test
