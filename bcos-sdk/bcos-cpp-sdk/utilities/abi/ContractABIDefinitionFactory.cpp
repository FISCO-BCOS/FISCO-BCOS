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
 * @file ContractABIDefinitionFactory.cpp
 * @author: octopus
 * @date 2022-05-23
 */
#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinitionFactory.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"
#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <cstring>
#include <stdexcept>
#include <type_traits>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;

ContractABIDefinition::Ptr ContractABIDefinitionFactory::buildABI(const std::string& _abi)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildABI") << LOG_KV("abi", _abi);

    Json::Value jABI;
    try
    {
        Json::Reader reader;
        if (!reader.parse(_abi, jABI))
        {
            UTILITIES_ABI_LOG(WARNING)
                << LOG_BADGE("buildABI") << LOG_DESC("invalid JSON string") << LOG_KV("abi", _abi);
            BOOST_THROW_EXCEPTION(std::runtime_error("invalid JSON string"));
        }
    }
    catch (std::runtime_error& re)
    {
        std::exception_ptr except_ptr = std::current_exception();
        std::rethrow_exception(except_ptr);
    }
    catch (std::exception& e)
    {
        UTILITIES_ABI_LOG(WARNING) << LOG_BADGE("buildABI") << LOG_DESC("parsing JSON exception")
                                   << LOG_KV("exception", e.what()) << LOG_KV("abi", _abi);
        BOOST_THROW_EXCEPTION(std::runtime_error("parsing JSON exception"));
    }

    return buildABI(jABI);
}

ContractABIDefinition::Ptr ContractABIDefinitionFactory::buildABI(const Json::Value& _jAbi)
{
    std::vector<ContractABIMethodDefinition::Ptr> methods;
    for (Json::ArrayIndex index = 0; index < _jAbi.size(); index++)
    {
        methods.push_back(buildMethod(_jAbi[index]));
    }

    auto contractABIDefinition = std::make_shared<ContractABIDefinition>();
    std::for_each(methods.begin(), methods.end(),
        [&contractABIDefinition, _jAbi](ContractABIMethodDefinition::Ptr _method) mutable {
            UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildABI") << LOG_KV("name", _method->name())
                                     << LOG_KV("type", _method->type())
                                     << LOG_KV("sig", _method->getMethodSignatureAsString());

            if (_method->type() == ContractABIMethodDefinition::CONSTRUCTOR_TYPE)
            {
                contractABIDefinition->addMethod(
                    ContractABIMethodDefinition::CONSTRUCTOR_TYPE, _method);
            }
            else if (_method->type() == ContractABIMethodDefinition::FALLBACK_TYPE)
            {
                contractABIDefinition->addMethod(
                    ContractABIMethodDefinition::FALLBACK_TYPE, _method);
            }

            else if (_method->type() == ContractABIMethodDefinition::RECEIVE_TYPE)
            {
                contractABIDefinition->addMethod(
                    ContractABIMethodDefinition::RECEIVE_TYPE, _method);
            }
            else if (_method->type() == ContractABIMethodDefinition::FUNCTION_TYPE)
            {
                contractABIDefinition->addMethod(_method->name(), _method);
            }
            else if (_method->type() == ContractABIMethodDefinition::EVENT_TYPE)
            {
                contractABIDefinition->addEvent(_method);
            }
            else
            {
                UTILITIES_ABI_LOG(WARNING)
                    << LOG_BADGE("buildABI") << LOG_DESC("unrecognized type")
                    << LOG_KV("name", _method->name()) << LOG_KV("type", _method->type())
                    << LOG_KV("abi", _jAbi.toStyledString());
            }
        });

    return contractABIDefinition;
}

