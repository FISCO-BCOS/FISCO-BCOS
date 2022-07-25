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
 * @brief define the exceptions for zkp of bcos-crypto
 * @file Common.h
 * @date 2022.07.18
 * @author yujiechen
 */
#pragma once
#include "Exceptions.h"
#include <bcos-utilities/Common.h>
#include <wedpr-crypto/WedprUtilities.h>

namespace bcos
{
namespace crypto
{
inline CInputBuffer bytesToInputBuffer(bytes const& data, size_t _length)
{
    if (data.size() < _length)
    {
        BOOST_THROW_EXCEPTION(
            InvalidInputInput() << errinfo_comment(
                "InvalidInputInput: the data size must be at least " + std::to_string(_length)));
    }
    return CInputBuffer{(const char*)data.data(), _length};
}
}  // namespace crypto
}  // namespace bcos