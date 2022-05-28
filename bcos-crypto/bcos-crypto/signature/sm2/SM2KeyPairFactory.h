/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief implementation for sm2 keyPairFactory algorithm
 * @file SM2KeyPairFactory.h
 * @date 2022.01.17
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyPairFactory.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>
#include <wedpr-crypto/WedprCrypto.h>
#include <memory>
namespace bcos
{
namespace crypto
{
class SM2KeyPairFactory : public KeyPairFactory
{
public:
    using Ptr = std::shared_ptr<SM2KeyPairFactory>;
    SM2KeyPairFactory() { m_keyPairGenerator = wedpr_sm2_gen_key_pair; }
    ~SM2KeyPairFactory() override {}

    virtual KeyPairInterface::UniquePtr createKeyPair() { return std::make_unique<SM2KeyPair>(); }
    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) override
    {
        return std::make_unique<SM2KeyPair>(_secretKey);
    }

    KeyPairInterface::UniquePtr generateKeyPair() override
    {
        auto keyPair = createKeyPair();
        COutputBuffer publicKey{keyPair->publicKey()->mutableData(), keyPair->publicKey()->size()};
        COutputBuffer privateKey{keyPair->secretKey()->mutableData(), keyPair->secretKey()->size()};
        auto retCode = m_keyPairGenerator(&publicKey, &privateKey);
        if (retCode != WEDPR_SUCCESS)
        {
            BOOST_THROW_EXCEPTION(
                GenerateKeyPairException() << errinfo_comment("generateKeyPair exception"));
        }
        return keyPair;
    }

protected:
    std::function<int8_t(COutputBuffer* output_private_key, COutputBuffer* output_public_key)>
        m_keyPairGenerator;
};
}  // namespace crypto
}  // namespace bcos