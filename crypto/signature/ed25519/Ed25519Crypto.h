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
#include <bcos-framework/interfaces/crypto/Signature.h>
namespace bcos
{
namespace crypto
{
const int ED25519_SIGNATURE_LEN = 64;

std::shared_ptr<bytes> ed25519Sign(
    KeyPairInterface::Ptr _keyPair, const HashType& _messageHash, bool _signatureWithPub = false);
KeyPairInterface::Ptr ed25519GenerateKeyPair();
bool ed25519Verify(PublicPtr _pubKey, const HashType& _messageHash, bytesConstRef _signatureData);
PublicPtr ed25519Recover(const HashType& _messageHash, bytesConstRef _signatureData);

std::pair<bool, bytes> ed25519Recover(Hash::Ptr _hashImpl, bytesConstRef _in);

class Ed25519Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<Ed25519Crypto>;
    Ed25519Crypto() = default;
    ~Ed25519Crypto() override {}
    std::shared_ptr<bytes> sign(KeyPairInterface::Ptr _keyPair, const HashType& _messageHash,
        bool _signatureWithPub = false) override
    {
        return ed25519Sign(_keyPair, _messageHash, _signatureWithPub);
    }

    bool verify(
        PublicPtr _pubKey, const HashType& _messageHash, bytesConstRef _signatureData) override
    {
        return ed25519Verify(_pubKey, _messageHash, _signatureData);
    }

    bool verify(std::shared_ptr<const bytes> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) override;

    PublicPtr recover(const HashType& _messageHash, bytesConstRef _signatureData) override
    {
        return ed25519Recover(_messageHash, _signatureData);
    }

    KeyPairInterface::Ptr generateKeyPair() override { return ed25519GenerateKeyPair(); }

    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) override
    {
        return ed25519Recover(_hashImpl, _in);
    }

    KeyPairInterface::Ptr createKeyPair(SecretPtr _secretKey) override;
};
}  // namespace crypto
}  // namespace bcos