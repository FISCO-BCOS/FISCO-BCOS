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
int SM3Hash::init(SM3_CTX* c)
{
    return ::SM3_Init(c);
}

int SM3Hash::update(SM3_CTX* c, const void* data, size_t len)
{
    return ::SM3_Update(c, data, len);
}

int SM3Hash::final(unsigned char* md, SM3_CTX* c)
{
    return ::SM3_Final(md, c);
}

unsigned char* SM3Hash::sm3(const unsigned char* d, size_t n, unsigned char* md)
{
    return ::SM3(d, n, md);
}

void SM3Hash::transForm(SM3_CTX* c, const unsigned char* data)
{
    ::SM3_Transform(c, data);
}

SM3Hash& SM3Hash::getInstance()
{
    static SM3Hash sm3;
    return sm3;
}
