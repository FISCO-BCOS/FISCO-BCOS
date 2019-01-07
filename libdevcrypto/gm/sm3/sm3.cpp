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
 * 
 * @author: asherli
 *
 * @date: 2019 use GmSSL
 */

#include "sm3.h"
#include <iostream>
int SM3Hash::init(sm3_ctx_t* c)
{
    ::sm3_init(c);
    return 1;
}

int SM3Hash::update(sm3_ctx_t* c, const unsigned char* data, size_t len)
{
    ::sm3_update(c, data, len);
    return 1;
}

int SM3Hash::final(unsigned char* md, sm3_ctx_t* c)
{
    ::sm3_final(c, md);
    return 1;
}

unsigned char* SM3Hash::sm3(const unsigned char* d, size_t n, unsigned char* md)
{
    ::sm3(d, n, md);
    return md;
}

// void SM3Hash::transForm(sm3_ctx_t* c, const unsigned char* data)
// {
//     ::SM3_Transform(c, data);
// }

SM3Hash& SM3Hash::getInstance()
{
    static SM3Hash sm3;
    return sm3;
}