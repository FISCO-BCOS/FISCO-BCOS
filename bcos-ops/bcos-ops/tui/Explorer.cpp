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
 * @file Explorer.cpp
 */
#include "Explorer.h"
#include <algorithm>

namespace bcos::ops::tui
{
namespace
{
Json::Value explorerParams(std::string const& _group)
{
    Json::Value params(Json::arrayValue);
    params.append(_group);
    params.append("");
    return params;
}

int64_t jsonInt64(Json::Value const& _value, int64_t _default)
{
    if (_value.isIntegral())
    {
        return _value.asInt64();
    }
    if (_value.isString())
    {
        try
        {
            return std::stoll(_value.asString());
        }
        catch (std::exception const&)
        {}
    }
    return _default;
}

void explorerTrim(ExplorerState& _state)
{
    while (_state.blocks.size() > _state.capacity)
    {
        _state.blocks.pop_back();
    }
    while (_state.txs.size() > _state.capacity)
    {
        _state.txs.pop_back();
    }
}
}  // namespace

size_t explorerCapacity(int _terminalRows)
{
    auto rows = _terminalRows < 0 ? size_t(0) : static_cast<size_t>(_terminalRows);
    return std::min(c_explorerMaxRows, std::max(c_explorerMinRows, rows));
}

BlockRow blockRowFromJson(Json::Value const& _block)
{
    BlockRow row;
    row.number = jsonInt64(_block["number"], -1);
    row.hash = _block.get("hash", "").asString();
    row.timestampMs = jsonInt64(_block["timestamp"], 0);
    row.sealer = _block["sealer"].isIntegral() ? std::to_string(_block["sealer"].asInt64()) :
                                                 _block.get("sealer", "").asString();
    row.txCount = static_cast<int>(_block["transactions"].size());
    return row;
}

std::vector<TxRow> txRowsFromBlock(Json::Value const& _block)
{
    std::vector<TxRow> rows;
    auto number = jsonInt64(_block["number"], -1);
    for (auto const& tx : _block["transactions"])
    {
        TxRow row;
        // onlyTxHash=true answers with bare hash strings; the explorer asks for full objects but
        // reads both shapes
        if (tx.isString())
        {
            row.hash = tx.asString();
        }
        else
        {
            row.hash = tx.get("hash", "").asString();
            row.from = tx.get("from", "").asString();
            row.to = tx.get("to", "").asString();
        }
        row.blockNumber = number;
        rows.push_back(std::move(row));
    }
    return rows;
}

void explorerFetch(
    ExplorerState& _state, RpcCall const& _call, std::string const& _group, int64_t _height)
{
    if (_height < 0)
    {
        return;
    }
    int64_t from = _state.lastFetchedHeight < 0 ?
                       std::max<int64_t>(0, _height - static_cast<int64_t>(_state.capacity) + 1) :
                       _state.lastFetchedHeight + 1;
    if (from > _height)
    {
        explorerTrim(_state);  // the capacity may have shrunk with the terminal
        return;
    }
    bool gap = false;
    if (_height - from + 1 > c_explorerMaxFetchPerRound)
    {
        from = _height - c_explorerMaxFetchPerRound + 1;
        gap = _state.lastFetchedHeight >= 0;
    }
    for (int64_t number = from; number <= _height; ++number)
    {
        auto params = explorerParams(_group);
        params.append(Json::Int64(number));
        params.append(false);
        params.append(false);
        Json::Value block;
        try
        {
            block = _call("getBlockByNumber", params);
        }
        catch (std::exception const&)
        {
            break;  // keep what arrived; the next round retries from lastFetchedHeight + 1
        }
        auto row = blockRowFromJson(block);
        if (row.number < 0)
        {
            row.number = number;
        }
        row.gapBelow = gap && number == from;
        _state.blocks.push_front(row);
        auto txs = txRowsFromBlock(block);
        for (auto it = txs.rbegin(); it != txs.rend(); ++it)
        {
            _state.txs.push_front(std::move(*it));
        }
        _state.lastFetchedHeight = number;
    }
    explorerTrim(_state);
}

void explorerFetchStatus(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    size_t _first, size_t _count)
{
    for (size_t i = _first; i < _first + _count && i < _state.txs.size(); ++i)
    {
        auto& row = _state.txs[i];
        if (row.status >= 0 || row.hash.empty())
        {
            continue;
        }
        auto params = explorerParams(_group);
        params.append(row.hash);
        params.append(false);
        try
        {
            auto receipt = _call("getTransactionReceipt", params);
            if (receipt.isMember("status"))
            {
                row.status = static_cast<int>(jsonInt64(receipt["status"], -1));
            }
        }
        catch (std::exception const&)
        {
            return;  // the receipt may not be written yet; retried on the next round
        }
    }
}

std::optional<int64_t> lastBlockAgeMs(ExplorerState const& _state, int64_t _nowMs)
{
    if (_state.blocks.empty() || _state.blocks.front().timestampMs <= 0)
    {
        return std::nullopt;
    }
    return std::max<int64_t>(0, _nowMs - _state.blocks.front().timestampMs);
}

std::optional<int64_t> avgIntervalMs(ExplorerState const& _state, size_t _lastN)
{
    if (_state.blocks.size() < 2 || _lastN == 0)
    {
        return std::nullopt;
    }
    auto const& newest = _state.blocks.front();
    auto const& oldest = _state.blocks[std::min(_lastN, _state.blocks.size() - 1)];
    auto span = newest.number - oldest.number;
    if (span <= 0 || newest.timestampMs <= 0 || oldest.timestampMs <= 0)
    {
        return std::nullopt;
    }
    return (newest.timestampMs - oldest.timestampMs) / span;
}
}  // namespace bcos::ops::tui
