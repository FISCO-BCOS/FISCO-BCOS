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
 * @brief `balance` group: BalancePrecompiled 0x…1011 through the preset table. The precompiled
 *        reports failures as a reverted receipt (status 15 PrecompiledError + message, see
 *        extension/BalancePrecompiled.cpp), so `ok` follows the receipt; register/unregister
 *        return an int32 code. The address only exists once feature_balance_precompiled is on
 *        (TransactionExecutor.cpp registers it behind the flag): `enable` turns the two flags on.
 * @file BalanceCmd.cpp
 */
#include "BalanceCmd.h"
#include "SysconfigCmd.h"
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
constexpr std::string_view c_balanceEnableHint =
    "hint: feature_balance_precompiled registers the governors as callers; on a chain without a "
    "committee run `balance register-caller <account>` (governor only) before add/sub/transfer";
constexpr std::string_view c_balanceDisabledHint =
    "hint: the balance precompiled is not registered on this chain, run `balance enable` "
    "(feature_balance + feature_balance_precompiled)";

struct BalanceSub
{
    std::string_view sub;
    size_t positionals;
    std::string_view usage;
};

constexpr std::array c_balanceSubs{
    BalanceSub{"get", 1, "balance get <account>"},
    BalanceSub{"add", 2, "balance add <account> <amount>"},
    BalanceSub{"sub", 2, "balance sub <account> <amount>"},
    BalanceSub{"transfer", 3, "balance transfer <from> <to> <amount>"},
    BalanceSub{"register-caller", 1, "balance register-caller <account>"},
    BalanceSub{"unregister-caller", 1, "balance unregister-caller <account>"},
    BalanceSub{"list-caller", 0, "balance list-caller"},
};

// bcos::protocol::TransactionStatus::CallAddressError (bcos-protocol/TransactionStatus.h:45);
// bcos-ops does not link bcos-protocol, same as Smoke.cpp. A 3.18 node reports the missing
// precompiled as status 16 RevertInstruction with message "Call address error." instead
// (TransactionExecutive.cpp callPrecompiled → CallAddressError exception), so both are checked.
constexpr int32_t c_statusCallAddressError = 19;
constexpr std::string_view c_callAddressErrorMessage = "Call address error";

/// a reverted call to an address no precompiled answers means the flag is off
bool balancePrecompiledMissing(Receipt const& _receipt)
{
    return _receipt.status == c_statusCallAddressError ||
           (!_receipt.ok() &&
               _receipt.message.find(c_callAddressErrorMessage) != std::string::npos);
}

/// read presets (get, list-caller) through a random-key sender; the single decoded value is
/// printed as `value`, the raw decoded array as `result`
int runBalanceRead(Connection const& _connection, Args const& _args, std::ostream& _out,
    Preset const& _preset, std::vector<std::string> const& _params)
{
    auto facts = chainFacts(_connection);
    TxSender sender(_connection.call, _connection.group, facts.chainId, facts.sm,
        makeAccount(facts.sm, std::nullopt));
    PresetRunner runner(sender, facts.sm, _connection.call, _connection.group);
    auto result = runner.run(_preset, _params);
    bool json = wantJson(_args.flag("json"));
    if (json)
    {
        auto value = result.toJson();
        if (result.decoded.isArray() && result.decoded.size() == 1)
        {
            value["value"] = result.decoded[0];
        }
        if (balancePrecompiledMissing(result.receipt))
        {
            value["hint"] = std::string(c_balanceDisabledHint);
        }
        printJson(_out, value);
    }
    else
    {
        printPresetResult(_out, result, false);
        if (balancePrecompiledMissing(result.receipt))
        {
            _out << c_balanceDisabledHint << '\n';
        }
    }
    return result.exitCode();
}

int runBalance(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage,
            "usage: balance get|add|sub|transfer|register-caller|unregister-caller|list-caller|"
            "enable ...");
    }
    return runBalanceOn(connect(connectOptionsFrom(_args)), _args, _out);
}
}  // namespace

int runBalanceOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    auto const& sub = positionals.empty() ? "" : positionals[0];
    if (sub == "enable")
    {
        return runEnableFeaturesOn(_connection, _args, _out,
            {"feature_balance", "feature_balance_precompiled"}, std::string(c_balanceEnableHint));
    }
    for (auto const& spec : c_balanceSubs)
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
        auto const* preset = findPreset("balance", spec.sub);
        if (!preset->write)
        {
            return runBalanceRead(_connection, _args, _out, *preset, params);
        }
        return runWritePreset(
            _connection, _args, _out, *preset, params, [](PresetResult const& _result) {
                return balancePrecompiledMissing(_result.receipt) ?
                           std::string(c_balanceDisabledHint) :
                           std::string();
            });
    }
    throw OpsError(c_exitUsage, "unknown balance subcommand: " + sub);
}

void registerBalanceCommand()
{
    registerCommand("balance",
        Command{"get <acct> (console getBalance) | add <acct> <amount> (addBalance) | sub "
                "<acct> <amount> (subBalance) | transfer <from> <to> <amount> (transfer) | "
                "register-caller <acct> (registerBalanceGovernor) | unregister-caller <acct> "
                "(unregisterBalanceGovernor) | list-caller (listBalanceGovernor) | enable "
                "(feature_balance + feature_balance_precompiled; two proposals on an auth "
                "chain). add/sub/transfer need a registered caller as --account",
            "get <acct> | add <acct> <amount> | sub <acct> <amount> | transfer <from> <to> "
            "<amount> | register-caller <acct> | unregister-caller <acct> | list-caller | enable   "
            "[--account <key-file>] [--yes] [--direct] [--interval <blocks>] [--node-dir <dir> | "
            "--rpc <host:port>] [--json]",
            {"yes", "direct"}, runBalance});
}
}  // namespace bcos::ops
