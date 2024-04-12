/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Bloom.h
 * @author: kyonGuo
 * @date 2024/4/12
 */

#pragma once
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-rpc/web3jsonrpc/model/Log.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <concepts/bcos-concepts/Basic.h>

namespace bcos::rpc
{
constexpr size_t BloomBytesSize = 256;
constexpr uint8_t LOWER_3_BITS = 0b00000111;
using Bloom = std::array<bcos::byte, BloomBytesSize>;

void bytesToBloom(bcos::concepts::ByteBuffer auto const& _bytes, Bloom& _bloom);

Bloom getLogsBloom(Logs const& logs);

inline std::string_view toStringView(Bloom const& bloom)
{
    return {reinterpret_cast<const char*>(bloom.data()), bloom.size()};
}

}  // namespace bcos::rpc