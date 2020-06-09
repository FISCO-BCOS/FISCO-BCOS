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

#include "EncryptedFile.h"
#include "libdevcrypto/CryptoInterface.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Base64.h>
#include <libdevcore/CommonIO.h>
#include <libdevcore/Exceptions.h>

using namespace std;
using namespace dev;

bytes EncryptedFile::decryptContents(const std::string& _filePath)
{
    bytes encFileBytes;
    bytes decFileBytes;
    try
    {
        LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Trying to read enc file")
                   << LOG_KV("file", _filePath);
        string encContextsStr = contentsString(_filePath);
        encFileBytes = fromHex(encContextsStr);
        LOG(DEBUG) << LOG_BADGE("ENCFILE") << LOG_DESC("Enc file contents")
                   << LOG_KV("string", encContextsStr) << LOG_KV("bytes", toHex(encFileBytes));

        auto dataKey = g_BCOSConfig.diskEncryption.dataKey;

        string decFileBytesBase64 =
            crypto::SymmetricDecrypt((const unsigned char*)encFileBytes.data(), encFileBytes.size(),
                (const unsigned char*)dataKey.data(), dataKey.size(),
                (const unsigned char*)dataKey.substr(0, 16).data());

        LOG(DEBUG) << "[ENCFILE] EncryptedFile Base64 key: " << decFileBytesBase64 << endl;
        decFileBytes = fromBase64(decFileBytesBase64);
    }
    catch (exception& e)
    {
        LOG(ERROR) << LOG_DESC("[ENCFILE] EncryptedFile error")
                   << LOG_KV("what", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }
    // LOG(DEBUG) << "[ENCFILE] Decrypt file [name/cipher/plain]: " << _filePath << "/"
    //           << toHex(encFileBytes) << "/" << toHex(decFileBytes) << endl;
    return decFileBytes;
}