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
/** @file GmAES.cpp
 * @author Asherli
 * @date 2018
 */

#include "libdevcrypto/AES.h"
#include "libdevcrypto/Exceptions.h"
#include "sm4/sm4.h"
#include <openssl/sm4.h>
#include <stdlib.h>
#include <string.h>

using namespace dev;
using namespace dev::crypto;
using namespace std;

string dev::aesCBCEncrypt(const unsigned char* _plainData, const int _plainDataSize,
    const unsigned char* _key, const int _keySize, const unsigned char* _ivData)
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

string dev::aesCBCDecrypt(const unsigned char* _cypherData, const int _cypherDataSize,
    const unsigned char* _key, const int _keySize, const unsigned char* _ivData)
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

string dev::aesCBCEncrypt(const string& _plainData, const string& _key, const std::string& _ivData)
{
    return aesCBCEncrypt((const unsigned char*)_plainData.data(), _plainData.size(),
        (const unsigned char*)_key.data(), _key.size(), (const unsigned char*)_ivData.data());
}

string dev::aesCBCDecrypt(const string& _cypherData, const string& _key, const std::string& _ivData)
{
    return aesCBCDecrypt((const unsigned char*)_cypherData.data(), _cypherData.size(),
        (const unsigned char*)_key.data(), _key.size(), (const unsigned char*)_ivData.data());
}

string dev::aesCBCEncrypt(const string& _plainData, const string& _key)
{
    string ivData(_key.substr(0, 16));

    return aesCBCEncrypt(_plainData, _key, ivData);
}

string dev::aesCBCDecrypt(const string& _cypherData, const string& _key)
{
    string ivData(_key.substr(0, 16));
    return aesCBCDecrypt(_cypherData, _key, ivData);
    ;
}

bytes dev::aesCBCEncrypt(bytesConstRef _plainData, bytesConstRef _key, bytesConstRef _ivData)
{
    return asBytes(aesCBCEncrypt(
        _plainData.data(), _plainData.size(), _key.data(), _key.size(), _ivData.data()));
}

bytes dev::aesCBCDecrypt(bytesConstRef _cypherData, bytesConstRef _key, bytesConstRef _ivData)
{
    return asBytes(aesCBCDecrypt(
        _cypherData.data(), _cypherData.size(), _key.data(), _key.size(), _ivData.data()));
}


bytes dev::aesCBCEncrypt(bytesConstRef _plainData, bytesConstRef _key)
{
    bytesConstRef ivData = _key.cropped(0, 16);
    return aesCBCEncrypt(_plainData, _key, ivData);
}

bytes dev::aesCBCDecrypt(bytesConstRef _cypherData, bytesConstRef _key)
{
    bytesConstRef ivData = _key.cropped(0, 16);
    return aesCBCDecrypt(_cypherData, _key, ivData);
}
