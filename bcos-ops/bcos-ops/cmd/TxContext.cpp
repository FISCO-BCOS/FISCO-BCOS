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
 * @file TxContext.cpp
 */
#include "TxContext.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/collect/GroupFacts.h"
#include "bcos-ops/tx/Account.h"
#include <unistd.h>
#include <iostream>
#include <ostream>

namespace bcos::ops
{
ChainFacts chainFacts(Connection const& _connection)
{
    ChainFacts facts;
    if (_connection.nodeDir)
    {
        facts.chainId = _connection.nodeDir->chainId;
        facts.sm = _connection.nodeDir->smCrypto;
    }
    try
    {
        auto group = groupFacts(_connection.call, _connection.group, std::nullopt);
        facts.chainId = group.chainId;
        facts.sm = group.smCrypto.value_or(facts.sm);
    }
    catch (std::exception const&)
    {
        if (facts.chainId.empty())
        {
            throw;
        }
    }
    return facts;
}

std::unique_ptr<TxSender> makeSender(
    Connection const& _connection, ChainFacts const& _facts, Args const& _args)
{
    auto account = makeAccount(_facts.sm, _args.option("account"));
    return std::make_unique<TxSender>(
        _connection.call, _connection.group, _facts.chainId, _facts.sm, std::move(account));
}

bool confirmWrite(Args const& _args, std::ostream& _out, Preset const& _preset,
    std::string const& _to, std::vector<std::string> const& _params)
{
    if (_args.flag("yes") || ::isatty(STDIN_FILENO) == 0)
    {
        return true;
    }
    std::string params;
    for (size_t i = 0; i < _params.size() && i < _preset.params.size(); ++i)
    {
        params += (i == 0 ? "" : ", ") + std::string(_preset.params[i].name) + "=" + _params[i];
    }
    printRows(_out, {{"to", _to}, {"method", std::string(_preset.signature)}, {"params", params}});
    _out << "proceed? [y/N] " << std::flush;
    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "Y" || answer == "yes";
}

void printPresetResult(std::ostream& _out, PresetResult const& _result, bool _json)
{
    if (_json)
    {
        printJson(_out, _result.toJson());
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("mode", _result.mode);
    rows.emplace_back("method", _result.method);
    if (!_result.receipt.txHash.empty())
    {
        rows.emplace_back("txHash", _result.receipt.txHash);
        rows.emplace_back("block", std::to_string(_result.receipt.blockNumber));
    }
    rows.emplace_back("status",
        std::to_string(_result.receipt.status) + (_result.receipt.ok() ? " ok" : " FAILED"));
    if (_result.code)
    {
        rows.emplace_back("code", std::to_string(*_result.code) + " " + _result.codeName);
    }
    else if (!_result.decoded.isNull())
    {
        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";
        rows.emplace_back("result", Json::writeString(writer, _result.decoded));
    }
    if (!_result.receipt.message.empty())
    {
        rows.emplace_back("message", _result.receipt.message);
    }
    rows.emplace_back("ok", _result.ok ? "yes" : "no");
    printRows(_out, rows);
}

int runWritePreset(Connection const& _connection, Args const& _args, std::ostream& _out,
    Preset const& _preset, std::vector<std::string> const& _params)
{
    if (!confirmWrite(_args, _out, _preset, std::string(_preset.to), _params))
    {
        _out << "aborted\n";
        return c_exitUsage;
    }
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    PresetRunner runner(*sender, facts.sm);
    auto result = runner.run(_preset, _params);
    printPresetResult(_out, result, wantJson(_args.flag("json")));
    return result.exitCode();
}
}  // namespace bcos::ops
