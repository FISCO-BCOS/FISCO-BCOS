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
 * @file JsonValidator.cpp
 * @author: kyonGuo
 * @date 2024/3/28
 */

#include "JsonValidator.h"
#include <bcos-rpc/Common.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/endpoints/EthMethods.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>

namespace bcos::rpc
{
namespace
{
enum class ParamKind
{
    Address,
    StorageSlot,
    BlockSelector,
    Boolean
};

// Positional signatures from ethereum/execution-apis at 68d76a5b1c09dff023782d97eec6ce87a6f6505e.
// Local compatibility also permits legacy hex encodings and bare block hashes.
constexpr std::array balanceParams{ParamKind::Address, ParamKind::BlockSelector};
constexpr std::array storageParams{
    ParamKind::Address, ParamKind::StorageSlot, ParamKind::BlockSelector};
constexpr std::array blockParams{ParamKind::BlockSelector, ParamKind::Boolean};

bool isAsciiHex(std::string_view digits)
{
    return std::ranges::all_of(digits, [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

bool matchesBlockSelector(std::string_view value)
{
    if (value == EarliestBlock || value == LatestBlock || value == PendingBlock ||
        value == SafeBlock || value == FinalizedBlock)
    {
        return true;
    }
    if (!value.starts_with("0x") || value.size() > 66)
    {
        return false;
    }
    auto const digits = value.substr(2);
    // The shared resolver interprets a 32-byte hex value as a hash, even when padded with zeros.
    if (digits.size() == 64)
    {
        return isAsciiHex(digits);
    }
    auto const height = safeFromQuantity(value);
    return height && *height <= std::numeric_limits<int64_t>::max();
}

bool matchesParam(ParamKind kind, Json::Value const& value)
{
    if (kind == ParamKind::Boolean)
    {
        return value.isBool();
    }
    char const* begin = nullptr;
    char const* end = nullptr;
    if (!value.getString(&begin, &end))
    {
        return false;
    }
    auto const text = std::string_view(begin, end);
    if (kind == ParamKind::BlockSelector)
    {
        return matchesBlockSelector(text);
    }
    auto const digits = text.starts_with("0x") || text.starts_with("0X") ? text.substr(2) : text;
    return (kind == ParamKind::Address ? digits.size() == 40 : digits.size() <= 64) &&
           isAsciiHex(digits);
}

void requirePositionalParams(
    Json::Value const& params, std::span<ParamKind const> kinds, Json::ArrayIndex required)
{
    auto const fail = [](std::string path) {
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidParams, "Invalid params at " + path));
    };
    auto const count = params.size();
    if (!params.isArray() || count < required || count > kinds.size())
    {
        fail("/params");
    }
    for (Json::ArrayIndex i = 0; i < count; ++i)
    {
        if (!matchesParam(kinds[i], params[i]))
        {
            fail("/params/" + std::to_string(i));
        }
    }
}
}  // namespace

std::tuple<bool, std::string> JsonValidator::validate(const Json::Value& root)
{
    return checkRequestFields(root);
}

void JsonValidator::requireParams(EthMethod method, Json::Value const& params)
{
    switch (method)
    {
    case EthMethod::eth_getBalance:
        requirePositionalParams(params, balanceParams, 1);
        break;
    case EthMethod::eth_getStorageAt:
        requirePositionalParams(params, storageParams, 2);
        break;
    case EthMethod::eth_getBlockByNumber:
        requirePositionalParams(params, blockParams, 2);
        break;
    default:
        break;
    }
}

std::tuple<bool, std::string> JsonValidator::checkRequestFields(const Json::Value& root)
{
    // flag for 4 fields
    auto flag = 0b111;
    for (auto item = root.begin(); item != root.end(); ++item)
    {
        if (item.name() == "jsonrpc")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b110;
        }
        else if (item.name() == "method")
        {
            if (!item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b101;
        }
        else if (item.name() == "params")
        {
            if (!item->isArray())
            {
                return {false, "Invalid field: " + item.name()};
            }
        }
        else if (item.name() == "id")
        {
            // JSON-RPC 2.0 allows id to be any JSON number or string
            if (!item->isNumeric() && !item->isString())
            {
                return {false, "Invalid field: " + item.name()};
            }
            flag &= 0b011;
        }
        else
        {
            return {false, "Invalid field: " + item.name()};
        }
    }
    if (flag != 0)
    {
        return {false, "Request not valid, required fields: jsonrpc, method, params, id"};
    }
    return {true, ""};
}

}  // namespace bcos::rpc
