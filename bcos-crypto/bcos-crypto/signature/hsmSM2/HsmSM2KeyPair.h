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
 * @brief implementation for hsm SM2 KeyPair
 * @file HsmSM2KeyPair.h
 * @date 2022.11.04
 * @author lucasli
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/signature/key/KeyPair.h>
#include <wedpr-crypto/WedprCrypto.h>

namespace bcos
{
namespace crypto
{
const int HSM_SM2_PRIVATE_KEY_LEN = 32;
const int HSM_SM2_PUBLIC_KEY_LEN = 64;
const int SDF_KEY_LEN = 256;

class HsmSM2KeyPair : public KeyPair
{
public:
    using Ptr = std::shared_ptr<HsmSM2KeyPair>;
    HsmSM2KeyPair(std::string _libPath)
      : KeyPair(HSM_SM2_PUBLIC_KEY_LEN, HSM_SM2_PRIVATE_KEY_LEN, KeyPairType::HsmSM2)
    {
        m_hsmLibPath = _libPath;
        m_publicKeyDeriver = wedpr_sm2_derive_public_key;
    }
    HsmSM2KeyPair(std::string _libPath, SecretPtr _secretKey);
    HsmSM2KeyPair(std::string _libPath, unsigned int _keyIndex, std::string _password);
    ~HsmSM2KeyPair() override {}

    PublicPtr priToPub(SecretPtr _secretKey);
    bool isInternalKey() const { return m_isInternalKey; }

    const std::string& hsmLibPath() const { return m_hsmLibPath; }
    void setHsmLibPath(std::string _libPath) { m_hsmLibPath = _libPath; }
    unsigned int keyIndex() const { return m_keyIndex; }
    void setKeyIndex(unsigned int _keyIndex)
    {
        m_keyIndex = _keyIndex;
        m_isInternalKey = true;
    }
    const std::string& password() const { return m_password; }
    void setPassword(std::string _password)
    {
        m_password = _password;
        m_isInternalKey = true;
    }

private:
    std::function<int8_t(const CInputBuffer* private_key, COutputBuffer* output_public_key)>
        m_publicKeyDeriver;
    bool m_isInternalKey = false;
    std::string m_hsmLibPath;
    unsigned int m_keyIndex;
    std::string m_password;
};
}  // namespace crypto
}  // namespace bcos