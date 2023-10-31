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
 * @brief implementation for hsm sm2 signature
 * @file HsmSM2Crypto.h
 * @date 2022.11.04
 * @author lucasli
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyPairFactory.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2KeyPairFactory.h>

namespace bcos::crypto
{
const int HSM_SM2_SIGNATURE_LEN = 64;
const int HSM_SM3_DIGEST_LENGTH = 32;

class HsmSM2Crypto : public SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<HsmSM2Crypto>;
    HsmSM2Crypto(std::string _libPath)
    {
        m_hsmLibPath = _libPath;
        m_keyPairFactory = std::make_shared<HsmSM2KeyPairFactory>(m_hsmLibPath);
    }
    ~HsmSM2Crypto() override = default;

    std::shared_ptr<bytes> sign(const KeyPairInterface& _keyPair, const HashType& _hash,
        bool _signatureWithPub = false) const override;

    bool verify(
        PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) const override;
    bool verify(std::shared_ptr<bytes const> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) const override;

    PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) const override;
    std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) const override;

    KeyPairInterface::UniquePtr generateKeyPair() const override;
    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) const override;
    KeyPairInterface::UniquePtr createKeyPair(unsigned int _keyIndex, std::string _password);

private:
    KeyPairFactory::Ptr m_keyPairFactory;
    std::string m_hsmLibPath;
};
}  // namespace bcos::crypto