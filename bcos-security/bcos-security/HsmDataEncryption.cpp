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

using namespace bcos;
using namespace crypto;
using namespace tool;

namespace bcos
{
namespace security
{
using namespace std;

HsmDataEncryption::HsmDataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_hsmLibPath = m_nodeConfig->hsmLibPath();
    m_encKeyIndex = m_nodeConfig->encKeyIndex();
    m_symmetricEncrypt = std::make_shared<HsmSM4Crypto>(m_hsmLibPath);
}

std::string HsmDataEncryption::encrypt(uint8_t* data, size_t size)
{
    random_bytes_engine rbe;
    std::vector<unsigned char> ivData(SM4_IV_DATA_SIZE);
    std::generate(std::begin(ivData), std::end(ivData), std::ref(rbe));
    // iv data would be changed after hsm encrypt, so keep it
    auto originIvData = ivData;

    bytes encData = m_symmetricEncrypt->symmetricEncryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(data), size, m_encKeyIndex, ivData.data(),
        SM4_IV_DATA_SIZE);
    // append iv data to end of encData
    std::string value((char*)encData.data(), encData.size());
    value.insert(value.end(), originIvData.begin(), originIvData.end());

    return value;
}

std::string HsmDataEncryption::decrypt(uint8_t* data, size_t size)
{
    // Symmetric with HsmKeyEncryption::decryptContents: without this guard a
    // stored blob shorter than the IV wraps size - SM4_IV_DATA_SIZE to ~2^64,
    // which both forms a wild IV pointer and reaches decryptedData.resize()
    // inside the crypto layer as a ~16-exabyte allocation request.
    if (size < SM4_IV_DATA_SIZE)
    {
        BOOST_THROW_EXCEPTION(DecryptFailed() << errinfo_comment(
                                  "HsmDataEncryption: ciphertext too short, size: " +
                                  std::to_string(size)));
    }
    size_t cipherDataSize = size - SM4_IV_DATA_SIZE;
    bytes decData = m_symmetricEncrypt->symmetricDecryptWithInternalKey(
        data, cipherDataSize, m_encKeyIndex, data + cipherDataSize, SM4_IV_DATA_SIZE);
    std::string value((char*)decData.data(), decData.size());

    return value;
}

}  // namespace security
}  // namespace bcos
