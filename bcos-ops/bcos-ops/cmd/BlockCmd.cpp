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
 * @brief `fisco-bcos block get <height|hash> [--txs] [--proof]` (console: getBlockByNumber /
 *        getBlockByHash). Header only by default; --txs asks for the full transactions;
 *        --proof fetches every transaction of the block with its txProof.
 * @file BlockCmd.cpp
 */
#include "TxCmd.h"
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/TxSender.h"
#include <algorithm>
#include <cctype>
#include <ostream>

namespace bcos::ops
{
namespace
{
bool looksLikeHash(std::string const& _value)
{
    return _value.starts_with("0x") && _value.size() == 66 &&
           std::all_of(_value.begin() + 2, _value.end(),
               [](unsigned char c) { return std::isxdigit(c) != 0; });
}

Json::Value fetchBlock(
    Connection const& _connection, std::string const& _ref, bool _onlyHeader, bool _onlyTxHash)
{
    Json::Value params(Json::arrayValue);
    params.append(_connection.group);
    params.append("");
    if (looksLikeHash(_ref))
    {
        params.append(_ref);
        params.append(_onlyHeader);
        params.append(_onlyTxHash);
        return _connection.call("getBlockByHash", params);
    }
    int64_t number = 0;
    try
    {
        size_t consumed = 0;
        number = std::stoll(_ref, &consumed);
        if (consumed != _ref.size() || number < 0)
        {
            throw std::invalid_argument(_ref);
        }
    }
    catch (std::exception const&)
    {
        throw OpsError(
            c_exitUsage, "block get expects a height or a 0x-prefixed 32-byte hash: " + _ref);
    }
    params.append(static_cast<Json::Int64>(number));
    params.append(_onlyHeader);
    params.append(_onlyTxHash);
    return _connection.call("getBlockByNumber", params);
}

std::string txHashOf(Json::Value const& _tx)
{
    return _tx.isString() ? _tx.asString() : _tx.get("hash", "").asString();
}

int runBlockGet(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: block get <height|hash> [--txs] [--proof]");
    }
    bool txs = _args.flag("txs");
    bool proof = _args.flag("proof");
    // --txs: full transactions; --proof alone: hashes only (the proofs come per tx below)
    auto block = fetchBlock(_connection, _args.positionals()[0], !txs && !proof, !txs);
    if (proof)
    {
        Json::Value proofs(Json::arrayValue);
        for (auto const& tx : block.get("transactions", Json::arrayValue))
        {
            auto hash = txHashOf(tx);
            if (hash.empty())
            {
                continue;
            }
            auto merged = fetchTransaction(_connection.call, _connection.group, hash, true);
            Json::Value entry;
            entry["hash"] = hash;
            entry["txProof"] = merged["transaction"].get("txProof", Json::arrayValue);
            // the receipt proof is txReceiptProof; receiptProof is an always-empty
            // compatibility field (JsonRpcImpl_2_0.cpp getTransactionReceipt)
            entry["receiptProof"] = merged["receipt"].get("txReceiptProof", Json::arrayValue);
            proofs.append(entry);
        }
        block["proofs"] = proofs;
    }
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, block);
        return c_exitOk;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("number", std::to_string(block.get("number", -1).asInt64()));
    rows.emplace_back("hash", block.get("hash", "").asString());
    rows.emplace_back("parentHash", block.get("parentInfo", Json::arrayValue).size() > 0 ?
                                        block["parentInfo"][0].get("blockHash", "").asString() :
                                        "");
    rows.emplace_back("timestamp", std::to_string(block.get("timestamp", 0).asInt64()));
    rows.emplace_back("sealer", std::to_string(block.get("sealer", -1).asInt64()));
    rows.emplace_back("gasUsed", block.get("gasUsed", "").asString());
    rows.emplace_back("txsRoot", block.get("txsRoot", "").asString());
    rows.emplace_back("receiptsRoot", block.get("receiptsRoot", "").asString());
    rows.emplace_back("stateRoot", block.get("stateRoot", "").asString());
    rows.emplace_back(
        "signatures", std::to_string(block.get("signatureList", Json::arrayValue).size()));
    auto const& transactions = block.get("transactions", Json::arrayValue);
    rows.emplace_back("txs", std::to_string(transactions.size()));
    if (proof)
    {
        rows.emplace_back("proofs", std::to_string(block["proofs"].size()));
    }
    printRows(_out, rows);
    if (txs)
    {
        for (auto const& tx : transactions)
        {
            _out << "  " << abridged(txHashOf(tx), 8) << "  from " << tx.get("from", "").asString()
                 << "  to " << tx.get("to", "").asString() << '\n';
        }
    }
    return c_exitOk;
}

int runBlock(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: block get <height|hash> [--txs] [--proof]");
    }
    auto connection = connect(connectOptionsFrom(_args));
    return runBlockOn(connection, _args, _out);
}
}  // namespace

int runBlockOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty() || _args.positionals()[0] != "get")
    {
        throw OpsError(c_exitUsage, "usage: block get <height|hash> [--txs] [--proof]");
    }
    std::vector<std::string> rest(_args.positionals().begin() + 1, _args.positionals().end());
    return runBlockGet(_connection, _args.withPositionals(rest), _out);
}

void registerBlockCommand()
{
    registerCommand("block",
        Command{"block header, transactions and per-tx proofs by height or hash (console: "
                "getBlockByNumber, getBlockByHash)",
            "get <height|0xhash> [--txs] [--proof]   [--node-dir <dir> | --rpc <host:port>] "
            "[--json]",
            {"txs", "proof"}, runBlock});
}
}  // namespace bcos::ops
