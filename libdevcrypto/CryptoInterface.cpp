/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file CryptoInterface.cpp
 * @author xingqiangbai
 * @date 2020-04-22
 *
 */

#include "CryptoInterface.h"
#include "AES.h"
#include "SM4Crypto.h"

using namespace std;

std::function<std::string(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::aesCBCEncrypt);
std::function<std::string(const unsigned char* _encryptedData, size_t _encryptedDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)>
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::aesCBCDecrypt);

void dev::crypto::initSMCtypro()
{
    dev::crypto::SymmetricEncrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::SM4Encrypt);
    dev::crypto::SymmetricDecrypt = static_cast<std::string (*)(const unsigned char*, size_t,
        const unsigned char*, size_t, const unsigned char*)>(dev::SM4Decrypt);
}
