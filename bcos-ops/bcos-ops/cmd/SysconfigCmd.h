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
 * @brief `sysconfig get|set|features|enable-features` and `consensus list|add-sealer|
 *        add-observer|remove|set-weight|set-term-weight` over an already open Connection
 *        (the entry the unit tests drive with a FakeRpc)
 * @file SysconfigCmd.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/RpcCall.h"
#include <iosfwd>
#include <string>
#include <vector>

namespace bcos::ops
{
/// _args.positionals()[0] is the sub-subcommand; returns the process exit code
int runSysconfigOn(Connection const& _connection, Args const& _args, std::ostream& _out);
int runConsensusOn(Connection const& _connection, Args const& _args, std::ostream& _out);
/// `sysconfig enable-features` over an open Connection: _requested in dependency order,
/// proposals on an auth chain; _hint is printed last when non-empty (balance enable)
int runEnableFeaturesOn(Connection const& _connection, Args const& _args, std::ostream& _out,
    std::vector<std::string> const& _requested, std::string const& _hint);
}  // namespace bcos::ops
