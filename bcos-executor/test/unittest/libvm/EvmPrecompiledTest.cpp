/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Unit tests for EVM precompiled contracts (sha256, ripemd160, identity,
 *        blake2_compression, modexp, alt_bn128), using official EIP and evmone
 *        test vectors to verify correctness after the evmone 0.21 upgrade.
 * @file EvmPrecompiledTest.cpp
 */

#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "vm/Precompiled.h"
#include <boost/test/unit_test.hpp>
#include <string_view>

namespace bcos::test
{

// ---------------------------------------------------------------------------
// Helper: call a named precompile and return the result pair
// ---------------------------------------------------------------------------
static std::pair<bool, bytes> exec(std::string_view name, bytesConstRef input)
{
    return executor::PrecompiledRegistrar::executor(std::string(name))(input);
}

static std::pair<bool, bytes> exec(std::string_view name, const bytes& input)
{
    return exec(name, ref(input));
}

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------
class EvmPrecompiledTestFixture
{
};

BOOST_FIXTURE_TEST_SUITE(testEvmPrecompiled, EvmPrecompiledTestFixture)

// ===== identity ============================================================

BOOST_AUTO_TEST_CASE(identity_empty)
{
    auto [ok, out] = exec("identity", bytes{});
    BOOST_CHECK(ok);
    BOOST_CHECK(out.empty());
}

BOOST_AUTO_TEST_CASE(identity_passthrough)
{
    bytes input = bcos::fromHex("deadbeef1234567890abcdef");
    auto [ok, out] = exec("identity", input);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(bcos::toHex(out), bcos::toHex(input));
}

// ===== sha256 ==============================================================
// Test vectors from https://www.di-mgt.com.au/sha_testvectors.html
// (same set used by evmone precompiles_sha256_test.cpp)

BOOST_AUTO_TEST_CASE(sha256_empty)
{
    auto [ok, out] = exec("sha256", bytes{});
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(
        bcos::toHex(out), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

BOOST_AUTO_TEST_CASE(sha256_abc)
{
    bytes input = bcos::fromHex("616263");  // "abc"
    auto [ok, out] = exec("sha256", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    // NIST FIPS 180-4 test vector (python: hashlib.sha256(b'abc').hexdigest())
    BOOST_CHECK_EQUAL(
        bcos::toHex(out), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

BOOST_AUTO_TEST_CASE(sha256_448bit_message)
{
    // "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    bytes input(reinterpret_cast<const uint8_t*>(
                    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        reinterpret_cast<const uint8_t*>(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") +
            56);
    auto [ok, out] = exec("sha256", input);
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(
        bcos::toHex(out), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// ===== ripemd160 ===========================================================
// Test vectors from https://homes.esat.kuleuven.be/~bosselae/ripemd160.html
// (same set used by evmone precompiles_ripemd160_test.cpp)
// Note: the precompile returns the 20-byte hash zero-padded on the LEFT to 32 bytes.

BOOST_AUTO_TEST_CASE(ripemd160_empty)
{
    auto [ok, out] = exec("ripemd160", bytes{});
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    // first 12 bytes must be zero, then the 20-byte hash
    for (size_t i = 0; i < 12; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
    BOOST_CHECK_EQUAL(bcos::toHex(bytesConstRef(out.data() + 12, 20)),
        "9c1185a5c5e9fc54612808977ee8f548b2258d31");
}

BOOST_AUTO_TEST_CASE(ripemd160_abc)
{
    bytes input = bcos::fromHex("616263");  // "abc"
    auto [ok, out] = exec("ripemd160", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    for (size_t i = 0; i < 12; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
    BOOST_CHECK_EQUAL(bcos::toHex(bytesConstRef(out.data() + 12, 20)),
        "8eb208f7e05d987a9b044a8e98c6b087f15a0bfc");
}

BOOST_AUTO_TEST_CASE(ripemd160_message_digest)
{
    // "message digest"
    bytes input(reinterpret_cast<const uint8_t*>("message digest"),
        reinterpret_cast<const uint8_t*>("message digest") + 14);
    auto [ok, out] = exec("ripemd160", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    BOOST_CHECK_EQUAL(bcos::toHex(bytesConstRef(out.data() + 12, 20)),
        "5d0689ef49d2fae572b881b123a85ffa21595f36");
}

// ===== modexp ==============================================================
// EIP-198 test vectors (simple cases)

BOOST_AUTO_TEST_CASE(modexp_zero_zero_zero)
{
    // baseLen=0, expLen=0, modLen=0 → empty output
    bytes input(96, 0);  // three 32-byte big-endian zeros
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_CHECK(out.empty());
}

BOOST_AUTO_TEST_CASE(modexp_3_pow_2_mod_5)
{
    // base=3, exp=2, mod=5 → 3^2 mod 5 = 4
    bytes input;
    // baseLen = 1 (32 bytes BE)
    input.resize(96, 0);
    input[31] = 1;          // baseLen = 1
    input[63] = 1;          // expLen  = 1
    input[95] = 1;          // modLen  = 1
    input.push_back(0x03);  // base = 3
    input.push_back(0x02);  // exp  = 2
    input.push_back(0x05);  // mod  = 5
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 1u);
    BOOST_CHECK_EQUAL(out[0], 0x04);  // result = 4
}

BOOST_AUTO_TEST_CASE(modexp_2_pow_3_mod_6)
{
    // base=2, exp=3, mod=6 → 8 mod 6 = 2
    bytes input(96, 0);
    input[31] = 1;  // baseLen = 1
    input[63] = 1;  // expLen  = 1
    input[95] = 1;  // modLen  = 1
    input.push_back(0x02);
    input.push_back(0x03);
    input.push_back(0x06);
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 1u);
    BOOST_CHECK_EQUAL(out[0], 0x02);
}

BOOST_AUTO_TEST_CASE(modexp_exp_zero_returns_one)
{
    // base=3, exp=0, mod=5 → 3^0 mod 5 = 1
    bytes input(96, 0);
    input[31] = 1;  // baseLen = 1
    // expLen stays 0 (exp=empty → zero → any_base^0 = 1)
    input[95] = 1;          // modLen  = 1
    input.push_back(0x03);  // base
    input.push_back(0x05);  // mod
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 1u);
    BOOST_CHECK_EQUAL(out[0], 0x01);
}

BOOST_AUTO_TEST_CASE(modexp_mod_zero_returns_zero)
{
    // mod=0: EIP-198 specifies result = 0, output length = modLen = 1
    // Source: Precompiled.cpp — `mod != 0 ? powm(...) : bigint{0}`
    bytes input(96, 0);
    input[31] = 1;          // baseLen = 1
    input[63] = 1;          // expLen  = 1
    input[95] = 1;          // modLen  = 1
    input.push_back(0x07);  // base = 7
    input.push_back(0x03);  // exp  = 3
    input.push_back(0x00);  // mod  = 0  ← the key case
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 1u);
    BOOST_CHECK_EQUAL(out[0], 0x00);  // result must be 0
}

BOOST_AUTO_TEST_CASE(modexp_modlen_zero_baselen_nonzero)
{
    // modLen=0 but baseLen≠0: does NOT hit the early-return shortcut
    // (which requires BOTH modLen==0 AND baseLen==0).
    // Falls through the full path: mod parsed as 0 → result=0, retLength=0 → {true, bytes{}}
    bytes input(96, 0);
    input[31] = 1;  // baseLen = 1
    input[63] = 1;  // expLen  = 1
    // modLen stays 0
    input.push_back(0x07);  // base = 7
    input.push_back(0x03);  // exp  = 3
    // (no mod bytes)
    auto [ok, out] = exec("modexp", input);
    BOOST_CHECK(ok);
    BOOST_CHECK(out.empty());  // modLen=0 → retLength=0 → empty output
}

// ===== modexp gas pricing ==================================================
// EIP-2565 formula: multComplexity(max(baseLen,modLen)) * max(adjExpLen,1) / 20
// multComplexity(x): x≤64 → x²; 64<x≤1024 → x²/4+96x-3072; x>1024 → x²/16+480x-199680

BOOST_AUTO_TEST_CASE(modexp_pricer_small_inputs)
{
    // baseLen=modLen=1, expLen=1 → maxLen=1, adjExpLen=msb(exp)
    // For exp=2 (msb=1): multComplexity(1)=1, max(1,1)=1, gas = 1*1/20 = 0
    // (integer division rounds down, minimum is floored by EVM callers but pricer itself returns 0)
    const std::string modexpName{"modexp"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(modexpName);
    bytes input(96, 0);
    input[31] = 1;          // baseLen = 1
    input[63] = 1;          // expLen  = 1
    input[95] = 1;          // modLen  = 1
    input.push_back(0x03);  // base
    input.push_back(0x02);  // exp = 2 → adjExpLen = msb(2) = 1
    input.push_back(0x05);  // mod
    // multComplexity(1)=1, max(1,1)=1, 1*1/20 = 0
    BOOST_CHECK_EQUAL(pricer(ref(input)), 0);
}

BOOST_AUTO_TEST_CASE(modexp_pricer_multcomplexity_branch_le64)
{
    // maxLen=32 (≤64): multComplexity(32) = 32*32 = 1024
    // exp=1 → adjExpLen = msb(1) = 0 → max(0,1) = 1
    // gas = 1024 * 1 / 20 = 51
    const std::string modexpName{"modexp"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(modexpName);
    bytes input(96, 0);
    input[31] = 32;  // baseLen = 32
    input[63] = 1;   // expLen  = 1
    input[95] = 32;  // modLen  = 32
    input.resize(96 + 32 + 1 + 32, 0);
    input[96] = 0x01;       // base = 1 (32 bytes, only last byte matters)
    input[96 + 32] = 0x01;  // exp  = 1 → adjExpLen = 0
    input[96 + 33] = 0x01;  // mod  = 1 (last byte of 32-byte big-endian)
    BOOST_CHECK_EQUAL(pricer(ref(input)), 51);
}

BOOST_AUTO_TEST_CASE(modexp_pricer_multcomplexity_branch_le1024)
{
    // maxLen=128 (64<x≤1024): multComplexity(128) = 128²/4 + 96*128 - 3072
    //                                              = 16384/4 + 12288 - 3072
    //                                              = 4096 + 12288 - 3072 = 13312
    // exp=1 → adjExpLen=0 → max(0,1)=1; gas = 13312 / 20 = 665
    const std::string modexpName{"modexp"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(modexpName);
    bytes input(96, 0);
    input[31] = 128;  // baseLen = 128
    input[63] = 1;    // expLen  = 1
    input[95] = 128;  // modLen  = 128
    input.resize(96 + 128 + 1 + 128, 0);
    input[96] = 0x01;        // base LSB
    input[96 + 128] = 0x01;  // exp = 1
    input[96 + 129] = 0x01;  // mod LSB
    BOOST_CHECK_EQUAL(pricer(ref(input)), 665);
}

BOOST_AUTO_TEST_CASE(modexp_pricer_multcomplexity_branch_gt1024)
{
    // maxLen=2048 (>1024): multComplexity(2048) = 2048²/16 + 480*2048 - 199680
    //                                            = 262144 + 983040 - 199680 = 1045504
    // exp=1 → adjExpLen=msb(1)=0 → max(0,1)=1; gas = 1045504 * 1 / 20 = 52275
    const std::string modexpName{"modexp"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(modexpName);
    bytes input(96, 0);
    // Use uint16_t to set lengths > 255
    input[30] = 0x08;  // baseLen high byte → 0x0800 = 2048
    input[31] = 0x00;
    input[62] = 0x00;  // expLen = 1
    input[63] = 0x01;
    input[94] = 0x08;  // modLen = 2048
    input[95] = 0x00;
    input.resize(96 + 2048 + 1 + 2048, 0);
    input[96] = 0x01;         // base = 1 (first byte of 2048-byte big-endian)
    input[96 + 2048] = 0x01;  // exp  = 1 → adjExpLen = 0
    input[96 + 2049] = 0x01;  // mod  = 1 (first byte of 2048-byte big-endian)
    BOOST_CHECK_EQUAL(pricer(ref(input)), 52275);
}

BOOST_AUTO_TEST_CASE(modexp_pricer_explengthadjust_long_exponent)
{
    // expLength > 32 branch in expLengthAdjust:
    //   adjExpLen = 8 * (expLength - 32) + highestBit(first 32 bytes of exp)
    // Setup: baseLen=32, expLen=33, modLen=32 → maxLen=32
    //   first 32 bytes of exp = 1 (msb=0) → highestBit=0 → adjExpLen = 8*(33-32)+0 = 8
    //   multComplexity(32) = 32² = 1024
    //   gas = 1024 * max(8,1) / 20 = 8192 / 20 = 409
    const std::string modexpName{"modexp"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(modexpName);
    bytes input(193, 0);  // 96 header + 32 base + 33 exp + 32 mod
    input[31] = 32;       // baseLen = 32
    input[63] = 33;       // expLen  = 33
    input[95] = 32;       // modLen  = 32
    input[127] = 0x01;    // base = 1  (last byte of 32-byte big-endian value)
    input[159] = 0x01;    // first 32 bytes of exp (big-endian) = 1 → highestBit = 0
    // input[160] = 0x00   33rd byte of exp (already 0)
    input[192] = 0x01;  // mod = 1   (last byte of 32-byte big-endian value)
    BOOST_CHECK_EQUAL(pricer(ref(input)), 409);
}

// ===== blake2_compression ==================================================
// EIP-152 test vectors.
// https://eips.ethereum.org/EIPS/eip-152#test-cases
//
// NOTE: The precompile at address 0x09 wraps blake2b_compress(). EIP-152 specifies:
//   [4B rounds BE][64B h LE][128B m LE][8B t0 LE][8B t1 LE][1B f]
//
// The shared body below (h, m, t, without rounds and f) is reused across valid
// test vectors 4-8.

// Shared body for EIP-152 test vectors 4-8:
//   h = BLAKE2b_IV XOR param_block for BLAKE2b-512 of "abc"
//   m = "abc" || zeros  (as LE uint64 words)
//   t = [3, 0]          (offset counter = 3 bytes in LE)
static const std::string BLAKE2_BODY_HEX =
    // h: 8 x uint64 LE = 64 bytes
    "48c9bdf267e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5"
    "d182e6ad7f520e511f6c3e2b8c68059b6bbd41fbabd9831f79217e1319cde05b"
    // m: 16 x uint64 LE = 128 bytes; m[0]="abc\0\0\0\0\0", m[1..15]=0
    "6162630000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    "0000000000000000"
    // t0: LE uint64 = 3 (3 bytes processed)
    "0300000000000000"
    // t1: LE uint64 = 0
    "0000000000000000";

BOOST_AUTO_TEST_CASE(blake2_compression_wrong_input_length)
{
    // Test vector 0-2: any input ≠ 213 bytes → failure
    auto [ok0, _0] = exec("blake2_compression", bytes{});
    BOOST_CHECK(!ok0);

    auto [ok1, _1] = exec("blake2_compression", bytes(200, 0));
    BOOST_CHECK(!ok1);

    auto [ok2, _2] = exec("blake2_compression", bytes(214, 0));
    BOOST_CHECK(!ok2);
}

BOOST_AUTO_TEST_CASE(blake2_compression_invalid_final_block_indicator)
{
    // Test vector 3: 213 bytes but f = 0x02 → failure
    bytes input = bcos::fromHex("0000000c" + BLAKE2_BODY_HEX + "02");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(blake2_compression_tv4_zero_rounds_final)
{
    // Test vector 4: rounds=0, f=1
    // Expected output from EIP-152.
    bytes input = bcos::fromHex("00000000" + BLAKE2_BODY_HEX + "01");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "08c9bcf367e6096a3ba7ca8485ae67bb2bf894fe72f36e3cf1361d5f3af54fa5"
        "d282e6ad7f520e511f6c3e2b8c68059b9442be0454267ce079217e1319cde05b");
}

BOOST_AUTO_TEST_CASE(blake2_compression_tv5_12_rounds_final)
{
    // Test vector 5: rounds=12, f=1 (the canonical "abc" BLAKE2b compression)
    // Expected output from EIP-152.
    bytes input = bcos::fromHex("0000000c" + BLAKE2_BODY_HEX + "01");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
        "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923");
}

BOOST_AUTO_TEST_CASE(blake2_compression_tv6_12_rounds_not_final)
{
    // Test vector 6: rounds=12, f=0
    bytes input = bcos::fromHex("0000000c" + BLAKE2_BODY_HEX + "00");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "75ab69d3190a562c51aef8d88f1c2775876944407270c42c9844252c26d28752"
        "98743e7f6d5ea2f2d3e8d226039cd31b4e426ac4f2d3d666a610c2116fde4735");
}

BOOST_AUTO_TEST_CASE(blake2_compression_tv7_1_round_final)
{
    // Test vector 7: rounds=1, f=1
    bytes input = bcos::fromHex("00000001" + BLAKE2_BODY_HEX + "01");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "b63a380cb2897d521994a85234ee2c181b5f844d2c624c002677e9703449d2fb"
        "a551b3a8333bcdf5f2f7e08993d53923de3d64fcc68c034e717b9293fed7a421");
}

BOOST_AUTO_TEST_CASE(blake2_compression_tv8_max_rounds_final)
{
    // Test vector 8: rounds=0xffffffff, f=1
    bytes input = bcos::fromHex("ffffffff" + BLAKE2_BODY_HEX + "01");
    BOOST_REQUIRE_EQUAL(input.size(), 213u);
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "fc59093aafa9ab43daae0e914c57635c5402d8e3d2130eb9b3cc181de7f0ecf9"
        "b22bf99a7815ce16419e200e01846e6b5df8cc7703041bbceb571de6631d2615");
}

BOOST_AUTO_TEST_CASE(blake2_compression_output_encoding_is_little_endian)
{
    // Verify that output h[] is written back as little-endian bytes (EIP-152 §spec:
    // "return the updated state vector h with unchanged encoding (little-endian)").
    // For tv5 (12 rounds, f=1), the first 8 bytes of the output are the LE bytes
    // of the first state word.  If output were big-endian, those bytes would be
    // reversed relative to the expected value.
    bytes input = bcos::fromHex("0000000c" + BLAKE2_BODY_HEX + "01");
    auto [ok, out] = exec("blake2_compression", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);

    // The first 8 output bytes must be 0xba 0x80 0xa5 0x3f 0x98 0x1c 0x4d 0x0d
    // (little-endian representation of uint64 0x0d4d1c983fa580ba).
    // If fromBigEndian/toBigEndian were used, the bytes would be 0x0d 0x4d 0x1c 0x98 …
    bytes expected_first8 = bcos::fromHex("ba80a53f981c4d0d");
    BOOST_CHECK_EQUAL_COLLECTIONS(
        out.begin(), out.begin() + 8, expected_first8.begin(), expected_first8.end());
}

// ===== blake2_compression gas pricing ======================================

BOOST_AUTO_TEST_CASE(blake2_compression_price_equals_rounds)
{
    // The pricer must return the number of rounds (cost = 1 gas per round, EIP-152 §gas).
    const std::string blake2Name{"blake2_compression"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(blake2Name);

    // 12 rounds → cost 12
    bytes input12 = bcos::fromHex("0000000c" + BLAKE2_BODY_HEX + "01");
    BOOST_CHECK_EQUAL(pricer(ref(input12)), 12);

    // 1 round → cost 1
    bytes input1 = bcos::fromHex("00000001" + BLAKE2_BODY_HEX + "01");
    BOOST_CHECK_EQUAL(pricer(ref(input1)), 1);

    // max rounds → cost 0xffffffff
    bytes inputMax = bcos::fromHex("ffffffff" + BLAKE2_BODY_HEX + "01");
    BOOST_CHECK_EQUAL(pricer(ref(inputMax)), 0xffffffff);
}

// ===== alt_bn128_G1_add ============================================================
// EIP-196 test vectors.  G1 generator = (1, 2) in Fq.
// 2*G1 computed via the elliptic-curve doubling formula (verified independently).
// BN254 field prime p = 0x30644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd47

BOOST_AUTO_TEST_CASE(alt_bn128_G1_add_infinity)
{
    // Empty input → both points zero-padded to (0,0) = infinity → result = infinity
    auto [ok, out] = exec("alt_bn128_G1_add", bytes{});
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    for (size_t i = 0; i < 64; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_add_generator_plus_generator)
{
    // G1 + G1 = 2*G1
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x = 1
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y = 2
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x = 1
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y = 2
    );
    auto [ok, out] = exec("alt_bn128_G1_add", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    // 2*G1 on BN254 (computed via doubling formula, matches go-ethereum reference)
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "030644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd3"
        "15ed738c0e0a7c92e7845f96b2ae9c0a68a6a449e3538fc7ff3ebf7a5a18a2c4");
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_add_generator_plus_infinity)
{
    // G1 + infinity = G1
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x = 1
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y = 2
        "0000000000000000000000000000000000000000000000000000000000000000"  // 0.x
        "0000000000000000000000000000000000000000000000000000000000000000"  // 0.y
    );
    auto [ok, out] = exec("alt_bn128_G1_add", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002");
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_add_invalid_point)
{
    // (1, 1) is not on BN254: 1^2=1 ≠ 1^3+3=4 → failure
    // Source returns {false, bytes(64, 0)} on invalid input.
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // x = 1
        "0000000000000000000000000000000000000000000000000000000000000001"  // y = 1 (invalid)
        "0000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000");
    auto [ok, out] = exec("alt_bn128_G1_add", input);
    BOOST_CHECK(!ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    for (size_t i = 0; i < 64; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

// ===== alt_bn128_G1_mul ============================================================
// EIP-196 test vectors.

BOOST_AUTO_TEST_CASE(alt_bn128_G1_mul_zero_scalar)
{
    // G1 * 0 = point at infinity
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y
        "0000000000000000000000000000000000000000000000000000000000000000"  // scalar = 0
    );
    auto [ok, out] = exec("alt_bn128_G1_mul", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    for (size_t i = 0; i < 64; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_mul_scalar_one)
{
    // G1 * 1 = G1
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y
        "0000000000000000000000000000000000000000000000000000000000000001"  // scalar = 1
    );
    auto [ok, out] = exec("alt_bn128_G1_mul", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002");
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_mul_scalar_two)
{
    // G1 * 2 = 2*G1 (consistent with G1 + G1 above)
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // G1.x
        "0000000000000000000000000000000000000000000000000000000000000002"  // G1.y
        "0000000000000000000000000000000000000000000000000000000000000002"  // scalar = 2
    );
    auto [ok, out] = exec("alt_bn128_G1_mul", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    BOOST_CHECK_EQUAL(bcos::toHex(out),
        "030644e72e131a029b85045b68181585d97816a916871ca8d3c208c16d87cfd3"
        "15ed738c0e0a7c92e7845f96b2ae9c0a68a6a449e3538fc7ff3ebf7a5a18a2c4");
}

BOOST_AUTO_TEST_CASE(alt_bn128_G1_mul_invalid_point)
{
    // (1, 1) is not on BN254 → failure
    // Source returns {false, bytes(64, 0)} on invalid input.
    bytes input = bcos::fromHex(
        "0000000000000000000000000000000000000000000000000000000000000001"  // x = 1
        "0000000000000000000000000000000000000000000000000000000000000001"  // y = 1 (invalid)
        "0000000000000000000000000000000000000000000000000000000000000001"  // scalar
    );
    auto [ok, out] = exec("alt_bn128_G1_mul", input);
    BOOST_CHECK(!ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);
    for (size_t i = 0; i < 64; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

// ===== alt_bn128_pairing_product ===================================================
// EIP-197 test vectors.
// Empty product = 1 (trivially satisfies the pairing equation).

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_empty_input)
{
    // Empty input → empty product pairing check → result = 1
    auto [ok, out] = exec("alt_bn128_pairing_product", bytes{});
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    for (size_t i = 0; i < 31; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
    BOOST_CHECK_EQUAL(out[31], 0x01);
}

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_invalid_input_size)
{
    // Input not a multiple of 192 → failure
    // Source returns {false, bytes(32, 0)} on size error.
    auto [ok, out] = exec("alt_bn128_pairing_product", bytes(100, 0));
    BOOST_CHECK(!ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    for (size_t i = 0; i < 32; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_null_pair)
{
    // One null pair: G1=(0,0), G2=(0,0,0,0) → passes (infinity satisfies pairing)
    auto [ok, out] = exec("alt_bn128_pairing_product", bytes(192, 0));
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    BOOST_CHECK_EQUAL(out[31], 0x01);
}

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_pricer)
{
    // EIP-197 §gas: 45000 + 34000 * k pairs
    const std::string pairingName{"alt_bn128_pairing_product"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(pairingName);
    bytes empty;
    BOOST_CHECK_EQUAL(pricer(ref(empty)), 45000);  // k=0
    bytes one_pair(192, 0);
    BOOST_CHECK_EQUAL(pricer(ref(one_pair)), 79000);  // k=1
    bytes two_pairs(384, 0);
    BOOST_CHECK_EQUAL(pricer(ref(two_pairs)), 113000);  // k=2
}

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_invalid_g1_point)
{
    // G1=(1,1) is not on BN254 (y²=1 ≠ x³+3=4) → {false, bytes(32,0)}
    // The source returns {false, bytes(32, 0)} for invalid G1.
    bytes input(192, 0);
    // G1.x = 1 at offset 0
    input[31] = 0x01;
    // G1.y = 1 at offset 32 (invalid — not on curve)
    input[63] = 0x01;
    // G2 left as zeros (irrelevant, validation fails before G2 is read)
    auto [ok, out] = exec("alt_bn128_pairing_product", input);
    BOOST_CHECK(!ok);
    // Source guarantees 32-byte zero buffer on failure
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    for (size_t i = 0; i < 32; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

BOOST_AUTO_TEST_CASE(alt_bn128_pairing_result_zero)
{
    // Pairing of (G1, G2_neg) where G2_neg is the negation of the standard G2 generator.
    // e(G1, -G2) != 1 (trivial pairing with no cancellation), so pairing_check returns false.
    // This exercises the `!*result` branch: output[31] stays 0.
    //
    // G1 generator: (1, 2) — valid BN254 point
    // G2 = the "wrong" G2 that evmmax validates but doesn't form e(G1,G2)=1 without G2's partner.
    // Simplest approach: use two G1 points both set to (1,2) but G2 = all-zeros.
    // The all-zero G2 is infinity and is accepted by the library; pairing e(G1,0) trivially gives
    // the identity → pairing_check passes → result = 1.
    // To get result=0 we need a genuinely non-trivial pair that fails the check.
    // Use a single pair with a valid G1 and the standard BN254 G2 generator but not the
    // matching pairing — specifically encode G2 with swapped coordinates to produce a valid
    // structure that passes from_bytes but whose pairing != 1.
    //
    // BN254 G2 generator in EIP encoding (a1_x, a0_x, a1_y, a0_y):
    //   a0_x = 11559732032986387107991004021392285783925812861821192530917403151452391805634
    //   a1_x = 10857046999023057135944570762232829481370756359578518086990519993285655852781
    //   a0_y = 4082367875863433681332203403145435568316851327593401208105741076214120093531
    //   a1_y = 8495653923123431417604973247489272438418190587263600148770280649306958101930
    // EIP-197 layout at offset 64: [a1_x(32) | a0_x(32) | a1_y(32) | a0_y(32)]
    bytes input = bcos::fromHex(
        // G1: (1, 2)
        "0000000000000000000000000000000000000000000000000000000000000001"
        "0000000000000000000000000000000000000000000000000000000000000002"
        // G2: standard BN254 G2 generator (EIP-197 encoding)
        "198e9393920d483a7260bfb731fb5d25f1aa493335a9e71297e485b7aef312c2"  // a1_x
        "1800deef121f1e76426a00665e5c4479674322d4f75edadd46debd5cd992f6ed"  // a0_x
        "090689d0585ff075ec9e99ad690c3395bc4b313370b38ef355acdadcd122975b"  // a1_y
        "12c85ea5db8c6deb4aab71808dcb408fe3d1e7690c43d37b4ce6cc0166fa7daa"  // a0_y
    );
    BOOST_REQUIRE_EQUAL(input.size(), 192u);
    auto [ok, out] = exec("alt_bn128_pairing_product", input);
    // e(G1, G2_gen) alone does not equal 1 — pairing_check with a single non-trivial pair
    // returns false (result = 0), so ok=true but output[31]=0.
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    for (size_t i = 0; i < 32; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);
}

// ===== point_evaluation ============================================================
// EIP-4844 KZG point evaluation precompile.

BOOST_AUTO_TEST_CASE(point_evaluation_wrong_input_length)
{
    // Any size != 192 → failure
    auto [ok0, out0] = exec("point_evaluation", bytes{});
    BOOST_CHECK(!ok0);

    auto [ok1, out1] = exec("point_evaluation", bytes(191, 0));
    BOOST_CHECK(!ok1);

    auto [ok2, out2] = exec("point_evaluation", bytes(193, 0));
    BOOST_CHECK(!ok2);
}

BOOST_AUTO_TEST_CASE(point_evaluation_invalid_versioned_hash)
{
    // 192 bytes with all-zero versioned hash (bytes[0..31]) does not match
    // sha256(commitment)[0]=0x01 KZG version byte → SHA256-based check fails
    auto [ok, out] = exec("point_evaluation", bytes(192, 0));
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(point_evaluation_valid_proof_success)
{
    // Official EIP-4844 KZG test vector (taken from the existing
    // bcos-executor/test/unittest/libprecompiled/PrecompiledTest.cpp).
    // Input layout (192 bytes):
    //   [0..31]   versioned_hash  = 0x01 || sha256(commitment)[1..]
    //   [32..63]  z (field element)
    //   [64..95]  y (field element)
    //   [96..143] commitment (48 bytes)
    //   [144..191] proof      (48 bytes)
    bytes input = bcos::fromHex(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
    BOOST_REQUIRE_EQUAL(input.size(), 192u);

    auto [ok, out] = exec("point_evaluation", input);
    BOOST_CHECK(ok);
    BOOST_REQUIRE_EQUAL(out.size(), 64u);

    // Return value: FIELD_ELEMENTS_PER_BLOB (4096) and BLS_MODULUS as 32-byte BE values.
    // FIELD_ELEMENTS_PER_BLOB = 0x1000 = 4096
    BOOST_CHECK_EQUAL(out[30], 0x10);
    BOOST_CHECK_EQUAL(out[31], 0x00);
    for (size_t i = 0; i < 30; ++i)
        BOOST_CHECK_EQUAL(out[i], 0);

    // BLS_MODULUS first byte = 0x73 (verified against EIP-4844 spec)
    BOOST_CHECK_EQUAL(out[32], 0x73);
}

BOOST_AUTO_TEST_CASE(point_evaluation_corrupted_versioned_hash_byte)
{
    // Version byte changed from 0x01 to 0x02 → versioned hash check fails.
    bytes input = bcos::fromHex(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
    BOOST_REQUIRE_EQUAL(input.size(), 192u);
    input[0] = 0x02;  // corrupt version byte
    auto [ok, out] = exec("point_evaluation", input);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(point_evaluation_corrupted_hash_body)
{
    // Version byte correct (0x01) but remaining 31 bytes of hash body tampered → fails.
    bytes input = bcos::fromHex(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
    BOOST_REQUIRE_EQUAL(input.size(), 192u);
    input[1] ^= 0x01;  // flip one bit in the hash body (version byte untouched)
    auto [ok, out] = exec("point_evaluation", input);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(point_evaluation_valid_commitment_bad_proof)
{
    // Versioned hash check passes (commitment bytes 96-143 unchanged) but
    // kzg_verify_proof returns false because z (bytes 32-63) was corrupted.
    // Covers the `if (!ok) return {false, {}};` branch in point_evaluation.
    bytes input = bcos::fromHex(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
    BOOST_REQUIRE_EQUAL(input.size(), 192u);
    // Corrupt z (bytes 32-63). versioned_hash = sha256(commitment)[0..]=0x01|...
    // depends only on bytes 96-143 (commitment), so hash check still passes.
    // kzg_verify_proof sees the wrong z and returns false.
    input[32] ^= 0x01;
    auto [ok, out] = exec("point_evaluation", input);
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(point_evaluation_pricer)
{
    // EIP-4844 §gas: flat cost of 50000
    const std::string kzgName{"point_evaluation"};
    auto& pricer = executor::PrecompiledRegistrar::pricer(kzgName);
    bytes empty;
    BOOST_CHECK_EQUAL(pricer(ref(empty)), 50000);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
