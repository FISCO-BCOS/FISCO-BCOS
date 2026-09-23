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
 * @file DebugEndpoint.h
 * @brief Geth debug namespace consumed by the fault-proof preimage server (kona-host):
 *        debug_dbGet answers a raw preimage (L2 state node RLP or contract code) for a
 *        content-address key, and debug_executePayload is the high-level execution-witness
 *        hint the host probes before falling back to the fine-grained dbGet path.
 */

#pragma once

#include <bcos-rpc/groupmgr/GroupManager.h>
#include <bcos-task/Task.h>
#include <json/json.h>

namespace bcos::rpc
{

/// Geth debug namespace consumed by the fault-proof preimage server (kona-host).
class DebugEndpoint
{
public:
    explicit DebugEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~DebugEndpoint() = default;

    /// debug_dbGet: return the raw preimage stored under a content-address key. The key is
    /// either a 32-byte state trie node hash, or the 33-byte "c" + codeHash form of the geth
    /// hashdb scheme that kona-host's L2Code hint sends.
    task::Task<void> dbGet(const Json::Value&, Json::Value&);

    /// debug_getRawHeader: return the RLP encoding of a block header, keyed by block hash.
    /// kona-host's L2BlockHeader / StartingL2Output hints call this with a 32-byte hash and
    /// expect the exact bytes whose keccak256 equals the block hash (JSON forms cannot
    /// satisfy the preimage-oracle key). Mirrors geth DebugAPI.GetRawHeader.
    task::Task<void> getRawHeader(const Json::Value&, Json::Value&);

    /// debug_executePayload: kona-host's high-level "execute a payload and return the whole
    /// stateless execution witness" hint. See the .cpp for why it answers MethodNotFound.
    task::Task<void> executePayload(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc
