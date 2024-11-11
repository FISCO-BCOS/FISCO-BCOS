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
 * @brief : KmsWrapper
 * @file KmsWrapper.h
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#pragma once
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../Common.h"

namespace bcos::security
{
class KMSWrapper
{
public:
    virtual std::shared_ptr<bytes> encrypt(
        const std::string& keyId, const std::string& filePath) = 0;

    virtual std::shared_ptr<bytes> decrypt(const std::shared_ptr<bytes>& ciphertext) = 0;
};
}