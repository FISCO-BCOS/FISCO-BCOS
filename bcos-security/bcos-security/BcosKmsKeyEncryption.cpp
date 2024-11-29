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

/**
 * @brief : Key Encryption
 * @author: HaoXuan40404
 * @date: 2024-11-07
 */

#include "BcosKmsKeyEncryption.h"
#include "BcosKms.h"
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

namespace bcos::security
{
BcosKmsKeyEncryption::BcosKmsKeyEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_compatibilityVersion = m_nodeConfig->compatibilityVersion();

    std::vector<std::string> values;
    boost::split(
        values, nodeConfig->keyEncryptionUrl(), boost::is_any_of(":"), boost::token_compress_on);
    if (2 != values.size())
    {
        BOOST_THROW_EXCEPTION(
            InvalidParameter() << errinfo_comment(
                "initGlobalConfig storage_security failed! Invalid key_center_url!"));
    }

    std::string keyCenterIp = values[0];
    unsigned short keyCenterPort = boost::lexical_cast<unsigned short>(values[1]);
    if (!m_nodeConfig->isValidPort(keyCenterPort))
    {
        BOOST_THROW_EXCEPTION(
            InvalidConfig() << errinfo_comment(
                "initGlobalConfig storage_security failed! Invalid key_manange_port!"));
    }

    std::string cipherDataKey = m_nodeConfig->bcosKmsKeySecurityCipherDataKey();

    BcosKms keyClient;
    keyClient.setIpPort(keyCenterIp, keyCenterPort);
    m_dataKey = asString(keyClient.getDataKey(cipherDataKey, m_nodeConfig->smCryptoType()));

    BCOS_LOG(INFO) << LOG_BADGE("BcosKmsKeyEncryption::init") << LOG_KV("key_center_ip:", keyCenterIp)
                   << LOG_KV("key_center_port:", keyCenterPort);

    if (!m_nodeConfig->smCryptoType())
    {
        m_symmetricEncrypt = std::make_shared<AESCrypto>();
    }
    else
    {
        m_symmetricEncrypt = std::make_shared<SM4Crypto>();
    }
}

BcosKmsKeyEncryption::BcosKmsKeyEncryption(const std::string& dataKey, const bool smCryptoType)
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

std::shared_ptr<bytes> BcosKmsKeyEncryption::decryptContents(const std::shared_ptr<bytes>& content)
{
    std::shared_ptr<bytes> decFileBytes;
    bytesPointer decFileBytesBase64Ptr = nullptr;
    try
    {
        std::string encContextsStr((const char*)content->data(), content->size());

        bytes encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("DECFILE") << LOG_DESC("Decrypt file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        // TODO: key manager should fit this logic
        // if (m_compatibilityVersion >=
        //     static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
        //{
        //     size_t const offsetIv = encFileBytes.size() - 16;
        //     size_t const cipherDataSize = encFileBytes.size() - 16;
        //     decFileBytesBase64Ptr = m_symmetricEncrypt->symmetricDecrypt(
        //         reinterpret_cast<const unsigned char*>(encFileBytes.data()), cipherDataSize,
        //         reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
        //         reinterpret_cast<const unsigned char*>(encFileBytes.data() + offsetIv), 16);
        // }
        // else
        //{
        decFileBytesBase64Ptr =
            m_symmetricEncrypt->symmetricDecrypt((const unsigned char*)encFileBytes.data(),
                encFileBytes.size(), (const unsigned char*)m_dataKey.data(), m_dataKey.size());
        //}

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

std::shared_ptr<bytes> BcosKmsKeyEncryption::decryptFile(const std::string& filename)
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
        // TODO: key manager should fit this logic
        // if (m_compatibilityVersion >=
        //     static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION))
        // {
        //     size_t const offsetIv = encFileBytes.size() - 16;
        //     size_t const cipherDataSize = encFileBytes.size() - 16;
        //     decFileBytesBase64Ptr = m_symmetricEncrypt->symmetricDecrypt(
        //         reinterpret_cast<const unsigned char*>(encFileBytes.data()), cipherDataSize,
        //         reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size(),
        //         reinterpret_cast<const unsigned char*>(encFileBytes.data() + offsetIv), 16);
        // }
        // else
        // {
        decFileBytesBase64Ptr =
            m_symmetricEncrypt->symmetricDecrypt((const unsigned char*)encFileBytes.data(),
                encFileBytes.size(), (const unsigned char*)m_dataKey.data(), m_dataKey.size());
        //}
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

std::shared_ptr<bytes> BcosKmsKeyEncryption::encryptContents(const std::shared_ptr<bytes>& content)
{
    BCOS_LOG(ERROR) << LOG_DESC("[BcosKmsKeyEncryption] encryptContents error");
    BOOST_THROW_EXCEPTION(NotImplementedError());
}

std::shared_ptr<bytes> BcosKmsKeyEncryption::encryptFile(const std::string& filename)
{
    BCOS_LOG(ERROR) << LOG_DESC("[BcosKmsKeyEncryption] encryptFile error");
    BOOST_THROW_EXCEPTION(NotImplementedError());
}

}  // namespace bcos::security
