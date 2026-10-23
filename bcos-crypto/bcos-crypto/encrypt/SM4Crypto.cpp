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
#include <openssl/evp.h>
#include <memory>
using namespace bcos;
using namespace bcos::crypto;

namespace
{
using EvpCipherCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
}  // namespace

bytes bcos::crypto::SM4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    FixedBytes<SM4_KEY_SIZE> fixedKeyData(_key, _keySize);
    FixedBytes<SM4_IV_SIZE> fixedIVData(_ivData, _ivDataSize);

    // SM4-CBC with PKCS7 padding (EVP default padding)
    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_sm4_cbc(), nullptr, fixedKeyData.data(),
                    fixedIVData.data()) != 1)
    {
        BOOST_THROW_EXCEPTION(EncryptException() << errinfo_comment("SM4 encrypt exception"));
    }

    bytes encryptedData(_plainDataSize + SM4_MAX_PADDING_LEN);
    int updateLen = 0;
    int finalLen = 0;
    if ((_plainDataSize > 0 && EVP_EncryptUpdate(ctx.get(), encryptedData.data(), &updateLen,
                                   _plainData, static_cast<int>(_plainDataSize)) != 1) ||
        EVP_EncryptFinal_ex(ctx.get(), encryptedData.data() + updateLen, &finalLen) != 1)
    {
        BOOST_THROW_EXCEPTION(EncryptException() << errinfo_comment("SM4 encrypt exception"));
    }
    encryptedData.resize(updateLen + finalLen);
    return encryptedData;
}


bytes bcos::crypto::SM4Decrypt(const unsigned char* _cipherData, size_t _cipherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData, size_t _ivDataSize)
{
    FixedBytes<SM4_KEY_SIZE> fixedKeyData(_key, _keySize);
    FixedBytes<SM4_IV_SIZE> fixedIVData(_ivData, _ivDataSize);

    // SM4-CBC with PKCS7 padding (EVP default padding)
    EvpCipherCtxPtr ctx(EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_sm4_cbc(), nullptr, fixedKeyData.data(),
                    fixedIVData.data()) != 1)
    {
        BOOST_THROW_EXCEPTION(DecryptException() << errinfo_comment("SM4 decrypt exception"));
    }

    // EVP_DecryptUpdate may hold back one block for the padding check
    bytes decryptedData(_cipherDataSize + SM4_MAX_PADDING_LEN);
    int updateLen = 0;
    int finalLen = 0;
    if ((_cipherDataSize > 0 && EVP_DecryptUpdate(ctx.get(), decryptedData.data(), &updateLen,
                                    _cipherData, static_cast<int>(_cipherDataSize)) != 1) ||
        EVP_DecryptFinal_ex(ctx.get(), decryptedData.data() + updateLen, &finalLen) != 1)
    {
        BOOST_THROW_EXCEPTION(DecryptException() << errinfo_comment("SM4 decrypt exception"));
    }
    decryptedData.resize(updateLen + finalLen);
    return decryptedData;
}
