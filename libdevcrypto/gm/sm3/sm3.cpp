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
 * @file: sm3.cpp
 * @author: websterchen
 *
 * @date: 2018
 */

#include "sm3.h"
unsigned char* SM3Hash::sm3(const unsigned char* d, size_t n, unsigned char* md)
{
    SM3_CTX c;
    static unsigned char m[SM3_DIGEST_LENGTH];

    if (md == NULL)
      md = m;
    sm3_init(&c);
    sm3_update(&c, d, n);
    sm3_final(md, &c);
    /*OPENSSL_cleanse(&c, sizeof(c));*/
    memset(&c, 0, sizeof(SM3_CTX));
    return (md);
}


SM3Hash& SM3Hash::getInstance()
{
    static SM3Hash sm3;
    return sm3;
}