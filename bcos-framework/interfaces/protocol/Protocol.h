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
    CONSENSUS_NODE = 0x2,
    OBSERVER_NODE = 0x4,
    NODE_OUTSIDE_GROUP = 0x8,
};

enum ModuleID
{
    PBFT = 1000,
    Raft = 1001,
    BlockSync = 2000,
    TxsSync = 2001,
    AMOP = 3000,
};
enum NodeArchitectureType
{
    AIR = 0,
    PRO = 1,
    MAX = 2,
};
}  // namespace protocol
}  // namespace bcos
