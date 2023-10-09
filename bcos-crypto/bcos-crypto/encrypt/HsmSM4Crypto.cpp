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
 * @brief implementation for Hsm SM4 encryption/decryption
 * @file HsmSM4Crypto.cpp
 * @date 2022.11.04
 * @author lucasli
 */
#include <bcos-crypto/encrypt/Exceptions.h>
#include <bcos-crypto/encrypt/HsmSM4Crypto.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>

#include "hsm-crypto/hsm/CryptoProvider.h"
#include "hsm-crypto/hsm/SDFCryptoProvider.h"

using namespace hsm;
using namespace bcos;
using namespace bcos::crypto;

bcos::bytesPointer HsmSM4Crypto::HsmSM4Encrypt(const unsigned char* _plainData,
    size_t _plainDataSize, const unsigned char* _key, size_t, const unsigned char* _ivData,
    size_t _ivDataSize)
{
    // note: parm _ivDataSize and _keySize wasn't used
    if (!_ivData || _ivDataSize < SM4_IV_DATA_SIZE)
    {
        CRYPTO_LOG(WARNING)
            << "[HsmSM4Crypto::HsmSM4Encrypt] invalid iv data or iv data size, "
               "iv data should not be NULL and iv data size shouldn't smaller than 16"
            << LOG_KV("_ivDataSize", _ivDataSize);
        BOOST_THROW_EXCEPTION(
            std::runtime_error("Hsm SM4 HsmSM4Encrypt error, invalid iv data or iv data size"));
    }

    // Add padding
    int padding = _plainDataSize % 16;
    int nSize = 16 - padding;
    int inDataVLen = _plainDataSize + nSize;
    bytes inDataV(inDataVLen);
    memcpy(inDataV.data(), _plainData, _plainDataSize);
    memset(inDataV.data() + _plainDataSize, 0, nSize);

    // Encrypt
    Key key = Key();
    std::shared_ptr<const std::vector<byte>> pbKeyValue =
        std::make_shared<const std::vector<byte>>(_key, _key + 16);
    key.setSymmetricKey(pbKeyValue);
    CryptoProvider& provider = SDFCryptoProvider::GetInstance(m_hsmLibPath);

    unsigned int size;
    auto encryptedData = std::make_shared<bytes>();
    encryptedData->resize(inDataVLen);
    provider.Encrypt(key, SM4_CBC, (unsigned char*)_ivData, (unsigned char*)inDataV.data(),
        inDataVLen, (unsigned char*)encryptedData->data(), &size);
    CRYPTO_LOG(DEBUG) << "[HsmSM4Crypto::Encrypt] Encrypt Success";
    return encryptedData;
}

bcos::bytesPointer HsmSM4Crypto::HsmSM4Decrypt(const unsigned char* _cipherData,
    size_t _cipherDataSize, const unsigned char* _key, size_t, const unsigned char* _ivData,
    size_t _ivDataSize)
{
    if (!_ivData || _ivDataSize < SM4_IV_DATA_SIZE)
    {
        CRYPTO_LOG(WARNING)
            << "[HsmSM4Crypto::HsmSM4Decrypt] invalid iv data or iv data size, "
               "iv data should not be NULL and iv data size shouldn't smaller than 16"
            << LOG_KV("_ivDataSize", _ivDataSize);
        BOOST_THROW_EXCEPTION(
            std::runtime_error("Hsm SM4 HsmSM4Decrypt error, invalid iv data or iv data size"));
    }

    auto decryptedData = std::make_shared<bytes>();
    decryptedData->resize(_cipherDataSize);
    Key key = Key();
    std::shared_ptr<const std::vector<byte>> pbKeyValue =
        std::make_shared<const std::vector<byte>>(_key, _key + 16);
    key.setSymmetricKey(pbKeyValue);
    CryptoProvider& provider = SDFCryptoProvider::GetInstance(m_hsmLibPath);

    unsigned int size;
    provider.Decrypt(key, SM4_CBC, (unsigned char*)_ivData, _cipherData, _cipherDataSize,
        (unsigned char*)decryptedData->data(), &size);
    CRYPTO_LOG(DEBUG) << "[HsmSM4Crypto::Decrypt] Decrypt Success";
    return decryptedData;
}

