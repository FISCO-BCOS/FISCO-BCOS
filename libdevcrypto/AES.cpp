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
#include <cryptopp/aes.h>
#include <cryptopp/filters.h>
#include <cryptopp/modes.h>
#include <cryptopp/pwdbased.h>
#include <cryptopp/sha.h>
#include <libdevcore/easylog.h>
#include <string.h>

using namespace dev;
using namespace dev::crypto;
using namespace std;

bytes dev::aesDecrypt(
    bytesConstRef _ivCipher, std::string const& _password, unsigned _rounds, bytesConstRef _salt)
{
    bytes pw = asBytes(_password);

    if (!_salt.size())
        _salt = &pw;

    bytes target(64);
    CryptoPP::PKCS5_PBKDF2_HMAC<CryptoPP::SHA256>().DeriveKey(
        target.data(), target.size(), 0, pw.data(), pw.size(), _salt.data(), _salt.size(), _rounds);

    try
    {
        CryptoPP::AES::Decryption aesDecryption(target.data(), 16);
        auto cipher = _ivCipher.cropped(16);
        auto iv = _ivCipher.cropped(0, 16);
        CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(aesDecryption, iv.data());
        std::string decrypted;
        CryptoPP::StreamTransformationFilter stfDecryptor(
            cbcDecryption, new CryptoPP::StringSink(decrypted));
        stfDecryptor.Put(cipher.data(), cipher.size());
        stfDecryptor.MessageEnd();
        return asBytes(decrypted);
    }
    catch (exception const& e)
    {
        LOG(ERROR) << e.what();
        return bytes();
    }
}

char* ascii2hex(const char* chs, int len)
{
    char hex[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

    char* ascii = (char*)calloc(len * 3 + 1, sizeof(char));  // calloc ascii

    int i = 0;
    while (i < len)
    {
        int b = chs[i] & 0x000000ff;
        ascii[i * 2] = hex[b / 16];
        ascii[i * 2 + 1] = hex[b % 16];
        ++i;
    }
    return ascii;
}
bytes dev::aesCBCEncrypt(
    bytesConstRef plainData, string const& keyData, int keyLen, bytesConstRef ivData)
{
    // LOG(DEBUG)<<"AES EN TYPE......................";
    string cipherData;
    CryptoPP::AES::Encryption aesEncryption((const byte*)keyData.c_str(), keyLen);
    CryptoPP::CBC_Mode_ExternalCipher::Encryption cbcEncryption(aesEncryption, ivData.data());
    CryptoPP::StreamTransformationFilter stfEncryptor(
        cbcEncryption, new CryptoPP::StringSink(cipherData));
    stfEncryptor.Put(plainData.data(), plainData.size());
    stfEncryptor.MessageEnd();
    return asBytes(cipherData);
}

bytes dev::aesCBCDecrypt(
    bytesConstRef cipherData, string const& keyData, int keyLen, bytesConstRef ivData)
{
    // LOG(DEBUG)<<"AES DE TYPE....................";
    string decryptedData;
    CryptoPP::AES::Decryption aesDecryption((const byte*)keyData.c_str(), keyLen);
    CryptoPP::CBC_Mode_ExternalCipher::Decryption cbcDecryption(aesDecryption, ivData.data());
    CryptoPP::StreamTransformationFilter stfDecryptor(
        cbcDecryption, new CryptoPP::StringSink(decryptedData));
    // stfDecryptor.Put( reinterpret_cast<const unsigned char*>( cipherData.c_str() ),cipherLen);
    stfDecryptor.Put(cipherData.data(), cipherData.size());
    stfDecryptor.MessageEnd();
    return asBytes(decryptedData);
}

// bytes dev::aesCBCEncrypt(
//     bytesConstRef plainData, string const& keyData, int keyLen, bytesConstRef ivData)
// {
//     return origAesCBCEncrypt(plainData, keyData, keyLen, ivData);
// }

// bytes dev::aesCBCDecrypt(
//     bytesConstRef cipherData, string const& keyData, int keyLen, bytesConstRef ivData)
// {
//     return origAesCBCDecrypt(cipherData, keyData, keyLen, ivData);
// }
