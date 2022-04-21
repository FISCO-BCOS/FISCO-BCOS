/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : Storage encrypt and decrypt helper
 * @author: 
 * @date: 2022-04-13
 */
#pragma once

#include "Common.h"

namespace bcos
{

namespace security
{

using EncHookFunction = std::function<void(std::string const&, std::string&)>;
using DecHookFunction = std::function<void(std::string&)>;

class StorageEncDecHelper
{
public:
    StorageEncDecHelper() = default;
    ~StorageEncDecHelper() = default;

public:
    static EncHookFunction getEncryptHandler(
        const std::vector<uint8_t>& _encryptKey, bool _enableCompress = false);
    static DecHookFunction getDecryptHandler(
        const std::vector<uint8_t>& _encryptKey, bool _enableCompress = false);

    static EncHookFunction getEncryptHandlerSM(
        const std::vector<uint8_t>& _encryptKey, bool _enableCompress = false);
    static DecHookFunction getDecryptHandlerSM(
        const std::vector<uint8_t>& _encryptKey, bool _enableCompress = false);
};

}

}