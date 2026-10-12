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
#include "TxCmd.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/collect/GroupFacts.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/AuthMode.h"
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

Routing routingFor(Connection const& _connection, Args const& _args, Preset const& _preset)
{
    Routing routing;
    routing.direct = _args.flag("direct");
    if (auto interval = _args.option("interval"))
    {
        routing.interval = _args.numberOr<int64_t>("interval", 0);
    }
    if (_preset.proposal && !routing.direct)
    {
        routing.proposals = chainUsesProposals(_connection);
    }
    return routing;
}

std::vector<std::pair<std::string, std::string>> confirmRows(Preset const& _preset,
    std::string const& _to, std::vector<std::string> const& _params, std::string const& _mode,
    std::string const& _proposalMethod)
{
    std::string params;
    for (size_t i = 0; i < _params.size(); ++i)
    {
        auto name = i < _preset.params.size() ? std::string(_preset.params[i].name) :
                                                "arg" + std::to_string(i);
        params += (i == 0 ? "" : ", ") + name + "=" + _params[i];
    }
    std::vector<std::pair<std::string, std::string>> rows = {{"mode", _mode}, {"to", _to},
        {"method", std::string(_preset.signature)}, {"params", params}};
    if (!_proposalMethod.empty())
    {
        rows.emplace_back("note", _proposalMethod);
    }
    return rows;
}

bool confirmWrite(Args const& _args, std::ostream& _out, Preset const& _preset,
    std::string const& _to, std::vector<std::string> const& _params, std::string const& _mode,
    std::string const& _proposalMethod)
{
    if (_args.flag("yes"))
    {
        return true;
    }
    if (::isatty(STDIN_FILENO) == 0)
    {
        throw OpsError(c_exitUsage, "stdin is not a terminal: pass --yes to run " +
                                        std::string(_preset.group) + " " +
                                        std::string(_preset.name) + " without confirmation");
    }
    printRows(_out, confirmRows(_preset, _to, _params, _mode, _proposalMethod));
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
    if (!_result.directMethod.empty())
    {
        rows.emplace_back("replaces", _result.directMethod);
    }
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
    if (_result.proposal)
    {
        auto const& proposal = *_result.proposal;
        rows.emplace_back("proposal", proposal.id + " " + proposal.statusName);
        if (!proposal.error.empty())
        {
            rows.emplace_back("proposal error", proposal.error);
        }
        rows.emplace_back("executed", proposal.executed ? "yes" : "no");
        if (proposal.execCode)
        {
            rows.emplace_back(
                "execCode", std::to_string(*proposal.execCode) + " " + proposal.execCodeName);
        }
        rows.emplace_back("agree", std::to_string(proposal.info["agreeVoters"].size()));
        rows.emplace_back("against", std::to_string(proposal.info["againstVoters"].size()));
    }
    rows.emplace_back("ok", _result.ok ? "yes" : "no");
    printRows(_out, rows);
}

PresetResult readPreset(Connection const& _connection, Preset const& _preset,
    std::vector<std::string> const& _params, std::string const& _to)
{
    auto facts = chainFacts(_connection);
    TxSender sender(_connection.call, _connection.group, facts.chainId, facts.sm,
        makeAccount(facts.sm, std::nullopt));
    PresetRunner runner(sender, facts.sm, _connection.call, _connection.group);
    return runner.run(_preset, _params, _to);
}

int runWritePreset(Connection const& _connection, Args const& _args, std::ostream& _out,
    Preset const& _preset, std::vector<std::string> const& _params, ResultHint const& _hint)
{
    auto routing = routingFor(_connection, _args, _preset);
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    PresetRunner runner(*sender, facts.sm, _connection.call, _connection.group);
    runner.setRouting(routing);
    bool viaProposal = runner.routesThroughProposal(_preset);
    auto const* committeePreset = viaProposal ? PresetRunner::proposalPreset(_preset) : nullptr;
    // the confirmation shows what is really sent: on the proposal path that is the committee
    // address with the proposal's arguments, not the precompiled call it stands in for
    if (!confirmWrite(_args, _out, viaProposal ? *committeePreset : _preset,
            std::string(viaProposal ? committeePreset->to : _preset.to),
            viaProposal ? runner.proposalArgs(_preset, _params) : _params,
            viaProposal ? "proposal" : "direct",
            viaProposal ? "stands in for " + std::string(_preset.signature) : ""))
    {
        _out << "aborted\n";
        return c_exitUsage;
    }
    PresetResult result;
    try
    {
        result = runner.run(_preset, _params);
    }
    catch (OpsRpcError const& e)
    {
        // a refusal by the node (txpool full, nonce, block limit…) has the same shape and exit
        // code as in the tx commands
        return printRpcRefusal(_out, e, wantJson(_args.flag("json")));
    }
    auto hint = _hint ? _hint(result) : std::string();
    if (wantJson(_args.flag("json")))
    {
        auto json = result.toJson();
        if (!hint.empty())
        {
            json["hint"] = hint;
        }
        printJson(_out, json);
    }
    else
    {
        printPresetResult(_out, result, false);
        if (!hint.empty())
        {
            _out << hint << '\n';
        }
    }
    return result.exitCode();
}
}  // namespace bcos::ops