std::vector<ContractABIMethodDefinition::Ptr> ContractABIDefinitionFactory::buildMethod(
    const std::string& _abi, const std::string& _methodName)
{
    Json::Value jABI;
    try
    {
        Json::Reader reader;
        if (!reader.parse(_abi, jABI))
        {
            UTILITIES_ABI_LOG(WARNING)
                << LOG_BADGE("buildMethod") << LOG_DESC("invalid JSON string")
                << LOG_KV("funcName", _methodName) << LOG_KV("abi", _abi);
            BOOST_THROW_EXCEPTION(std::runtime_error("invalid JSON string"));
        }
    }
    catch (std::runtime_error& re)
    {
        std::exception_ptr except_ptr = std::current_exception();
        std::rethrow_exception(except_ptr);
    }
    catch (std::exception& e1)
    {
        UTILITIES_ABI_LOG(WARNING)
            << LOG_BADGE("buildMethod") << LOG_DESC("parsing JSON exception")
            << LOG_KV("error", e1.what()) << LOG_KV("funcName", _methodName) << LOG_KV("abi", _abi);
        BOOST_THROW_EXCEPTION(std::runtime_error("parsing JSON exception"));
    }

    std::vector<ContractABIMethodDefinition::Ptr> methods;
    for (Json::ArrayIndex index = 0; index < jABI.size(); index++)
    {
        std::string name = jABI[index]["name"].asString();
        std::string type = jABI[index]["type"].asString();

        if ((type == ContractABIMethodDefinition::EVENT_TYPE))
        {  // event type or not the match function, continue
            continue;
        }

        if ((_methodName == name) ||
            ((_methodName == ContractABIMethodDefinition::CONSTRUCTOR_TYPE) &&
                (type == _methodName)))
        {
            methods.push_back(buildMethod(jABI[index]));
        }
    }

    return methods;
}

ContractABIMethodDefinition::UniquePtr ContractABIDefinitionFactory::buildMethod(
    const Json::Value& _jMethod)
{
    std::string name;
    // name: the name of the function or event
    if (_jMethod.isMember("name"))
    {
        name = _jMethod["name"].asString();
    }

    // type: "function", "constructor", "receive", "fallback", "event"
    if (!_jMethod.isMember("type"))
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("missing \"type\" field on the method"));
    }
    std::string type = _jMethod["type"].asString();

    // constant:
    bool constant = false;
    if (_jMethod.isMember("constant"))
    {
        constant = _jMethod["constant"].asBool();
    }

    // payable:
    bool payable = false;
    if (_jMethod.isMember("payable"))
    {
        payable = _jMethod["payable"].asBool();
    }

    // anonymous: true if event was declared as anonymous.
    bool anonymous = false;
    if (_jMethod.isMember("anonymous"))
    {
        anonymous = _jMethod["anonymous"].asBool();
    }

    // stateMutability: a string with one of the following values: pure (specified to not read
    // blockchain state), view (specified to not modify the blockchain state), nonpayable
    // (function does not accept Ether - the default) and payable (function accepts Ether).
    std::string stateMutability;
    if (_jMethod.isMember("stateMutability"))
    {
        stateMutability = _jMethod["stateMutability"].asString();
    }

    std::vector<NamedType::Ptr> inputs;
    // input
    if (_jMethod.isMember("inputs"))
    {
        const auto& jInput = _jMethod["inputs"];

        for (Json::ArrayIndex index = 0; index < jInput.size(); index++)
        {
            inputs.push_back(buildNamedType(jInput[index]));
        }
    }

    std::vector<NamedType::Ptr> outputs;
    // output
    if (_jMethod.isMember("outputs"))
    {
        const auto& jOutput = _jMethod["outputs"];

        for (Json::ArrayIndex index = 0; index < jOutput.size(); index++)
        {
            outputs.push_back(buildNamedType(jOutput[index]));
        }
    }

    auto function = std::make_unique<ContractABIMethodDefinition>();

    function->setName(name);
    function->setType(type);
    function->setConstant(constant);
    function->setPayable(payable);
    function->setAnonymous(anonymous);
    function->setStateMutability(stateMutability);
    function->setInputs(inputs);
    function->setOutputs(outputs);

    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildMethod") << LOG_KV("name", name)
                             << LOG_KV("type", type)
                             << LOG_KV("method sig", function->getMethodSignatureAsString());
    return function;
}

