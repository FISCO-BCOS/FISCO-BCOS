/**
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
 * @brief implementation for secp256k1 signature algorithm
 * @file Secp256k1Crypto.h
 * @date 2021.03.05
 * @author yujiechen
 */

#pragma once
#include <bcos-crypto/interfaces/crypto/Signature.h>

namespace bcos::crypto
{
constexpr uint16_t SECP256K1_SIGNATURE_R_LEN = 32;
constexpr uint16_t SECP256K1_SIGNATURE_S_LEN = 32;
constexpr int SECP256K1_SIGNATURE_LEN = 65;
constexpr int SECP256K1_UNCOMPRESS_PUBLICKEY_LEN = 65;
constexpr int SECP256K1_PUBLICKEY_LEN = 64;
constexpr int SECP256K1_SIGNATURE_V = 64;

// secp256k1 group order n and n/2 (boost-u256 form) — single home for the EIP-2 low-s gates
// (RPC decode in Web3Transaction.cpp; the P2P verify gate in Transaction.cpp lands with part
// 4b, PR #5477) so the two thresholds cannot drift apart.
inline const u256 c_secp256k1n(
    "0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
inline const u256 c_secp256k1nOver2 = c_secp256k1n / 2;

std::shared_ptr<bytes> secp256k1Sign(const KeyPairInterface& _keyPair, const HashType& _hash);
bool secp256k1Verify(const PublicPtr& _pubKey, const HashType& _hash, bytesConstRef _signatureData);
std::unique_ptr<KeyPairInterface> secp256k1GenerateKeyPair();

PublicPtr secp256k1Recover(const HashType& _hash, bytesConstRef _signatureData);
std::pair<bool, bytes> secp256k1Recover(Hash::Ptr _hashImpl, bytesConstRef _in);

class Secp256k1Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<Secp256k1Crypto>;
    Secp256k1Crypto() = default;
    ~Secp256k1Crypto() override = default;
    std::shared_ptr<bytes> sign(
        const KeyPairInterface& _keyPair, const HashType& _hash, bool) const override
    {
        return secp256k1Sign(_keyPair, _hash);
    }
    bool verify(
        PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) const override
    {
        return secp256k1Verify(_pubKey, _hash, _signatureData);
    }

    bool verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) const override;

    PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) const override
    {
        return secp256k1Recover(_hash, _signatureData);
    }
    std::unique_ptr<KeyPairInterface> generateKeyPair() const override
    {
        return secp256k1GenerateKeyPair();
    }

    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) const override
    {
        return secp256k1Recover(_hashImpl, _in);
    }
    std::pair<bool, bytes> recoverAddress(crypto::Hash& _hashImpl, const HashType& _hash,
        bytesConstRef _signatureData) const override;

    std::unique_ptr<KeyPairInterface> createKeyPair(SecretPtr _secretKey) const override;
};
}  // namespace bcos::crypto
