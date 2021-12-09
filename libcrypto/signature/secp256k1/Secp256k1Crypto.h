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
#include <bcos-framework/interfaces/crypto/Signature.h>

namespace bcos
{
namespace crypto
{
const int SECP256K1_SIGNATURE_LEN = 65;
std::shared_ptr<bytes> secp256k1Sign(KeyPairInterface::Ptr _keyPair, const HashType& _hash);
bool secp256k1Verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData);
KeyPairInterface::Ptr secp256k1GenerateKeyPair();

PublicPtr secp256k1Recover(const HashType& _hash, bytesConstRef _signatureData);
std::pair<bool, bytes> secp256k1Recover(Hash::Ptr _hashImpl, bytesConstRef _in);

class Secp256k1Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<Secp256k1Crypto>;
    Secp256k1Crypto() = default;
    ~Secp256k1Crypto() override {}
    std::shared_ptr<bytes> sign(
        KeyPairInterface::Ptr _keyPair, const HashType& _hash, bool) override
    {
        return secp256k1Sign(_keyPair, _hash);
    }
    bool verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) override
    {
        return secp256k1Verify(_pubKey, _hash, _signatureData);
    }

    bool verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) override;

    PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) override
    {
        return secp256k1Recover(_hash, _signatureData);
    }
    KeyPairInterface::Ptr generateKeyPair() override { return secp256k1GenerateKeyPair(); }

    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) override
    {
        return secp256k1Recover(_hashImpl, _in);
    }

    KeyPairInterface::Ptr createKeyPair(SecretPtr _secretKey) override;
};
}  // namespace crypto
}  // namespace bcos
