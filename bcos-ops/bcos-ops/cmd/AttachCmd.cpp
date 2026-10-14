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
 * @brief `attach <method> [params-json]` (any method, result printed as is) and
 *        `log-level get|set [--module M] <level>` (socket only; admin_* live there)
 * @file AttachCmd.cpp
 */
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include <ostream>

namespace bcos::ops
{
namespace
{
/// params: a JSON array text, or positional values (JSON-typed when they parse) → array
Json::Value paramsFrom(std::vector<std::string> const& _positionals, size_t _from)
{
    Json::Value params(Json::arrayValue);
    if (_positionals.size() <= _from)
    {
        return params;
    }
    if (_positionals.size() == _from + 1 && !_positionals[_from].empty() &&
        _positionals[_from][0] == '[')
    {
        return parseJson(_positionals[_from], "params");
    }
    Json::CharReaderBuilder builder;
    builder["failIfExtra"] = true;
    for (size_t i = _from; i < _positionals.size(); ++i)
    {
        auto const& arg = _positionals[i];
        Json::Value parsed;
        std::string errors;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!arg.empty() && arg != "true" && arg != "false" && arg[0] != '[' && arg[0] != '{' &&
            !std::isdigit(static_cast<unsigned char>(arg[0])) && arg[0] != '-')
        {
            params.append(arg);
        }
        else if (reader->parse(arg.data(), arg.data() + arg.size(), &parsed, &errors))
        {
            params.append(parsed);
        }
        else
        {
            params.append(arg);
        }
    }
    return params;
}

int runAttach(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: attach <method> [params-json | args...]");
    }
    auto connection = connect(connectOptionsFrom(_args));
    auto const& method = _args.positionals()[0];
    auto params = paramsFrom(_args.positionals(), 1);
    // the FISCO methods take [groupID, nodeName, ...]; fill the group in when the caller gave
    // none and the method is not one of the group-less ones
    static std::set<std::string> const c_groupless = {"getGroupList", "getGroupInfoList",
        "getGroupPeers", "getGroupInfo", "getGroupNodeInfo", "getPeers", "getNodeInfo",
        "admin_getLogLevel", "admin_setLogLevel"};
    if (params.empty() && !c_groupless.contains(method))
    {
        params.append(connection.group);
        params.append("");
    }
    auto result = connection.call(method, params);
    if (result.isString() && !wantJson(_args.flag("json")))
    {
        _out << result.asString() << '\n';
    }
    else if (result.isIntegral() && !wantJson(_args.flag("json")))
    {
        _out << result.asInt64() << '\n';
    }
    else
    {
        printJson(_out, result);
    }
    return c_exitOk;
}

int runLogLevel(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    auto const& positionals = _args.positionals();
    if (positionals.empty() || (positionals[0] != "get" && positionals[0] != "set"))
    {
        throw OpsError(c_exitUsage, "usage: log-level get | log-level set [--module M] <level>");
    }
    auto options = connectOptionsFrom(_args);
    options.allowRpc = false;  // admin_* only exist on the socket
    if (options.rpc)
    {
        throw OpsError(c_exitUsage, "log-level works on the local socket only; drop --rpc");
    }
    auto connection = connect(options);
    Json::Value params(Json::arrayValue);
    std::string method = "admin_getLogLevel";
    if (positionals[0] == "set")
    {
        if (positionals.size() < 2)
        {
            throw OpsError(c_exitUsage, "usage: log-level set [--module M] <level|inherit>");
        }
        method = "admin_setLogLevel";
        params.append(positionals[1]);
        if (auto module = _args.option("module"))
        {
            params.append(*module);
        }
    }
    auto result = connection.call(method, params);
    printJson(_out, result);
    return c_exitOk;
}

}  // namespace

void registerAttachCommands()
{
    registerCommand("attach",
        Command{"call any JSON-RPC method on the node (local socket first) and print the result",
            "<method> [params-json | args...] [--node-dir <dir> | --rpc <host:port>] [--json]", {},
            runAttach});
    registerCommand("log-level",
        Command{"get or set the node's log level in memory, per module (local socket only)",
            "get | set [--module PBFT|TXPOOL|SYNC|SCHEDULER|EXECUTOR|LEDGER|RPC|GATEWAY|FRONT] "
            "<trace|debug|info|warning|error|inherit> [--node-dir <dir>]",
            {}, runLogLevel});
}
}  // namespace bcos::ops
