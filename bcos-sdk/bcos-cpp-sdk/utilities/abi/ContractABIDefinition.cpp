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
 * @file ContractABIDefinition.cpp
 * @author: octopus
 * @date 2022-05-19
 */

#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include "bcos-cpp-sdk/utilities/Common.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"
#include "bcos-utilities/BoostLog.h"
#include <json/reader.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;

const std::string ContractABIMethodDefinition::CONSTRUCTOR_TYPE = "constructor";
const std::string ContractABIMethodDefinition::FUNCTION_TYPE = "function";
const std::string ContractABIMethodDefinition::EVENT_TYPE = "event";
const std::string ContractABIMethodDefinition::FALLBACK_TYPE = "fallback";
const std::string ContractABIMethodDefinition::RECEIVE_TYPE = "receive";

std::vector<std::string> ContractABIDefinition::methodIDs(
    const std::string& _methodName, bcos::crypto::Hash::Ptr _hashImpl) const
{
    std::vector<std::string> ids;
    auto it = m_methods.find(_methodName);
    if (it == m_methods.end())
    {
        return ids;
    }

    for (const auto& method : it->second)
    {
        ids.push_back(method->getMethodIDAsString(_hashImpl));
    }

    return ids;
}

std::vector<std::string> ContractABIDefinition::methodNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, methods] : m_methods)
    {
        names.push_back(name);
    }
    return names;
}

std::vector<ContractABIMethodDefinition::Ptr> ContractABIDefinition::getMethod(
    const std::string& _methodName, bool _allowOverload)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("getMethod") << LOG_KV("methodName", _methodName);

    // check method exist or overloaded methods exist
    auto it = m_methods.find(_methodName);
    if (it == m_methods.end())
    {  // function not exist
        UTILITIES_ABI_LOG(WARNING)
            << LOG_BADGE("getMethod") << LOG_DESC("method not exist in the contract abi")
            << LOG_KV("methodsInABI", boost::join(methodNames(), ","))
            << LOG_KV("methodName", _methodName);
        BOOST_THROW_EXCEPTION(
            std::runtime_error("method not exist in the contract abi, method: " + _methodName));
    }

    if (!_allowOverload && it->second.size() != 1)
    {  // overload function
        UTILITIES_ABI_LOG(WARNING) << LOG_BADGE("getMethod")
                                   << LOG_DESC(_methodName +
                                               " is an overloaded method, please use "
                                               "\"getMethodByMethodID\" instead")
                                   << LOG_KV("methodName", _methodName);
        BOOST_THROW_EXCEPTION(std::runtime_error(
            _methodName + " is an overloaded method, please use \"getMethodByMethodID\" instead"));
    }

    return it->second;
}

ContractABIMethodDefinition::Ptr ContractABIDefinition::getMethodByMethodID(
    const std::string& _methodID, bcos::crypto::Hash::Ptr _hashImpl)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("getMethodByMethodID") << LOG_KV("methodID", _methodID);

    // check method exist or overloaded methods exist
    for (const auto& [name, methods] : m_methods)
    {
        for (const auto& method : methods)
        {
            if (method->getMethodIDAsString(_hashImpl) == _methodID)
            {
                return method;
            }
        }
    }

    UTILITIES_ABI_LOG(WARNING) << LOG_BADGE("getMethodByMethodID")
                               << LOG_DESC("methodID not exist in the contract abi")
                               << LOG_KV("methodID", _methodID);
    BOOST_THROW_EXCEPTION(
        std::runtime_error("methodID not exist in the contract abi, methodID: " + _methodID));
}

std::vector<ContractABIMethodDefinition::Ptr> ContractABIDefinition::getEvent(
    const std::string& _eventName, bool _allowOverload)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("getEvent") << LOG_KV("eventName", _eventName);

    // check method exist or overloaded methods exist
    auto it = m_events.find(_eventName);
    if (it == m_events.end())
    {  // function not exist
        UTILITIES_ABI_LOG(WARNING)
            << LOG_BADGE("getEvent") << LOG_DESC("event not exist in the contract abi")
            << LOG_KV("eventName", _eventName);
        BOOST_THROW_EXCEPTION(
            std::runtime_error("event not exist in the contract abi, event: " + _eventName));
    }

    if (!_allowOverload && it->second.size() != 1)
    {  // overload function
        UTILITIES_ABI_LOG(WARNING) << LOG_BADGE("getEvent")
                                   << LOG_DESC(_eventName +
                                               " is an overloaded event, please use "
                                               "\"getEventByTopic\" instead")
                                   << LOG_KV("eventName", _eventName);
        BOOST_THROW_EXCEPTION(std::runtime_error(
            _eventName + " is an overloaded event, please use \"getEventByTopic\" instead"));
    }

    return it->second;
}

ContractABIMethodDefinition::Ptr ContractABIDefinition::getEventByTopic(
    const std::string& _eventTopic, bcos::crypto::Hash::Ptr _hashImpl)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("getEventByTopic") << LOG_KV("topic", _eventTopic);

    // check method exist or overloaded methods exist
    for (const auto& [name, event] : m_events)
    {
        for (const auto& e : event)
        {
            if (e->getMethodIDAsString(_hashImpl) == _eventTopic)
            {
                return e;
            }
        }
    }

    UTILITIES_ABI_LOG(WARNING) << LOG_BADGE("getEventByTopic")
                               << LOG_DESC("event topic not exist in the contract abi")
                               << LOG_KV("topic", _eventTopic);
    BOOST_THROW_EXCEPTION(
        std::runtime_error("event topic not exist in the contract abi, topic: " + _eventTopic));
}