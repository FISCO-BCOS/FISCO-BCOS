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
 * @author Alex Leverington <nessence@gmail.com> Asherli
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
using namespace dev;
using namespace dev::crypto;
using namespace std;

string dev::aesCBCEncrypt(const string& _plainData, const string& _key)
{
    string ivData(_key.substr(0, 16));
    string cipherData;
    CryptoPP::AES::Encryption aesEncryption((const unsigned char*)_key.data(), _key.size());
    CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption(
        aesEncryption, (const unsigned char*)ivData.data());
    CryptoPP::StreamTransformationFilter stfEncryptor(
        cbcEncryption, new CryptoPP::StringSink(cipherData));
    stfEncryptor.Put((const unsigned char*)_plainData.data(), _plainData.size());
    stfEncryptor.MessageEnd();
    return cipherData;
}

string dev::aesCBCDecrypt(const string& _cypherData, const string& _key)
{
    string ivData(_key.substr(0, 16));
    // LOG(DEBUG)<<"AES DE TYPE....................";
    string decryptedData;
    CryptoPP::AES::Decryption aesDecryption((const unsigned char*)_key.data(), _key.size());
    CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(
        aesDecryption, (const unsigned char*)ivData.data());
    CryptoPP::StreamTransformationFilter stfDecryptor(
        cbcDecryption, new CryptoPP::StringSink(decryptedData));
    // stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
    stfDecryptor.Put((const unsigned char*)_cypherData.data(), _cypherData.size());
    stfDecryptor.MessageEnd();
    return decryptedData;
}

bytes dev::aesCBCEncrypt(bytesConstRef _plainData, bytesConstRef _key)
{
    bytesConstRef ivData = _key.cropped(0, 16);
    string cipherData;
    CryptoPP::AES::Encryption aesEncryption(_key.data(), _key.size());
    CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption(aesEncryption, ivData.data());
    CryptoPP::StreamTransformationFilter stfEncryptor(
        cbcEncryption, new CryptoPP::StringSink(cipherData));
    stfEncryptor.Put(_plainData.data(), _plainData.size());
    stfEncryptor.MessageEnd();
    return asBytes(cipherData);
}

bytes dev::aesCBCDecrypt(bytesConstRef _cypherData, bytesConstRef _key)
{
    bytes ivData = _key.cropped(0, 16).toBytes();
    // bytesConstRef ivData = _key.cropped(0, 16);
    // LOG(DEBUG)<<"AES DE TYPE....................";
    string decryptedData;
    CryptoPP::AES::Decryption aesDecryption(_key.data(), _key.size());
    CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(aesDecryption, ref(ivData).data());
    CryptoPP::StreamTransformationFilter stfDecryptor(
        cbcDecryption, new CryptoPP::StringSink(decryptedData));
    // stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
    stfDecryptor.Put(_cypherData.data(), _cypherData.size());
    stfDecryptor.MessageEnd();
    return asBytes(decryptedData);
}
