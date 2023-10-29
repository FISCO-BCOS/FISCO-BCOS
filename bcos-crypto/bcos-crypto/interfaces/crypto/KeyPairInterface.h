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
 * @brief interfaces for KeyPair
 * @file KeyPairInterface.h
 * @author: yujiechen
 * @date 2021-04-02
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <cstddef>
#include <memory>
namespace bcos::crypto
{
enum class KeyPairType : int
{
    Secp256K1 = 0,
    SM2 = 1,
    Ed25519 = 2,
    HsmSM2 = 3
};
class KeyPairInterface
{
public:
    using Ptr = std::shared_ptr<KeyPairInterface>;
    using UniquePtr = std::unique_ptr<KeyPairInterface>;

    KeyPairInterface() = default;
    KeyPairInterface(const KeyPairInterface&) = default;
    KeyPairInterface(KeyPairInterface&&) = delete;
    KeyPairInterface& operator=(const KeyPairInterface&) = default;
    KeyPairInterface& operator=(KeyPairInterface&&) = delete;
    virtual ~KeyPairInterface() = default;

    virtual SecretPtr secretKey() const = 0;
    virtual PublicPtr publicKey() const = 0;
    virtual Address address(Hash::Ptr _hashImpl) = 0;
    virtual KeyPairType keyPairType() const = 0;
};

Address inline calculateAddress(Hash::Ptr _hashImpl, PublicPtr _publicKey)
{
    return right160(_hashImpl->hash(_publicKey));
}

Address inline calculateAddress(crypto::Hash& _hashImpl, PublicPtr _publicKey)
{
    return right160(_hashImpl.hash(_publicKey));
}

bytes inline calculateAddress(crypto::Hash& _hashImpl, uint8_t* _publicKey, size_t _len)
{
    auto address = _hashImpl.hash(bytesConstRef(_publicKey, _len));
    return {address.begin() + 12, address.end()};
}

}  // namespace bcos::crypto
