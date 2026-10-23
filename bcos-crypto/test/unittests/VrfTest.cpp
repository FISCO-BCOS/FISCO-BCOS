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
 * @brief permanent golden-vector and negative-case tests for the native VRF
 * implementations (bcos-crypto/vrf/). The golden vectors follow the
 * WeDPR-Lab-Crypto VRF spec; byte-compatibility with the real wedprcrypto
 * library was established by the (since removed) VrfDifferentialTest.
 * @file VrfTest.cpp
 */
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/vrf/Curve25519Vrf.h"
#include "bcos-crypto/vrf/Secp256k1Vrf.h"
#include "bcos-crypto/zkp/ristretto/RistrettoGroup.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/testutils/TestPromptFixture.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::crypto;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(VrfTest, TestPromptFixture)

BOOST_AUTO_TEST_CASE(curve25519_golden_vectors)
{
    auto secret = fromHex("72616e646f6d206b6579");       // "random key"
    auto message = fromHex("74657374206d657373616765");  // "test message"
    auto publicKey = fromHex("c4b70a0e351166512f31d0d048cb930f7c84a07953add0e3c4f24a125f56cc57");
    auto proof = fromHex(
        "3c0b4d46ef2bb2fe5cb1ae4169b3467092ccdb09a325136cb15d7045366b61402a4f2275e497f8a5ccfb99"
        "f490aa516899c2cc436592d2b16c6eb1bb3d45670fde272c81e37c049ada64163a83aa256778cb5d474fe5"
        "5a959e90962b2f0c040f");
    auto proofHash = fromHex("dd96d1d6c11ea4c0679c0cfe0ddf01ba3ba8477119116f11bef2ca3b45ea06f1");

    BOOST_CHECK(curve25519VrfDerivePublicKey(ref(secret)) == publicKey);
    BOOST_CHECK(curve25519VrfVerify(ref(publicKey), ref(message), ref(proof)));
    BOOST_CHECK(curve25519VrfProofToHash(ref(proof)) == proofHash);
    BOOST_CHECK(curve25519VrfIsValidPublicKey(ref(publicKey)));

    // a fresh proof must verify and hash consistently
    auto freshProof = curve25519VrfProve(ref(secret), ref(message));
    BOOST_REQUIRE_EQUAL(freshProof.size(), CURVE25519_VRF_PROOF_SIZE);
    BOOST_CHECK(curve25519VrfVerify(ref(publicKey), ref(message), ref(freshProof)));
    BOOST_CHECK_EQUAL(curve25519VrfProofToHash(ref(freshProof)).size(), 32u);
}

BOOST_AUTO_TEST_CASE(curve25519_unreduced_challenge_rejected)
{
    auto message = fromHex("74657374206d657373616765");
    auto publicKey = fromHex("c4b70a0e351166512f31d0d048cb930f7c84a07953add0e3c4f24a125f56cc57");
    auto proof = fromHex(
        "3c0b4d46ef2bb2fe5cb1ae4169b3467092ccdb09a325136cb15d7045366b61402a4f2275e497f8a5ccfb99"
        "f490aa516899c2cc436592d2b16c6eb1bb3d45670fde272c81e37c049ada64163a83aa256778cb5d474fe5"
        "5a959e90962b2f0c040f");
    // sanity: the untouched golden proof verifies
    BOOST_REQUIRE(curve25519VrfVerify(ref(publicKey), ref(message), ref(proof)));

    // l = 2^252 + 27742317777372353535851937790883648493, little-endian
    const std::array<std::uint8_t, 32> groupOrder = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
    // replace the challenge c (proof bytes 32..64) by c + l: arithmetically the same scalar
    // (so all scalar multiplications agree), but dalek compares the challenge BYTES, so the
    // proof must be rejected — we used to reduce c eagerly and accept it
    auto forged = proof;
    std::uint16_t carry = 0;
    for (size_t i = 0; i < 32; i++)
    {
        carry = carry + forged[32 + i] + groupOrder[i];
        forged[32 + i] = static_cast<byte>(carry & 0xff);
        carry >>= 8;
    }
    // c < l and l < 2^253, so c + l < 2^254: bit 255 stays clear and dalek's from_bits
    // accepts it as a distinct scalar
    BOOST_REQUIRE_EQUAL(carry, 0u);
    BOOST_REQUIRE((forged[63] & 0x80) == 0);
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(forged)));
}

