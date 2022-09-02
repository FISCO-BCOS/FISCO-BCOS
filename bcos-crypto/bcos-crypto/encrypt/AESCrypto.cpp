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
 * @brief implementation for AES encryption/decryption
 * @file AESCrypto.cpp
 * @date 2021.04.03
 * @author yujiechen
 */
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <wedpr-crypto/WedprCrypto.h>

using namespace bcos;
using namespace bcos::crypto;

bytesPointer bcos::crypto::AESEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    CInputBuffer plainText{(const char*)_plainData, _plainDataSize};

    FixedBytes<AES_KEY_SIZE> fixedKeyData(_key, _keySize);
    CInputBuffer key{(const char*)fixedKeyData.data(), AES_KEY_SIZE};

    FixedBytes<AES_IV_DATA_SIZE> FixedIVData(_ivData, _ivDataSize);
    CInputBuffer ivData{(const char*)FixedIVData.data(), AES_IV_DATA_SIZE};

    auto encryptedData = std::make_shared<bytes>();
    size_t ciperDataSize = _plainDataSize + AES_MAX_PADDING_SIZE;
    encryptedData->resize(ciperDataSize);
    COutputBuffer encryptResult{(char*)encryptedData->data(), ciperDataSize};

    if (wedpr_aes256_encrypt(&plainText, &key, &ivData, &encryptResult) == WEDPR_ERROR)
    {
        BOOST_THROW_EXCEPTION(EncryptException() << errinfo_comment("AES encrypt exception"));
    }
    encryptedData->resize(encryptResult.len);
    return encryptedData;
}

bytesPointer bcos::crypto::AESDecrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    CInputBuffer ciper{(const char*)_cipherData, _cipherDataSize};

    FixedBytes<AES_KEY_SIZE> fixedKeyData(_key, _keySize);
    CInputBuffer key{(const char*)fixedKeyData.data(), AES_KEY_SIZE};

    FixedBytes<AES_IV_DATA_SIZE> fixedIVData(_ivData, _ivDataSize);
    CInputBuffer iv{(const char*)fixedIVData.data(), AES_IV_DATA_SIZE};

    auto decodedData = std::make_shared<bytes>();
    auto plainDataSize = _cipherDataSize;
    decodedData->resize(plainDataSize);
    COutputBuffer decodedResult{(char*)decodedData->data(), plainDataSize};

    if (wedpr_aes256_decrypt(&ciper, &key, &iv, &decodedResult) == WEDPR_ERROR)
    {
        BOOST_THROW_EXCEPTION(DecryptException() << errinfo_comment("AES decrypt exception"));
    }
    decodedData->resize(decodedResult.len);

    return decodedData;
}