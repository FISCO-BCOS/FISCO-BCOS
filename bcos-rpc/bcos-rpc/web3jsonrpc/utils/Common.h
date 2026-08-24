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
 * @file Common.h
 * @author: kyonGuo
 * @date 2024/3/29
 */

#pragma once

#include <bcos-rpc/Common.h>
#include <bcos-utilities/DataConvertUtility.h>

#include <boost/algorithm/string/case_conv.hpp>
#include <string>
#include <string_view>

namespace bcos::rpc
{
/// Normalize a protocol::LogEntry address view to 40-char lowercase hex (no 0x prefix).
/// Producers are inconsistent: the OP execution path stores the 20 raw address bytes,
/// while legacy paths store the ASCII hex string (with or without 0x). Length 20 is
/// unambiguous (a hex-string form is 40/42 chars), so branch on it.
inline std::string toLogAddressHex(std::string_view address)
{
    if (address.size() == 20)
    {
        auto hex = bcos::toHex(address);
        boost::algorithm::to_lower(hex);
        return hex;
    }
    std::string_view view = address;
    if (view.starts_with("0x") || view.starts_with("0X"))
    {
        view.remove_prefix(2);
    }
    std::string hex{view};
    boost::algorithm::to_lower(hex);
    return hex;
}

constexpr const uint64_t LowestGasPrice{21000};
constexpr const uint64_t LowestGasUsed{21000};
enum Web3JsonRpcError : int32_t
{
    Web3DefaultError = -32000,
};
enum EngineError : int32_t
{
    // -38000: Engine API base
    UnknownPayload = -38001,
    InvalidForkchoiceState = -38002,
    InvalidPayloadAttributes = -38003,
    TooLargeRequest = -38004,
    UnsupportedFork = -38005,
    TooDeepReorg = -38006,
};
}  // namespace bcos::rpc