ContractABIMethodDefinition::UniquePtr ContractABIDefinitionFactory::buildMethod(
    const std::string& _methodSig)
{
    auto p0 = _methodSig.find("(");
    auto p1 = _methodSig.find(")");
    if ((p0 == std::string::npos) || (p1 == std::string::npos) || (p0 >= p1))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("invalid method signature: " + _methodSig));
    }

    std::string name = _methodSig.substr(0, p0);
    boost::trim(name);
    if (name.empty())
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("invalid method signature: " + _methodSig));
    }

    std::string strType = _methodSig.substr(p0 + 1, p1 - p0 - 1);
    boost::trim(strType);
    if (strType.find("tuple") != std::string::npos)
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("unsupported build method by signature contains tuple params"));
    }

    std::vector<std::string> types;
    boost::split(types, strType, boost::is_any_of(","));

    std::vector<NamedType::Ptr> inputs;
    for (std::string& type : types)
    {
        boost::trim(type);
        if (type.empty())
        {
            continue;
        }

        inputs.push_back(buildNamedType("", type));
    }

    auto function = std::make_unique<ContractABIMethodDefinition>();

    function->setName(name);
    function->setType(ContractABIMethodDefinition::FUNCTION_TYPE);
    function->setInputs(inputs);

    return function;
}

Struct::UniquePtr ContractABIDefinitionFactory::buildEvent(
    const ContractABIMethodDefinition& _event)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildEvent") << LOG_KV("name", _event.name())
                             << LOG_KV("event sig", _event.getMethodSignatureAsString());


    auto sPtr = Struct::newValue();

    const auto& inputs = _event.inputs();
    for (const auto& input : inputs)
    {
        // TODO: how to handle indexed params
        if (input->indexed())
        {
            continue;
        }

        auto helper = std::make_shared<NamedTypeHelper>(input->type());
        auto abstractType = buildAbstractType(input, helper);
        sPtr->addMember(std::move(abstractType));
    }

    return sPtr;
}

Struct::UniquePtr ContractABIDefinitionFactory::buildInput(
    const ContractABIMethodDefinition& _method)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildInput") << LOG_KV("name", _method.name())
                             << LOG_KV("method sig", _method.getMethodSignatureAsString());

    auto sPtr = Struct::newValue();

    const auto& inputs = _method.inputs();
    for (const auto& input : inputs)
    {
        auto helper = std::make_shared<NamedTypeHelper>(input->type());
        sPtr->addMember(buildAbstractType(input, helper));
    }

    return sPtr;
}

Struct::UniquePtr ContractABIDefinitionFactory::buildOutput(
    const ContractABIMethodDefinition& _method)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("buildOutput") << LOG_KV("name", _method.name())
                             << LOG_KV("method sig", _method.getMethodSignatureAsString());

    auto sPtr = Struct::newValue();

    const auto& outputs = _method.outputs();
    for (const auto& output : outputs)
    {
        auto helper = std::make_shared<NamedTypeHelper>(output->type());
        sPtr->addMember(buildAbstractType(output, helper));
    }

    return sPtr;
}

NamedType::Ptr ContractABIDefinitionFactory::buildNamedType(std::string _name, std::string _type)
{
    // TODO:: check _type valid solc/liquid type string
    auto namedType = std::make_shared<NamedType>(_name, _type);
    return namedType;
}


NamedType::Ptr ContractABIDefinitionFactory::buildNamedType(const Json::Value& _jType)
{
    // name:
    std::string name;
    if (_jType.isMember("name"))
    {
        name = _jType["name"].asString();
    }

    // type:
    if (!_jType.isMember("type"))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("invalid type in abi, missing \"type\" field"));
    }
    std::string type = _jType["type"].asString();

    // indexed
    bool indexed = false;
    if (_jType.isMember("indexed"))
    {
        indexed = _jType["indexed"].asBool();
    }

    // components
    std::vector<NamedType::Ptr> components;
    if (_jType.isMember("components"))
    {
        const auto& jComponents = _jType["components"];
        for (Json::ArrayIndex index = 0; index < jComponents.size(); index++)
        {
            components.push_back(buildNamedType(jComponents[index]));
        }
    }

    auto namedType = std::make_shared<NamedType>(name, type, indexed, components);
    return namedType;
}

