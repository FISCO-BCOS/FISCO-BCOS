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
#include "../libutilities/DataConvertUtility.h"
#include "../libutilities/Exceptions.h"

namespace bcos
{
namespace protocol
{
DERIVE_BCOS_EXCEPTION(PBObjectEncodeException);
DERIVE_BCOS_EXCEPTION(PBObjectDecodeException);
template <typename T>
bytesPointer encodePBObject(T _pbObject)
{
    auto encodedData = std::make_shared<bytes>();
    encodedData->resize(_pbObject->ByteSizeLong());
    if (_pbObject->SerializeToArray(encodedData->data(), encodedData->size()))
    {
        return encodedData;
    }
    BOOST_THROW_EXCEPTION(
        PBObjectEncodeException() << errinfo_comment("encode PBObject into bytes data failed"));
}

template <typename T>
void encodePBObject(bytes& _encodedData, T _pbObject)
{
    auto encodedData = std::make_shared<bytes>();
    _encodedData.resize(_pbObject->ByteSizeLong());
    if (!_pbObject->SerializeToArray(_encodedData.data(), _encodedData.size()))
    {
        BOOST_THROW_EXCEPTION(
            PBObjectEncodeException() << errinfo_comment("encode PBObject into bytes data failed"));
    }
}

template <typename T>
void decodePBObject(T _pbObject, bytesConstRef _data)
{
    if (!_pbObject->ParseFromArray(_data.data(), _data.size()))
    {
        BOOST_THROW_EXCEPTION(
            PBObjectDecodeException() << errinfo_comment(
                "decode bytes data into PBObject failed, data: " + *toHexString(_data)));
    }
}
}  // namespace protocol
}  // namespace bcos