bcos::bytesPointer HsmSM4Crypto::symmetricEncryptWithInternalKey(const unsigned char* _plainData,
    size_t _plainDataSize, const unsigned int _keyIndex, const unsigned char* _ivData,
    size_t _ivDataSize)
{
    if (!_ivData || _ivDataSize < SM4_IV_DATA_SIZE)
    {
        CRYPTO_LOG(WARNING)
            << "[HsmSM4Crypto::symmetricEncryptWithInternalKey] invalid iv data or iv data size, "
               "iv data should not be NULL and iv data size shouldn't smaller than 16"
            << LOG_KV("_ivDataSize", _ivDataSize);
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "Hsm SM4 EncryptWithInternalKey error, invalid iv data or iv data size"));
    }

    // Add padding
    int nRemain = _plainDataSize % SM4_BLOCK_SIZE;
    int paddingLen = SM4_BLOCK_SIZE - nRemain;
    int inDataVLen = _plainDataSize + paddingLen;
    bytes inDataV(inDataVLen);
    memcpy(inDataV.data(), _plainData, _plainDataSize);
    memset(inDataV.data() + _plainDataSize, paddingLen, paddingLen);

    // Encrypt
    unsigned int size;
    auto encryptedData = std::make_shared<bytes>();
    encryptedData->resize(inDataVLen);
    SDFCryptoProvider& provider = SDFCryptoProvider::GetInstance(m_hsmLibPath);
    auto encryptCode = provider.EncryptWithInternalKey((unsigned int)_keyIndex, SM4_CBC,
        (unsigned char*)_ivData, (unsigned char*)inDataV.data(), inDataVLen,
        (unsigned char*)(encryptedData->data()), &size);
    if (encryptCode != SDR_OK)
    {
        CRYPTO_LOG(WARNING) << "[HsmSM4Crypto::symmetricEncryptWithInternalKey] encrypt ERROR "
                            << LOG_KV("error", provider.GetErrorMessage(encryptCode));
        BOOST_THROW_EXCEPTION(std::runtime_error("Hsm SM4 EncryptWithInternalKey error"));
    }

    return encryptedData;
}

bcos::bytesPointer HsmSM4Crypto::symmetricDecryptWithInternalKey(const unsigned char* _cipherData,
    size_t _cipherDataSize, const unsigned int _keyIndex, const unsigned char* _ivData,
    size_t _ivDataSize)
{
    if (!_ivData || _ivDataSize < SM4_IV_DATA_SIZE)
    {
        CRYPTO_LOG(WARNING)
            << "[HsmSM4Crypto::symmetricDecryptWithInternalKey] invalid iv data or iv data size, "
               "iv data should not be NULL and iv data size shouldn't smaller than 16"
            << LOG_KV("_ivDataSize", _ivDataSize);
        BOOST_THROW_EXCEPTION(std::runtime_error(
            "Hsm SM4 DecryptWithInternalKey error, invalid iv data or iv data size"));
    }

    auto decryptedData = std::make_shared<bytes>();
    decryptedData->resize(_cipherDataSize);
    SDFCryptoProvider& provider = SDFCryptoProvider::GetInstance(m_hsmLibPath);
    unsigned int size;
    auto decryptCode =
        provider.DecryptWithInternalKey((unsigned int)_keyIndex, SM4_CBC, (unsigned char*)_ivData,
            _cipherData, _cipherDataSize, (unsigned char*)decryptedData->data(), &size);
    if (decryptCode != SDR_OK)
    {
        CRYPTO_LOG(WARNING) << "[HsmSM4Crypto::symmetricDecryptWithInternalKey] decrypt ERROR "
                            << LOG_KV("error", provider.GetErrorMessage(decryptCode));
        BOOST_THROW_EXCEPTION(std::runtime_error("Hsm SM4 DecryptWithInternalKey error"));
    }

    // exclude padding data in decryptedData
    int paddingCount = (int)decryptedData->back();
    int pureDecryptedDataSize = decryptedData->size() - paddingCount;
    decryptedData->resize(pureDecryptedDataSize);

    return decryptedData;
}