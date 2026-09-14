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
 * @file util.h
 * @author: kyonGuo
 * @date 2024/3/29
 */

#pragma once
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-rpc/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace bcos::rpc
{
/// The log entry's address as 40 hex digits, no 0x prefix. LogEntry::address() carries
/// whichever form the executing lane produced, and the two lanes disagree:
///   * the OP lane stores the raw 20 bytes (bcos-evm/opstack/OpTransition.cpp
///     mapOpLogAddress — a byte copy, deliberately not a hex encode);
///   * the FISCO / eth-mode lane stores the ASCII hex text (bcos-executor HostContext::log
///     passes myAddress(), which is the text form on that lane).
/// Every JSON producer of a log address must normalize through here: hex-encoding the text
/// form yields hex-of-ASCII (ethers rejects the whole receipt), while copying the byte form
/// verbatim yields non-printable garbage. One definition, because two copies is how one
/// consumer ends up right on one lane and wrong on the other.
[[nodiscard]] inline std::string logEntryAddressHex(bcos::protocol::LogEntry const& entry)
{
    auto const raw = entry.address();
    constexpr std::size_t c_hexAddressChars = 40;
    if (raw.size() == c_hexAddressChars && std::all_of(raw.begin(), raw.end(), [](char c) {
            return std::isxdigit(static_cast<unsigned char>(c)) != 0;
        }))
    {
        return std::string(raw);
    }
    return bcos::toHex(raw);
}

/// EIP-55 checksum an address given as hex text without the 0x prefix. Deliberately does
/// NOT validate the input: callers feed lane-dependent forms (a FISCO-native tx.to may be
/// a BFS link path, a feature_raw_address chain carries raw bytes), and a response
/// producer must never fail on transaction input — an unchecksummable address degrades to
/// the unchecked form instead of throwing the whole eth_* call away. One home for the
/// idiom: the private copies this replaces had already drifted (one threw, the rest did
/// not).
[[nodiscard]] inline std::string checksummedHexAddress(std::string hexNoPrefix)
{
    bcos::toChecksumAddress(
        hexNoPrefix, bcos::crypto::keccak256Hash(bcos::bytesConstRef(hexNoPrefix)).hex());
    return hexNoPrefix;
}

/// 0x-tolerant form: strips an optional 0x prefix before checksumming (returns without
/// the prefix; the caller adds it back).
[[nodiscard]] inline std::string checksummedHexAddressFromHex(std::string_view hexAddress)
{
    auto const hexNoPrefix = hexAddress.starts_with("0x") ? hexAddress.substr(2) : hexAddress;
    return checksummedHexAddress(hexNoPrefix);
}

void buildJsonContent(Json::Value& result, Json::Value& response);
void buildJsonError(
    Json::Value const& request, int32_t code, std::string message, Json::Value& response);
void buildJsonErrorWithData(
    Json::Value& data, int32_t code, std::string message, Json::Value& response);

bcos::bytes toBytesResponse(Json::Value const& jResp);

inline auto printJson(const Json::Value& value)
{
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}
inline std::string_view toView(const Json::Value& value)
{
    const char* begin = nullptr;
    const char* end = nullptr;
    if (!value.getString(&begin, &end))
    {
        return {};
    }
    std::string_view view(begin, end - begin);
    return view;
}
}  // namespace bcos::rpc
