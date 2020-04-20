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
/** @file AES.h
 * @author Alex Leverington <nessence@gmail.com> asherli
 * @date 2018
 *
 * AES
 * todo: use openssl tassl
 */

#pragma once

#include "Common.h"

namespace dev
{
DEV_SIMPLE_EXCEPTION(AESKeyLengthError);
bytes aesCBCEncrypt(bytesConstRef _plainData, bytesConstRef _key);
bytes aesCBCDecrypt(bytesConstRef _cypherData, bytesConstRef _key);
bytes aesCBCEncrypt(bytesConstRef _plainData, bytesConstRef _key, bytesConstRef _ivData);
bytes aesCBCDecrypt(bytesConstRef _cypherData, bytesConstRef _key, bytesConstRef _ivData);
std::string aesCBCEncrypt(const std::string& _plainData, const std::string& _key);
std::string aesCBCDecrypt(const std::string& _cypherData, const std::string& _key);
std::string aesCBCEncrypt(
    const std::string& _plainData, const std::string& _key, const std::string& _ivData);
std::string aesCBCDecrypt(
    const std::string& _cypherData, const std::string& _key, const std::string& _ivData);
std::string aesCBCEncrypt(const unsigned char* _plainData, const int _plainDataSize,
    const unsigned char* _key, const int _keySize, const unsigned char* _ivData);
std::string aesCBCDecrypt(const unsigned char* _cypherData, const int _cypherDataSize,
    const unsigned char* _key, const int _keySize, const unsigned char* _ivData);
}  // namespace dev
