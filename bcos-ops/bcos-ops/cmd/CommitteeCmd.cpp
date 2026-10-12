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
 *  limitations under the License. *
 * @brief committee: the CommitteeManager / ProposalManager / Committee contracts bcos-auth
 *        deploys at genesis. Reads go through the preset table's committee rows with the
 *        addresses resolved from CommitteeManager (_committee(), _proposalMgr()); the nine
 *        propose-* writes, vote and revoke are write presets (governor only).
 * @file CommitteeCmd.cpp
 */
#include "CommitteeCmd.h"
#include "TxContext.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/Presets.h"
#include <ostream>

namespace bcos::ops
{
namespace
{
constexpr int64_t c_committeeDefaultInterval = 1000;
constexpr size_t c_committeeDefaultLast = 10;

struct CommitteeWrite
{
    std::string_view sub;
    std::string_view preset;
    size_t positionals;
    bool interval;  // appends --interval (default height + 1000) as the last argument
    std::string_view usage;
};

// console names are the preset rows' `console`; the help text lists them
constexpr std::array c_committeeWrites{
    CommitteeWrite{"propose-governor", "propose-governor", 2, true,
        "committee propose-governor <account> <weight> [--interval N]"},
    CommitteeWrite{"propose-rate", "propose-rate", 2, true,
        "committee propose-rate <participatesRate> <winRate> [--interval N]"},
    CommitteeWrite{"propose-deploy-auth-type", "propose-deploy-auth-type", 1, true,
        "committee propose-deploy-auth-type <0|1|2> [--interval N]"},
    CommitteeWrite{"propose-deploy-auth", "propose-deploy-auth", 2, true,
        "committee propose-deploy-auth <account> <true|false> [--interval N]"},
    CommitteeWrite{"propose-reset-admin", "propose-reset-admin", 2, true,
        "committee propose-reset-admin <contract> <admin> [--interval N]"},
    CommitteeWrite{"propose-rm-node", "propose-rm-node", 1, true,
        "committee propose-rm-node <nodeId> [--interval N]"},
    CommitteeWrite{"propose-weight", "propose-weight", 3, true,
        "committee propose-weight <nodeId> <weight> <add:true|false> [--interval N]"},
    CommitteeWrite{"propose-sysconfig", "propose-sysconfig", 2, true,
        "committee propose-sysconfig <key> <value> [--interval N]"},
    CommitteeWrite{"propose-vote-computer", "propose-vote-computer", 1, true,
        "committee propose-vote-computer <address> [--interval N]"},
    CommitteeWrite{"vote", "vote", 2, false, "committee vote <id> <true|false>"},
    CommitteeWrite{"revoke", "revoke", 1, false, "committee revoke <id>"},
};

/// a runner over a read-only (random key) sender for the view calls
struct CommitteeReader
{
    ChainFacts facts;
    std::unique_ptr<TxSender> sender;
    PresetRunner runner;
    explicit CommitteeReader(Connection const& _connection)
      : facts(chainFacts(_connection)),
        sender(std::make_unique<TxSender>(_connection.call, _connection.group, facts.chainId,
            facts.sm, makeAccount(facts.sm, std::nullopt))),
        runner(*sender, facts.sm, _connection.call, _connection.group)
    {}
    /// first decoded output of a committee read preset, or throws with the preset name
    Json::Value read(
        std::string_view _name, std::vector<std::string> const& _args, std::string const& _to = "")
    {
        auto result = runner.run(*findPreset("committee", _name), _args, _to);
        if (!result.ok || !result.decoded.isArray())
        {
            throw OpsError(c_exitChecksFailed,
                "committee " + std::string(_name) + " failed: status " +
                    std::to_string(result.receipt.status) + " " + result.receipt.message);
        }
        return result.decoded;
    }
};

std::string committeeStringOf(Json::Value const& _value)
{
    return _value.isString() ? _value.asString() : _value.toStyledString();
}

int runCommitteeInfo(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    CommitteeReader reader(_connection);
    auto committee = reader.read("committee-address", {})[0].asString();
    auto info = reader.read("info", {}, committee);
    Json::Value value;
    value["committee"] = committee;
    value["participatesRate"] = committeeStringOf(info.get(0U, ""));
    value["winRate"] = committeeStringOf(info.get(1U, ""));
    value["governors"] = Json::Value(Json::arrayValue);
    auto governors = info.get(2U, Json::Value(Json::arrayValue));
    auto weights = info.get(3U, Json::Value(Json::arrayValue));
    for (Json::ArrayIndex i = 0; i < governors.size(); ++i)
    {
        Json::Value governor;
        governor["address"] = governors[i];
        governor["weight"] = weights.get(i, "");
        value["governors"].append(governor);
    }
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, value);
        return c_exitOk;
    }
    std::vector<std::pair<std::string, std::string>> rows = {{"committee", committee},
        {"participatesRate", value["participatesRate"].asString() + "%"},
        {"winRate", value["winRate"].asString() + "%"}};
    for (auto const& governor : value["governors"])
    {
        rows.emplace_back("governor", committeeStringOf(governor["address"]) + "  weight " +
                                          committeeStringOf(governor["weight"]));
    }
    printRows(_out, rows);
    return c_exitOk;
}

