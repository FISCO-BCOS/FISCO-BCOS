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
 * @brief implementation for ed25519 keyPair algorithm
 * @file Ed25519KeyPair.cpp
 * @date 2021.04.01
 * @author yujiechen
 */

#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/ed25519/Ed25519KeyPair.h>
#include <openssl/evp.h>
#include <memory>

using namespace bcos;
using namespace bcos::crypto;

Ed25519KeyPair::Ed25519KeyPair()
  : KeyPair(ED25519_PUBLIC_LEN, ED25519_PRIVATE_LEN, KeyPairType::Ed25519)
{}

Ed25519KeyPair::Ed25519KeyPair(SecretPtr _secretKey) : Ed25519KeyPair()
{
    m_secretKey = _secretKey;
    m_publicKey = priToPub(_secretKey);
    m_type = KeyPairType::Ed25519;
}

PublicPtr Ed25519KeyPair::priToPub(SecretPtr _secretKey)
{
    return ed25519PriToPub(_secretKey);
}

PublicPtr bcos::crypto::ed25519PriToPub(SecretPtr _secretKey)
{
    // 32-byte seed private key, public key is derived via Ed25519 scalar multiplication
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(
        EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
            reinterpret_cast<const unsigned char*>(_secretKey->constData()), _secretKey->size()),
        &EVP_PKEY_free);
    auto pub = std::make_shared<KeyImpl>(ED25519_PUBLIC_LEN);
    size_t publicKeyLen = pub->size();
    if (!key || EVP_PKEY_get_raw_public_key(key.get(),
                    reinterpret_cast<unsigned char*>(pub->mutableData()), &publicKeyLen) != 1)
    {
        BOOST_THROW_EXCEPTION(
            PriToPublicKeyException() << errinfo_comment("ed25519PriToPub exception"));
    }
    return pub;
}
