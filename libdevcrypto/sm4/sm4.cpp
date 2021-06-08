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
 * @file: sm4.cpp
 * @author: websterchen
 *
 * @date: 2018
 */

#include "sm4.h"
#include <stdlib.h>
#include <cstring>

int SM4::setKey(const unsigned char* userKey, size_t length)
{
#ifdef FISCO_SDF
    (void)length;
    return ::SM4_set_key(userKey, &key);
#else
    return ::SM4_set_key(userKey, length, &key);
#endif
}

void SM4::encrypt(const unsigned char* in, unsigned char* out)
{
    ::SM4_encrypt(in, out, &key);
}

void SM4::decrypt(const unsigned char* in, unsigned char* out)
{
    ::SM4_decrypt(in, out, &key);
}

void SM4::cbcEncrypt(
    const unsigned char* in, unsigned char* out, size_t length, unsigned char* ivec, const int enc)
{
    unsigned char* iv = (unsigned char*)malloc(16);
    std::memcpy(iv, ivec, 16);
    ::SM4_cbc_encrypt(in, out, length, &key, iv, enc);
    free(iv);
}

SM4& SM4::getInstance()
{
    static SM4 sm4;
    return sm4;
}