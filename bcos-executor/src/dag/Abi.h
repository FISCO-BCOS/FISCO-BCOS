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
 * @brief ABI data structure used in transaction construction
 * @file Abi.h
 * @author: catli
 * @date: 2021-09-11
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace bcos
{
namespace executor
{
struct ConflictField
{
    std::uint8_t kind;
    std::vector<std::uint8_t> accessPath;
    bool readOnly;
    std::uint8_t slot;
};

struct ParameterAbi
{
    std::string type;
    std::vector<ParameterAbi> components;

    ParameterAbi() = default;

    ParameterAbi(const std::string& type) { this->type = type; }

    ParameterAbi(const std::string& type, const std::vector<ParameterAbi>& components)
    {
        this->type = type;
        this->components = components;
    }

    friend std::ostream& operator<<(std::ostream& output, const ParameterAbi& param)
    {
        output << "{\"type\": " << param.type << ", \"components\": [";
        for (auto& component : param.components)
        {
            output << component;
        }
        output << "]}";
        return output;
    }
};

struct FunctionAbi
{
    std::string name;
    std::vector<ParameterAbi> inputs;
    std::vector<ConflictField> conflictFields;

    static std::unique_ptr<FunctionAbi> deserialize(
        std::string_view abiStr, const bcos::bytes& expected, bcos::crypto::Hash::Ptr hashImpl);
};
}  // namespace executor
}  // namespace bcos