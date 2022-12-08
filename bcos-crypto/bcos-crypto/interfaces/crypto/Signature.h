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
 * @brief interfaces for Signature
 * @file Signature.h
 * @author: yujiechen
 * @date 2021-03-03
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <memory>
#include <mutex>
namespace bcos
{
namespace crypto
{
class SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<SignatureCrypto>;
    using UniquePtr = std::unique_ptr<SignatureCrypto>;
    SignatureCrypto() = default;
    virtual ~SignatureCrypto() = default;

    // sign returns a signature of a given hash
    virtual std::shared_ptr<bytes> sign(const KeyPairInterface& _keyPair, const HashType& _hash,
        bool _signatureWithPub = false) const = 0;

    // verify checks whether a signature is calculated from a given hash
    virtual bool verify(
        PublicPtr _pubKey, const HashType& _hash, bytesConstRef _signatureData) const = 0;
    virtual bool verify(std::shared_ptr<const bytes> _pubKeyBytes, const HashType& _hash,
        bytesConstRef _signatureData) const = 0;

    // recover recovers the public key from the given signature
    virtual PublicPtr recover(const HashType& _hash, bytesConstRef _signatureData) const = 0;

    // generateKeyPair generates keyPair
    virtual KeyPairInterface::UniquePtr generateKeyPair() const = 0;

    // recoverAddress recovers address from a signature(for precompiled)
    virtual std::pair<bool, bytes> recoverAddress(Hash::Ptr _hashImpl, bytesConstRef _in) const = 0;

    virtual KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) const = 0;
};
}  // namespace crypto
}  // namespace bcos
