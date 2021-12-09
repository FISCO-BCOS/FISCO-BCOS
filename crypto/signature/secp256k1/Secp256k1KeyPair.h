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
 * @brief implementation for secp256k1 KeyPair
 * @file Secp256k1KeyPair.h
 * @date 2021.03.05
 * @author yujiechen
 */
#pragma once
#include "../key/KeyPair.h"
#include <bcos-framework/interfaces/crypto/Signature.h>

namespace bcos
{
namespace crypto
{
const int SECP256K1_PUBLIC_LEN = 64;
const int SECP256K1_PRIVATE_LEN = 32;

PublicPtr secp256k1PriToPub(SecretPtr _secret);
class Secp256k1KeyPair : public KeyPair
{
public:
    using Ptr = std::shared_ptr<Secp256k1KeyPair>;
    Secp256k1KeyPair() : KeyPair(SECP256K1_PUBLIC_LEN, SECP256K1_PRIVATE_LEN) {}
    explicit Secp256k1KeyPair(SecretPtr _secretKey) : Secp256k1KeyPair()
    {
        m_secretKey = _secretKey;
        m_publicKey = priToPub(_secretKey);
    }
    virtual PublicPtr priToPub(SecretPtr _secret) { return secp256k1PriToPub(_secret); }
};
}  // namespace crypto
}  // namespace bcos
