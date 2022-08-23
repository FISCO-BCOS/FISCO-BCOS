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
 * @brief implementation for SM4 encryption/decryption
 * @file SM4Crypto.cpp
 * @date 2021.04.03
 * @author yujiechen
 */
#include <bcos-crypto/encrypt/Exceptions.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-utilities/FixedBytes.h>
#include <wedpr-crypto/WedprCrypto.h>
using namespace bcos;
using namespace bcos::crypto;

bytesPointer bcos::crypto::SM4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    CInputBuffer plain{(const char*)_plainData, _plainDataSize};

    FixedBytes<SM4_KEY_SIZE> fixedKeyData(_key, _keySize);
    CInputBuffer key{(const char*)fixedKeyData.data(), SM4_KEY_SIZE};

    FixedBytes<SM4_IV_SIZE> fixedIVData(_ivData, _ivDataSize);
    CInputBuffer iv{(const char*)fixedIVData.data(), SM4_IV_SIZE};

    auto encryptedData = std::make_shared<bytes>();
    encryptedData->resize(_plainDataSize + SM4_MAX_PADDING_LEN);
    COutputBuffer ciper{(char*)encryptedData->data(), encryptedData->size()};

    if (wedpr_sm4_encrypt(&plain, &key, &iv, &ciper) == WEDPR_ERROR)
    {
        BOOST_THROW_EXCEPTION(EncryptException() << errinfo_comment("SM4 encrypt exception"));
    }
    encryptedData->resize(ciper.len);
    return encryptedData;
}


bytesPointer bcos::crypto::SM4Decrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    CInputBuffer cipher{(const char*)_cipherData, _cipherDataSize};

    FixedBytes<SM4_KEY_SIZE> fixedKeyData(_key, _keySize);
    CInputBuffer key{(const char*)fixedKeyData.data(), SM4_KEY_SIZE};

    FixedBytes<SM4_IV_SIZE> fixedIVData(_ivData, _ivDataSize);
    CInputBuffer iv{(const char*)fixedIVData.data(), SM4_IV_SIZE};

    auto decryptedData = std::make_shared<bytes>();
    decryptedData->resize(_cipherDataSize);
    COutputBuffer plain{(char*)decryptedData->data(), decryptedData->size()};

    if (wedpr_sm4_decrypt(&cipher, &key, &iv, &plain) == WEDPR_ERROR)
    {
        BOOST_THROW_EXCEPTION(DecryptException() << errinfo_comment("SM4 decrypt exception"));
    }
    decryptedData->resize(plain.len);
    return decryptedData;
}