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

#include "BcosKmsDataEncryption.h"
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

namespace bcos
{
namespace security
{
BcosKmsDataEncryption::BcosKmsDataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_compatibilityVersion = m_nodeConfig->compatibilityVersion();

    std::vector<std::string> values;
    boost::split(values, nodeConfig->storageSecuirtyKeyCenterUrl(), boost::is_any_of(":"),
        boost::token_compress_on);
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


    std::string cipherDataKey = m_nodeConfig->storageSecurityCipherDataKey();

    BcosKms keyClient;
    keyClient.setIpPort(keyCenterIp, keyCenterPort);
    m_dataKey = asString(keyClient.getDataKey(cipherDataKey, m_nodeConfig->smCryptoType()));

    BCOS_LOG(INFO) << LOG_BADGE("BcosKmsDataEncryption:init") << LOG_KV("key_center_ip:", keyCenterIp)
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

BcosKmsDataEncryption::BcosKmsDataEncryption(const std::string& dataKey, const bool smCryptoType)
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

std::string BcosKmsDataEncryption::encrypt(const std::string& data)
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

std::string BcosKmsDataEncryption::decrypt(const std::string& data)
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
