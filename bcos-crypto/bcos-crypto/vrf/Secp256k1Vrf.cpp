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
 * @brief implementation for the secp256k1 VRF
 * @file Secp256k1Vrf.cpp
 */
#include "Secp256k1Vrf.h"
#include "bcos-crypto/hash/Sha256.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "bcos-crypto/utilities/ByteBufferUtils.h"
#include <secp256k1.h>
#include <array>
#include <cstring>
#include <memory>
#include <optional>

using namespace bcos;
using namespace bcos::crypto;

namespace
{
// secp256k1 group order n, big-endian
constexpr std::array<std::uint8_t, 32> GROUP_ORDER{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41};

using ScalarBE = std::array<std::uint8_t, 32>;

bool isZero(const ScalarBE& _scalar)
{
    std::uint8_t acc = 0;
    for (auto byte : _scalar)
    {
        acc |= byte;
    }
    return acc == 0;
}

// big-endian comparison against n
bool scalarBelowOrder(const std::uint8_t* _scalar)
{
    return std::memcmp(_scalar, GROUP_ORDER.data(), GROUP_ORDER.size()) < 0;
}

// 0 < scalar < n
bool isValidSecretScalar(const std::uint8_t* _scalar)
{
    return secp256k1_ec_seckey_verify(secp256k1Context(), _scalar) == 1;
}

// SHA-256(data) as a big-endian scalar; nullopt when the digest is >= n
std::optional<ScalarBE> hashToScalar(bytesConstRef _data)
{
    auto hash = sha256Hash(_data);
    ScalarBE scalar;
    std::memcpy(scalar.data(), hash.data(), scalar.size());
    if (!scalarBelowOrder(scalar.data()))
    {
        return std::nullopt;
    }
    return scalar;
}

bool parsePublicKey(bytesConstRef _encoded, secp256k1_pubkey* _point)
{
    if (_encoded.empty())
    {
        return false;
    }
    return secp256k1_ec_pubkey_parse(
               secp256k1Context(), _point, _encoded.data(), _encoded.size()) == 1;
}

bcos::bytes serializeCompressed(const secp256k1_pubkey& _point)
{
    bcos::bytes encoded(SECP256K1_VRF_PUBLIC_KEY_SIZE);
    size_t encodedSize = encoded.size();
    secp256k1_ec_pubkey_serialize(
        secp256k1Context(), encoded.data(), &encodedSize, &_point, SECP256K1_EC_COMPRESSED);
    encoded.resize(encodedSize);
    return encoded;
}

// point = scalar·point; zero scalars are rejected by libsecp256k1
bool tweakMul(secp256k1_pubkey* _point, const std::uint8_t* _scalar)
{
    return secp256k1_ec_pubkey_tweak_mul(secp256k1Context(), _point, _scalar) == 1;
}

// point = point + addend (the combine API zeroes its output before loading the inputs,
// so the result must go through a temporary — output must not alias an input)
bool combine(secp256k1_pubkey* _point, const secp256k1_pubkey& _addend)
{
    secp256k1_pubkey combined;
    const secp256k1_pubkey* points[] = {_point, &_addend};
    if (secp256k1_ec_pubkey_combine(secp256k1Context(), &combined, points, 2) == 0)
    {
        return false;
    }
    *_point = combined;
    return true;
}
}  // namespace

bcos::bytes bcos::crypto::secp256k1VrfDerivePublicKey(bytesConstRef _secretKey)
{
    if (_secretKey.size() != SECP256K1_VRF_SECRET_KEY_SIZE ||
        !isValidSecretScalar(_secretKey.data()))
    {
        return {};
    }
    secp256k1_pubkey point;
    if (secp256k1_ec_pubkey_create(secp256k1Context(), &point, _secretKey.data()) == 0) [[unlikely]]
    {
        return {};
    }
    return serializeCompressed(point);
}

