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
 * @brief the chain facts every command needs from getGroupInfo, parsed in one place
 * @file GroupFacts.h
 */
#pragma once

#include "bcos-ops/RpcCall.h"
#include <optional>
#include <string>

namespace bcos::ops
{
struct GroupFacts
{
    std::string chainId;
    std::optional<std::string> version;
    std::optional<bool> smCrypto;
    std::optional<bool> authCheck;
};

/// getGroupInfo(group): chainID from the top level; version/smCryptoType/isAuthCheck from the
/// iniConfig of the entry whose nodeID equals _selfNodeId (first entry when unknown). Throws
/// OpsError{1} when the RPC fails or the node list is empty.
GroupFacts groupFacts(
    RpcCall const& _call, std::string const& _group, std::optional<std::string> const& _selfNodeId);
}  // namespace bcos::ops
