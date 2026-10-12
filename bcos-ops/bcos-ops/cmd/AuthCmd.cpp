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
 * @brief `auth` group: AuthManagerPrecompiled 0x…1005 through the preset table. Six reads;
 *        four committee-only writes that become CommitteeManager proposals on an auth chain
 *        (--direct forces the precompiled call, which answers -50000 to an EOA); six
 *        contract-admin writes that are always direct. `set-contract-status` picks the
 *        (address,bool isFreeze) overload for true/false (true freezes) and (address,uint8)
 *        for 0 normal / 1 frozen / 2 abolished (AuthManagerPrecompiled.cpp:41-42, :354-366;
 *        the uint8 form needs chain version >= 3.2).
 * @file AuthCmd.cpp
 */
#include "BalanceCmd.h"
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
struct AuthSub
{
    std::string_view sub;
    std::string_view preset;  // "" = resolved from the arguments (set-contract-status)
    size_t positionals;
    std::string_view usage;
};

constexpr std::array c_authSubs{
    AuthSub{"get-admin", "get-admin", 1, "auth get-admin <contract>"},
    AuthSub{"deploy-type", "deploy-type", 0, "auth deploy-type"},
    AuthSub{"has-deploy-auth", "has-deploy-auth", 1, "auth has-deploy-auth <account>"},
    AuthSub{"check-method-auth", "check-method-auth", 3,
        "auth check-method-auth <contract> <selector> <account>"},
    AuthSub{"get-method-auth", "get-method-auth", 2, "auth get-method-auth <contract> <selector>"},
    AuthSub{"contract-available", "contract-available", 1, "auth contract-available <contract>"},
    AuthSub{"reset-admin", "reset-admin", 2, "auth reset-admin <contract> <admin>"},
    AuthSub{"set-deploy-auth-type", "set-deploy-auth-type", 1, "auth set-deploy-auth-type <0|1|2>"},
    AuthSub{"open-deploy-auth", "open-deploy-auth", 1, "auth open-deploy-auth <account>"},
    AuthSub{"close-deploy-auth", "close-deploy-auth", 1, "auth close-deploy-auth <account>"},
    AuthSub{"set-method-auth-type", "set-method-auth-type", 3,
        "auth set-method-auth-type <contract> <selector> <0|1|2>"},
    AuthSub{"open-method-auth", "open-method-auth", 3,
        "auth open-method-auth <contract> <selector> <account>"},
    AuthSub{"close-method-auth", "close-method-auth", 3,
        "auth close-method-auth <contract> <selector> <account>"},
    AuthSub{"set-contract-status", "", 2,
        "auth set-contract-status <contract> <true (freeze)|false (unfreeze)|0 normal|1 frozen|2 "
        "abolished>"},
};

/// get-method-auth answers (type, whitelist, blacklist); every other read a single value
Json::Value authReadValue(Preset const& _preset, Json::Value const& _decoded)
{
    if (!_decoded.isArray())
    {
        return Json::Value(Json::nullValue);
    }
    if (_preset.name == "get-method-auth" && _decoded.size() == 3)
    {
        Json::Value value;
        value["type"] = _decoded[0];
        value["whitelist"] = _decoded[1];
        value["blacklist"] = _decoded[2];
        return value;
    }
    return _decoded.size() == 1 ? _decoded[0] : _decoded;
}

int runAuthRead(Connection const& _connection, Args const& _args, std::ostream& _out,
    Preset const& _preset, std::vector<std::string> const& _params)
{
    auto facts = chainFacts(_connection);
    TxSender sender(_connection.call, _connection.group, facts.chainId, facts.sm,
        makeAccount(facts.sm, std::nullopt));
    PresetRunner runner(sender, facts.sm, _connection.call, _connection.group);
    auto result = runner.run(_preset, _params);
    auto value = authReadValue(_preset, result.decoded);
    if (wantJson(_args.flag("json")))
    {
        auto json = result.toJson();
        json["value"] = value;
        printJson(_out, json);
        return result.exitCode();
    }
    if (!result.ok)
    {
        printPresetResult(_out, result, false);
        return result.exitCode();
    }
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    _out << (value.isString() ? value.asString() : Json::writeString(writer, value)) << '\n';
    return result.exitCode();
}

/// true/false → setContractStatus(address,bool); 0/1/2 → setContractStatus(address,uint8)
Preset const* contractStatusPreset(std::string const& _status)
{
    if (_status == "true" || _status == "false")
    {
        return findPreset("auth", "set-contract-available");
    }
    if (_status == "0" || _status == "1" || _status == "2")
    {
        return findPreset("auth", "set-contract-status");
    }
    throw OpsError(c_exitUsage,
        "auth set-contract-status: status must be true (freeze) | false (unfreeze) or 0|1|2 "
        "(normal, frozen, abolished), got " +
            _status);
}

int runAuth(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: auth <subcommand> ... (see --help)");
    }
    return runAuthOn(connect(connectOptionsFrom(_args)), _args, _out);
}
}  // namespace

int runAuthOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    auto const& sub = positionals.empty() ? "" : positionals[0];
    for (auto const& spec : c_authSubs)
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
        auto const* preset =
            spec.preset.empty() ? contractStatusPreset(params[1]) : findPreset("auth", spec.preset);
        if (!preset->write)
        {
            return runAuthRead(_connection, _args, _out, *preset, params);
        }
        return runWritePreset(_connection, _args, _out, *preset, params);
    }
    throw OpsError(c_exitUsage, "unknown auth subcommand: " + sub);
}

void registerAuthCommand()
{
    registerCommand("auth",
        Command{"get-admin <contract> (console getContractAdmin) | deploy-type (getDeployAuth) | "
                "has-deploy-auth <acct> (checkDeployAuth) | check-method-auth <contract> <sel> "
                "<acct> (checkMethodAuth) | get-method-auth <contract> <sel> (getMethodAuth) | "
                "contract-available <contract> (contractAvailable) | reset-admin <contract> "
                "<admin> (resetAdmin) | set-deploy-auth-type <0|1|2> (setDeployAuthType) | "
                "open-deploy-auth <acct> (openDeployAuth) | close-deploy-auth <acct> "
                "(closeDeployAuth) | set-method-auth-type <contract> <sel> <type> (setMethodAuth) "
                "| open-method-auth <contract> <sel> <acct> (openMethodAuth) | close-method-auth "
                "<contract> <sel> <acct> (closeMethodAuth) | set-contract-status <contract> "
                "<true|false|0|1|2> (true = freezeContract, false = unfreezeContract, 2 = "
                "abolishContract). The "
                "four committee writes become proposals on an auth chain; the method/status "
                "writes need the contract admin as --account",
            "get-admin <contract> | deploy-type | has-deploy-auth <acct> | check-method-auth "
            "<contract> <sel> <acct> | get-method-auth <contract> <sel> | contract-available "
            "<contract> | reset-admin <contract> <admin> | set-deploy-auth-type <0|1|2> | "
            "open-deploy-auth <acct> | close-deploy-auth <acct> | set-method-auth-type <contract> "
            "<sel> <0|1|2> | open-method-auth <contract> <sel> <acct> | close-method-auth "
            "<contract> <sel> <acct> | set-contract-status <contract> <true|false|0|1|2>   "
            "[--account <key-file>] [--yes] [--direct] [--interval <blocks>] [--node-dir <dir> | "
            "--rpc <host:port>] [--json]",
            {"yes", "direct"}, runAuth});
}
}  // namespace bcos::ops
