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
 * @file: sm3.h
 * @author: websterchen
 *
 * @date: 2018
 */
#pragma once
#include "internal/sm3.h"
#include <string.h>
#include <memory>
class SM3Hash
{
public:
    unsigned char* sm3(const unsigned char* d, size_t n, unsigned char* md);
    static SM3Hash& getInstance();
};