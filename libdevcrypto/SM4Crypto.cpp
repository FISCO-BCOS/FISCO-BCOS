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
/** @file SM4Crypto.cpp
 * @author Asherli
 * @date 2018
 */

#include "SM4Crypto.h"
#include "libdevcrypto/Exceptions.h"
#include "sm4/sm4.h"
#include <openssl/sm4.h>
#include <stdlib.h>
#include <string.h>

using namespace bcos;
using namespace bcos::crypto;
using namespace std;

string bcos::crypto::sm4Encrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)
{
    int padding = _plainDataSize % 16;
    int nSize = 16 - padding;
    int inDataVLen = _plainDataSize + nSize;
    bytes inDataV(inDataVLen);
    memcpy(inDataV.data(), _plainData, _plainDataSize);
    memset(inDataV.data() + _plainDataSize, nSize, nSize);

    string enData;
    enData.resize(inDataVLen);
    SM4::getInstance().setKey(_key, _keySize);
    SM4::getInstance().cbcEncrypt(
        inDataV.data(), (unsigned char*)enData.data(), inDataVLen, (unsigned char*)_ivData, 1);
    return enData;
}

string bcos::crypto::sm4Decrypt(const unsigned char* _cypherData, size_t _cypherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)
{
    string deData;
    deData.resize(_cypherDataSize);
    SM4::getInstance().setKey(_key, _keySize);
    SM4::getInstance().cbcEncrypt(
        _cypherData, (unsigned char*)deData.data(), _cypherDataSize, (unsigned char*)_ivData, 0);
    int padding = deData.at(_cypherDataSize - 1);
    int deLen = _cypherDataSize - padding;
    return deData.substr(0, deLen);
}
