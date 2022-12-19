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
    size_t _plainDataSize, const unsigned char* _key, size_t, const unsigned char* _ivData, size_t)
{
    // note: parm _ivDataSize and _keySize wasn't used
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
    size_t _cipherDataSize, const unsigned char* _key, size_t, const unsigned char* _ivData, size_t)
{
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