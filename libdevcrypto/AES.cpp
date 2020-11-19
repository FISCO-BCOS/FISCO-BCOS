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
/** @file AES.cpp
 * @author Asherli
 * @date 2018
 */

#include "AES.h"
#include "Exceptions.h"
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/sha.h>
#include <stdlib.h>
#include <string>


using namespace std;
using namespace bcos;
using namespace bcos::crypto;

string bcos::crypto::aesCBCEncrypt(const unsigned char* _plainData, size_t _plainDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)
{
    string cipherData;
    CryptoPP::AES::Encryption aesEncryption(_key, _keySize);
    CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption(aesEncryption, _ivData);
    CryptoPP::StreamTransformationFilter stfEncryptor(
        cbcEncryption, new CryptoPP::StringSink(cipherData));
    stfEncryptor.Put(_plainData, _plainDataSize);
    stfEncryptor.MessageEnd();
    return cipherData;
}

string bcos::crypto::aesCBCDecrypt(const unsigned char* _cypherData, size_t _cypherDataSize,
    const unsigned char* _key, size_t _keySize, const unsigned char* _ivData)
{
    string decryptedData;
    CryptoPP::AES::Decryption aesDecryption(_key, _keySize);
    CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(aesDecryption, _ivData);
    CryptoPP::StreamTransformationFilter stfDecryptor(
        cbcDecryption, new CryptoPP::StringSink(decryptedData));
    // stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
    stfDecryptor.Put(_cypherData, _cypherDataSize);
    stfDecryptor.MessageEnd();
    return decryptedData;
}

string bcos::crypto::aesCBCEncrypt(
    const string& _plainData, const string& _key, const std::string& _ivData)
{
    return aesCBCEncrypt((const unsigned char*)_plainData.data(), _plainData.size(),
        (const unsigned char*)_key.data(), _key.size(), (const unsigned char*)_ivData.data());
}

string bcos::crypto::aesCBCDecrypt(
    const string& _cypherData, const string& _key, const std::string& _ivData)
{
    return aesCBCDecrypt((const unsigned char*)_cypherData.data(), _cypherData.size(),
        (const unsigned char*)_key.data(), _key.size(), (const unsigned char*)_ivData.data());
}

string bcos::crypto::aesCBCEncrypt(const string& _plainData, const string& _key)
{
    string ivData(_key.substr(0, 16));
    return aesCBCEncrypt(_plainData, _key, ivData);
}

string bcos::crypto::aesCBCDecrypt(const string& _cypherData, const string& _key)
{
    string ivData(_key.substr(0, 16));
    return aesCBCDecrypt(_cypherData, _key, ivData);
}
