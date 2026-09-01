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
 * @file TestPayloadId.cpp
 * @brief derivePayloadId test vectors, byte-aligned with op-geth
 *        BuildPayloadArgs.Id() (miner/payload_building.go).
 *
 * All expected values below were verified with three independent
 * implementations of the op-geth algorithm: Python hashlib.sha256, a
 * standalone Go program, and the openssl CLI. The exact byte stream fed to
 * SHA-256 is quoted in each case.
 */

#include "bcos-framework/engine/PayloadId.h"
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos::engine;
using bcos::crypto::HashType;

namespace bcos::test
{
namespace
{
using h256 = bcos::h256;

/// Common inputs for the fixed vectors.
struct FixtureInputs
{
    h256 parentHash = h256(std::string(64, '1'));                      // 0x11 x 32
    h256 prevRandao = h256(std::string(64, '2'));                      // 0x22 x 32
    bcos::Address feeRecipient = bcos::Address(std::string(40, '3'));  // 0x33 x 20
    // Internal ms; 1_700_000_000_000 ms = 1700000000 s = 0x6553f100.
    uint64_t timestampMs = 1'700'000'000'000;
};

PayloadAttributes makeAttrs(FixtureInputs const& f)
{
    PayloadAttributes attrs;
    attrs.timestamp = f.timestampMs;
    attrs.prevRandao = f.prevRandao;
    attrs.suggestedFeeRecipient = f.feeRecipient;
    return attrs;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(TestPayloadId)

/// (a) minimal: no withdrawals, no optionals, V1.
/// Byte stream: 11x32 || 000000006553f100 || 22x32 || 33x20 || c0
BOOST_AUTO_TEST_CASE(MinimalV1)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x01), "0x010f846a7ea7b1aa");
}

/// (f1) version byte difference: same as (a) but V3.
BOOST_AUTO_TEST_CASE(VersionByteOverwritesDigestByte0)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    // Digest for (a) starts 0x86...; the ID's first byte is the version (0x01/0x03),
    // the remaining 7 bytes are digest bytes 1..7 (0x0f846a7ea7b1aa).
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x01), "0x010f846a7ea7b1aa");
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x03), "0x030f846a7ea7b1aa");
    // Suffix (bytes 1..7) identical across versions.
    auto v1 = derivePayloadId(attrs, f.parentHash, {}, 0x01);
    auto v3 = derivePayloadId(attrs, f.parentHash, {}, 0x03);
    BOOST_CHECK_EQUAL(v1.substr(4), v3.substr(4));
    // Prefix is exactly the version byte.
    BOOST_CHECK_EQUAL(v1.substr(0, 4), "0x01");
    BOOST_CHECK_EQUAL(v3.substr(0, 4), "0x03");
}

/// (b) one zero-valued withdrawal, V2.
/// Withdrawal RLP: d8 80 80 94 00x20 80 ; list of 1: d9 d8 80 80 94 00x20 80
/// Byte stream: 11x32 || 000000006553f100 || 22x32 || 33x20 ||
///              d9 d8 80 80 94 00x20 80
BOOST_AUTO_TEST_CASE(OneZeroWithdrawalV2)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    WithdrawalV1 w;
    w.index = 0;
    w.validatorIndex = 0;
    w.amount = 0;
    w.address = bcos::Address{};
    attrs.withdrawals = std::vector<WithdrawalV1>{w};
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x02), "0x02b67ed1c721e284");
}

/// (c) beacon root only, V2.
/// Byte stream: 11x32 || 000000006553f100 || 22x32 || 33x20 || c0 || 44x32
BOOST_AUTO_TEST_CASE(BeaconRootOnlyV2)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    attrs.parentBeaconBlockRoot = h256(std::string(64, '4'));  // 0x44 x 32
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x02), "0x02311bf76bd95ce9");
}

/// (d) two txs, noTxPool=false, V3.
/// Byte stream: 11x32 || 000000006553f100 || 22x32 || 33x20 || c0 ||
///              00 0000000000000002 55x32 66x32
BOOST_AUTO_TEST_CASE(TwoTxsNoTxPoolFalseV3)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    attrs.withdrawals = std::vector<WithdrawalV1>{};  // empty list -> c0
    attrs.noTxPool = false;
    std::array<h256, 2> txHashes = {
        h256(std::string(64, '5')),
        h256(std::string(64, '6')),
    };
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, txHashes, 0x03), "0x035143a4096b6671");
}

