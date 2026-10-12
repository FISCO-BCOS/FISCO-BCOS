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
 * @brief what every transaction-sending command needs from a Connection: the chain id and
 *        crypto type, a TxSender with the --account key, and the confirmation prompt that
 *        write commands show unless --yes or a non-interactive stdin skips it.
 * @file TxContext.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/tx/PresetCall.h"
#include "bcos-ops/tx/TxSender.h"
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>

namespace bcos::ops
{
struct ChainFacts
{
    std::string chainId;
    bool sm = false;
};

/// chain id / crypto type from getGroupInfo, the node dir when the RPC is unavailable
ChainFacts chainFacts(Connection const& _connection);
/// a sender signing with --account (PEM or hex) or a random key when absent
std::unique_ptr<TxSender> makeSender(
    Connection const& _connection, ChainFacts const& _facts, Args const& _args);
/// prints `mode / to / method / params` (and the proposal method standing in for the direct
/// call) and asks `proceed? [y/N]` on an interactive stdin without --yes; returns false when the
/// operator declined. A non-interactive stdin without --yes is a usage error.
bool confirmWrite(Args const& _args, std::ostream& _out, Preset const& _preset,
    std::string const& _to, std::vector<std::string> const& _params,
    std::string const& _mode = "direct", std::string const& _proposalMethod = "");
/// --direct / --interval and the chain's auth mode (one getSystemConfigByKey, only asked when
/// _preset has a proposal form)
Routing routingFor(Connection const& _connection, Args const& _args, Preset const& _preset);
/// shared rendering of a PresetResult: JSON (toJson) or the table rows
void printPresetResult(std::ostream& _out, PresetResult const& _result, bool _json);
/// run a write preset end to end: confirm, send, print, exit code. _hint, when set, turns the
/// result into an operator hint printed after it (JSON: a `hint` member); "" prints nothing
using ResultHint = std::function<std::string(PresetResult const&)>;
/// runs a read preset (a `call`) through a random-key sender; _to overrides the preset's address
/// for the committee contracts resolved at runtime
PresetResult readPreset(Connection const& _connection, Preset const& _preset,
    std::vector<std::string> const& _params, std::string const& _to = "");
int runWritePreset(Connection const& _connection, Args const& _args, std::ostream& _out,
    Preset const& _preset, std::vector<std::string> const& _params, ResultHint const& _hint = {});
}  // namespace bcos::ops
