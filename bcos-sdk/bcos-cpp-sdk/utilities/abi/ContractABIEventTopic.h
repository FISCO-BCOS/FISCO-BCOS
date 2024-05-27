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
 * @file ContractABIEventTopic.h
 * @author: octopus
 * @date 2021-09-01
 */

#pragma once
#include <bcos-cpp-sdk/utilities/abi/ContractABITypeCodec.h>
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <string>
#include <vector>

#define CONTRACT_EVENT_TOPIC_LENGTH (64)

namespace bcos
{
namespace codec
{
namespace abi
{
class ContractABIEventTopic
{
public:
    using Ptr = std::shared_ptr<ContractABIEventTopic>;
    using ConstPtr = std::shared_ptr<const ContractABIEventTopic>;

public:
    explicit ContractABIEventTopic(bcos::crypto::Hash::Ptr _hashImpl) : m_hashImpl(_hashImpl) {}

public:
    /**
     * @brief
     *
     * @param _topic
     * @return true
     * @return false
     */
    static bool validEventTopic(const std::string& _topic)
    {
        if ((_topic.compare(0, 2, "0x") == 0) || (_topic.compare(0, 2, "0X") == 0))
        {
            return _topic.length() == (CONTRACT_EVENT_TOPIC_LENGTH + 2);
        }

        return _topic.length() == CONTRACT_EVENT_TOPIC_LENGTH;
    }

public:
    // u256 => topic
    std::string u256ToTopic(bcos::u256 _u)
    {
        bcos::bytes data;
        m_solCodec.serialize(_u, 256, data);
        return toHexStringWithPrefix(data);
    }

    // s256 => topic
    std::string i256ToTopic(bcos::s256 _i)
    {
        bcos::bytes data;
        m_solCodec.serialize(_i, 256, data);
        return toHexStringWithPrefix(data);
    }

    // string => topic
    std::string stringToTopic(const std::string& _str)
    {
        auto hashValue = m_hashImpl->hash(_str);
        return hashValue.hexPrefixed();
    }

    // bytes => topic
    std::string bytesToTopic(const bcos::bytes& _bs)
    {
        auto hashValue = m_hashImpl->hash(_bs);
        return hashValue.hexPrefixed();
    }

    // bytesN(eg:bytes32) => topic
    std::string bytesNToTopic(const bcos::bytes& _bsn)
    {
        bcos::bytes data;
        m_solCodec.serialize(_bsn, true, data);
        return toHexStringWithPrefix(data);
    }

private:
    bcos::crypto::Hash::Ptr m_hashImpl;

    bcos::cppsdk::abi::ContractABITypeCodecSolImpl m_solCodec;
};

}  // namespace abi
}  // namespace codec
}  // namespace bcos