AbstractType::UniquePtr ContractABIDefinitionFactory::buildAbstractType(
    NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper)
{
    AbstractType::UniquePtr abstractType = nullptr;
    if (_namedTypeHelper->isFixedList())
    {
        // T[N] => fixed list
        abstractType = buildFixedList(_namedType, _namedTypeHelper);
    }
    else if (_namedTypeHelper->isDynamicList())
    {
        // T[] => dynamic list
        abstractType = buildDynamicList(_namedType, _namedTypeHelper);
    }
    else if (_namedTypeHelper->isTuple())
    {
        // tuple(T0,T1,T2...)
        abstractType = buildTuple(_namedType);
    }
    else
    {
        // basic type
        abstractType = buildBaseAbstractType(_namedTypeHelper);
    }

    abstractType->setName(_namedType->name());

    return abstractType;
}

AbstractType::UniquePtr ContractABIDefinitionFactory::buildDynamicList(
    NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper)
{
    // reduce dimension
    _namedTypeHelper->removeExtent();

    auto abstractType = DynamicList::newValue();
    abstractType->addMember(buildAbstractType(_namedType, _namedTypeHelper));

    return abstractType;
}

AbstractType::UniquePtr ContractABIDefinitionFactory::buildFixedList(
    NamedType::ConstPtr _namedType, NamedTypeHelper::Ptr _namedTypeHelper)
{
    assert(_namedTypeHelper->isFixedList());
    auto dimension = _namedTypeHelper->extent(_namedTypeHelper->extentsSize());
    auto abstractType = FixedList::newValue(dimension);

    // reduce dimension
    _namedTypeHelper->removeExtent();

    for (std::size_t index = 0; index < dimension; index++)
    {
        abstractType->addMember(buildAbstractType(_namedType, _namedTypeHelper));
    }

    return abstractType;
}

AbstractType::UniquePtr ContractABIDefinitionFactory::buildTuple(NamedType::ConstPtr _namedType)
{
    auto sPtr = Struct::newValue();
    const auto& components = _namedType->components();
    for (auto component : components)
    {
        auto helper = std::make_shared<NamedTypeHelper>(component->type());
        sPtr->addMember(buildAbstractType(component, helper));
    }
    return sPtr;
}

AbstractType::UniquePtr ContractABIDefinitionFactory::buildBaseAbstractType(
    NamedTypeHelper::Ptr _namedTypeHelper)
{
    AbstractType::UniquePtr abstractType = nullptr;

    std::string _strType = _namedTypeHelper->baseType();

    if (boost::starts_with(_strType, "uint"))
    {
        std::size_t extent = 256;
        if (_strType.length() > strlen("uint"))
        {
            try
            {
                extent = std::stoi(_strType.substr(strlen("uint")));
            }
            catch (std::exception& _e)
            {
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("invalid uintN, it should be in format as "
                                       "uint8,uint16 ... uint248,uint256, type is " +
                                       _strType));
            }
        }

        abstractType = Uint::newValue(extent, u256(0));
    }
    else if (boost::starts_with(_strType, "int"))
    {
        std::size_t extent = 256;
        if (_strType.length() > strlen("int"))
        {
            try
            {
                extent = std::stoi(_strType.substr(strlen("int")));
            }
            catch (std::exception& _e)
            {
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("invalid intN, it should be in format as "
                                       "int8,int16 ... int248,int256, type is " +
                                       _strType));
            }
        }

        abstractType = Int::newValue(extent, s256(0));
    }
    else if (boost::starts_with(_strType, "bool"))
    {
        abstractType = Boolean::newValue();
    }
    else if (boost::starts_with(_strType, "string"))
    {
        abstractType = String::newValue();
    }
    else if (_strType == "bytes")
    {
        abstractType = DynamicBytes::newValue();
    }
    else if (boost::starts_with(_strType, "bytes"))
    {
        std::size_t fixedN = 0;
        try
        {
            fixedN = std::stoi(_strType.substr(strlen("bytes")));
        }
        catch (std::exception& _e)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid bytesN(bytes1~bytes32), type is " + _strType));
        }

        abstractType = FixedBytes::newValue(fixedN);
    }

    else if (boost::starts_with(_strType, "address"))
    {
        abstractType = Addr::newValue();
    }
    // else if (boost::starts_with(_strType, "fixed") || boost::starts_with(_strType, "ufixed"))
    //  {}
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("Unrecognized type:" + _strType));
    }

    return abstractType;
}