Json::Value proposalJson(
    CommitteeReader& _reader, std::string const& _manager, std::string const& _id)
{
    auto fields = _reader.read("proposal", {_id}, _manager);
    auto status = fields.get(4U, "0");
    int statusCode = status.isIntegral() ? status.asInt() : std::stoi(committeeStringOf(status));
    Json::Value value;
    value["id"] = _id;
    value["resourceId"] = fields.get(0U, "");
    value["proposer"] = fields.get(1U, "");
    value["proposalType"] = fields.get(2U, "");
    value["blockNumberInterval"] = fields.get(3U, "");
    value["status"] = statusCode;
    value["statusName"] = proposalStatusName(statusCode);
    value["agreeVoters"] = fields.get(5U, Json::Value(Json::arrayValue));
    value["againstVoters"] = fields.get(6U, Json::Value(Json::arrayValue));
    return value;
}

void printProposalRow(std::ostream& _out, Json::Value const& _proposal)
{
    _out << std::left << std::setw(6) << _proposal["id"].asString() << std::setw(6)
         << committeeStringOf(_proposal["proposalType"]) << std::setw(10)
         << _proposal["statusName"].asString() << std::setw(44)
         << committeeStringOf(_proposal["proposer"]) << "agree " << _proposal["agreeVoters"].size()
         << "  against " << _proposal["againstVoters"].size() << '\n';
}

int runCommitteeProposals(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    CommitteeReader reader(_connection);
    auto manager = reader.read("proposal-mgr-address", {})[0].asString();
    auto count = std::stoll(committeeStringOf(reader.read("proposal-count", {}, manager)[0]));
    auto last = _args.numberOr<int64_t>("last", static_cast<int64_t>(c_committeeDefaultLast));
    Json::Value list(Json::arrayValue);
    for (auto id = std::max<int64_t>(1, count - last + 1); id <= count; ++id)
    {
        list.append(proposalJson(reader, manager, std::to_string(id)));
    }
    if (wantJson(_args.flag("json")))
    {
        Json::Value value;
        value["proposalManager"] = manager;
        value["count"] = Json::Int64(count);
        value["proposals"] = list;
        printJson(_out, value);
        return c_exitOk;
    }
    _out << "proposals " << count << " (manager " << manager << ")\n";
    for (auto const& proposal : list)
    {
        printProposalRow(_out, proposal);
    }
    return c_exitOk;
}

