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
 * @file Signature.h
 * @author: lucasli
 * @date 2022-12-14
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
class Signature
{
public:
    using Ptr = std::shared_ptr<Signature>;
    using ConstPtr = std::shared_ptr<const Signature>;

public:
    std::shared_ptr<bcos::bytes> sign(const bcos::crypto::KeyPairInterface& _keyPair,
        const bcos::crypto::HashType& _hash,
        const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
    bool verify(CryptoType _cryptoType, std::shared_ptr<bcos::bytes const> _pubKeyBytes,
        const bcos::crypto::HashType& _hash, bytesConstRef _signatureData,
        const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
    bcos::crypto::PublicPtr recover(CryptoType _cryptoType, const bcos::crypto::HashType& _hash,
        bytesConstRef _signatureData,
        const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
    std::pair<bool, bcos::bytes> recoverAddress(CryptoType _cryptoType,
        bcos::crypto::Hash::Ptr _hashImpl, bytesConstRef _in,
        const std::string _hsmLibPath = "/usr/local/lib/libgmt0018.so");
};
}  // namespace utilities
}  // namespace cppsdk
}  // namespace bcos