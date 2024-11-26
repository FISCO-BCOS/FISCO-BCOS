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
 *
 */
/**
 * @brief : Kms Provider
 * @file CloudKmsType.h
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#pragma once
#include <magic_enum.hpp>
#include <string>
namespace bcos::security
{
enum class CloudKmsType
{
    AWS,     // Amazon Web Services KMS
    UNKNOWN  // Unknown KMS provider
};


// convert the provider name to the corresponding enum value
inline static std::string cloudKmsTypeToString(CloudKmsType provider)
{
    return std::string(magic_enum::enum_name(provider));
};


inline static CloudKmsType cloudKmsTypeFromString(const std::string& kmsTypeStr)
{
    auto result = magic_enum::enum_cast<CloudKmsType>(kmsTypeStr);
    if (!result)
    {
        return CloudKmsType::UNKNOWN;
    }
    return result.value();
}
}  // namespace bcos::security