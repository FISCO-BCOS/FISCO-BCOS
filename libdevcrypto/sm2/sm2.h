/*
    This file is part of fisco-bcos.

    fisco-bcos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    fisco-bcos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with fisco-bcos.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: sm2.h
 * @author: websterchen
 *
 * @date: 2018
 */
#pragma once
#include "libdevcore/Log.h"
#include <openssl/sm2.h>
#include <openssl/sm3.h>
#include <iostream>
#include <string>
#define CRYPTO_LOG(LEVEL) LOG(LEVEL) << "[CRYPTO] "

class SM2
{
public:
    bool genKey();
    std::string getPublicKey();
    std::string getPrivateKey();
    bool sign(const char* originalData, int originalDataLen, const std::string& privateKey,
        unsigned char* r, unsigned char* s);
    int verify(const unsigned char* _signData, size_t _sigLenth, const unsigned char* _originalData,
        size_t _originalLength, const unsigned char* _publicKey);
    std::string priToPub(const std::string& privateKey);
    char* strlower(char* s);
    std::string ascii2hex(const char* chs, int len);
    static SM2& getInstance();

    static int sm2GetZ(std::string const& _privateKey, const EC_KEY* _ecKey, unsigned char* _zValue,
        size_t& _zValueLen);
    
    static int sm2GetZFromPublicKey(std::string const & _publicKeyHex, unsigned char* _zValue, size_t& _zValueLen);

private:
    std::string publicKey;
    std::string privateKey;
};