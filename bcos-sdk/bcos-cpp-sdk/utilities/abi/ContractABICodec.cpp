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
 * @file ContractABICodec.cpp
 * @author: octopus
 * @date 2022-05-25
 */

#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h>
#include <bcos-cpp-sdk/utilities/abi/ContractABIType.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Base64.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;
using namespace bcos::cppsdk::abi;

void ContractABICodec::stringToJsonValue(const std::string& _params, Json::Value& _jParams)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("stringToJsonValue") << LOG_KV("params", _params);

    try
    {
        Json::Reader reader;
        if (!reader.parse(_params, _jParams))
        {
            UTILITIES_ABI_LOG(WARNING)
                << LOG_BADGE("stringToJsonValue") << LOG_DESC("invalid JSON string")
                << LOG_KV("params", _params);
            BOOST_THROW_EXCEPTION(std::runtime_error("invalid JSON string"));
        }
    }
    catch (std::exception& e)
    {
        UTILITIES_ABI_LOG(WARNING)
            << LOG_BADGE("stringToJsonValue") << LOG_DESC("parsing JSON exception")
            << LOG_KV("exception", e.what()) << LOG_KV("params", _params);
        BOOST_THROW_EXCEPTION(
            std::runtime_error("invalid JSON string, parsing abi JSON exception"));
    }
}

//--------------------------------  encode begin --------------------------------
bcos::bytes ContractABICodec::encodeConstructor(
    const std::string& _abi, const std::string& _bin, const std::string& _jsonParams)
{
    return *fromHexString(_bin) +
           encodeMethod(_abi, ContractABIMethodDefinition::CONSTRUCTOR_TYPE, _jsonParams);
}

bcos::bytes ContractABICodec::encodeMethod(
    const std::string& _abi, const std::string& _methodName, const std::string& _jsonParams)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return encodeMethod(*contractABI->getMethod(_methodName, false)[0], _jsonParams);
}

bcos::bytes ContractABICodec::encodeMethodByMethodID(
    const std::string& _abi, const std::string& _methodID, const std::string& _jsonParams)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return encodeMethod(*contractABI->getMethodByMethodID(_methodID, m_hashImpl), _jsonParams);
}

bcos::bytes ContractABICodec::encodeMethodBySignature(
    const std::string& _methodSig, const std::string& _jsonParams)
{
    auto func = m_contractABIDefinitionFactory.buildMethod(_methodSig);
    return encodeMethod(*func, _jsonParams);
}

bcos::bytes ContractABICodec::encodeMethod(
    const ContractABIMethodDefinition& _method, const std::string& _jsonParams)
{
    UTILITIES_ABI_LOG(TRACE) << LOG_BADGE("encodeMethod")
                             << LOG_DESC("encode abi params by function object")
                             << LOG_KV("methodName", _method.name())
                             << LOG_KV("methodID",
                                    m_hashImpl ? _method.getMethodIDAsString(m_hashImpl) : "")
                             << LOG_KV("jsonParams", _jsonParams);
    Json::Value jParams;
    stringToJsonValue(_jsonParams, jParams);
    return encodeMethod(_method, jParams);
}

bcos::bytes ContractABICodec::encodeMethod(
    const ContractABIMethodDefinition& _method, const Json::Value& _jParams)
{
    auto input = m_contractABIDefinitionFactory.buildInput(_method);
    initAbstractTypeWithJsonParams(*input, _jParams);
    bcos::bytes buffer = _method.getMethodID(m_hashImpl);
    // bcos::bytes buffer;
    m_codec->serialize(*input, buffer);
    return buffer;
}

//--------------------------------  encode end ----------------------------------

//--------------------------------  decode begin --------------------------------

std::string ContractABICodec::decodeMethodInput(
    std::string _abi, std::string _methodName, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeMethodInput(*contractABI->getMethod(_methodName, false)[0], _data);
}

std::string ContractABICodec::decodeMethodInputByMethodID(
    std::string _abi, std::string _methodID, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeMethodInput(*contractABI->getMethodByMethodID(_methodID, m_hashImpl), _data);
}

std::string ContractABICodec::decodeMethodInput(
    const ContractABIMethodDefinition& _method, const bcos::bytes& _data)
{
    auto input = m_contractABIDefinitionFactory.buildInput(_method);
    m_codec->deserialize(static_cast<AbstractType&>(*input), _data, 4);

    return input->toJsonString();
}

std::string ContractABICodec::decodeMethodInputByMethodSig(
    const std::string& _methodSig, const bcos::bytes& _data)
{
    auto method = m_contractABIDefinitionFactory.buildMethod(_methodSig);
    return decodeMethodInput(*method, _data);
}

std::string ContractABICodec::decodeMethodOutput(
    std::string _abi, std::string _methodName, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeMethodOutput(*contractABI->getMethod(_methodName)[0], _data);
}

std::string ContractABICodec::decodeMethodOutputByMethodID(
    std::string _abi, std::string _methodID, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeMethodOutput(*contractABI->getMethodByMethodID(_methodID, m_hashImpl), _data);
}

