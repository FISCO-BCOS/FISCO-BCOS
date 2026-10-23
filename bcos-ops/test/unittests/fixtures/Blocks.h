/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief scripted block / receipt JSON in the shape JsonRpcImpl_2_0 emits, for the explorer
 *        and the home page tests
 * @file Blocks.h
 */
#pragma once

#include "FakeRpc.h"
#include <json/json.h>
#include <string>

namespace bcos::ops::test
{
inline std::string fakeHash(int64_t _seed, char _tag = 'b')
{
    char buffer[80];
    std::snprintf(buffer, sizeof(buffer), "0x%c%015llx%048llx", _tag,
        static_cast<unsigned long long>(_seed), static_cast<unsigned long long>(_seed * 7919));
    return buffer;
}

/// block _number at genesis + _number seconds with _txCount transactions
inline Json::Value fakeBlock(int64_t _number, int _txCount, int64_t _timestampMs = -1)
{
    Json::Value block;
    block["number"] = Json::Int64(_number);
    block["hash"] = fakeHash(_number);
    block["parentHash"] = fakeHash(_number - 1);
    block["timestamp"] =
        Json::Int64(_timestampMs >= 0 ? _timestampMs : 1760000000000 + _number * 1000);
    block["sealer"] = Json::Int64(_number % 4);
    block["gasUsed"] = "0";
    block["transactions"] = Json::Value(Json::arrayValue);
    for (int i = 0; i < _txCount; ++i)
    {
        Json::Value tx;
        tx["hash"] = fakeHash(_number * 100 + i, 't');
        tx["from"] = "0x3f12000000000000000000000000000000009a" + std::to_string(i % 10);
        tx["to"] = i % 2 == 0 ? "0xf50500000000000000000000000000000043c1" : "";
        block["transactions"].append(tx);
    }
    return block;
}

/// blocks 0.._height into the fake, each with _txPerBlock transactions
inline void scriptChain(FakeRpc& _rpc, int64_t _height, int _txPerBlock)
{
    _rpc.set("getBlockNumber", Json::Int64(_height));
    for (int64_t n = 0; n <= _height; ++n)
    {
        _rpc.setFor("getBlockByNumber", 2, std::to_string(n), fakeBlock(n, _txPerBlock));
    }
}

inline Json::Value fakeReceipt(std::string const& _hash, int _status, int64_t _block)
{
    Json::Value receipt;
    receipt["transactionHash"] = _hash;
    receipt["status"] = _status;
    receipt["blockNumber"] = Json::Int64(_block);
    receipt["gasUsed"] = "6d6c";
    receipt["output"] = "0x";
    return receipt;
}
}  // namespace bcos::ops::test