bcos::bytes bcos::crypto::secp256k1VrfProve(bytesConstRef _secretKey, bytesConstRef _input)
{
    if (_secretKey.size() != SECP256K1_VRF_SECRET_KEY_SIZE ||
        !isValidSecretScalar(_secretKey.data()))
    {
        return {};
    }
    auto publicKey = secp256k1VrfDerivePublicKey(_secretKey);

    // H = hash_to_scalar(pub ‖ msg)·G
    auto hashVec = bytesConcat({refOf(publicKey), _input});
    auto h = hashToScalar(refOf(hashVec));
    if (!h) [[unlikely]]
    {
        return {};
    }
    secp256k1_pubkey pointH;
    if (secp256k1_ec_pubkey_create(secp256k1Context(), &pointH, h->data()) == 0) [[unlikely]]
    {
        return {};
    }
    // gamma = x·H
    secp256k1_pubkey gamma = pointH;
    if (!tweakMul(&gamma, _secretKey.data())) [[unlikely]]
    {
        return {};
    }
    // Message-bound nonce k = SHA-256(secret ‖ SHA-256(input)): wedprcrypto's
    // message-independent k = SHA-256(secret) made two proofs under one key leak the secret
    // via s1 − s2 = (c1 − c2)·x mod n. Prover-side only: gamma and proofToHash are
    // independent of k and verification accepts any valid (c, s), so no consensus-visible
    // value changes.
    auto inputHash = sha256Hash(_input);
    auto nonceInput = bytesConcat({_secretKey, inputHash.ref()});
    ScalarBE k;
    std::memcpy(k.data(), sha256Hash(refOf(nonceInput)).data(), k.size());
    // resample with an appended counter on the vanishingly unlikely invalid scalar
    for (std::uint8_t counter = 0; !isValidSecretScalar(k.data()); ++counter)
    {
        auto retryInput = bytesConcat({refOf(nonceInput), bytesConstRef(&counter, 1)});
        std::memcpy(k.data(), sha256Hash(refOf(retryInput)).data(), k.size());
    }
    secp256k1_pubkey pointK;
    if (secp256k1_ec_pubkey_create(secp256k1Context(), &pointK, k.data()) == 0) [[unlikely]]
    {
        return {};
    }
    secp256k1_pubkey pointKH = pointH;
    if (!tweakMul(&pointKH, k.data())) [[unlikely]]
    {
        return {};
    }
    // c = hash_to_scalar(comp(H) ‖ comp(gamma) ‖ comp(k·G) ‖ comp(k·H))
    auto encodedH = serializeCompressed(pointH);
    auto encodedGamma = serializeCompressed(gamma);
    auto encodedK = serializeCompressed(pointK);
    auto encodedKH = serializeCompressed(pointKH);
    auto challengeInput =
        bytesConcat({refOf(encodedH), refOf(encodedGamma), refOf(encodedK), refOf(encodedKH)});
    auto c = hashToScalar(refOf(challengeInput));
    if (!c) [[unlikely]]
    {
        return {};
    }
    // s = k + c·x (mod n); scalar arithmetic via the libsecp256k1 seckey tweak operations
    ScalarBE s;
    if (isZero(*c))
    {
        s = k;
    }
    else
    {
        std::memcpy(s.data(), _secretKey.data(), s.size());
        if (secp256k1_ec_seckey_tweak_mul(secp256k1Context(), s.data(), c->data()) == 0 ||
            secp256k1_ec_seckey_tweak_add(secp256k1Context(), s.data(), k.data()) == 0) [[unlikely]]
        {
            return {};
        }
    }
    bcos::bytes proof;
    proof.reserve(SECP256K1_VRF_PROOF_SIZE);
    proof.insert(proof.end(), encodedGamma.begin(), encodedGamma.end());
    proof.insert(proof.end(), c->begin(), c->end());
    proof.insert(proof.end(), s.begin(), s.end());
    return proof;
}

