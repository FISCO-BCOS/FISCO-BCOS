/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
/**
 * @brief : Encrypt type enum
 * @author: HaoXuan40404
 * @date: 2024-11-26
 */
#pragma once
#include <magic_enum.hpp>
#include <string>

namespace bcos::security
{
enum class StorageEncryptionType
{
    LEGACY,
    BCOSKMS,
    UNKOWN
};

// get the string of the enum
inline static std::string storageEncryptionTypeToString(StorageEncryptionType type)
{
    return std::string(magic_enum::enum_name(type));
}

// get the enum from the string
inline static StorageEncryptionType storageEncryptionTypeFromString(const std::string& typeStr)
{
    if(typeStr.empty()) 
    {
        return StorageEncryptionType::LEGACY;
    }
    auto result = magic_enum::enum_cast<StorageEncryptionType>(typeStr, magic_enum::case_insensitive);
    if (!result)
    {
        return StorageEncryptionType::UNKOWN;
    }
    return result.value();
}
}  // namespace bcos::security