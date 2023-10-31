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
 */
/**
 * @brief : HSM Encrypt file
 * @author: lucasli
 * @date: 2023-02-17
 */

#include "HsmDataEncryption.h"
#include <bcos-crypto/encrypt/HsmSM4Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/Log.h>

using namespace std;
using namespace bcos;
using namespace crypto;
using namespace tool;

namespace bcos
{
namespace security
{
HsmDataEncryption::HsmDataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_hsmLibPath = m_nodeConfig->hsmLibPath();
    m_encKeyIndex = m_nodeConfig->encKeyIndex();
    m_symmetricEncrypt = std::make_shared<HsmSM4Crypto>(m_hsmLibPath);
}

std::shared_ptr<bytes> HsmDataEncryption::encryptContents(const std::shared_ptr<bytes>& contents)
{
    random_bytes_engine rbe;
    std::vector<unsigned char> ivData(SM4_IV_DATA_SIZE);
    std::generate(std::begin(ivData), std::end(ivData), std::ref(rbe));
    // iv data would be changed after hsm encrypt, so keep it
    auto originIvData = ivData;

    bytesPointer encData = m_symmetricEncrypt->symmetricEncryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(contents->data()), contents->size(), m_encKeyIndex,
        ivData.data(), SM4_IV_DATA_SIZE);
    // append iv data to end of encData
    encData->insert(encData->end(), originIvData.begin(), originIvData.end());
    return encData;
}

std::shared_ptr<bytes> HsmDataEncryption::decryptContents(const std::shared_ptr<bytes>& contents)
{
    size_t cipherDataSize = contents->size() - SM4_IV_DATA_SIZE;
    bytesPointer decData = m_symmetricEncrypt->symmetricDecryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(contents->data()), cipherDataSize, m_encKeyIndex,
        reinterpret_cast<const unsigned char*>(contents->data() + cipherDataSize),
        SM4_IV_DATA_SIZE);
    return decData;
}

std::string HsmDataEncryption::encrypt(uint8_t* data, size_t size)
{
    random_bytes_engine rbe;
    std::vector<unsigned char> ivData(SM4_IV_DATA_SIZE);
    std::generate(std::begin(ivData), std::end(ivData), std::ref(rbe));
    // iv data would be changed after hsm encrypt, so keep it
    auto originIvData = ivData;

    bytesPointer encData = m_symmetricEncrypt->symmetricEncryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(data), size, m_encKeyIndex, ivData.data(),
        SM4_IV_DATA_SIZE);
    // append iv data to end of encData
    std::string value((char*)encData->data(), encData->size());
    value.insert(value.end(), originIvData.begin(), originIvData.end());

    return value;
}

std::string HsmDataEncryption::decrypt(uint8_t* data, size_t size)
{
    size_t cipherDataSize = size - SM4_IV_DATA_SIZE;
    bytesPointer decData = m_symmetricEncrypt->symmetricDecryptWithInternalKey(
        data, cipherDataSize, m_encKeyIndex, data + cipherDataSize, SM4_IV_DATA_SIZE);
    std::string value((char*)decData->data(), decData->size());

    return value;
}

}  // namespace security
}  // namespace bcos
