/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
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

void DataEncryption::init()
{
    std::string keyCenterIp = m_nodeConfig->storageSecurityKeyCenterIp();
    unsigned short keyCenterPort = m_nodeConfig->storageSecurityKeyCenterPort();
    std::string cipherDataKey = m_nodeConfig->storageSecurityCipherDataKey();

    bool smCryptoType = m_nodeConfig->smCryptoType();

    KeyCenter keyClient;
    keyClient.setIpPort(keyCenterIp, keyCenterPort);
    m_dataKey = asString(keyClient.getDataKey(cipherDataKey, smCryptoType));

    BCOS_LOG(INFO) << LOG_BADGE("DataEncryption::init") << LOG_KV("key_center_ip:", keyCenterIp)
                   << LOG_KV("key_center_port:", keyCenterPort);

    if (false == smCryptoType)
        m_symmetricEncrypt = std::make_shared<AESCrypto>();
    else
        m_symmetricEncrypt = std::make_shared<SM4Crypto>();
}

std::shared_ptr<bytes> DataEncryption::decryptContents(const std::shared_ptr<bytes>& contents)
{
    bytes encFileBytes;
    std::shared_ptr<bytes> decFileBytes;
    try
    {
        std::string encContextsStr((const char*)contents->data(), contents->size());

        encFileBytes = fromHex(encContextsStr);
        BCOS_LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Enc file contents")
                        << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        bytesPointer decFileBytesBase64Ptr =
            m_symmetricEncrypt->symmetricDecrypt((const unsigned char*)encFileBytes.data(),
                encFileBytes.size(), (const unsigned char*)m_dataKey.data(), m_dataKey.size());

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
    // LOG(DEBUG) << "[ENCFILE] Decrypt file [name/cipher/plain]: " << _filePath << "/"
    //           << toHex(encFileBytes) << "/" << toHex(decFileBytes) << endl;
    return decFileBytes;
}

std::string DataEncryption::encrypt(const std::string& data)
{
    bytesPointer encData = m_symmetricEncrypt->symmetricEncrypt(
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size());

    std::string value(encData->size(), 0);
    memcpy(value.data(), encData->data(), encData->size());

    return value;
}

std::string DataEncryption::decrypt(const std::string& data)
{
    bytesPointer decData = m_symmetricEncrypt->symmetricDecrypt(
        reinterpret_cast<const unsigned char*>(data.data()), data.size(),
        reinterpret_cast<const unsigned char*>(m_dataKey.data()), m_dataKey.size());

    std::string value(decData->size(), 0);
    memcpy(value.data(), decData->data(), decData->size());

    return value;
}

}  // namespace security

}  // namespace bcos
