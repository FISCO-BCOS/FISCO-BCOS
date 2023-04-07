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
 * @file ContractABICodec.h
 * @author: octopus
 * @date 2022-05-25
 */
#pragma once

#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIDefinitionFactory.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h"
#include "bcos-cpp-sdk/utilities/abi/ContractABIType.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/value.h>
#include <memory>
#include <string>

namespace bcos
{
namespace cppsdk
{
namespace abi
{

class ContractABICodec
{
public:
    using Ptr = std::shared_ptr<ContractABICodec>;
    using ConstPtr = std::shared_ptr<const ContractABICodec>;

public:
    ContractABICodec(bcos::crypto::Hash::Ptr _hashImpl, ContractABITypeCodecInterface::Ptr _codec)
      : m_hashImpl(_hashImpl), m_codec(_codec)
    {}

    //--------------------------------  encode begin --------------------------------

    bcos::bytes encodeConstructor(
        const std::string& _abi, const std::string& _bin, const std::string& _jsonParams);

    bcos::bytes encodeMethod(
        const std::string& _abi, const std::string& _methodName, const std::string& _jsonParams);

    bcos::bytes encodeMethodByMethodID(
        const std::string& _abi, const std::string& _methodID, const std::string& _jsonParams);

    bcos::bytes encodeMethodBySignature(
        const std::string& _methodSig, const std::string& _jsonParams);

    bcos::bytes encodeMethod(
        const ContractABIMethodDefinition& _method, const std::string& _jsonParams);

    bcos::bytes encodeMethod(
        const ContractABIMethodDefinition& _method, const Json::Value& _jParams);

    //--------------------------------  encode end ----------------------------------

    //--------------------------------  decode input begin --------------------------

    std::string decodeMethodInput(
        std::string _abi, std::string _methodName, const bcos::bytes& _data);

    std::string decodeMethodInputByMethodID(
        std::string _abi, std::string _methodID, const bcos::bytes& _data);

    std::string decodeMethodInput(
        const ContractABIMethodDefinition& _method, const bcos::bytes& _data);

    std::string decodeMethodInputByMethodSig(
        const std::string& _methodSig, const bcos::bytes& _data);
    //--------------------------------  decode input end --------------------------

    //--------------------------------  decode output begin --------------------------
    std::string decodeMethodOutput(
        const ContractABIMethodDefinition& _method, const bcos::bytes& _data);

    std::string decodeMethodOutput(
        std::string _abi, std::string _methodName, const bcos::bytes& _data);

    std::string decodeMethodOutputByMethodID(
        std::string _abi, std::string _methodID, const bcos::bytes& _data);


    //--------------------------------  decode output end --------------------------------

    //--------------------------------  decode event begin --------------------------------

    std::string decodeEvent(const ContractABIMethodDefinition& _event, const bcos::bytes& _data);

    std::string decodeEvent(std::string _abi, std::string _eventName, const bcos::bytes& _data);

    std::string decodeEventByTopic(
        std::string _abi, std::string _eventTopic, const bcos::bytes& _data);

    //--------------------------------  decode event end --------------------------------

public:
    void stringToJsonValue(const std::string& _params, Json::Value& _jParams);
    void initAbstractTypeWithJsonParams(AbstractType& _tv, const std::string& _params);
    void initAbstractTypeWithJsonParams(AbstractType& _tv, const Json::Value& _jParams);

public:
    bcos::crypto::Hash::Ptr hash() const { return m_hashImpl; }
    ContractABITypeCodecInterface::Ptr codec() const { return m_codec; }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;
    ContractABITypeCodecInterface::Ptr m_codec;

    ContractABIDefinitionFactory m_contractABIDefinitionFactory;
};

}  // namespace abi
}  // namespace cppsdk
}  // namespace bcos