bool bcos::crypto::secp256k1VrfVerify(
    bytesConstRef _publicKey, bytesConstRef _input, bytesConstRef _proof)
{
    if (_proof.size() != SECP256K1_VRF_PROOF_SIZE)
    {
        return false;
    }
    secp256k1_pubkey pubPoint;
    if (!parsePublicKey(_publicKey, &pubPoint))
    {
        return false;
    }
    secp256k1_pubkey gamma;
    if (!parsePublicKey(bytesConstRef(_proof.data(), SECP256K1_VRF_PUBLIC_KEY_SIZE), &gamma))
    {
        return false;
    }
    const auto* cBytes = _proof.data() + SECP256K1_VRF_PUBLIC_KEY_SIZE;
    const auto* sBytes = cBytes + 32;
    // proof scalars >= n are rejected
    if (!scalarBelowOrder(cBytes) || !scalarBelowOrder(sBytes))
    {
        return false;
    }
    auto hashVec = bytesConcat({_publicKey, _input});
    auto h = hashToScalar(refOf(hashVec));
    if (!h)
    {
        return false;
    }
    secp256k1_pubkey pointH;
    if (secp256k1_ec_pubkey_create(secp256k1Context(), &pointH, h->data()) == 0)
    {
        return false;
    }
    ScalarBE c, s;
    std::memcpy(c.data(), cBytes, c.size());
    std::memcpy(s.data(), sBytes, s.size());
    bool const cZero = isZero(c);

    // u = s·G − c·P
    secp256k1_pubkey u;
    if (secp256k1_ec_pubkey_create(secp256k1Context(), &u, s.data()) == 0)
    {
        return false;
    }
    if (!cZero)
    {
        secp256k1_pubkey cP = pubPoint;
        if (!tweakMul(&cP, c.data()) || secp256k1_ec_pubkey_negate(secp256k1Context(), &cP) == 0 ||
            !combine(&u, cP))
        {
            return false;
        }
    }
    // v = s·H − c·gamma
    secp256k1_pubkey v = pointH;
    if (!tweakMul(&v, s.data()))
    {
        return false;
    }
    if (!cZero)
    {
        secp256k1_pubkey cGamma = gamma;
        if (!tweakMul(&cGamma, c.data()) ||
            secp256k1_ec_pubkey_negate(secp256k1Context(), &cGamma) == 0 || !combine(&v, cGamma))
        {
            return false;
        }
    }
    // c' = hash_to_scalar(comp(H) ‖ gamma(from proof) ‖ comp(u) ‖ comp(v))
    auto encodedH = serializeCompressed(pointH);
    auto encodedU = serializeCompressed(u);
    auto encodedV = serializeCompressed(v);
    auto challengeInput =
        bytesConcat({refOf(encodedH), bytesConstRef(_proof.data(), SECP256K1_VRF_PUBLIC_KEY_SIZE),
            refOf(encodedU), refOf(encodedV)});
    auto cPrime = hashToScalar(refOf(challengeInput));
    if (!cPrime)
    {
        return false;
    }
    return std::memcmp(cPrime->data(), c.data(), c.size()) == 0;
}

bcos::bytes bcos::crypto::secp256k1VrfProofToHash(bytesConstRef _proof)
{
    if (_proof.size() != SECP256K1_VRF_PROOF_SIZE)
    {
        return {};
    }
    secp256k1_pubkey gamma;
    if (!parsePublicKey(bytesConstRef(_proof.data(), SECP256K1_VRF_PUBLIC_KEY_SIZE), &gamma))
    {
        return {};
    }
    // multiply gamma by the cofactor 8, then SHA-256 of the compressed encoding
    const ScalarBE cofactor{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 8};
    if (!tweakMul(&gamma, cofactor.data())) [[unlikely]]
    {
        return {};
    }
    auto encoded = serializeCompressed(gamma);
    auto hash = sha256Hash(bytesConstRef(encoded.data(), encoded.size()));
    return bcos::bytes(hash.begin(), hash.end());
}

bool bcos::crypto::secp256k1VrfIsValidPublicKey(bytesConstRef _publicKey)
{
    secp256k1_pubkey point;
    return parsePublicKey(_publicKey, &point);
}
