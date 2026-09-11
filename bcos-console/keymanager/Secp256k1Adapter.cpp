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
 * @brief: Secp256k1KeyPairFactoryAdapter implementation
 * @file: Secp256k1Adapter.cpp
 */

#include "KeyManager.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>

namespace bcos::console
{
Secp256k1KeyPairFactoryAdapter::Secp256k1KeyPairFactoryAdapter()
  : m_crypto(std::make_shared<::bcos::crypto::Secp256k1Crypto>())
{}

::bcos::crypto::KeyPairInterface::UniquePtr Secp256k1KeyPairFactoryAdapter::createKeyPair(
    ::bcos::crypto::SecretPtr _secretKey)
{
    return m_crypto->createKeyPair(std::move(_secretKey));
}

::bcos::crypto::KeyPairInterface::UniquePtr Secp256k1KeyPairFactoryAdapter::generateKeyPair()
{
    return m_crypto->generateKeyPair();
}
}  // namespace bcos::console
