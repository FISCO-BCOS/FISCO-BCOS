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
#include <libdevcore/Exceptions.h>
#include <libdevcore/easylog.h>

using namespace std;
using namespace dev;

bytes EncryptedFile::decryptContents(
    const std::string& _filePath, std::shared_ptr<dev::KeyCenter> _keyCenter)
{
    if (!_keyCenter)
        _keyCenter.reset(&(g_keyCenter));

    bytes encFileBytes;
    bytes decFileBytes;
    try
    {
        encFileBytes = fromHex(contentsString(_filePath));

        bytes dataKey = _keyCenter->getDataKey(g_BCOSConfig.diskEncryption.cipherDataKey);
        decFileBytes = aesCBCDecrypt(ref(encFileBytes), ref(dataKey));
    }
    catch (exception& e)
    {
        BOOST_THROW_EXCEPTION(EncryptedFileError());
    }
    LOG(DEBUG) << "[ENCFILE] Decrypt file [name/cipher/plain]: " << _filePath << "/"
               << toHex(encFileBytes) << "/" << toHex(decFileBytes) << endl;
    return decFileBytes;
}