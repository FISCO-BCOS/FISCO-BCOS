/*
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
 * @file KeyPairBuilder.h
 * @author: octopus
 * @date 2022-01-13
 */
#pragma once

#include <bcos-cpp-sdk/utilities/crypto/Common.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-crypto/signature/key/KeyPair.h>
#include <bcos-utilities/Common.h>
#include <memory>
namespace bcos
{
namespace cppsdk
{
namespace utilities
{
class KeyPairBuilder
{
public:
    using Ptr = std::shared_ptr<KeyPairBuilder>;
    using ConstPtr = std::shared_ptr<const KeyPairBuilder>;

public:
    /**
     * @brief
     *
     * @param _cryptoType
     * @return bcos::crypto::KeyPair::UniquePtr
     */
    bcos::crypto::KeyPairInterface::UniquePtr genKeyPair(
        CryptoType _cryptoType, const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
    bcos::crypto::KeyPairInterface::UniquePtr genKeyPair(CryptoType _cryptoType,
        bytesConstRef _privateKey, const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
    bcos::crypto::KeyPairInterface::UniquePtr useHsmKeyPair(unsigned int _keyIndex,
        std::string _password, const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
};
}  // namespace utilities
}  // namespace cppsdk
}  // namespace bcos