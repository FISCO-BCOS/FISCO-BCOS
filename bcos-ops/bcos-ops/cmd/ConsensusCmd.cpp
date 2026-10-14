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
 * @file ConsensusCmd.cpp
 */
#include "SysconfigCmd.h"
#include "TxContext.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/Presets.h"
#include <iomanip>
#include <ostream>

namespace bcos::ops
{
namespace
{
Json::Value groupParams(std::string const& _group)
{
    Json::Value params(Json::arrayValue);
    params.append(_group);
    params.append("");
    return params;
}

int runList(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto sealers = _connection.call("getSealerList", groupParams(_connection.group));
    auto observers = _connection.call("getObserverList", groupParams(_connection.group));
    Json::Value rows(Json::arrayValue);
    for (auto const& sealer : sealers)
    {
        Json::Value row;
        row["nodeId"] = sealer.get("nodeID", "");
        row["role"] = "sealer";
        row["weight"] = sealer.get("weight", 0);
        row["termWeight"] = sealer.get("termWeight", 0);
        rows.append(row);
    }
    for (auto const& observer : observers)
    {
        Json::Value row;
        row["nodeId"] = observer.isString() ? observer : observer.get("nodeID", "");
        row["role"] = "observer";
        row["weight"] = 0;
        row["termWeight"] = 0;
        rows.append(row);
    }
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, rows);
        return c_exitOk;
    }
    _out << std::left << std::setw(10) << "role" << std::setw(8) << "weight" << std::setw(12)
         << "termWeight" << "nodeId" << '\n';
    for (auto const& row : rows)
    {
        _out << std::left << std::setw(10) << row["role"].asString() << std::setw(8)
             << row["weight"].asInt64() << std::setw(12) << row["termWeight"].asInt64()
             << sanitizeForTerminal(row["nodeId"].asString()) << '\n';
    }
    return c_exitOk;
}

/// the preset of a write subcommand plus the positionals it takes
struct WriteSpec
{
    std::string_view preset;
    size_t positionals;
    std::string_view usage;
};
constexpr std::array c_writes{
    WriteSpec{"add-sealer", 2, "consensus add-sealer <nodeId> <weight> [--term-weight N]"},
    WriteSpec{"add-observer", 1, "consensus add-observer <nodeId>"},
    WriteSpec{"remove", 1, "consensus remove <nodeId>"},
    WriteSpec{"set-weight", 2, "consensus set-weight <nodeId> <weight>"},
    WriteSpec{"set-term-weight", 2, "consensus set-term-weight <nodeId> <weight>"},
};

int runConsensus(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage,
            "usage: consensus list|add-sealer|add-observer|remove|set-weight|set-term-weight ...");
    }
    return runConsensusOn(connect(connectOptionsFrom(_args)), _args, _out);
}
}  // namespace

int runConsensusOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    auto const& sub = positionals.empty() ? "" : positionals[0];
    if (sub == "list")
    {
        return runList(_connection, _args, _out);
    }
    for (auto const& spec : c_writes)
    {
        if (spec.preset != sub)
        {
            continue;
        }
        if (positionals.size() < spec.positionals + 1)
        {
            throw OpsError(c_exitUsage, "usage: " + std::string(spec.usage));
        }
        std::vector<std::string> params(
            positionals.begin() + 1, positionals.begin() + 1 + spec.positionals);
        auto const* preset = findPreset("consensus", spec.preset);
        if (sub == "add-sealer" && _args.option("term-weight"))
        {
            preset = findPreset("consensus", "add-sealer-term");
            params.push_back(*_args.option("term-weight"));
        }
        return runWritePreset(_connection, _args, _out, *preset, params);
    }
    throw OpsError(c_exitUsage, "unknown consensus subcommand: " + sub);
}

void registerConsensusCommand()
{
    registerCommand("consensus",
        Command{"list (console getSealerList + getObserverList) | add-sealer <nodeId> <weight> "
                "[--term-weight N] (addSealer) | add-observer <nodeId> (addObserver) | remove "
                "<nodeId> (removeNode) | set-weight <nodeId> <weight> (setConsensusWeight) | "
                "set-term-weight <nodeId> <weight> (governor only). Writes go through a "
                "committee proposal on an auth chain.",
            "list | add-sealer <nodeId> <weight> [--term-weight N] | add-observer <nodeId> | "
            "remove <nodeId> | set-weight <nodeId> <weight> | set-term-weight <nodeId> <weight>   "
            "[--account <key-file>] [--yes] [--direct] [--interval <blocks>] [--node-dir <dir> | "
            "--rpc <host:port>] [--json]",
            {"yes", "direct"}, runConsensus, {"account", "interval", "term-weight"}, true});
}
}  // namespace bcos::ops
