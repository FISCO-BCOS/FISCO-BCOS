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
#include <bcos-rpc/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <json/json.h>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace bcos::rpc
{
void buildJsonContent(Json::Value& result, Json::Value& response);
void buildJsonError(
    Json::Value const& request, int32_t code, std::string message, Json::Value& response);
void buildJsonErrorWithData(
    Json::Value& data, int32_t code, std::string message, Json::Value& response);

bcos::bytes toBytesResponse(Json::Value const& jResp);

/// Decode a solidity Error(string) revert payload (selector 0x08c379a0) into the op-geth
/// style eth_call error message. Any non-Error(string) or malformed output degrades to the
/// bare "execution reverted" message — a reverted call must never surface a fabricated
/// reason, only the reason the contract actually emitted.
inline std::string decodeRevertMessage(std::string_view outputHex)
{
    // std::string_view for the constexpr (a constexpr std::string is not a constant
    // expression under gcc-14 -Werror: it refers to a result of operator new); every
    // use converts explicitly so no compiler's implicit-conversion leniency is relied on.
    static constexpr std::string_view kReverted = "execution reverted";
    auto bytes = fromHexWithPrefix(outputHex);
    // Error(string): selector(4) || offset==32 (32) || length (32) || payload
    if (bytes.size() < 4U + 32U + 32U ||
        !std::equal(bytes.begin(), bytes.begin() + 4,
            std::array<bcos::byte, 4>{0x08, 0xc3, 0x79, 0xa0}.begin()))
    {
        return std::string(kReverted);
    }
    auto word = [&bytes](std::size_t offset) {
        std::size_t value = 0;
        for (std::size_t i = 0; i < 32; ++i)
        {
            if (value > (std::numeric_limits<std::size_t>::max() >> 8))
            {
                return std::numeric_limits<std::size_t>::max();
            }
            value = (value << 8) | static_cast<std::uint8_t>(bytes[offset + i]);
        }
        return value;
    };
    if (word(4) != 32)
    {
        return std::string(kReverted);
    }
    auto length = word(36);
    if (length > bytes.size() - (4U + 32U + 32U))
    {
        return std::string(kReverted);
    }
    auto reason = std::string(
        reinterpret_cast<char const*>(bytes.data() + 68), static_cast<std::size_t>(length));
    return std::string(kReverted) + ": " + reason;
}

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

/// Pure initializer for the eth_estimateGas upward-search interval, regression-tested in
/// EthEstimateGasBudgetTest. Given the first-run consumption (known bad) and the limit
/// run #1 actually executed at (cap-clamped upstream), returns a well-ordered
/// [lowerBound, upperBound] pair. Passing any ceiling run #1 did not execute at once
/// inverted the bounds and wrapped the unsigned width test.
struct EstimateSearchBounds
{
    u256 lowerBound;
    u256 upperBound;
};
inline EstimateSearchBounds estimateSearchBounds(const u256& gasUsed, const u256& firstRunLimit)
{
    EstimateSearchBounds bounds{.lowerBound = gasUsed,  // known-bad
        .upperBound = firstRunLimit};                   // proven-viable anchor
    if (bounds.upperBound < bounds.lowerBound) [[unlikely]]
    {
        // Defensive floor: only reachable if the reported consumption exceeded even the
        // cap-clamped run #1 limit (charge-reporting drift). Clamp so every width
        // subtraction below stays non-negative; the loops then no-op.
        bounds.upperBound = bounds.lowerBound;
    }
    return bounds;
}
}  // namespace bcos::rpc
