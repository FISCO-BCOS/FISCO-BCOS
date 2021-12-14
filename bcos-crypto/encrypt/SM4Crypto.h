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
 * @brief implementation for sm4 encryption/decryption
 * @file SM4Crypto.h
 * @date 2021.04.03
 * @author yujiechen
 */
#pragma once
#include <bcos-framework/interfaces/crypto/SymmetricEncryption.h>

namespace bcos
{
namespace crypto
{
const int SM4_MAX_PADDING_LEN = 26;
const int SM4_KEY_SIZE = 16;
const int SM4_IV_SIZE = 16;
bytesPointer SM4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize);
bytesPointer SM4Decrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize);
class SM4Crypto : public SymmetricEncryption
{
public:
    using Ptr = std::shared_ptr<SM4Crypto>;
    SM4Crypto() = default;
    ~SM4Crypto() override {}
    bytesPointer symmetricEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
        const unsigned char* _key, size_t _keySize) override
    {
        return symmetricEncrypt(_plainData, _plainDataSize, _key, _keySize, _key, 16);
    }
    bytesPointer symmetricDecrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
        const unsigned char* _key, size_t _keySize) override
    {
        return symmetricDecrypt(_cipherData, _cipherDataSize, _key, _keySize, _key, 16);
    }

    bytesPointer symmetricEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
        const unsigned char* _key, size_t _keySize, const unsigned char* _ivData,
        size_t _ivDataSize) override
    {
        return SM4Encrypt(_plainData, _plainDataSize, _key, _keySize, _ivData, _ivDataSize);
    }
    bytesPointer symmetricDecrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
        const unsigned char* _key, size_t _keySize, const unsigned char* _ivData,
        size_t _ivDataSize) override
    {
        return SM4Decrypt(_cipherData, _cipherDataSize, _key, _keySize, _ivData, _ivDataSize);
    }
};
}  // namespace crypto
}  // namespace bcos