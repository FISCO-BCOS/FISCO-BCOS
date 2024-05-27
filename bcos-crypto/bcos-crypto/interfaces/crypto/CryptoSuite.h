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
 * @brief crypto suite toolkit
 * @file CryptoSuite.h
 * @author: yujiechen
 * @date 2021-03-03
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-crypto/interfaces/crypto/Signature.h>
#include <bcos-crypto/interfaces/crypto/SymmetricEncryption.h>
#include <mutex>
#include <utility>

namespace bcos::crypto
{
class CryptoSuite
{
public:
    using Ptr = std::shared_ptr<CryptoSuite>;
    using UniquePtr = std::unique_ptr<CryptoSuite>;

    CryptoSuite(std::shared_ptr<Hash> _hashImpl, std::shared_ptr<SignatureCrypto> _signatureImpl,
        std::shared_ptr<SymmetricEncryption> _symmetricEncryptionHandler)
      : m_hashImpl(std::move(_hashImpl)),
        m_signatureImpl(std::move(_signatureImpl)),
        m_symmetricEncryptionHandler(std::move(_symmetricEncryptionHandler))
    {}
    virtual ~CryptoSuite() = default;
    Hash::Ptr hashImpl() { return m_hashImpl; }
    std::shared_ptr<SignatureCrypto> signatureImpl() { return m_signatureImpl; }
    std::shared_ptr<SymmetricEncryption> symmetricEncryptionHandler()
    {
        return m_symmetricEncryptionHandler;
    }

    template <typename T>
    HashType hash(T&& _data)
    {
        return m_hashImpl->hash(_data);
    }

    virtual Address calculateAddress(PublicPtr _public)
    {
        return right160(m_hashImpl->hash(_public));
    }

    virtual void setKeyFactory(KeyFactory::Ptr _keyFactory) { m_keyFactory = _keyFactory; }
    virtual std::shared_ptr<KeyFactory> keyFactory() { return m_keyFactory; }

private:
    Hash::Ptr m_hashImpl;
    SignatureCrypto::Ptr m_signatureImpl;
    SymmetricEncryption::Ptr m_symmetricEncryptionHandler;
    KeyFactory::Ptr m_keyFactory;
};
}  // namespace bcos::crypto
