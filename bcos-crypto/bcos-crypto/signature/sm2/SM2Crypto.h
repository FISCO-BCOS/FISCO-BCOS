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
#include <bcos-crypto/interfaces/crypto/KeyPairFactory.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/sm2/SM2KeyPairFactory.h>
#include <wedpr-crypto/WedprCrypto.h>

namespace bcos
{
namespace crypto
{
const int SM2_SIGNATURE_LEN = 64;
class SM2Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<SM2Crypto>;
    SM2Crypto()
    {
        m_signer = wedpr_sm2_sign_fast;
        m_verifier = wedpr_sm2_verify;
        m_keyPairFactory = std::make_shared<SM2KeyPairFactory>();
    }
    virtual ~SM2Crypto() {}
    std::shared_ptr<bytes> sign(const KeyPairInterface& _keyPair, const HashType& _hash,
        bool _signatureWithPub = false) override;

    bool verify(PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) override;

    bool verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) override;

    PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) override;
    KeyPairInterface::UniquePtr generateKeyPair() override;

    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) override;

    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) override;

protected:
    std::function<int8_t(const CInputBuffer* private_key, const CInputBuffer* public_key,
        const CInputBuffer* message_hash, COutputBuffer* output_signature)>
        m_signer;

    std::function<int8_t(const CInputBuffer* public_key, const CInputBuffer* message_hash,
        const CInputBuffer* signature)>
        m_verifier;
    KeyPairFactory::Ptr m_keyPairFactory;
};
}  // namespace crypto
}  // namespace bcos