/*
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file test_KzgPrecompileRegistered.cpp
 * @brief EIP-4844 KZG point-evaluation (0x0a) registration + known-answer (A6.14)
 */
#include "vm/Precompiled.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor;

BOOST_AUTO_TEST_SUITE(KzgPrecompileRegisteredTest)

// The KZG precompile is registered under the name "point_evaluation" via
// ETH_REGISTER_PRECOMPILED / ETH_REGISTER_PRECOMPILED_PRICER in
// bcos-executor/src/vm/Precompiled.cpp. initEvmEnvironment inserts it at
// address 0x0a. We assert the registrar resolves both entries.
//
// Registrar-level assertion is used rather than constructing a full
// TransactionExecutor: the 0x0a -> point_evaluation insertion is a single
// literal line in initEvmEnvironment and building an executor requires heavy
// storage/ledger fixtures.
BOOST_AUTO_TEST_CASE(Registration)
{
    BOOST_CHECK_NO_THROW(PrecompiledRegistrar::executor("point_evaluation"));
    BOOST_CHECK_NO_THROW(PrecompiledRegistrar::pricer("point_evaluation"));
}

BOOST_AUTO_TEST_CASE(KzgKnownAnswer)
{
    // Reference vector: c-kzg-4844 verify_kzg_proof_case_correct_proof_0_0
    // (zero polynomial). commitment = proof = G1 infinity (0xc0 + 47 zero
    // bytes), z = y = 0, output = valid. The EIP-4844 point-evaluation input
    // is 192 bytes: versioned_hash(32) || z(32) || y(32) || commitment(48) ||
    // proof(48), where versioned_hash = sha256(commitment) with byte[0]=0x01.
    // Bind the name to a named std::string before calling executor() — GCC 13/14
    // (with -Werror=dangling-reference) heuristically flags the temporary
    // std::string constructed from a string literal as a "possible dangling
    // reference" through the const-ref return, even though executor() actually
    // returns a reference into PrecompiledRegistrar's static registry. clang
    // does not emit this warning. Without the named binding, the GCC matrix
    // legs fail at compile time.
    static std::string const kPointEvaluationName = "point_evaluation";
    auto const& exec = PrecompiledRegistrar::executor(kPointEvaluationName);

    // 192-byte well-formed input (versioned hash precomputed for the above
    // commitment via sha256, high byte set to VERSIONED_HASH_VERSION_KZG=0x01).
    std::string const inputHex =
        "010657f37554c781402a22917dee2f75def7ab966d7b770905398eba3c444014"  // versioned_hash
        "0000000000000000000000000000000000000000000000000000000000000000"  // z
        "0000000000000000000000000000000000000000000000000000000000000000"  // y
        "c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00000"  // commitment (48B)
        "0"
        "c00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00000"  // proof (48B)
        "0";
    bytes input = bcos::fromHex(inputHex);
    BOOST_REQUIRE_EQUAL(input.size(), 192U);

    auto [ok, out] = exec(bytesConstRef(input.data(), input.size()));
    BOOST_CHECK(ok);

    // Success output = 64 bytes: FIELD_ELEMENTS_PER_BLOB (0x1000) || BLS_MODULUS.
    bytes expected = bcos::fromHex(
        "000000000000000000000000000000000000000000000000000000000000100073eda"
        "753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001");
    BOOST_REQUIRE_EQUAL(expected.size(), 64U);
    BOOST_CHECK_EQUAL_COLLECTIONS(out.begin(), out.end(), expected.begin(), expected.end());

    // Corrupted versioned hash (flip the high byte) -> failure, empty output.
    bytes bad = input;
    bad[0] ^= 0xff;
    auto [badOk, badOut] = exec(bytesConstRef(bad.data(), bad.size()));
    BOOST_CHECK(!badOk);
    BOOST_CHECK(badOut.empty());

    // Wrong-length input -> failure (impl rejects size != 192).
    bytes truncated(input.begin(), input.begin() + 160);
    auto [shortOk, shortOut] = exec(bytesConstRef(truncated.data(), truncated.size()));
    BOOST_CHECK(!shortOk);
    BOOST_CHECK(shortOut.empty());
}

BOOST_AUTO_TEST_SUITE_END()
