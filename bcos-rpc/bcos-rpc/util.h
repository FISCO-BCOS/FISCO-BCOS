/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file util.h
 * @author: jdkuang
 * @date 2024/4/24
 */

#pragma once
#include "bcos-framework/protocol/ProtocolTypeDef.h"

namespace bcos::rpc
{
/// Resolve a default-block tag to (block number, isLatest). "safe"/"finalized" resolve to
/// latest - safeDepth / latest - finalizedDepth (clamped at 0). With the default depth 0 they
/// equal "latest" (PBFT commits are final); a positive depth makes them historical blocks,
/// matching Ethereum semantics with configurable depths ([web3_rpc] safe_block_depth /
/// finalized_block_depth).
std::tuple<protocol::BlockNumber, bool> getBlockNumberByTag(protocol::BlockNumber latest,
    std::string_view blockTag, protocol::BlockNumber safeDepth = 0,
    protocol::BlockNumber finalizedDepth = 0);
}  // namespace bcos::rpc