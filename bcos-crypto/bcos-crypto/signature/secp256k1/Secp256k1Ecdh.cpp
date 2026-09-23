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
 * @file Secp256k1Ecdh.cpp
 * @brief secp256k1 ECDH implementation (libsecp256k1 ecdh module).
 * @date 2026/8/18
 */
#include "Secp256k1Ecdh.h"

#include <secp256k1.h>
#include <secp256k1_ecdh.h>
#include <array>
#include <cstring>
#include <stdexcept>

namespace bcos::crypto
{
namespace
{
secp256k1_context* secp256k1EcdhContext()
{
    static secp256k1_context* ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    return ctx;
}

// Degenerate hash that copies the x-coordinate — the RLPx ECIES shared secret
// (matches geth's ecies.GenerateShared and silkworm's ecdh_hash_function_copy_x).
int ecdhHashFunctionCopyX(
    unsigned char* _output, const unsigned char* _x32, const unsigned char*, void*)
{
    memcpy(_output, _x32, 32);
    return 1;
}

bytes secp256k1EcdhImpl(
    bytesConstRef _publicKey, bytesConstRef _privateKey, secp256k1_ecdh_hash_function _hashFunction)
{
    if (_publicKey.size() != 64)
    {
        throw std::invalid_argument("secp256k1Ecdh: public key must be 64 bytes");
    }
    if (_privateKey.size() != 32)
    {
        throw std::invalid_argument("secp256k1Ecdh: private key must be 32 bytes");
    }

    // The FISCO convention stores the uncompressed point without the 0x04 prefix.
    std::array<unsigned char, 65> rawPublicKey{};
    rawPublicKey[0] = 0x04;
    memcpy(rawPublicKey.data() + 1, _publicKey.data(), 64);

    secp256k1_pubkey publicKey;
    if (secp256k1_ec_pubkey_parse(
            secp256k1EcdhContext(), &publicKey, rawPublicKey.data(), rawPublicKey.size()) != 1)
    {
        throw std::invalid_argument("secp256k1Ecdh: failed to parse public key");
    }

    bytes sharedSecret(32, 0);
    if (secp256k1_ecdh(secp256k1EcdhContext(), sharedSecret.data(), &publicKey,
            _privateKey.data(), _hashFunction, nullptr) != 1)
    {
        throw std::runtime_error("secp256k1Ecdh: ECDH computation failed");
    }
    return sharedSecret;
}
}  // namespace

bytes secp256k1EcdhCopyX(bytesConstRef _publicKey, bytesConstRef _privateKey)
{
    return secp256k1EcdhImpl(_publicKey, _privateKey, ecdhHashFunctionCopyX);
}

bytes secp256k1EcdhSha256(bytesConstRef _publicKey, bytesConstRef _privateKey)
{
    // A null hash function selects libsecp256k1's default SHA-256 KDF.
    return secp256k1EcdhImpl(_publicKey, _privateKey, nullptr);
}
}  // namespace bcos::crypto
