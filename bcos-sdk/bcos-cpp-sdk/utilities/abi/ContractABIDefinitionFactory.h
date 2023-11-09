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
 * @file ContractABIDefinitionFactory.h
 * @author: octopus
 * @date 2022-05-23
 */
#pragma once

#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIType.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace bcos
{
namespace cppsdk
{
namespace abi
{

class ContractABIDefinitionFactory
{
public:
    using Ptr = std::shared_ptr<ContractABIDefinitionFactory>;
    using ConstPtr = std::shared_ptr<const ContractABIDefinitionFactory>;
    using UniquePtr = std::unique_ptr<ContractABIDefinitionFactory>;

public:
    ContractABIDefinition::Ptr buildABI(const std::string& _abi);
    ContractABIDefinition::Ptr buildABI(const Json::Value& _jAbi);

public:
    ContractABIMethodDefinition::UniquePtr buildMethod(const std::string& _methodSig);
    std::vector<ContractABIMethodDefinition::Ptr> buildMethod(
        const std::string& _abi, const std::string& _methodName);
    ContractABIMethodDefinition::UniquePtr buildMethod(const Json::Value& _jMethod);

public:
    Struct::UniquePtr buildInput(const ContractABIMethodDefinition& _method);
    Struct::UniquePtr buildOutput(const ContractABIMethodDefinition& _method);
    Struct::UniquePtr buildEvent(const ContractABIMethodDefinition& _event);

public:
    NamedType::Ptr buildNamedType(const Json::Value& _jType);
    NamedType::Ptr buildNamedType(std::string _name, std::string _type);

public:
    AbstractType::UniquePtr buildAbstractType(
        NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper);

    AbstractType::UniquePtr buildDynamicList(
        NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper);
    AbstractType::UniquePtr buildFixedList(
        NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper);
    AbstractType::UniquePtr buildTuple(NamedType::ConstPtr _namedType);

    AbstractType::UniquePtr buildBaseAbstractType(NamedTypeHelper::Ptr _namedTypeHelper);
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos