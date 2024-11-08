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
 * @date: 2024-11-07
 */
#pragma once
#include <stdexcept>
#include <string>
#include <magic_enum.hpp>

enum class KeyEncryptionType
{
    DISABLED,
    DEFAULT,
    BKMS,
    KMS,
    HSM
};

// get the string of the enum
inline std::string GetKeyEncryptionTypeString(KeyEncryptionType type)
{
    return std::string(magic_enum::enum_name(type));
}

// get the enum from the string
inline KeyEncryptionType GetKeyEncryptionTypeFromString(const std::string& typeStr)
{
   auto result = magic_enum::enum_cast<KeyEncryptionType>(typeStr);
   if (!result)
   {
       return KeyEncryptionType::DEFAULT;
   }
   return result.value();
}