std::string ContractABICodec::decodeMethodOutput(
    const ContractABIMethodDefinition& _method, const bcos::bytes& _data)
{
    auto output = m_contractABIDefinitionFactory.buildOutput(_method);
    m_codec->deserialize(static_cast<AbstractType&>(*output), _data, 0);

    return output->toJsonString();
}

//--------------------------------  decode end --------------------------------

std::string ContractABICodec::decodeEvent(
    const ContractABIMethodDefinition& _event, const bcos::bytes& _data)
{
    auto event = m_contractABIDefinitionFactory.buildEvent(_event);
    m_codec->deserialize(static_cast<AbstractType&>(*event), _data, 0);

    return event->toJsonString();
}

std::string ContractABICodec::decodeEvent(
    std::string _abi, std::string _eventName, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeEvent(*contractABI->getEvent(_eventName, false)[0], _data);
}

std::string ContractABICodec::decodeEventByTopic(
    std::string _abi, std::string _eventTopic, const bcos::bytes& _data)
{
    auto contractABI = m_contractABIDefinitionFactory.buildABI(_abi);
    return decodeEvent(*contractABI->getEventByTopic(_eventTopic, m_hashImpl), _data);
}

#define JsonTypeMatchCheck(__type_name, __json_params, _json_type)                               \
    do                                                                                           \
    {                                                                                            \
        if (!__json_params.isConvertibleTo(_json_type))                                          \
        {                                                                                        \
            BOOST_THROW_EXCEPTION(std::runtime_error(                                            \
                "type mismatch, the type of argument required is " + __type_name +               \
                " , the json parameter type is " +                                               \
                getJsonValueTypeAsString(__json_params.type()) + " ,the json object value is " + \
                __json_params.toStyledString()));                                                \
        }                                                                                        \
    } while (0)

#define JsonTypeMatchCheckV2(__type_name, __json_params, _json_type0, _json_type1)               \
    do                                                                                           \
    {                                                                                            \
        if (!(__json_params.isConvertibleTo(_json_type0) ||                                      \
                __json_params.isConvertibleTo(_json_type1)))                                     \
        {                                                                                        \
            BOOST_THROW_EXCEPTION(std::runtime_error(                                            \
                "type mismatch, the type of argument required is " + __type_name +               \
                " , the json parameter type is " +                                               \
                getJsonValueTypeAsString(__json_params.type()) + " ,the json object value is " + \
                __json_params.toStyledString()));                                                \
        }                                                                                        \
    } while (0)

bcos::bytes decodeBytesFromString(const std::string& _str)
{
    const static std::string hexPrefix = "hex://";
    const static std::string base64Prefix = "base64://";
    if (boost::starts_with(_str, hexPrefix))
    {  // hex format
        try
        {
            return std::move(*fromHexString(_str.substr(hexPrefix.size())));
        }
        catch (...)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid hexadecimal string, value is " + _str));
        }
    }
    else if (boost::starts_with(_str, base64Prefix))
    {
        try
        {
            // base64 format
            return std::move(*base64DecodeBytes(_str.substr(base64Prefix.size())));
        }
        catch (...)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("invalid base64 string, value is " + _str));
        }
    }
    else
    {
        BOOST_THROW_EXCEPTION(
            std::runtime_error("bytes data should be input as either hexadecimal "
                               "string(hex://xxxxx) or base64 string(base64://xxxxxx)"));
    }
}

std::string getJsonValueTypeAsString(const Json::ValueType& _valueType)
{
    switch (_valueType)
    {
    case Json::ValueType::nullValue:
    {
        return "null";
    }
    case Json::ValueType::intValue:
    {
        return "int";
    }
    case Json::ValueType::uintValue:
    {
        return "uint";
    }
    case Json::ValueType::realValue:
    {
        return "real";
    }
    case Json::ValueType::stringValue:
    {
        return "string";
    }
    case Json::ValueType::booleanValue:
    {
        return "boolean";
    }
    case Json::ValueType::arrayValue:
    {
        return "array";
    }
    case Json::ValueType::objectValue:
    {
        return "object";
    }
    default:
    {
        return "unrecognized type";
    }
    }
}

void ContractABICodec::initAbstractTypeWithJsonParams(AbstractType& _tv, const std::string& _params)
{
    Json::Value jParams;
    stringToJsonValue(_params, jParams);
    initAbstractTypeWithJsonParams(_tv, jParams);
}

