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
#include "bcos-ops/OpsError.h"
#include <algorithm>
#include <cctype>

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

std::string blockParentHash(Json::Value const& _block)
{
    // the node's shape: parentInfo[{blockNumber, blockHash}]; parentHash as a fallback
    auto const& info = _block["parentInfo"];
    if (info.isArray() && !info.empty() && info[0].isObject())
    {
        return info[0].get("blockHash", "").asString();
    }
    return _block.get("parentHash", "").asString();
}

namespace
{
bool explorerSameHash(std::string const& _a, std::string const& _b)
{
    auto strip = [](std::string const& _h) {
        auto body = _h.starts_with("0x") || _h.starts_with("0X") ? _h.substr(2) : _h;
        std::transform(body.begin(), body.end(), body.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return body;
    };
    return strip(_a) == strip(_b);
}

bool explorerStopped(std::atomic<bool> const* _stop)
{
    return _stop != nullptr && _stop->load();
}

/// one block into the rows; false when the RPC failed or _stop was raised
bool explorerFetchOne(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    int64_t _number, bool _front, bool _gapBelow, std::atomic<bool> const* _stop)
{
    if (explorerStopped(_stop))
    {
        return false;
    }
    auto params = explorerParams(_group);
    params.append(Json::Int64(_number));
    params.append(false);
    params.append(false);
    Json::Value block;
    try
    {
        block = _call("getBlockByNumber", params);
    }
    catch (std::exception const&)
    {
        return false;
    }
    auto row = blockRowFromJson(block);
    if (row.number < 0)
    {
        row.number = _number;
    }
    // reorg (EL modes have them): a new head whose parent is not the row below it replaced
    // that row's block. Walk the rows down in this round until the node's block at a row's
    // height has the row's hash again (bounded by the rows held), drop everything above that
    // fork point and let the next round refetch from it. A row without a hash cannot be
    // compared and is never treated as a fork.
    auto parent = blockParentHash(block);
    if (_front && !parent.empty() && !_state.blocks.empty() &&
        !_state.blocks.front().hash.empty() && _state.blocks.front().number == _number - 1 &&
        !explorerSameHash(_state.blocks.front().hash, parent))
    {
        auto dropTop = [&_state]() {
            auto height = _state.blocks.front().number;
            _state.blocks.pop_front();
            while (!_state.txs.empty() && _state.txs.front().blockNumber >= height)
            {
                _state.txs.pop_front();
            }
        };
        dropTop();  // the row under the new head is known to be replaced
        while (!_state.blocks.empty() && !_state.blocks.front().hash.empty() &&
               !explorerStopped(_stop))
        {
            auto ancestor = explorerParams(_group);
            ancestor.append(Json::Int64(_state.blocks.front().number));
            ancestor.append(true);  // header only
            ancestor.append(true);
            std::string nodeHash;
            try
            {
                nodeHash = blockRowFromJson(_call("getBlockByNumber", ancestor)).hash;
            }
            catch (std::exception const&)
            {
                break;  // unknown: keep the row, the next round compares again
            }
            if (explorerSameHash(_state.blocks.front().hash, nodeHash))
            {
                break;
            }
            dropTop();
        }
        _state.lastFetchedHeight = _state.blocks.empty() ? -1 : _state.blocks.front().number;
        ++_state.reorgs;
        return false;
    }
    row.gapBelow = _gapBelow;
    auto txs = txRowsFromBlock(block);
    if (_front)
    {
        _state.blocks.push_front(row);
        for (auto it = txs.rbegin(); it != txs.rend(); ++it)
        {
            _state.txs.push_front(std::move(*it));
        }
    }
    else
    {
        _state.blocks.push_back(row);
        for (auto& tx : txs)
        {
            _state.txs.push_back(std::move(tx));
        }
    }
    return true;
}
}  // namespace

void explorerFetch(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    int64_t _height, std::atomic<bool> const* _stop)
{
    if (_height < 0)
    {
        return;
    }
    if (_height < _state.lastFetchedHeight)
    {
        // the chain went backwards (node restarted from a snapshot, reorg): start over
        _state.blocks.clear();
        _state.txs.clear();
        _state.lastFetchedHeight = -1;
    }
    int64_t from = _state.lastFetchedHeight < 0 ?
                       std::max<int64_t>(0, _height - static_cast<int64_t>(_state.capacity) + 1) :
                       _state.lastFetchedHeight + 1;
    int64_t budget = c_explorerMaxFetchPerRound;
    if (from <= _height)
    {
        bool gap = false;
        if (_height - from + 1 > budget)
        {
            from = _height - budget + 1;
            gap = _state.lastFetchedHeight >= 0;
        }
        for (int64_t number = from; number <= _height; ++number)
        {
            if (!explorerFetchOne(
                    _state, _call, _group, number, true, gap && number == from, _stop))
            {
                explorerTrim(_state);
                return;  // keep what arrived; the next round retries from lastFetchedHeight + 1
            }
            _state.lastFetchedHeight = number;
            --budget;
        }
    }
    // back-fill: rows the first open could not fetch within the budget, oldest-first below the
    // list, as long as the list is contiguous (a gap row means older rows already sit below it)
    bool contiguous = std::none_of(_state.blocks.begin(), _state.blocks.end(),
        [](BlockRow const& row) { return row.gapBelow; });
    while (contiguous && budget > 0 && _state.blocks.size() < _state.capacity &&
           !_state.blocks.empty() && _state.blocks.back().number > 0)
    {
        auto number = _state.blocks.back().number - 1;
        if (!explorerFetchOne(_state, _call, _group, number, false, false, _stop))
        {
            break;
        }
        --budget;
    }
    explorerTrim(_state);  // the capacity may have shrunk with the terminal
}

void explorerFetchStatus(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    size_t _first, size_t _count, std::atomic<bool> const* _stop)
{
    for (size_t i = _first; i < _first + _count && i < _state.txs.size(); ++i)
    {
        auto& row = _state.txs[i];
        if (row.status >= 0 || row.hash.empty())
        {
            continue;
        }
        if (explorerStopped(_stop))
        {
            return;
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
        catch (OpsRpcError const&)
        {
            continue;  // this receipt is not written yet; the next rows may be
        }
        catch (std::exception const&)
        {
            return;  // transport failure: nothing else will answer this round
        }
    }
}

namespace
{
template <typename Key, typename Map>
void explorerRemember(Map& _map, std::deque<Key>& _order, Key const& _key)
{
    if (std::find(_order.begin(), _order.end(), _key) == _order.end())
    {
        _order.push_back(_key);
    }
    while (_order.size() > c_explorerDetailCache)
    {
        _map.erase(_order.front());
        _order.pop_front();
    }
}

bool explorerStoreBlock(ExplorerState& _state, Json::Value _block)
{
    auto number = jsonInt64(_block["number"], -1);
    if (number < 0)
    {
        return false;
    }
    _state.blockDetails[number] = std::move(_block);
    explorerRemember(_state.blockDetails, _state.blockDetailOrder, number);
    return true;
}
}  // namespace

bool explorerFetchBlockDetail(
    ExplorerState& _state, RpcCall const& _call, std::string const& _group, int64_t _number)
{
    auto params = explorerParams(_group);
    params.append(Json::Int64(_number));
    params.append(false);
    params.append(false);
    try
    {
        return explorerStoreBlock(_state, _call("getBlockByNumber", params));
    }
    catch (std::exception const&)
    {
        return false;
    }
}

bool explorerFetchBlockDetailByHash(ExplorerState& _state, RpcCall const& _call,
    std::string const& _group, std::string const& _hash)
{
    auto params = explorerParams(_group);
    params.append(_hash);
    params.append(false);
    params.append(false);
    try
    {
        return explorerStoreBlock(_state, _call("getBlockByHash", params));
    }
    catch (std::exception const&)
    {
        return false;
    }
}

bool explorerFetchTxDetail(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    std::string const& _hash)
{
    auto params = explorerParams(_group);
    params.append(_hash);
    params.append(false);
    TxDetail detail;
    try
    {
        detail.transaction = _call("getTransaction", params);
        detail.receipt = _call("getTransactionReceipt", params);
        detail.pending = !detail.receipt.isObject() || !detail.receipt.isMember("status");
    }
    catch (std::exception const&)
    {
        detail.pending = true;  // not on chain yet (or unreachable): shown as pending, retried
    }
    _state.txDetails[_hash] = std::move(detail);
    explorerRemember(_state.txDetails, _state.txDetailOrder, _hash);
    return !_state.txDetails[_hash].pending;
}

void explorerFetchWanted(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    std::vector<DetailWant> const& _wants, std::atomic<bool> const* _stop)
{
    for (auto const& want : _wants)
    {
        if (explorerStopped(_stop))
        {
            return;
        }
        if (want.block)
        {
            if (want.number >= 0 && !_state.blockDetails.contains(want.number))
            {
                explorerFetchBlockDetail(_state, _call, _group, want.number);
            }
            else if (want.number < 0 && !want.hash.empty())
            {
                bool cached = std::any_of(
                    _state.blockDetails.begin(), _state.blockDetails.end(), [&](auto const& entry) {
                        return entry.second["hash"].asString() == want.hash;
                    });
                if (!cached)
                {
                    explorerFetchBlockDetailByHash(_state, _call, _group, want.hash);
                }
            }
            continue;
        }
        if (want.hash.empty())
        {
            continue;
        }
        auto it = _state.txDetails.find(want.hash);
        if (it == _state.txDetails.end() || it->second.pending)
        {
            explorerFetchTxDetail(_state, _call, _group, want.hash);
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
