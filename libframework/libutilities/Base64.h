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
 * @file Base64.h
 * @author: xingqiangbai
 */
#pragma once
#include "Common.h"

namespace bcos
{
std::string base64Encode(const byte* _begin, const size_t _dataSize);

std::string base64Encode(std::string const& _data);
std::string base64Encode(bytesConstRef _data);

std::shared_ptr<bytes> base64DecodeBytes(std::string const& _data);
std::string base64Decode(std::string const& _data);
}  // namespace bcos
