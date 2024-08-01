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
 * @brief : Encrypt file
 * @author: jimmyshi, websterchen
 * @date: 2018-12-06
 */

#include "DataEncryption.h"
#include "KeyCenter.h"
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-framework/protocol/Protocol.h>
#include <bcos-utilities/Base64.h>
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
DataEncryption::DataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_compatibilityVersion = m_nodeConfig->compatibilityVersion();

    if (m_nodeConfig->storageSecurityEnable())
    {
        std::string keyCenterIp = m_nodeConfig->storageSecurityKeyCenterIp();
        unsigned short keyCenterPort = m_nodeConfig->storageSecurityKeyCenterPort();
        std::string cipherDataKey = m_nodeConfig->storageSecurityCipherDataKey();

        KeyCenter keyClient;
        keyClient.setIpPort(keyCenterIp, keyCenterPort);
        m_dataKey = asString(keyClient.getDataKey(cipherDataKey, m_nodeConfig->smCryptoType()));

        BCOS_LOG(INFO) << LOG_BADGE("DataEncryption::init") << LOG_KV("key_center_ip:", keyCenterIp)
                       << LOG_KV("key_center_port:", keyCenterPort);
    }

    if (!m_nodeConfig->smCryptoType())
    {
        m_symmetricEncrypt = std::make_shared<AESCrypto>();
    }
    else
    {
        m_symmetricEncrypt = std::make_shared<SM4Crypto>();
    }
}

DataEncryption::DataEncryption(const std::string& dataKey, const bool smCryptoType)
{
    m_dataKey = dataKey;

    if (!smCryptoType)
    {
        m_symmetricEncrypt = std::make_shared<AESCrypto>();
    }
    else
    {
        m_symmetricEncrypt = std::make_shared<SM4Crypto>();
    }
}

std::shared_ptr<bytes> DataEncryption::decryptContents(const std::shared_ptr<bytes>& content)
{
    std::shared_ptr<bytes> decFileBytes;
    bytesPointer decFileBytesBase64Ptr = nullptr;
    try
    {
        std::string encContextsStr((const char*)content->data(), content->size());

        bytes encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("DECFILE") << LOG_DESC("Decrypt file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        if (m_compatibilityVersion >=
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
        {
            size_t const offsetIv = encFileBytes.size() - 16;
            size_t const cipherDataSize = encFileBytes.size() - 16;
            decFileBytesBase64Ptr = m_symmetricEncrypt->symmetricDecrypt(
                reinterpret_cast<const unsigned char*>(encFileBytes.data()), cipherDataSize,
                reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
                reinterpret_cast<const unsigned char*>(encFileBytes.data() + offsetIv), 16);
        }
        else
        {
            decFileBytesBase64Ptr =
                m_symmetricEncrypt->symmetricDecrypt((const unsigned char*)encFileBytes.data(),
                    encFileBytes.size(), (const unsigned char*)m_dataKey.data(), m_dataKey.size());
        }

        BCOS_LOG(DEBUG) << "[ENCFILE] DecryptedFile Base64 key: "
                        << asString(*decFileBytesBase64Ptr) << endl;
        decFileBytes = base64DecodeBytes(asString(*decFileBytesBase64Ptr));
    }
    catch (exception& e)
    {
        BCOS_LOG(ERROR) << LOG_DESC("[ENCFILE] DecryptedFile error")
                        << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }

    return decFileBytes;
}

std::shared_ptr<bytes> DataEncryption::decryptFile(const std::string& filename)
{
    std::shared_ptr<bytes> decFileBytes;
    try
    {
        std::shared_ptr<bytes> keyContent = readContents(boost::filesystem::path(filename));
        std::string encContextsStr((const char*)keyContent->data(), keyContent->size());
        bytes encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Enc file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        bytesPointer decFileBytesBase64Ptr = nullptr;
        if (m_compatibilityVersion >=
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
        {
            size_t const offsetIv = encFileBytes.size() - 16;
            size_t const cipherDataSize = encFileBytes.size() - 16;
            decFileBytesBase64Ptr = m_symmetricEncrypt->symmetricDecrypt(
                reinterpret_cast<const unsigned char*>(encFileBytes.data()), cipherDataSize,
                reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
                reinterpret_cast<const unsigned char*>(encFileBytes.data() + offsetIv), 16);
        }
        else
        {
            decFileBytesBase64Ptr =
                m_symmetricEncrypt->symmetricDecrypt((const unsigned char*)encFileBytes.data(),
                    encFileBytes.size(), (const unsigned char*)m_dataKey.data(), m_dataKey.size());
        }
        BCOS_LOG(DEBUG) << "[ENCFILE] EncryptedFile Base64 key: "
                        << asString(*decFileBytesBase64Ptr) << endl;
        decFileBytes = base64DecodeBytes(asString(*decFileBytesBase64Ptr));
    }
    catch (exception& e)
    {
        BCOS_LOG(ERROR) << LOG_DESC("[ENCFILE] EncryptedFile error")
                        << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }

    return decFileBytes;
}

std::string DataEncryption::encrypt(const std::string& data)
{
    bytesPointer encData = nullptr;
    if (m_compatibilityVersion >= static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        random_bytes_engine rbe;
        std::vector<unsigned char> ivData(16);
        std::generate(std::begin(ivData), std::end(ivData), std::ref(rbe));

        encData = m_symmetricEncrypt->symmetricEncrypt(
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
            ivData.data(), 16);
        encData->insert(encData->end(), ivData.begin(), ivData.end());
    }
    else
    {
        encData = m_symmetricEncrypt->symmetricEncrypt(
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size());
    }
    std::string value((char*)encData->data(), encData->size());

    return value;
}

std::string DataEncryption::decrypt(const std::string& data)
{
    bytesPointer decData = nullptr;
    if (m_compatibilityVersion >= static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
    {
        size_t offsetIv = data.size() - 16;
        size_t cipherDataSize = data.size() - 16;
        decData = m_symmetricEncrypt->symmetricDecrypt(
            reinterpret_cast<const unsigned char*>(data.data()), cipherDataSize,
            reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
            reinterpret_cast<const unsigned char*>(data.data() + offsetIv), 16);
    }
    else
    {
        decData = m_symmetricEncrypt->symmetricDecrypt(
            reinterpret_cast<const unsigned char*>(data.data()), data.size(),
            reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size());
    }
    std::string value((char*)decData->data(), decData->size());

    return value;
}

}  // namespace security

}  // namespace bcos
