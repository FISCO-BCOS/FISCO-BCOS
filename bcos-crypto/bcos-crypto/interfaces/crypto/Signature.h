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
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/key/KeyImpl.h>
#include <memory>
#include <mutex>
namespace bcos::crypto
{
class SignatureCrypto
{
public:
    using Ptr = std::shared_ptr<SignatureCrypto>;
    using UniquePtr = std::unique_ptr<SignatureCrypto>;
    SignatureCrypto() = default;
    virtual ~SignatureCrypto() = default;

    // sign returns a signature of a given hash
    virtual bytes sign(const KeyPairInterface& _keyPair, const HashType& _hash,
        bool _signatureWithPub = false) const = 0;

    // verify checks whether a signature is calculated from a given hash.
    // The Public& overload returns false for an incorrectly sized key, while bytesConstRef throws
    // InvalidKey. Secp256k1 also throws InvalidSignature for malformed signatures and recovery
    // failure; the other implementations return false for short signatures.
    bool verify(const Public& _pubKey, const HashType& _hash, bytesConstRef _signatureData) const
    {
        try
        {
            return verify(
                bytesConstRef(reinterpret_cast<const byte*>(_pubKey.constData()), _pubKey.size()),
                _hash, _signatureData);
        }
        catch (const InvalidKey&)
        {
            return false;
        }
    }
    virtual bool verify(
        bytesConstRef _pubKeyBytes, const HashType& _hash, bytesConstRef _signatureData) const = 0;

    // recover is intentionally closed to KeyImpl: SignatureCrypto does not support alternative
    // key representations, so interfaces/crypto depends on signature/key and
    // signature/Exceptions.h by design.
    virtual KeyImpl recover(const HashType& _hash, bytesConstRef _signatureData) const = 0;
    virtual bytes recoverAddress(
        crypto::Hash& _hashImpl, const HashType& _hash, bytesConstRef _signatureData) const
    {
        auto pubKey = recover(_hash, _signatureData);
        return calculateAddress(_hashImpl, pubKey).asBytes();
    }

    // generateKeyPair generates keyPair
    virtual std::unique_ptr<KeyPairInterface> generateKeyPair() const = 0;

    // recoverAddress recovers address from a signature(for precompiled)
    virtual std::pair<bool, bytes> recoverAddress(Hash& _hashImpl, bytesConstRef _in) const = 0;
    virtual std::unique_ptr<KeyPairInterface> createKeyPair(SecretPtr _secretKey) const = 0;
    std::unique_ptr<KeyPairInterface> createKeyPair(const Secret& _secretKey) const
    {
        return createKeyPair(std::make_shared<KeyImpl>(_secretKey.data()));
    }
};
}  // namespace bcos::crypto
