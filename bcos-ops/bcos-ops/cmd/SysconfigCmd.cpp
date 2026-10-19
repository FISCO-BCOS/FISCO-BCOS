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
 * @file SysconfigCmd.cpp
 */
#include "SysconfigCmd.h"
#include "TxContext.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/FeatureFlags.h"
#include "bcos-ops/tx/Presets.h"
#include <iomanip>
#include <ostream>

namespace bcos::ops
{
namespace
{
int runSysconfigGet(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().size() < 2)
    {
        throw OpsError(c_exitUsage, "usage: sysconfig get <key>");
    }
    Json::Value params(Json::arrayValue);
    params.append(_connection.group);
    params.append("");
    params.append(_args.positionals()[1]);
    auto result = _connection.call("getSystemConfigByKey", params);
    if (wantJson(_args.flag("json")))
    {
        Json::Value value;
        value["key"] = _args.positionals()[1];
        value["value"] = result.get("value", "");
        value["enableNumber"] = result.get("blockNumber", Json::Value(Json::nullValue));
        printJson(_out, value);
        return c_exitOk;
    }
    printRows(_out, {{"key", _args.positionals()[1]}, {"value", result.get("value", "").asString()},
                        {"enableNumber", result.get("blockNumber", "").asString()}});
    return c_exitOk;
}

int runFeatures(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto states = readFeatureStates(_connection.call, _connection.group);
    if (wantJson(_args.flag("json")))
    {
        Json::Value list(Json::arrayValue);
        for (auto const& state : states)
        {
            list.append(state.toJson());
        }
        printJson(_out, list);
        return c_exitOk;
    }
    for (auto const& state : states)
    {
        _out << std::left << std::setw(56) << state.name << (state.enabled ? "on " : "off")
             << (state.deprecated ? "  (deprecated, drives nothing)" : "") << '\n';
    }
    return c_exitOk;
}

}  // namespace

int runEnableFeaturesOn(Connection const& _connection, Args const& _args, std::ostream& _out,
    std::vector<std::string> const& _requested, std::string const& _hint)
{
    if (_requested.empty())
    {
        throw OpsError(c_exitUsage, "usage: sysconfig enable-features <flag> [flag...]");
    }
    auto order = featureEnableOrder(_requested);  // validates the names before any prompt
    auto const* preset = findPreset("sysconfig", "set");
    std::string listed;
    for (auto const& name : order)
    {
        listed += (listed.empty() ? "" : " ") + name;
    }
    // on an auth chain every flag is one createSetSysConfigProposal (spec §5.1.5)
    auto routing = routingFor(_connection, _args, *preset);
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    PresetRunner runner(*sender, facts.sm, _connection.call, _connection.group);
    runner.setRouting(routing);
    bool viaProposal = runner.routesThroughProposal(*preset);
    auto const* committeePreset = viaProposal ? PresetRunner::proposalPreset(*preset) : nullptr;
    if (!confirmWrite(_args, _out, *preset, std::string(preset->to), {listed, "1"},
            viaProposal ? "proposal" : "direct",
            committeePreset != nullptr ? std::string(committeePreset->signature) : ""))
    {
        _out << "aborted\n";
        return c_exitUsage;
    }
    auto steps = enableFeatures(_connection.call, _connection.group, runner, _requested);
    bool ok = true;
    for (auto const& step : steps)
    {
        ok = ok && step.action != "failed";
    }
    if (wantJson(_args.flag("json")))
    {
        Json::Value value;
        value["ok"] = ok;
        value["mode"] = viaProposal ? "proposal" : "direct";
        value["steps"] = Json::Value(Json::arrayValue);
        for (auto const& step : steps)
        {
            value["steps"].append(step.toJson());
        }
        if (!_hint.empty())
        {
            value["hint"] = _hint;
        }
        printJson(_out, value);
    }
    else
    {
        for (auto const& step : steps)
        {
            _out << std::left << std::setw(56) << step.name << step.action;
            if (step.result && step.result->proposal)
            {
                _out << " (proposal " << step.result->proposal->id << " "
                     << step.result->proposal->statusName << ")";
            }
            if (step.result && step.result->code && *step.result->code != 0)
            {
                _out << " (" << step.result->codeName << ")";
            }
            else if (step.result && !step.result->receipt.message.empty())
            {
                _out << " (" << step.result->receipt.message << ")";
            }
            _out << '\n';
        }
        if (!_hint.empty())
        {
            _out << _hint << '\n';
        }
    }
    return ok ? c_exitOk : c_exitChecksFailed;
}

namespace
{
int runSysconfig(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: sysconfig get|set|features|enable-features ...");
    }
    return runSysconfigOn(connect(connectOptionsFrom(_args)), _args, _out);
}
}  // namespace

int runSysconfigOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& sub = _args.positionals().empty() ? "" : _args.positionals()[0];
    if (sub == "get")
    {
        return runSysconfigGet(_connection, _args, _out);
    }
    if (sub == "set")
    {
        if (_args.positionals().size() < 3)
        {
            throw OpsError(c_exitUsage, "usage: sysconfig set <key> <value>");
        }
        return runWritePreset(_connection, _args, _out, *findPreset("sysconfig", "set"),
            {_args.positionals()[1], _args.positionals()[2]});
    }
    if (sub == "features")
    {
        return runFeatures(_connection, _args, _out);
    }
    if (sub == "enable-features")
    {
        return runEnableFeaturesOn(_connection, _args, _out,
            std::vector<std::string>(_args.positionals().begin() + 1, _args.positionals().end()),
            "");
    }
    throw OpsError(c_exitUsage, "unknown sysconfig subcommand: " + sub);
}

void registerSysconfigCommand()
{
    registerCommand("sysconfig",
        Command{"get <key> (console getSystemConfigByKey) | set <key> <value> "
                "(setSystemConfigByKey; committee proposal on an auth chain) | features (every "
                "feature_/bugfix_ flag with its state) | enable-features <flag...> (turns them on "
                "in dependency order, already-on skipped)",
            "get <key> | set <key> <value> | features | enable-features <flag...>   [--account "
            "<key-file>] [--yes] [--direct] [--interval <blocks>] [--node-dir <dir> | --rpc "
            "<host:port>] [--json]",
            {"yes", "direct"}, runSysconfig, {"account", "interval"}, true});
}
}  // namespace bcos::ops
