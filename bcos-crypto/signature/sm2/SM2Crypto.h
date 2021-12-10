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
 * @brief implementation for sm2 signature
 * @file SM2Crypto.h
 * @date 2021.03.10
 * @author yujiechen
 */
#pragma once
#include <bcos-framework/interfaces/crypto/Signature.h>

namespace bcos
{
namespace crypto
{
const int SM2_SIGNATURE_LEN = 64;
std::shared_ptr<bytes> sm2Sign(
    KeyPairInterface::Ptr _keyPair, const HashType& _hash, bool _signatureWithPub = false);
bool sm2Verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData);
KeyPairInterface::Ptr sm2GenerateKeyPair();
PublicPtr sm2Recover(const HashType& _hash, bytesConstRef _signData);

std::pair<bool, bytes> sm2Recover(Hash::Ptr _hashImpl, bytesConstRef _in);

class SM2Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<SM2Crypto>;
    SM2Crypto() = default;
    ~SM2Crypto() override {}
    std::shared_ptr<bytes> sign(KeyPairInterface::Ptr _keyPair, const HashType& _hash,
        bool _signatureWithPub = false) override
    {
        return sm2Sign(_keyPair, _hash, _signatureWithPub);
    }

    bool verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) override
    {
        return sm2Verify(_pubKey, _hash, _signatureData);
    }

    bool verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) override;

    PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) override
    {
        return sm2Recover(_hash, _signatureData);
    }
    KeyPairInterface::Ptr generateKeyPair() override { return sm2GenerateKeyPair(); }

    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) override
    {
        return sm2Recover(_hashImpl, _in);
    }

    KeyPairInterface::Ptr createKeyPair(SecretPtr _secretKey) override;
};
}  // namespace crypto
}  // namespace bcos