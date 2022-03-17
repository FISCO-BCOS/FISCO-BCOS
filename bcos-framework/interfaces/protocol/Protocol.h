/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Protocol for all the modules
 * @file Protocol.h
 * @author: yujiechen
 * @date 2021-04-21
 */
#pragma once
#include <memory>
namespace bcos
{
namespace protocol
{
// Note: both MessageExtFieldFlag and NodeType occupy the ext fields
enum MessageExtFieldFlag : uint32_t
{
    Response = 0x0001,
};
enum NodeType : uint32_t
{
    None = 0x0,
    CONSENSUS_NODE = 0x0010,
    OBSERVER_NODE = 0x0100,
    NODE_OUTSIDE_GROUP = 0x1000,
};
enum ModuleID
{
    PBFT = 1000,
    Raft = 1001,
    BlockSync = 2000,
    TxsSync = 2001,
    AMOP = 3000,
};
enum ProtocolModuleID : uint32_t
{
    NodeService = 0x0,
    GatewayService = 0x1,
    RpcService = 0x2,
    ExecutorService = 0x3,
};
enum ProtocolVersion : uint32_t
{
    INVALID = 0,  // Negotiation failed
    V1 = 1,
};
enum Version
{
    RC4_VERSION = 4,
};
}  // namespace protocol
}  // namespace bcos
