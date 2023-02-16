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
HsmDataEncryption::HsmDataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
{
    m_nodeConfig = nodeConfig;
    m_hsmLibPath = m_nodeConfig->hsmLibPath();
    m_encKeyIndex = m_nodeConfig->encKeyIndex();
    m_symmetricEncrypt = std::make_shared<HsmSM4Crypto>(m_hsmLibPath);
}

std::shared_ptr<bytes> HsmDataEncryption::decryptContents(const std::shared_ptr<bytes>& content)
{
    std::shared_ptr<bytes> decFileBytes;
    try
    {
        std::string encContextsStr((const char*)content->data(), content->size());

        bytes encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Enc file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        bytesPointer decFileBytesBase64Ptr =
            m_symmetricEncrypt->symmetricDecryptWithInternalKey((const unsigned char*)encFileBytes.data(),
                encFileBytes.size(), m_encKeyIndex);

        BCOS_LOG(DEBUG) << "[HsmDataEncryption][ENCFILE] EncryptedFile Base64 key: "
                        << asString(*decFileBytesBase64Ptr) << endl;
        decFileBytes = base64DecodeBytes(asString(*decFileBytesBase64Ptr));
    }
    catch (exception& e)
    {
        BCOS_LOG(ERROR) << LOG_DESC("[HsmDataEncryption][ENCFILE] EncryptedFile error")
                        << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }

    return decFileBytes;
}

std::shared_ptr<bytes> HsmDataEncryption::decryptFile(const std::string& filename)
{
    std::shared_ptr<bytes> decFileBytes;
    try
    {
        std::shared_ptr<bytes> keyContent = readContents(boost::filesystem::path(filename));

        std::string encContextsStr((const char*)keyContent->data(), keyContent->size());

        bytes encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Enc file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        bytesPointer decFileBytesBase64Ptr =
            m_symmetricEncrypt->symmetricDecryptWithInternalKey((const unsigned char*)encFileBytes.data(),
                encFileBytes.size(), m_encKeyIndex);

        BCOS_LOG(DEBUG) << "[HsmDataEncryption][ENCFILE] EncryptedFile Base64 key: "
                        << asString(*decFileBytesBase64Ptr) << endl;
        decFileBytes = base64DecodeBytes(asString(*decFileBytesBase64Ptr));
    }
    catch (exception& e)
    {
        BCOS_LOG(ERROR) << LOG_DESC("[HsmDataEncryption][ENCFILE] EncryptedFile error")
                        << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }

    return decFileBytes;
}

std::string HsmDataEncryption::encrypt(const std::string& data)
{
    bytesPointer encData = m_symmetricEncrypt->symmetricEncryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(data.data()), data.size(), m_encKeyIndex);

    std::string value(encData->size(), 0);
    memcpy(value.data(), encData->data(), encData->size());

    return value;
}

std::string HsmDataEncryption::decrypt(const std::string& data)
{
    bytesPointer decData = m_symmetricEncrypt->symmetricDecryptWithInternalKey(
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        m_encKeyIndex);
    std::string value(decData->size(), 0);
    memcpy(value.data(), decData->data(), decData->size());
    int padding = value.at(decData->size() - 1);
    int deLen = decData->size() - padding;

    return value.substr(0, deLen);
}

}  // namespace security
}  // namespace bcos
