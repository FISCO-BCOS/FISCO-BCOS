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
 * @brief fills NodeStatus from the public JSON-RPC methods; the rpc and attach sources share it
 *        and only differ in the RpcCall they pass.
 * @file RpcCollector.h
 */
#pragma once

#include "bcos-ops/NodeStatus.h"
#include "bcos-ops/RpcCall.h"
#include <string>

namespace bcos::ops
{
/// every RPC failure is caught and recorded in reasons[field]; nothing throws
struct LocalFallbacks
{
    std::optional<int64_t> txpoolLimit;         // [txpool] limit, config.ini
    std::optional<int64_t> consensusTimeoutMs;  // [consensus] consensus_timeout, config.genesis
};

NodeStatus collectFromRpc(RpcCall const& _call, std::string const& _group,
    std::string const& _source, LocalFallbacks const& _fallbacks = {});

/// `getGroupList` → first group id; throws OpsError{1} when there is none
std::string discoverGroup(RpcCall const& _call);
}  // namespace bcos::ops
