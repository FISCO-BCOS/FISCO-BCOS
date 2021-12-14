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
 * @brief tools for scale codec
 * info
 * @file ScaleUtils.cpp
 * @author: catli
 * @date: 2021-09-11
 */

#include "ScaleUtils.h"
#include <bcos-framework/libcodec/scale/Scale.h>
#include <boost/algorithm/string/predicate.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

optional<size_t> bcos::executor::decodeCompactInteger(const bytes& encodedBytes, size_t startPos)
{
    if (encodedBytes.size() - startPos < 1)
    {
        return nullopt;
    }

    auto _1stByte = encodedBytes[startPos];
    auto flag = (_1stByte)&0b00000011u;
    auto number = 0u;

    switch (flag)
    {
    case 0b00u:
    {
        // single-byte mode:
        // upper six bits are the LE encoding of the value (valid only for values of 0-63).
        number = static_cast<size_t>(_1stByte >> 2u);
        break;
    }
    case 0b01u:
    {
        // two-byte mode:
        // upper six bits and the following byte is the LE encoding of the value (valid only
        // for values 64-(2**14-1)).
        if (encodedBytes.size() - startPos < 2)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor")
                                << LOG_DESC("not enough data to decode compact integer");
            return nullopt;
        }
        auto _2ndByte = encodedBytes[startPos + 1];
        number =
            (static_cast<size_t>((_1stByte)&0b11111100u) + static_cast<size_t>(_2ndByte) * 256u) >>
            2u;
        break;
    }
    case 0b10u:
    {
        // four-byte mode:
        // upper six bits and the following three bytes are the LE encoding of the value
        // (valid only for values (2**14)-(2**30-1))
        number = _1stByte;
        size_t multiplier = 256u;
        if (encodedBytes.size() - startPos < 4)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor")
                                << LOG_DESC("not enough data to decode compact integer");
            return nullopt;
        }

        for (auto i = 1u; i < 4u; ++i)
        {
            number += encodedBytes[startPos + i] * multiplier;
            multiplier = multiplier << 8u;
        }
        number = number >> 2u;
        break;
    }
    case 0b11:
    {
        // big-integer mode:
        // upper six bits are the number of bytes following, less four. The value is
        // contained, LE encoded, in the bytes following. The final (most significant) byte
        // must be non-zero. Valid only for values (2**30)-(2**536-1)
        auto bytesCount = ((_1stByte) >> 2u) + 4u;
        if (encodedBytes.size() - startPos < bytesCount + 1)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor")
                                << LOG_DESC("not enough data to decode compact integer");
            return nullopt;
        }

        auto multiplier = 1u;
        for (auto i = 1u; i < bytesCount + 1; ++i)
        {
            number += (encodedBytes[startPos + i]) * multiplier;
            multiplier *= 256u;
        }
        break;
    }
    default:
        return nullopt;
    }
    return number;
}

optional<size_t> bcos::executor::scaleEncodingLength(
    const ParameterAbi& param, const bytes& encodedBytes, size_t startPos)
{
    auto& type = param.type;
    if (boost::ends_with(type, "]"))
    {
        auto leftBracketPos = type.rfind("[");
        if (leftBracketPos == type.npos)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor") << LOG_DESC("unable to parse array type")
                                << LOG_KV("type", type);
            return nullopt;
        }

        auto size = 0u;
        auto length = 0u;
        if (leftBracketPos == type.length() - 2)
        {
            auto compactLength = decodeCompactInteger(encodedBytes, startPos);
            if (compactLength)
            {
                size = compactLength.value();
                length += codec::scale::compactLen(size);
                startPos += length;
            }
            else
            {
                EXECUTOR_LOG(ERROR)
                    << LOG_BADGE("executor") << LOG_DESC("unable to parse length of dynamic array");
                return nullopt;
            }
        }
        else
        {
            auto dimmension = type.substr(leftBracketPos + 1, type.length() - leftBracketPos - 1);
            try
            {
                size = stoul(dimmension);
            }
            catch (...)
            {
                EXECUTOR_LOG(ERROR)
                    << LOG_BADGE("executor") << LOG_DESC("unable to parse dimmension")
                    << LOG_KV("dimmension", dimmension);
                return nullopt;
            }
        }

        auto subParam = ParameterAbi{type.substr(0, leftBracketPos), param.components};
        for (auto i = 0u; i < size; ++i)
        {
            auto subTypeLength = scaleEncodingLength(subParam, encodedBytes, startPos);
            if (subTypeLength)
            {
                auto value = subTypeLength.value();
                length += value;
                startPos += value;
            }
            else
            {
                EXECUTOR_LOG(ERROR)
                    << LOG_BADGE("executor") << LOG_DESC("unable to calculate length of element");
                return nullopt;
            }
        }
        return {length};
    }

    if (boost::starts_with(type, "uint") || boost::starts_with(type, "int"))
    {
        auto digitStartPos = type.rfind("t");
        auto digitsNum = 0u;
        try
        {
            digitsNum = stoul(type.substr(digitStartPos + 1));
        }
        catch (...)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor") << LOG_DESC("unable to parse type")
                                << LOG_KV("type", type);
            return nullopt;
        }
        return digitsNum >> 3;
    }

    if (type == "string" || type == "bytes")
    {
        auto compactLength = decodeCompactInteger(encodedBytes, startPos);
        if (compactLength)
        {
            auto value = compactLength.value();
            return {value + codec::scale::compactLen(value)};
        }
        else
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor")
                                << LOG_DESC("unable to parse string or bytes");
            return nullopt;
        }
    }

    if (type == "bool" || type == "byte")
    {
        return 1;
    }

    if (boost::starts_with(type, "bytes"))
    {
        auto digitStartPos = type.rfind("s");
        auto digitsNum = 0u;
        try
        {
            digitsNum = stoul(type.substr(digitStartPos + 1));
        }
        catch (...)
        {
            EXECUTOR_LOG(ERROR) << LOG_BADGE("executor") << LOG_DESC("unable to parse type")
                                << LOG_KV("type", type);
            return nullopt;
        }
        return digitsNum;
    }

    if (type == "tuple")
    {
        auto length = 0u;
        for (auto& component : param.components)
        {
            auto componentLength = scaleEncodingLength(component, encodedBytes, startPos);
            if (componentLength)
            {
                auto value = componentLength.value();
                length += value;
                startPos += value;
            }
            else
            {
                EXECUTOR_LOG(ERROR)
                    << LOG_BADGE("executor") << LOG_DESC("unable to parse component")
                    << LOG_KV("type", component);
                return nullopt;
            }
        }
        return {length};
    }

    EXECUTOR_LOG(ERROR) << LOG_BADGE("executor") << LOG_DESC("unable to parse type")
                        << LOG_KV("type", type);
    return nullopt;
}