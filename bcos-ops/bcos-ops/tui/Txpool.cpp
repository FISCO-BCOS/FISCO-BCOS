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
 * @brief see Txpool.h
 * @file Txpool.cpp
 */
#include "Txpool.h"
#include <algorithm>

namespace bcos::ops::tui
{
namespace
{
int64_t txpoolNonce(std::string const& _key, Json::Value const& _tx)
{
    try
    {
        return std::stoll(_key);  // geth keys the nonce map by the decimal nonce
    }
    catch (std::exception const&)
    {}
    try
    {
        return std::stoll(_tx.get("nonce", "").asString(), nullptr, 0);
    }
    catch (std::exception const&)
    {
        return -1;
    }
}

void txpoolAppend(std::vector<TxpoolRow>& _rows, Json::Value const& _bySender, bool _pending)
{
    if (!_bySender.isObject())
    {
        return;
    }
    for (auto const& sender : _bySender.getMemberNames())
    {
        auto const& byNonce = _bySender[sender];
        if (!byNonce.isObject())
        {
            continue;
        }
        for (auto const& key : byNonce.getMemberNames())
        {
            auto const& tx = byNonce[key];
            TxpoolRow row;
            row.pending = _pending;
            row.hash = tx.get("hash", "").asString();
            row.from = tx.get("from", sender).asString();
            row.nonce = txpoolNonce(key, tx);
            row.to = tx["to"].isString() ? tx["to"].asString() : "";
            row.value = tx["value"].isString() ? tx["value"].asString() : "";
            _rows.push_back(std::move(row));
        }
    }
}
}  // namespace

std::vector<TxpoolRow> flattenTxpoolContent(Json::Value const& _content)
{
    std::vector<TxpoolRow> rows;
    txpoolAppend(rows, _content["pending"], true);
    txpoolAppend(rows, _content["queued"], false);
    std::stable_sort(rows.begin(), rows.end(), [](TxpoolRow const& a, TxpoolRow const& b) {
        if (a.pending != b.pending)
        {
            return a.pending;
        }
        if (a.from != b.from)
        {
            return a.from < b.from;
        }
        return a.nonce < b.nonce;
    });
    return rows;
}

TxpoolState txpoolFetch(
    std::string const& _mode, RpcCall const* _web3, std::atomic<bool> const* _stop)
{
    TxpoolState state;
    if (_mode != "opstack-el")
    {
        state.note = "txpool content not exposed (mode=" + _mode + ")";
        return state;
    }
    if (_web3 == nullptr)
    {
        state.note = "txpool content not exposed (web3_rpc disabled)";
        return state;
    }
    if (_stop != nullptr && _stop->load())
    {
        return state;
    }
    try
    {
        state.rows =
            flattenTxpoolContent((*_web3)("txpool_content", Json::Value(Json::arrayValue)));
        state.content = true;
    }
    catch (std::exception const& e)
    {
        state.error = e.what();
    }
    return state;
}
}  // namespace bcos::ops::tui
