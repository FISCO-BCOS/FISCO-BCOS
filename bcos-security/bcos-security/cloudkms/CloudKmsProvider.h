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
 * @file CloudKmsProvider.h
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#pragma once
#include <algorithm>
#include <array>
#include <magic_enum.hpp>
#include <string>
namespace bcos::security
{
enum class CloudKmsProvider
{
    AWS,      // Amazon Web Services KMS
    AZURE,    // Microsoft Azure Key Vault
    HUAWEI,   // Huawei Cloud KMS
    ALIYUN,   // Alibaba Cloud KMS
    TENCENT,  // Tencent Cloud KMS
    GCP,      // Google Cloud KMS
    BAIDU,    // Baidu Cloud KMS
    UNKNOWN   // Unknown KMS provider
};

class CloudKmsProviderHelper
{
public:
    // convert the provider name to the corresponding enum value
    inline static std::string ToString(CloudKmsProvider provider)
    {
        return std::string(magic_enum::enum_name(provider));
    };


    inline static CloudKmsProvider FromString(const std::string& kmsTypeStr)
    {
        auto result = magic_enum::enum_cast<CloudKmsProvider>(kmsTypeStr);
        if (!result)
        {
            return CloudKmsProvider::UNKNOWN;
        }
        return result.value();
    }
};
}  // namespace bcos::security