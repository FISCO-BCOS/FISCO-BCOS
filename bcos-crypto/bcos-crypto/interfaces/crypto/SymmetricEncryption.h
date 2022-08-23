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
 * @brief interfaces for Symmetric encryption
 * @file SymmetricEncryption.h
 * @author: yujiechen
 * @date 2021-03-03
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <memory>
#include <mutex>

namespace bcos
{
namespace crypto
{
class SymmetricEncryption
{
public:
    using Ptr = std::shared_ptr<SymmetricEncryption>;
    using UniquePtr = std::unique_ptr<SymmetricEncryption>;
    SymmetricEncryption() = default;
    virtual ~SymmetricEncryption() {}

    // symmetricEncrypt encrypts plain data with default ivData
    virtual bytesPointer symmetricEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
        const unsigned char* _key, size_t _keySize) = 0;
    // symmetricDecrypt encrypts plain data with default ivData
    virtual bytesPointer symmetricDecrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
        const unsigned char* _key, size_t _keySize) = 0;

    // symmetricEncrypt encrypts plain data with given ivData
    virtual bytesPointer symmetricEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
        const unsigned char* _key, size_t _keySize, const unsigned char* _ivData,
        size_t _ivDataSize) = 0;
    // symmetricDecrypt encrypts plain data with given ivData
    virtual bytesPointer symmetricDecrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
        const unsigned char* _key, size_t _keySize, const unsigned char* _ivData,
        size_t _ivDataSize) = 0;
};
}  // namespace crypto
}  // namespace bcos
