/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-M*: modexp (EIP-198) edge cases vs legacy behaviour.
 *  @file CompatModexpTest.cpp
 */

#include "CompatTestFixture.h"
#include "vm/Precompiled.h"
#include <Common.h>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace bcos::test
{
using compat::compatMakeModexpInput;

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_AUTO_TEST_SUITE(CompatModexp)

BOOST_AUTO_TEST_CASE(FC_M_modexp_compatibility)
{
    // Small vector from legacy ForwardCompatibility / EvmPrecompiledTest: 2^8 mod 10 = 6
    auto run = [](bytes const& input) {
        return executor::PrecompiledRegistrar::executor("modexp")(ref(input));
    };
    auto r = run(compatMakeModexpInput({0x02}, {0x08}, {0x0a}));
    BOOST_REQUIRE(r.first);
    BOOST_CHECK_EQUAL(r.second[0], 6);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_7_pow_0_mod_11)
{
    bytes input(96, 0);
    input[31] = 1;
    input[63] = 1;
    input[95] = 1;
    input.push_back(0x07);
    input.push_back(0x00);
    input.push_back(0x0B);
    auto result = executor::PrecompiledRegistrar::executor("modexp")(ref(input));
    BOOST_CHECK(result.first);
    BOOST_REQUIRE_EQUAL(result.second.size(), 1u);
    BOOST_CHECK_EQUAL(static_cast<unsigned>(result.second[0]), 1u);
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_mod_zero_empty)
{
    bytes inputZeroMod(96, 0);
    inputZeroMod[31] = 1;
    inputZeroMod[63] = 1;
    inputZeroMod[95] = 0;
    inputZeroMod.push_back(0x02);
    inputZeroMod.push_back(0x03);
    auto resultZeroMod = executor::PrecompiledRegistrar::executor("modexp")(ref(inputZeroMod));
    BOOST_CHECK(resultZeroMod.first);
    BOOST_CHECK(resultZeroMod.second.empty());
}

BOOST_AUTO_TEST_CASE(FC_M_modexp_large_boundary)
{
    // EvmPrecompiledTest::modexpCompatibility — 7-byte modulus output (all zeros)
    bytes shortInput(96, 0);
    shortInput[31] = 1;
    shortInput[63] = 1;
    shortInput[95] = 7;
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(shortInput));
    BOOST_REQUIRE(r.first);
    BOOST_CHECK_EQUAL(r.second.size(), 7u);
    BOOST_CHECK(r.second == bytes(7, 0));
}

// base=0: 0^exp mod m = 0 (for m > 0)
BOOST_AUTO_TEST_CASE(FC_M_modexp_base_zero)
{
    auto in = compatMakeModexpInput({0x00}, {0x03}, {0x0b});  // 0^3 mod 11 = 0
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(in));
    BOOST_REQUIRE(r.first);
    BOOST_REQUIRE_EQUAL(r.second.size(), 1u);
    BOOST_CHECK_EQUAL(r.second[0], 0);
}

// exp=0: base^0 mod m = 1 (for m > 1)
BOOST_AUTO_TEST_CASE(FC_M_modexp_exp_zero)
{
    auto in = compatMakeModexpInput({0x07}, {0x00}, {0x0b});  // 7^0 mod 11 = 1
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(in));
    BOOST_REQUIRE(r.first);
    BOOST_REQUIRE_EQUAL(r.second.size(), 1u);
    BOOST_CHECK_EQUAL(r.second[0], 1);
}

// Right-padding: declared lengths exceed actual input → missing bytes treated as zero
BOOST_AUTO_TEST_CASE(FC_M_modexp_right_padding)
{
    // Declare baseLen=2, expLen=1, modLen=1.
    // base = [0x02, 0x00] = 0x0200 = 512 (big-endian, trailing zero = right-padded)
    // exp = 0x03 = 3, mod = 0x05 = 5
    // 512 mod 5 = 2 (since 512 = 5*102 + 2), so 512^3 mod 5 = 2^3 mod 5 = 8 mod 5 = 3
    bytes in(96, 0);
    in[30] = 0;
    in[31] = 2;  // baseLen = 2
    in[62] = 0;
    in[63] = 1;  // expLen  = 1
    in[94] = 0;
    in[95] = 1;          // modLen  = 1
    in.push_back(0x02);  // base[0]
    in.push_back(0x00);  // base[1] = trailing zero → base = [0x02, 0x00] = 512
    in.push_back(0x03);  // exp = 3
    in.push_back(0x05);  // mod = 5
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(in));
    BOOST_REQUIRE(r.first);
    BOOST_REQUIRE_EQUAL(r.second.size(), 1u);
    BOOST_CHECK_EQUAL(r.second[0], 3);  // 512^3 mod 5 = 2^3 mod 5 = 3
}

// mod=0 with non-zero modLen → return all-zeros of modLen bytes (EIP-198)
BOOST_AUTO_TEST_CASE(FC_M_modexp_mod_zero_nonzero_len)
{
    // baseLen=1, expLen=1, modLen=3. mod bytes = {0,0,0} → mod=0.
    bytes in(96, 0);
    in[31] = 1;          // baseLen = 1
    in[63] = 1;          // expLen  = 1
    in[95] = 3;          // modLen  = 3
    in.push_back(0x02);  // base = 2
    in.push_back(0x03);  // exp  = 3
    in.push_back(0x00);  // mod byte 1 = 0
    in.push_back(0x00);  // mod byte 2 = 0
    in.push_back(0x00);  // mod byte 3 = 0 → mod == 0
    auto r = executor::PrecompiledRegistrar::executor("modexp")(ref(in));
    BOOST_REQUIRE(r.first);
    BOOST_CHECK_EQUAL(r.second.size(), 3u);
    BOOST_CHECK(r.second == bytes(3, 0));  // all-zeros, length = modLen
}

BOOST_AUTO_TEST_SUITE_END()  // CompatModexp
BOOST_AUTO_TEST_SUITE_END()  // Compat

}  // namespace bcos::test