BOOST_AUTO_TEST_CASE(curve25519_negative_cases)
{
    auto secret = fromHex("72616e646f6d206b6579");
    auto message = fromHex("74657374206d657373616765");
    auto publicKey = curve25519VrfDerivePublicKey(ref(secret));
    auto proof = curve25519VrfProve(ref(secret), ref(message));

    // tampered proof
    for (size_t pos : {size_t(0), size_t(47), size_t(95)})
    {
        auto tampered = proof;
        tampered[pos] ^= 0x01;
        BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(tampered)));
    }
    // wrong message / wrong key
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), bytesConstRef{}, ref(proof)));
    auto otherKey =
        curve25519VrfDerivePublicKey(bytesConstRef(reinterpret_cast<const byte*>("other"), 5));
    BOOST_CHECK(!curve25519VrfVerify(ref(otherKey), ref(message), ref(proof)));
    // proof length must be exactly 96
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), bytesConstRef{}));
    BOOST_CHECK(!curve25519VrfVerify(
        ref(publicKey), ref(message), bytesConstRef(proof.data(), proof.size() - 1)));
    auto longProof = proof;
    longProof.push_back(0);
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(longProof)));
    // undecodable public key / proof point
    bytes badKey(32, 0xff);
    BOOST_CHECK(!curve25519VrfVerify(ref(badKey), ref(message), ref(proof)));
    auto badGamma = proof;
    badGamma[0] = 0xff;
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(badGamma)));
    // isValidPublicKey: identity is valid, wrong lengths are not
    BOOST_CHECK(curve25519VrfIsValidPublicKey(bytesConstRef(bytes(32, 0).data(), 32)));
    BOOST_CHECK(!curve25519VrfIsValidPublicKey(bytesConstRef{}));
    BOOST_CHECK(!curve25519VrfIsValidPublicKey(bytesConstRef(badKey.data(), 31)));
    // proofToHash on malformed proofs
    BOOST_CHECK(curve25519VrfProofToHash(bytesConstRef{}).empty());
    BOOST_CHECK(curve25519VrfProofToHash(bytesConstRef(proof.data(), 95)).empty());
}

BOOST_AUTO_TEST_CASE(curve25519_identity_and_zero_edge_cases)
{
    auto secret = fromHex("72616e646f6d206b6579");
    auto message = fromHex("74657374206d657373616765");
    auto publicKey = curve25519VrfDerivePublicKey(ref(secret));
    auto proof = curve25519VrfProve(ref(secret), ref(message));

    // the identity encoding (32 zero bytes) is a VALID Ristretto point for both
    // curve25519-dalek and libsodium's crypto_core_ristretto255_is_valid_point
    RistrettoPointBytes identity{};
    BOOST_CHECK(ristrettoIsValidPoint(identity.data()));
    BOOST_CHECK(ristrettoIsIdentity(identity));
    // the canonical basepoint is valid, 0xff... is not
    auto basepoint = ristrettoScalarmultBase(RistrettoScalarBytes{1});
    BOOST_CHECK(ristrettoIsValidPoint(basepoint.data()));
    BOOST_CHECK(!ristrettoIsValidPoint(bytes(32, 0xff).data()));

    // identity group law: x·identity = 0·P = identity, P + identity = P
    BOOST_CHECK(ristrettoScalarmult(RistrettoScalarBytes{7}, identity) == identity);
    BOOST_CHECK(ristrettoScalarmult(RistrettoScalarBytes{}, basepoint) == identity);
    BOOST_CHECK(ristrettoScalarmultBase(RistrettoScalarBytes{}) == identity);
    BOOST_CHECK(ristrettoAdd(basepoint, identity) == basepoint);
    BOOST_CHECK(ristrettoSub(basepoint, basepoint) == identity);

    // identity public key: decodes fine, verification is well-defined (false here)
    bytes identityKey(32, 0);
    BOOST_CHECK(curve25519VrfIsValidPublicKey(ref(identityKey)));
    BOOST_CHECK(!curve25519VrfVerify(ref(identityKey), ref(message), ref(proof)));

    // proof with gamma = identity: valid encoding, verify must be false, and
    // proofToHash hashes 8·identity = identity deterministically
    auto identityGammaProof = proof;
    std::fill_n(identityGammaProof.begin(), RISTRETTO_POINT_LEN, 0);
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(identityGammaProof)));
    auto identityHash = curve25519VrfProofToHash(ref(identityGammaProof));
    BOOST_REQUIRE_EQUAL(identityHash.size(), 32u);
    BOOST_CHECK(identityHash == keccak256Hash(bytesConstRef(identity.data(), 32)).asBytes());

    // proof with c = s = 0 scalars: no crash, deterministic result (false)
    auto zeroScalarProof = proof;
    std::fill(zeroScalarProof.begin() + RISTRETTO_POINT_LEN, zeroScalarProof.end(), 0);
    BOOST_CHECK(!curve25519VrfVerify(ref(publicKey), ref(message), ref(zeroScalarProof)));
}

