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
 * @file Ed25519KeyPair.h
 * @date 2021.04.01
 * @author yujiechen
 */
#pragma once
#include "../key/KeyPair.h"
#include <bcos-framework/interfaces/crypto/Signature.h>

namespace bcos
{
namespace crypto
{
const int ED25519_PUBLIC_LEN = 32;
const int ED25519_PRIVATE_LEN = 32;

PublicPtr ed25519PriToPub(SecretPtr _secretKey);
class Ed25519KeyPair : public KeyPair
{
public:
    Ed25519KeyPair() : KeyPair(ED25519_PUBLIC_LEN, ED25519_PRIVATE_LEN) {}
    explicit Ed25519KeyPair(SecretPtr _secretKey) : Ed25519KeyPair()
    {
        m_secretKey = _secretKey;
        m_publicKey = priToPub(_secretKey);
    }
    ~Ed25519KeyPair() override {}
    virtual PublicPtr priToPub(SecretPtr _secretKey) { return ed25519PriToPub(_secretKey); }
};
}  // namespace crypto
}  // namespace bcos