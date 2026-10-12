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
 * @brief `account new [--sm] [--out <dir>]`: a key pair in console's accounts/ layout
 *        (<0xaddr>.pem + <0xaddr>.public.pem). Replaces console's get_account.sh /
 *        get_gm_account.sh, so build_chain.sh no longer downloads them.
 * @file AccountCmd.cpp
 */
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/Account.h"
#include <ostream>

namespace bcos::ops
{
namespace
{
int runAccountNew(Args const& _args, std::ostream& _out)
{
    bool sm = _args.flag("sm");
    auto dir = _args.optionOr("out", ".");
    auto keyPair = makeAccount(sm, std::nullopt);
    auto files = writePemKeyPair(dir, sm, *keyPair);
    auto address = accountAddress(sm, *keyPair);
    if (wantJson(_args.flag("json")))
    {
        Json::Value value;
        value["address"] = address;
        value["privateKeyFile"] = files.privateKeyFile;
        value["publicKeyFile"] = files.publicKeyFile;
        value["sm"] = sm;
        printJson(_out, value);
        return c_exitOk;
    }
    printRows(_out, {{"address", address}, {"privateKey", files.privateKeyFile},
                        {"publicKey", files.publicKeyFile}, {"curve", sm ? "sm2" : "secp256k1"}});
    return c_exitOk;
}

int runAccount(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty() || _args.positionals()[0] != "new")
    {
        throw OpsError(c_exitUsage, "usage: account new [--sm] [--out <dir>] [--json]");
    }
    return runAccountNew(_args, _out);
}
}  // namespace

void registerAccountCommand()
{
    registerCommand("account",
        Command{"new: generate an account key pair in console's accounts/ layout (replaces "
                "console's get_account.sh / get_gm_account.sh)",
            "new [--sm] [--out <dir>] [--json]", {"sm"}, runAccount});
}
}  // namespace bcos::ops