BOOST_AUTO_TEST_CASE(secp256k1_golden_vectors)
{
    auto secret = fromHex("0000000000000000000000000000000000000000000000000000000000000001");
    auto message = fromHex("74657374");  // "test"
    auto publicKey = fromHex("0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798");
    auto proof = fromHex(
        "03e5e9a2b9b0c3ff9083a402169ab513782fd96c0ff9305afd2510c8ff1e3602fc7a0283dcf79755005a"
        "b93476d982c6fab05fac6a9b2b0f88957a6df34dd8885a664b9aba2093a11132475cf37f5c934de3cbb6"
        "c0abc07813891b3432784ed2de");
    auto proofHash = fromHex("ed99045774503ff28089e5d7572f800f0bc09dbed064fbf72d1da358c6c60758");

    BOOST_CHECK(secp256k1VrfDerivePublicKey(ref(secret)) == publicKey);
    // prove is deterministic: the output must equal the golden proof byte-for-byte
    BOOST_CHECK(secp256k1VrfProve(ref(secret), ref(message)) == proof);
    BOOST_CHECK(secp256k1VrfVerify(ref(publicKey), ref(message), ref(proof)));
    BOOST_CHECK(secp256k1VrfProofToHash(ref(proof)) == proofHash);
    BOOST_CHECK(secp256k1VrfIsValidPublicKey(ref(publicKey)));
}

BOOST_AUTO_TEST_CASE(secp256k1_negative_cases)
{
    auto secret = fromHex("0000000000000000000000000000000000000000000000000000000000000001");
    auto message = fromHex("74657374");
    auto publicKey = secp256k1VrfDerivePublicKey(ref(secret));
    auto proof = secp256k1VrfProve(ref(secret), ref(message));

    // tampered proof
    for (size_t pos : {size_t(0), size_t(50), size_t(96)})
    {
        auto tampered = proof;
        tampered[pos] ^= 0x01;
        BOOST_CHECK(!secp256k1VrfVerify(ref(publicKey), ref(message), ref(tampered)));
    }
    // wrong message
    BOOST_CHECK(!secp256k1VrfVerify(ref(publicKey), bytesConstRef{}, ref(proof)));
    // proof length must be exactly 97
    BOOST_CHECK(!secp256k1VrfVerify(ref(publicKey), ref(message), bytesConstRef{}));
    BOOST_CHECK(!secp256k1VrfVerify(
        ref(publicKey), ref(message), bytesConstRef(proof.data(), proof.size() - 1)));
    auto longProof = proof;
    longProof.push_back(0);
    BOOST_CHECK(!secp256k1VrfVerify(ref(publicKey), ref(message), ref(longProof)));
    // proof scalars >= n rejected
    auto order = fromHex("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141");
    for (size_t offset : {size_t(33), size_t(65)})
    {
        auto badProof = proof;
        std::copy(order.begin(), order.end(), badProof.begin() + offset);
        BOOST_CHECK(!secp256k1VrfVerify(ref(publicKey), ref(message), ref(badProof)));
    }
    // invalid secrets derive nothing
    BOOST_CHECK(secp256k1VrfDerivePublicKey(bytesConstRef{}).empty());
    BOOST_CHECK(secp256k1VrfDerivePublicKey(bytesConstRef(bytes(32, 0).data(), 32)).empty());
    BOOST_CHECK(secp256k1VrfDerivePublicKey(ref(order)).empty());
    BOOST_CHECK(secp256k1VrfProve(bytesConstRef{}, ref(message)).empty());
    // isValidPublicKey: identity/garbage rejected, uncompressed accepted
    BOOST_CHECK(!secp256k1VrfIsValidPublicKey(bytesConstRef{}));
    BOOST_CHECK(!secp256k1VrfIsValidPublicKey(bytesConstRef(bytes(33, 0).data(), 33)));
    auto uncompressed = fromHex(
        "0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655d"
        "a4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8");
    BOOST_CHECK(secp256k1VrfIsValidPublicKey(ref(uncompressed)));
    // proofToHash on malformed proofs
    BOOST_CHECK(secp256k1VrfProofToHash(bytesConstRef{}).empty());
    BOOST_CHECK(secp256k1VrfProofToHash(bytesConstRef(proof.data(), 96)).empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
