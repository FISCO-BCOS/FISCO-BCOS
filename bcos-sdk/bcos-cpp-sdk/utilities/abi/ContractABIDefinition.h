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
 * @file ContractABIDefinition.h
 * @author: octopus
 * @date 2022-05-19
 */
#pragma once

#include <json/json.h>
#include <json/value.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"

namespace bcos
{
namespace cppsdk
{
namespace abi
{

class ContractABIDefinition
{
public:
    using Ptr = std::shared_ptr<ContractABIDefinition>;
    using ConstPtr = std::shared_ptr<const ContractABIDefinition>;
    using UniquePtr = std::unique_ptr<ContractABIDefinition>;

public:
    ContractABIDefinition() = default;

private:
    // name => methods, support overload
    std::unordered_map<std::string, std::vector<ContractABIMethodDefinition::Ptr>> m_methods;
    // name => events, support overload
    std::unordered_map<std::string, std::vector<ContractABIMethodDefinition::Ptr>> m_events;

public:
    void addMethod(const std::string& _name, ContractABIMethodDefinition::Ptr _method)
    {
        m_methods[_name].push_back(_method);
    }

    void addEvent(ContractABIMethodDefinition::Ptr _event)
    {
        m_events[_event->name()].push_back(_event);
    }

    std::unordered_map<std::string, std::vector<ContractABIMethodDefinition::Ptr>> const& methods()
        const
    {
        return m_methods;
    }

    std::unordered_map<std::string, std::vector<ContractABIMethodDefinition::Ptr>> const& events()
        const
    {
        return m_events;
    }

    std::vector<std::string> methodNames() const;

    std::vector<std::string> methodIDs(
        const std::string& _methodName, bcos::crypto::Hash::Ptr _hashImpl) const;

    std::vector<ContractABIMethodDefinition::Ptr> getMethod(
        const std::string& _methodName, bool allowOverload = false);

    ContractABIMethodDefinition::Ptr getMethodByMethodID(
        const std::string& _methodID, bcos::crypto::Hash::Ptr _hashImpl);

    std::vector<ContractABIMethodDefinition::Ptr> getEvent(
        const std::string& _eventName, bool allowOverload = false);

    ContractABIMethodDefinition::Ptr getEventByTopic(
        const std::string& _eventTopic, bcos::crypto::Hash::Ptr _hashImpl);
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos