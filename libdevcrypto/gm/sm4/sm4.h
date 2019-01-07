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
 * @file: sm4.h
 * @author: websterchen
 *
 * @date: 2018
 * 
 * @author: asherli
 *
 * @date: 2019
 * use GmSSL
 */
#pragma once
#include <openssl/sms4.h>
#include <stdio.h>
#include <stdlib.h>
class SM4
{
public:
    void setEncKey(const unsigned char* userKey);
    void setDecKey(const unsigned char* userKey);
    void encrypt(const unsigned char* in, unsigned char* out);
    void decrypt(const unsigned char* in, unsigned char* out);
    void cbcEncrypt(const unsigned char* in, unsigned char* out, size_t length, unsigned char* ivec,
        const int enc);

    static SM4& getInstance();

private:
    sms4_key_t key;
};