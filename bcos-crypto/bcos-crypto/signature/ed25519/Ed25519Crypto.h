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
 * @brief implementation for ed25519 signature algorithm
 * @file Ed25519Crypto.h
 * @date 2021.04.01
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/Signature.h>
namespace bcos
{
namespace crypto
{
const int ED25519_SIGNATURE_LEN = 64;

bytes ed25519Sign(
    const KeyPairInterface& _keyPair, const HashType& _messageHash, bool _signatureWithPub = false);
KeyPairInterface::UniquePtr ed25519GenerateKeyPair();
bool ed25519Verify(
    bytesConstRef _pubKeyBytes, const HashType& _messageHash, bytesConstRef _signatureData);
KeyImpl ed25519Recover(const HashType& _messageHash, bytesConstRef _signatureData);

std::pair<bool, bytes> ed25519Recover(Hash& _hashImpl, bytesConstRef _in);

class Ed25519Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<Ed25519Crypto>;
    using SignatureCrypto::verify;
    Ed25519Crypto() = default;
    ~Ed25519Crypto() override {}
    bytes sign(const KeyPairInterface& _keyPair, const HashType& _messageHash,
        bool _signatureWithPub = false) const override
    {
        return ed25519Sign(_keyPair, _messageHash, _signatureWithPub);
    }

    bool verify(bytesConstRef _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) const override;

    KeyImpl recover(const HashType& _messageHash, bytesConstRef _signatureData) const override
    {
        return ed25519Recover(_messageHash, _signatureData);
    }

    KeyPairInterface::UniquePtr generateKeyPair() const override
    {
        return ed25519GenerateKeyPair();
    }

    std::pair<bool, bytes> recoverAddress(Hash& _hashImpl, bytesConstRef _in) const override
    {
        return ed25519Recover(_hashImpl, _in);
    }

    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) const override;
    KeyPairInterface::UniquePtr createKeyPair(const Secret& _secretKey) const override;
};
}  // namespace crypto
}  // namespace bcos