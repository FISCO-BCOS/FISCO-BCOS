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
 * @brief the browser rows of the home page: recent blocks and the transactions flattened out of
 *        them, fetched incrementally. Pure functions over an ExplorerState so the refresher can
 *        work on a copy outside the model mutex and tests can drive them with a fake RpcCall.
 * @file Explorer.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <json/json.h>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
struct BlockRow
{
    int64_t number = -1;
    std::string hash;
    int txCount = 0;
    int64_t timestampMs = 0;
    std::string sealer;
    bool gapBelow = false;  // older blocks were skipped between this row and the next one
};

struct TxRow
{
    std::string hash;
    std::string from;
    std::string to;
    int status = -1;  // receipt status; -1 until fetched
    int64_t blockNumber = -1;
};

struct ExplorerState
{
    std::deque<BlockRow> blocks;  // newest first
    std::deque<TxRow> txs;        // newest first
    int64_t lastFetchedHeight = -1;
    size_t capacity = 20;
};

constexpr size_t c_explorerMinRows = 20;
constexpr size_t c_explorerMaxRows = 200;
constexpr int64_t c_explorerMaxFetchPerRound = 50;

/// max(terminal rows, 20) capped at 200
size_t explorerCapacity(int _terminalRows);
BlockRow blockRowFromJson(Json::Value const& _block);
/// block order preserved (first tx of the block first)
std::vector<TxRow> txRowsFromBlock(Json::Value const& _block);
/// fetches (lastFetchedHeight, _height] with getBlockByNumber [group,"",n,false,false]; the
/// first call back-fills _state.capacity blocks; at most 50 per call (older ones are skipped and
/// the oldest fetched row carries gapBelow); a failing RPC keeps what was fetched so far
void explorerFetch(
    ExplorerState& _state, RpcCall const& _call, std::string const& _group, int64_t _height);
/// getTransactionReceipt for the rows [_first, _first + _count) that have no status yet
void explorerFetchStatus(ExplorerState& _state, RpcCall const& _call, std::string const& _group,
    size_t _first, size_t _count);
std::optional<int64_t> lastBlockAgeMs(ExplorerState const& _state, int64_t _nowMs);
/// mean interval over the newest min(_lastN + 1, blocks) rows; gaps are averaged across
std::optional<int64_t> avgIntervalMs(ExplorerState const& _state, size_t _lastN = 20);
}  // namespace bcos::ops::tui
