/**
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
 * @file Common.h
 * @author: yujiechen
 * @date 2021-04-12
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <limits>
#include <algorithm>
#include <string>

namespace bcos::protocol
{
DERIVE_BCOS_EXCEPTION(PBObjectEncodeException);
DERIVE_BCOS_EXCEPTION(PBObjectDecodeException);
template <typename T>
bytesPointer encodePBObject(T _pbObject)
{
    auto encodedData = std::make_shared<bytes>();
    auto size = _pbObject->ByteSizeLong();
    if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        BOOST_THROW_EXCEPTION(PBObjectEncodeException() << errinfo_comment(
                                  "encode PBObject failed: data size exceeds int range"));
    }
    encodedData->resize(size);
    if (_pbObject->SerializeToArray(encodedData->data(), static_cast<int>(encodedData->size())))
    {
        return encodedData;
    }
    BCOS_LOG(WARNING) << LOG_BADGE("PBFTMessage")
                      << LOG_DESC("encode PBObject into bytes data failed")
                      << LOG_KV("PBObjectSize", _pbObject->ByteSizeLong());
    BOOST_THROW_EXCEPTION(
        PBObjectEncodeException() << errinfo_comment("encode PBObject into bytes data failed"));
}

template <typename T>
void encodePBObject(bytes& _encodedData, T _pbObject)
{
    auto size = _pbObject->ByteSizeLong();
    if (size > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        BOOST_THROW_EXCEPTION(PBObjectEncodeException() << errinfo_comment(
                                  "encode PBObject failed: data size exceeds int range"));
    }
    _encodedData.resize(size);
    if (!_pbObject->SerializeToArray(_encodedData.data(), static_cast<int>(_encodedData.size())))
    {
        BOOST_THROW_EXCEPTION(
            PBObjectEncodeException() << errinfo_comment("encode PBObject into bytes data failed"));
    }
}

template <typename T>
void decodePBObject(T _pbObject, bytesConstRef _data)
{
    if (_data.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        BOOST_THROW_EXCEPTION(PBObjectDecodeException() << errinfo_comment(
                                  "decode PBObject failed: data size exceeds int range"));
    }
    if (!_pbObject->ParseFromArray(_data.data(), static_cast<int>(_data.size())))
    {
        // Truncate hex output to avoid excessive memory usage for large payloads
        constexpr size_t c_maxHexBytes = 64;
        const auto truncatedRef = _data.getCroppedData(0, std::min(_data.size(), c_maxHexBytes));
        auto hexStr = toHex(truncatedRef);
        const auto shownBytes = std::min(_data.size(), c_maxHexBytes);
        if (_data.size() > c_maxHexBytes)
        {
            hexStr += "...(truncated)";
        }
        BOOST_THROW_EXCEPTION(
            PBObjectDecodeException() << errinfo_comment(
                "decode bytes data into PBObject failed, size: " + std::to_string(_data.size()) +
                ", shown: " + std::to_string(shownBytes) + " bytes, data: " + hexStr));
    }
}

}  // namespace bcos::protocol
