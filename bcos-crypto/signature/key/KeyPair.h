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
 * @brief Basic implementation of KeyPairInterface
 * @file KeyPair.h
 * @date 2021.4.2
 * @author yujiechen
 */
#pragma once
#include "KeyImpl.h"
#include <bcos-framework/interfaces/crypto/KeyPairInterface.h>
#include <bcos-framework/libutilities/FixedBytes.h>

namespace bcos
{
namespace crypto
{
Address inline calculateAddress(Hash::Ptr _hashImpl, PublicPtr _publicKey)
{
    return right160(_hashImpl->hash(_publicKey));
}
class KeyPair : public KeyPairInterface
{
public:
    using Ptr = std::shared_ptr<KeyPair>;
    KeyPair(int _pubKeyLen, int _secretLen)
      : m_publicKey(std::make_shared<KeyImpl>(_pubKeyLen)),
        m_secretKey(std::make_shared<KeyImpl>(_secretLen))
    {}

    KeyPair(Public const& _publicKey, Secret const& _secretKey)
      : m_publicKey(std::make_shared<KeyImpl>(_publicKey.data())),
        m_secretKey(std::make_shared<KeyImpl>(_secretKey.data()))
    {}

    ~KeyPair() override {}
    SecretPtr secretKey() const override { return m_secretKey; }
    PublicPtr publicKey() const override { return m_publicKey; }

    Address address(Hash::Ptr _hashImpl) override
    {
        return calculateAddress(_hashImpl, m_publicKey);
    }

protected:
    PublicPtr m_publicKey;
    SecretPtr m_secretKey;
};
}  // namespace crypto
}  // namespace bcos