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

bcos::bytes concat(std::initializer_list<bytesConstRef> _parts)
{
    bcos::bytes out;
    size_t total = 0;
    for (auto const& part : _parts)
    {
        total += part.size();
    }
    out.reserve(total);
    for (auto const& part : _parts)
    {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

bytesConstRef refOf(bcos::bytes const& _data)
{
    return bytesConstRef(_data.data(), _data.size());
}

template <size_t N>
bytesConstRef refOf(std::array<std::uint8_t, N> const& _data)
{
    return bytesConstRef(_data.data(), N);
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

void appendPoint(bcos::bytes& _buffer, const RistrettoPointBytes& _point)
{
    _buffer.insert(_buffer.end(), _point.begin(), _point.end());
}

void appendScalar(bcos::bytes& _buffer, const RistrettoScalarBytes& _scalar)
{
    _buffer.insert(_buffer.end(), _scalar.begin(), _scalar.end());
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
    auto hashVec = concat({refOf(publicKey), _input});
    auto pointH = hashToPoint(refOf(hashVec));

    // gamma = x·H
    auto gammaBytes = ristrettoScalarmult(x, pointH);

    // random nonce: 64 random bytes wide-reduced mod l, like dalek Scalar::random
    auto nonce = cryptoRandomBytes(64);
    auto k = ristrettoScalarReduce(nonce.data(), nonce.size());
    auto baseKBytes = ristrettoScalarmultBase(k);
    auto pointKBytes = ristrettoScalarmult(k, pointH);

    // c = wide-reduce(SHA3-512(hashVec ‖ pub ‖ gamma ‖ baseK ‖ pointK))
    auto challengeInput = concat({refOf(hashVec), refOf(publicKey), refOf(gammaBytes),
        refOf(baseKBytes), refOf(pointKBytes)});
    auto challengeHash = sha3_512(refOf(challengeInput));
    auto c = ristrettoScalarReduce(challengeHash.data(), challengeHash.size());

    // s = k − c·x (mod l)
    auto s = ristrettoScalarMulAdd(ristrettoScalarNegate(c), x, k);

    bcos::bytes proof;
    proof.reserve(CURVE25519_VRF_PROOF_SIZE);
    appendPoint(proof, gammaBytes);
    appendScalar(proof, c);
    appendScalar(proof, s);
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
    // c, s as little-endian scalars with bit 255 masked, never rejected (dalek from_bits)
    auto c = ristrettoScalarFromBits(_proof.data() + RISTRETTO_POINT_LEN);
    auto s = ristrettoScalarFromBits(_proof.data() + 2 * RISTRETTO_SCALAR_LEN);

    auto hashVec = concat({_publicKey, _input});
    auto pointH = hashToPoint(refOf(hashVec));

    // u = c·P + s·G1
    auto u = ristrettoAdd(ristrettoScalarmult(c, pubPoint), ristrettoScalarmultBase(s));
    // v = c·gamma + s·H
    auto v = ristrettoAdd(ristrettoScalarmult(c, gamma), ristrettoScalarmult(s, pointH));

    // c' = wide-reduce(SHA3-512(pub ‖ msg ‖ pub ‖ gamma(from proof) ‖ u ‖ v))
    auto challengeInput = concat({refOf(hashVec), _publicKey,
        bytesConstRef(_proof.data(), RISTRETTO_POINT_LEN), refOf(u), refOf(v)});
    auto challengeHash = sha3_512(refOf(challengeInput));
    auto cPrime = ristrettoScalarReduce(challengeHash.data(), challengeHash.size());
    return ristrettoEqual(cPrime, c.data());
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
