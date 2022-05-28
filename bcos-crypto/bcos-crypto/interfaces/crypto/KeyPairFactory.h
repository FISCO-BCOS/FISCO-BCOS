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
 * @file KeyPairFactory.h
 * @author: yujiechen
 * @date 2022-01-18
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <bcos-utilities/Common.h>
namespace bcos
{
namespace crypto
{
class KeyPairFactory
{
public:
    using Ptr = std::shared_ptr<KeyPairFactory>;
    KeyPairFactory() = default;
    virtual ~KeyPairFactory() {}
    virtual KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) = 0;
    virtual KeyPairInterface::UniquePtr generateKeyPair() = 0;
};
}  // namespace crypto
}  // namespace bcos
