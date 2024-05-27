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
 * @brief implementation for hsm sm2 keyPairFactory algorithm
 * @file HsmSM2KeyPairFactory.h
 * @date 2022.11.04
 * @author lucasli
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyPairFactory.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2KeyPair.h>
#include <memory>
namespace bcos
{
namespace crypto
{
class HsmSM2KeyPairFactory : public KeyPairFactory
{
public:
    using Ptr = std::shared_ptr<HsmSM2KeyPairFactory>;
    HsmSM2KeyPairFactory(std::string _libPath)
    {
        m_hsmLibPath = _libPath;
        CRYPTO_LOG(INFO) << "[HsmSM2KeyPairFactory::HsmSM2KeyPairFactory]"
                         << LOG_KV("_libPath", _libPath) << LOG_KV("lib_path", m_hsmLibPath);
    }
    ~HsmSM2KeyPairFactory() override {}

    void setHsmLibPath(std::string _libPath) { m_hsmLibPath = _libPath; }

    KeyPairInterface::UniquePtr createKeyPair(unsigned int _keyIndex, std::string _password)
    {
        return std::make_unique<HsmSM2KeyPair>(m_hsmLibPath, _keyIndex, _password);
    }

    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) override
    {
        return std::make_unique<HsmSM2KeyPair>(m_hsmLibPath, _secretKey);
    }

    KeyPairInterface::UniquePtr generateKeyPair() override
    {
        return std::make_unique<HsmSM2KeyPair>(m_hsmLibPath);
    }

private:
    std::string m_hsmLibPath;
};
}  // namespace crypto
}  // namespace bcos