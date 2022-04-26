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
 * @brief : Storage encrypt and decrypt heloer
 * @author: 
 * @date: 2022-04-13
 */

#include "StorageEncDecHelper.h"
#include <bcos-crypto/encrypt/AESCrypto.h>
#include <bcos-crypto/encrypt/SM4Crypto.h>
#include <bcos-utilities/DataConvertUtility.h>

using namespace bcos;
using namespace crypto;

namespace bcos
{

namespace security
{

EncHookFunction StorageEncDecHelper::getEncryptHandler(const std::vector<uint8_t>& _encryptKey)
{
    // get dataKey according to ciperDataKey from keyCenter
    return [=](std::string const& data, std::string& encData) {
        try
        {
            SymmetricEncryption::Ptr encrypt = std::make_shared<AESCrypto>();
            bytesPointer result = encrypt->symmetricEncrypt((const unsigned char*)data.data(),
                data.size(), (const unsigned char*)_encryptKey.data(), _encryptKey.size());

            encData.resize(result->size());
            memcpy(encData.data(), result->data(), result->size());
        }
        catch (const std::exception& e)
        {
            std::string error_info =
                "encryt value for data=" + std::string(data.data(), data.size()) +
                " failed, EINFO: " + boost::diagnostic_information(e);
            BCOS_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(EncryptFailed() << errinfo_comment(error_info));
        }
    };
}

DecHookFunction StorageEncDecHelper::getDecryptHandler(const std::vector<uint8_t>& _decryptKey)
{
    return [=](std::string& data) {
        try
        {
            SymmetricEncryption::Ptr encrypt = std::make_shared<AESCrypto>();
            bytesPointer result = encrypt->symmetricDecrypt((const unsigned char*)data.data(),
                data.size(), (const unsigned char*)_decryptKey.data(), _decryptKey.size());

            data.resize(result->size());
            memcpy(data.data(), result->data(), result->size());
        }
        catch (const std::exception& e)
        {
            std::string error_info = "decrypt value for key=" + toHex(_decryptKey) + " failed";
            BCOS_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(DecryptFailed() << errinfo_comment(error_info));
        }
    };
}

EncHookFunction StorageEncDecHelper::getEncryptHandlerSM(const std::vector<uint8_t>& _encryptKey)
{
    // get dataKey according to ciperDataKey from keyCenter
    return [=](std::string const& data, std::string& encData) {
        try
        {
            SymmetricEncryption::Ptr encrypt = std::make_shared<SM4Crypto>();
            bytesPointer result = encrypt->symmetricEncrypt((const unsigned char*)data.data(),
                data.size(), (const unsigned char*)_encryptKey.data(), _encryptKey.size());

            encData.resize(result->size());
            memcpy(encData.data(), result->data(), result->size());
        }
        catch (const std::exception& e)
        {
            std::string error_info =
                "encryt value for data=" + std::string(data.data(), data.size()) +
                " failed, EINFO: " + boost::diagnostic_information(e);
            BCOS_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(EncryptFailed() << errinfo_comment(error_info));
        }
    };
}

DecHookFunction StorageEncDecHelper::getDecryptHandlerSM(const std::vector<uint8_t>& _decryptKey)
{
    return [=](std::string& data) {
        try
        {
            SymmetricEncryption::Ptr encrypt = std::make_shared<SM4Crypto>();
            bytesPointer result = encrypt->symmetricDecrypt((const unsigned char*)data.data(),
                data.size(), (const unsigned char*)_decryptKey.data(), _decryptKey.size());

            data.resize(result->size());
            memcpy(data.data(), result->data(), result->size());
        }
        catch (const std::exception& e)
        {
            std::string error_info = "decrypt value for key=" + toHex(_decryptKey) + " failed";
            BCOS_LOG(ERROR) << LOG_DESC(error_info);
            BOOST_THROW_EXCEPTION(DecryptFailed() << errinfo_comment(error_info));
        }
    };
}

}  // namespace security

}  // namespace bcos