int runCommitteeProposal(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().size() < 2)
    {
        throw OpsError(c_exitUsage, "usage: committee proposal <id>");
    }
    CommitteeReader reader(_connection);
    auto manager = reader.read("proposal-mgr-address", {})[0].asString();
    auto proposal = proposalJson(reader, manager, _args.positionals()[1]);
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, proposal);
        return c_exitOk;
    }
    std::vector<std::pair<std::string, std::string>> rows = {{"id", proposal["id"].asString()},
        {"type", committeeStringOf(proposal["proposalType"])},
        {"status", proposal["statusName"].asString()},
        {"proposer", committeeStringOf(proposal["proposer"])},
        {"resource", committeeStringOf(proposal["resourceId"])},
        {"blockNumberInterval", committeeStringOf(proposal["blockNumberInterval"])}};
    for (auto const& voter : proposal["agreeVoters"])
    {
        rows.emplace_back("agree", committeeStringOf(voter));
    }
    for (auto const& voter : proposal["againstVoters"])
    {
        rows.emplace_back("against", committeeStringOf(voter));
    }
    printRows(_out, rows);
    return c_exitOk;
}

int runCommittee(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage,
            "usage: committee info|proposals|proposal|propose-*|vote|revoke ... (see --help)");
    }
    return runCommitteeOn(connect(connectOptionsFrom(_args)), _args, _out);
}
}  // namespace

int runCommitteeOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    auto const& sub = positionals.empty() ? "" : positionals[0];
    if (sub == "info")
    {
        return runCommitteeInfo(_connection, _args, _out);
    }
    if (sub == "proposals")
    {
        return runCommitteeProposals(_connection, _args, _out);
    }
    if (sub == "proposal")
    {
        return runCommitteeProposal(_connection, _args, _out);
    }
    for (auto const& spec : c_committeeWrites)
    {
        if (spec.sub != sub)
        {
            continue;
        }
        if (positionals.size() < spec.positionals + 1)
        {
            throw OpsError(c_exitUsage, "usage: " + std::string(spec.usage));
        }
        std::vector<std::string> params(
            positionals.begin() + 1, positionals.begin() + 1 + spec.positionals);
        if (spec.interval)
        {
            int64_t interval = 0;
            if (_args.option("interval"))
            {
                interval = _args.numberOr<int64_t>("interval", 0);
            }
            else
            {
                Json::Value heightParams(Json::arrayValue);
                heightParams.append(_connection.group);
                heightParams.append("");
                interval = _connection.call("getBlockNumber", heightParams).asInt64() +
                           c_committeeDefaultInterval;
            }
            params.push_back(std::to_string(interval));
        }
        return runWritePreset(
            _connection, _args, _out, *findPreset("committee", spec.preset), params);
    }
    throw OpsError(c_exitUsage, "unknown committee subcommand: " + sub);
}

void registerCommitteeCommand()
{
    registerCommand("committee",
        Command{"info (console getCommitteeInfo) | proposals [--last N] / proposal <id> "
                "(getProposalInfo) | propose-governor <acct> <weight> (updateGovernorProposal) | "
                "propose-rate <p> <w> (setRateProposal) | propose-deploy-auth-type <t> "
                "(setDeployAuthTypeProposal) | propose-deploy-auth <acct> <open> "
                "(modifyDeployAuthProposal) | propose-reset-admin <contract> <admin> "
                "(resetAdminProposal) | propose-rm-node <nodeId> (rmNodeProposal) | "
                "propose-weight <nodeId> <weight> <add> (setConsensusWeightProposal) | "
                "propose-sysconfig <key> <value> (setSysConfigProposal) | propose-vote-computer "
                "<addr> (upgradeVoteComputerProposal) | vote <id> <true|false> (voteProposal) | "
                "revoke <id> (revokeProposal). Governance contracts of an auth chain (-a).",
            "info | proposals [--last N] | proposal <id> | propose-governor <acct> <weight> | "
            "propose-rate <participates> <win> | propose-deploy-auth-type <0|1|2> | "
            "propose-deploy-auth <acct> <true|false> | propose-reset-admin <contract> <admin> | "
            "propose-rm-node <nodeId> | propose-weight <nodeId> <weight> <add> | "
            "propose-sysconfig <key> <value> | propose-vote-computer <addr> | vote <id> "
            "<true|false> | revoke <id>   [--interval <blocks>] [--account <key-file>] [--yes] "
            "[--node-dir <dir> | --rpc <host:port>] [--json]",
            {"yes", "direct"}, runCommittee});
}
}  // namespace bcos::ops
