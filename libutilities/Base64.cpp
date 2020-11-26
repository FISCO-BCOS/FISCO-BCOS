/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @file Base64.cpp
 * @author: xingqiangbai
 * @date 2020-10-14
 */

#include "Base64.h"
#include <boost/algorithm/string.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/iostreams/copy.hpp>

using namespace boost::archive::iterators;

std::string bcos::base64Encode(const byte* _begin, const size_t _dataSize)
{
    using It = base64_from_binary<transform_width<byte*, 6, 8>>;
    auto tmp = std::string(It(_begin), It(_begin + _dataSize));
    return tmp.append((3 - _dataSize % 3) % 3, '=');
}

std::string bcos::base64Encode(std::string const& _data)
{
    return base64Encode((const byte*)_data.data(), _data.size());
}

std::string bcos::base64Encode(bytesConstRef _data)
{
    return base64Encode(_data.data(), _data.size());
}

std::shared_ptr<bcos::bytes> bcos::base64DecodeBytes(std::string const& _data)
{
    using It = transform_width<binary_from_base64<bcos::byte*>, 8, 6>;
    size_t dataEndPos = _data.find_first_of('=');
    size_t dataSize = _data.size();
    if (dataEndPos > 0)
    {
        dataSize = dataEndPos;
    }
    return std::make_shared<bcos::bytes>(It(_data.data()), It(_data.data() + dataSize));
}

std::string bcos::base64Decode(std::string const& _data)
{
    auto decodedData = base64DecodeBytes(_data);
    return std::string(decodedData->begin(), decodedData->end());
}