/// (e) all optional fields + V4.
/// Withdrawal: index=1, validatorIndex=2, addr 0x77x20, amount 10^18
///   item: 01 02 94 77x20 88 0de0b6b3a7640000  (e0 header)
///   list: e1 e0 01 02 94 77x20 88 0de0b6b3a7640000
/// Byte stream tail: e1 e0 01 02 94 77x20 88 0de0b6b3a7640000 ||
///   44x32 (beaconRoot) || 01 (noTxPool) || 0000000000000001 (txCount) ||
///   55x32 (tx) || 0000000001c9c380 (gasLimit 30000000) ||
///   0a0b0c0d0e0f1011 (eip1559Params) || 00000000000f4240 (minBaseFee 1000000)
BOOST_AUTO_TEST_CASE(AllOptionalsV4)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    WithdrawalV1 w;
    w.index = 1;
    w.validatorIndex = 2;
    w.amount = 1'000'000'000'000'000'000;             // 10^18
    w.address = bcos::Address(std::string(40, '7'));  // 0x77 x 20
    attrs.withdrawals = std::vector<WithdrawalV1>{w};
    attrs.parentBeaconBlockRoot = h256(std::string(64, '4'));  // 0x44 x 32
    attrs.noTxPool = true;
    std::array<h256, 1> txHashes = {h256(std::string(64, '5'))};
    attrs.gasLimit = 30'000'000;
    attrs.eip1559Params = bcos::bytes{0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11};
    attrs.minBaseFee = 1'000'000;
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, txHashes, 0x04), "0x040ae1596f1e71db");
}

/// nullopt withdrawals ≡ empty list: identical IDs.
BOOST_AUTO_TEST_CASE(NulloptWithdrawalsEqualsEmptyList)
{
    FixtureInputs f;
    auto a1 = makeAttrs(f);  // withdrawals nullopt
    auto a2 = makeAttrs(f);
    a2.withdrawals = std::vector<WithdrawalV1>{};
    BOOST_CHECK_EQUAL(
        derivePayloadId(a1, f.parentHash, {}, 0x01), derivePayloadId(a2, f.parentHash, {}, 0x01));
}

/// noTxPool=false with 0 txs ≡ absent tx block: identical IDs.
BOOST_AUTO_TEST_CASE(NoTxPoolFalseWithZeroTxsEqualsAbsent)
{
    FixtureInputs f;
    auto a1 = makeAttrs(f);  // tx block absent
    auto a2 = makeAttrs(f);
    a2.noTxPool = false;
    BOOST_CHECK_EQUAL(
        derivePayloadId(a1, f.parentHash, {}, 0x01), derivePayloadId(a2, f.parentHash, {}, 0x01));
}

/// Determinism and non-collision.
BOOST_AUTO_TEST_CASE(DeterminismAndNonCollision)
{
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    BOOST_CHECK_EQUAL(derivePayloadId(attrs, f.parentHash, {}, 0x01),
        derivePayloadId(attrs, f.parentHash, {}, 0x01));  // determinism

    // Timestamp +1s -> different ID.
    auto attrs2 = attrs;
    attrs2.timestamp += 1000;
    BOOST_CHECK_NE(derivePayloadId(attrs, f.parentHash, {}, 0x01),
        derivePayloadId(attrs2, f.parentHash, {}, 0x01));

    // feeRecipient changed -> different ID.
    auto attrs3 = attrs;
    attrs3.suggestedFeeRecipient = bcos::Address(std::string(40, '9'));
    BOOST_CHECK_NE(derivePayloadId(attrs, f.parentHash, {}, 0x01),
        derivePayloadId(attrs3, f.parentHash, {}, 0x01));

    // noTxPool=true with 0 txs vs. absent tx block -> different ID.
    auto attrs4 = attrs;
    attrs4.noTxPool = true;
    BOOST_CHECK_NE(derivePayloadId(attrs, f.parentHash, {}, 0x01),
        derivePayloadId(attrs4, f.parentHash, {}, 0x01));
}

/// Timestamp floor semantics: non-multiple-of-1000 ms truncates to seconds.
BOOST_AUTO_TEST_CASE(TimestampFloorSemantics)
{
    FixtureInputs f;
    auto a1 = makeAttrs(f);  // 1_700_000_000_000 ms -> 1700000000 s
    auto a2 = makeAttrs(f);
    a2.timestamp = 1'700'000'000'999;  // -> 1700000000 s (floor)
    BOOST_CHECK_EQUAL(
        derivePayloadId(a1, f.parentHash, {}, 0x01), derivePayloadId(a2, f.parentHash, {}, 0x01));

    auto a3 = makeAttrs(f);
    a3.timestamp = 999;  // -> 0 s
    auto a4 = makeAttrs(f);
    a4.timestamp = 0;
    BOOST_CHECK_EQUAL(
        derivePayloadId(a3, f.parentHash, {}, 0x01), derivePayloadId(a4, f.parentHash, {}, 0x01));
}

/// u256 RLP edge: >64-bit values encode as minimal BE without truncation.
BOOST_AUTO_TEST_CASE(U256RlpEdgeCases)
{
    // 2^70 index -> RLP int of 9 bytes (0x89 + 9 bytes).
    FixtureInputs f;
    auto attrs = makeAttrs(f);
    WithdrawalV1 w;
    w.index = bcos::u256{1} << 70;
    w.validatorIndex = 0;
    w.amount = 0;
    w.address = bcos::Address{};
    attrs.withdrawals = std::vector<WithdrawalV1>{w};
    // Pin the full ID, not just inequality: 0x02fcd01f27c51b12 is the measured reference
    // vector over the >64-bit withdrawal index (the 9-byte RLP int hashes differently from
    // any 64-bit truncation). A mutation that narrows the index to 64 bits changes the hash
    // and fails this pin.
    auto id = derivePayloadId(attrs, f.parentHash, {}, 0x02);
    BOOST_CHECK_EQUAL(id, "0x02fcd01f27c51b12");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