void ContractABICodec::initAbstractTypeWithJsonParams(
    AbstractType& _tv, const Json::Value& _jParams)
{
    switch (_tv.type())
    {
    case Type::BOOL:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::booleanValue);
        static_cast<Boolean&>(_tv).setValue(_jParams.asBool());

        break;
    }
    case Type::UINT:
    {
        JsonTypeMatchCheckV2(
            _tv.name(), _jParams, Json::ValueType::uintValue, Json::ValueType::stringValue);
        if (_jParams.isConvertibleTo(Json::ValueType::uintValue))
        {
            static_cast<Uint&>(_tv).setValue(_jParams.asUInt());
        }
        else if (_jParams.isConvertibleTo(Json::ValueType::stringValue))
        {
            auto str = _jParams.asString();
            if (!isUnsigned(str))
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "use string as uint type argument, but string format is invalid, value is " +
                    str));
            }

            static_cast<Uint&>(_tv).setValue(u256(str));
        }

        break;
    }
    case Type::INT:
    {
        JsonTypeMatchCheckV2(
            _tv.name(), _jParams, Json::ValueType::intValue, Json::ValueType::stringValue);

        if (_jParams.isConvertibleTo(Json::ValueType::intValue))
        {
            static_cast<Int&>(_tv).setValue(_jParams.asInt());
        }
        else if (_jParams.isConvertibleTo(Json::ValueType::stringValue))
        {
            auto str = _jParams.asString();
            if (!isSigned(str))
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "use string as int type argument, but string format is invalid, value is " +
                    str));
            }

            static_cast<Int&>(_tv).setValue(s256(str));
        }

        break;
    }
    case Type::FIXED_BYTES:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::stringValue);

        auto data = decodeBytesFromString(_jParams.asString());
        static_cast<FixedBytes&>(_tv).setValue(data);

        break;
    }
    case Type::ADDRESS:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::stringValue);
        try
        {
            std::string addr = _jParams.asString();
            static_cast<Addr&>(_tv).setValue(addr);
        }
        catch (std::exception& _e)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid address format: " + _jParams.asString()));
        }

        break;
    }
    case Type::STRING:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::stringValue);

        static_cast<String&>(_tv).setValue(_jParams.asString());

        break;
    }
    case Type::DYNAMIC_BYTES:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::stringValue);

        auto data = decodeBytesFromString(_jParams.asString());
        static_cast<DynamicBytes&>(_tv).setValue(data);

        break;
    }
    case Type::FIXED:
    {
        // unsupported type
        break;
    }
    case Type::UFIXED:
    {
        // unsupported type
        break;
    }
    case Type::DYNAMIC_LIST:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::arrayValue);

        auto& dynamicList = static_cast<DynamicList&>(_tv);
        assert(dynamicList.value().size() == 1);
        auto baseTV = dynamicList.value()[0];
        dynamicList.setEmpty();
        for (unsigned index = 0; index < _jParams.size(); index++)
        {
            dynamicList.addMember(baseTV->clone());
            initAbstractTypeWithJsonParams(*dynamicList.value().back(), _jParams[index]);
        }

        break;
    }
    case Type::FIXED_LIST:
    {
        JsonTypeMatchCheck(_tv.name(), _jParams, Json::ValueType::arrayValue);

        auto& fixedList = static_cast<FixedList&>(_tv);
        if (_jParams.size() != fixedList.dimension())
        {
            // parameter number does not match
            BOOST_THROW_EXCEPTION(std::runtime_error(
                "parameter number does not match for the static array(" + _tv.name() +
                ") ,the parameter number is " + std::to_string(_jParams.size()) +
                " ,parameter value is :" + _jParams.toStyledString()));
        }

        for (unsigned index = 0; index < fixedList.dimension(); index++)
        {
            initAbstractTypeWithJsonParams(*fixedList.value()[index], _jParams[index]);
        }
        break;
    }
    case Type::STRUCT:
    {
        JsonTypeMatchCheckV2(
            _tv.name(), _jParams, Json::ValueType::arrayValue, Json::ValueType::objectValue);

        auto& sPtr = static_cast<Struct&>(_tv);

        if (_jParams.isArray())
        {
            // parameter as json array
            if (sPtr.memberSize() != _jParams.size())
            {
                // parameter number does not match for
                // tuple param
                BOOST_THROW_EXCEPTION(std::runtime_error(
                    "use JSON array as the tuple's parameters, but parameter number does not "
                    "match for the"
                    "tuple(" +
                    _tv.name() + ") ,the parameter number is " + std::to_string(_jParams.size()) +
                    " ,parameter value is :" + _jParams.toStyledString()));
            }

            for (unsigned index = 0; index < _jParams.size(); index++)
            {
                initAbstractTypeWithJsonParams(*sPtr.value()[index], _jParams[index]);
            }
        }
        else if (_jParams.isObject())
        {
            // parameter as json object
            auto& kvValues = sPtr.kvValue();
            for (auto& [key, value] : kvValues)
            {
                if (!_jParams.isMember(key))
                {
                    // params not found by key
                    BOOST_THROW_EXCEPTION(std::runtime_error(
                        "use JSON array as the tuple's parameters, but parameters does not match "
                        "for the tuple"
                        "(" +
                        _tv.name() + "), cannot found key: " + key +
                        ",the parameter value is :" + _jParams.toStyledString()));
                }

                initAbstractTypeWithJsonParams(*value, _jParams[key]);
            }
        }
        break;
    }
    break;
    default:
    {
        break;
    }
    }
}