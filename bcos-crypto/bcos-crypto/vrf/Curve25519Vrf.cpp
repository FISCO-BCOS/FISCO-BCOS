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
 * @brief implementation for the curve25519 (Ristretto255) VRF
 * @file Curve25519Vrf.cpp
 */
#include "Curve25519Vrf.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/random/CryptoRandom.h"
#include "bcos-crypto/utilities/ByteBufferUtils.h"
#include "bcos-crypto/zkp/ristretto/RistrettoGroup.h"
#include <openssl/evp.h>
#include <boost/throw_exception.hpp>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace bcos;
using namespace bcos::crypto;

namespace
{
// FIPS SHA3-512 (padding 0x06, NOT Keccak) via the OpenSSL EVP interface
std::array<std::uint8_t, 64> sha3_512(bytesConstRef _data)
{
    std::array<std::uint8_t, 64> digest{};
    if (EVP_Digest(_data.data(), _data.size(), digest.data(), nullptr, EVP_sha3_512(), nullptr) !=
        1) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"sha3_512: EVP_Digest error!"});
    }
    return digest;
}

// x = SHA3-512(secret) interpreted as a 64-byte little-endian integer reduced mod l
// (dalek Scalar::hash_from_bytes::<Sha3_512> = from_bytes_mod_order_wide)
RistrettoScalarBytes secretToScalar(bytesConstRef _secretKey)
{
    auto hash = sha3_512(_secretKey);
    return ristrettoScalarReduce(hash.data(), hash.size());
}

// H = ristretto255_from_hash(SHA3-512(data)); byte-identical to dalek
// RistrettoPoint::from_uniform_bytes
RistrettoPointBytes hashToPoint(bytesConstRef _data)
{
    auto hash = sha3_512(_data);
    return ristrettoFromHash(hash.data());
}
}  // namespace

bcos::bytes bcos::crypto::curve25519VrfDerivePublicKey(bytesConstRef _secretKey)
{
    auto x = secretToScalar(_secretKey);
    auto encoded = ristrettoScalarmultBase(x);
    return bcos::bytes(encoded.begin(), encoded.end());
}

bcos::bytes bcos::crypto::curve25519VrfProve(bytesConstRef _secretKey, bytesConstRef _input)
{
    auto publicKey = curve25519VrfDerivePublicKey(_secretKey);
    auto x = secretToScalar(_secretKey);
    auto hashVec = bytesConcat({refOf(publicKey), _input});
    auto pointH = hashToPoint(refOf(hashVec));

    // gamma = x·H
    auto gammaBytes = ristrettoScalarmult(x, pointH);

    // random nonce: 64 random bytes wide-reduced mod l, like dalek Scalar::random
    auto nonce = cryptoRandomBytes(64);
    auto k = ristrettoScalarReduce(nonce.data(), nonce.size());
    auto baseKBytes = ristrettoScalarmultBase(k);
    auto pointKBytes = ristrettoScalarmult(k, pointH);

    // c = wide-reduce(SHA3-512(hashVec ‖ pub ‖ gamma ‖ baseK ‖ pointK))
    auto challengeInput = bytesConcat({refOf(hashVec), refOf(publicKey), refOf(gammaBytes),
        refOf(baseKBytes), refOf(pointKBytes)});
    auto challengeHash = sha3_512(refOf(challengeInput));
    auto c = ristrettoScalarReduce(challengeHash.data(), challengeHash.size());

    // s = k − c·x (mod l)
    auto s = ristrettoScalarMulAdd(ristrettoScalarNegate(c), x, k);

    bcos::bytes proof;
    proof.reserve(CURVE25519_VRF_PROOF_SIZE);
    appendBytes(proof, gammaBytes);
    appendBytes(proof, c);
    appendBytes(proof, s);
    return proof;
}

bool bcos::crypto::curve25519VrfVerify(
    bytesConstRef _publicKey, bytesConstRef _input, bytesConstRef _proof)
{
    if (_publicKey.size() != CURVE25519_VRF_PUBLIC_KEY_SIZE ||
        _proof.size() != CURVE25519_VRF_PROOF_SIZE)
    {
        return false;
    }
    if (!ristrettoIsValidPoint(_publicKey.data()))
    {
        return false;
    }
    if (!ristrettoIsValidPoint(_proof.data()))
    {
        return false;
    }
    RistrettoPointBytes pubPoint;
    std::memcpy(pubPoint.data(), _publicKey.data(), RISTRETTO_POINT_LEN);
    RistrettoPointBytes gamma;
    std::memcpy(gamma.data(), _proof.data(), RISTRETTO_POINT_LEN);
    // c, s as little-endian scalars with bit 255 masked, never rejected (dalek from_bits).
    // NOT reduced: dalek compares the recomputed challenge against the raw masked bytes,
    // so c+l (bit 255 still clear) must be rejected even though it is arithmetically
    // equivalent to c — libsodium reduces internally in the scalar multiplications below.
    auto c = ristrettoScalarFromBitsRaw(_proof.data() + RISTRETTO_POINT_LEN);
    auto s = ristrettoScalarFromBitsRaw(_proof.data() + 2 * RISTRETTO_SCALAR_LEN);

    auto hashVec = bytesConcat({_publicKey, _input});
    auto pointH = hashToPoint(refOf(hashVec));

    // u = c·P + s·G1
    auto u = ristrettoAdd(ristrettoScalarmult(c, pubPoint), ristrettoScalarmultBase(s));
    // v = c·gamma + s·H
    auto v = ristrettoAdd(ristrettoScalarmult(c, gamma), ristrettoScalarmult(s, pointH));

    // c' = wide-reduce(SHA3-512(pub ‖ msg ‖ pub ‖ gamma(from proof) ‖ u ‖ v))
    auto challengeInput = bytesConcat({refOf(hashVec), _publicKey,
        bytesConstRef(_proof.data(), RISTRETTO_POINT_LEN), refOf(u), refOf(v)});
    auto challengeHash = sha3_512(refOf(challengeInput));
    auto cPrime = ristrettoScalarReduce(challengeHash.data(), challengeHash.size());
    // dalek compares Scalar by raw bytes: the reduced recomputed challenge must equal the
    // proof's masked-but-unreduced c byte-for-byte
    return std::memcmp(cPrime.data(), c.data(), RISTRETTO_SCALAR_LEN) == 0;
}

bcos::bytes bcos::crypto::curve25519VrfProofToHash(bytesConstRef _proof)
{
    if (_proof.size() != CURVE25519_VRF_PROOF_SIZE)
    {
        return {};
    }
    if (!ristrettoIsValidPoint(_proof.data()))
    {
        return {};
    }
    RistrettoPointBytes gamma;
    std::memcpy(gamma.data(), _proof.data(), RISTRETTO_POINT_LEN);
    // multiply gamma by the cofactor 8, then Keccak-256 of the encoding
    const RistrettoScalarBytes cofactor{8};
    auto encoded = ristrettoScalarmult(cofactor, gamma);
    auto hash = keccak256Hash(bytesConstRef(encoded.data(), encoded.size()));
    return bcos::bytes(hash.begin(), hash.end());
}

bool bcos::crypto::curve25519VrfIsValidPublicKey(bytesConstRef _publicKey)
{
    if (_publicKey.size() != CURVE25519_VRF_PUBLIC_KEY_SIZE)
    {
        return false;
    }
    return ristrettoIsValidPoint(_publicKey.data());
}
