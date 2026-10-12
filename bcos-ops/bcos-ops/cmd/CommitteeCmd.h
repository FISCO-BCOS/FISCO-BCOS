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
 * @brief `committee info|proposals|proposal|propose-*|vote|revoke` over an already open
 *        Connection (the entry the unit tests drive with a FakeRpc)
 * @file CommitteeCmd.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/RpcCall.h"
#include "bcos-ops/cmd/TxContext.h"
#include "bcos-ops/tx/PresetCall.h"
#include "bcos-ops/tx/TxSender.h"
#include <json/json.h>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
/// a PresetRunner over a read-only (random key) sender for the committee view calls; the CLI
/// and the TUI's gov page read the committee through it (one implementation of the lookups)
struct CommitteeReader
{
    ChainFacts facts;
    std::unique_ptr<TxSender> sender;
    PresetRunner runner;
    explicit CommitteeReader(Connection const& _connection);
    /// first decoded output of a committee read preset, or throws OpsError{2} naming the preset
    Json::Value read(
        std::string_view _name, std::vector<std::string> const& _args, std::string const& _to = "");
};

/// _args.positionals()[0] is the sub-subcommand; returns the process exit code
int runCommitteeOn(Connection const& _connection, Args const& _args, std::ostream& _out);
}  // namespace bcos::ops
