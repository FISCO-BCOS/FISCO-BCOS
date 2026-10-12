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
 * @brief does this chain route committee-only writes through CommitteeManager proposals?
 *        System config auth_check_status (settable from 3.3 on) wins; when the node has not
 *        stored it the genesis executor.is_auth_check of the node dir decides; a bare --rpc
 *        connection without a node dir defaults to direct calls.
 * @file AuthMode.h
 */
#pragma once

#include "bcos-ops/NodeDir.h"
#include "bcos-ops/RpcCall.h"
#include <optional>
#include <string>

namespace bcos::ops
{
/// one getSystemConfigByKey; only error 3008 (no row: genesis before 3.3) falls back to _nodeDir,
/// other RPC errors propagate
bool chainUsesProposals(
    RpcCall const& _call, std::string const& _group, std::optional<NodeDir> const& _nodeDir);
/// per-command cache of chainUsesProposals for the Connection
bool chainUsesProposals(Connection const& _connection);
}  // namespace bcos::ops
