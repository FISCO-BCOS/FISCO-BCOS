/*
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
 * @file Common.h
 * @author: octopus
 * @date 2021-05-04
 */
#pragma once
#include "libnetwork/Common.h"

#define GATEWAY_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Gateway]"
#define GATEWAY_CONFIG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Config]"
#define GATEWAY_FACTORY_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Factory]"
#define NODE_MANAGER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][GatewayNodeManager]"
#define ROUTER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][Router]"
#define RATELIMIT_MGR_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][RateLimiterManager]"

namespace bcos
{
namespace gateway
{
/// The maximum length of a p2p message is 32 MB. When changing this value, keep
/// front::MAX_PAYLOAD_LENGTH (bcos-front/bcos-front/FrontMessage.h) equal to it, otherwise
/// messages in between the two caps pass the gateway but are dropped by the front service.
/// Note: configuring p2p.allow_max_msg_size above this constant re-opens that gap for
/// compressible messages (the sender checks the configured bound on the uncompressed size,
/// while the wire-level decode bound only sees the compressed bytes).
constexpr uint64_t MAX_MESSAGE_LENGTH = 32 * 1024 * 1024;
enum GroupType : uint16_t
{
    // group with at-least one consensus node
    GROUP_WITH_CONSENSUS_NODE = 0x0,
    // group without consensus node
    GROUP_WITHOUT_CONSENSUS_NODE = 0x1,
    // group without consensus node and observer node
    OUTSIDE_GROUP = 0x2,
};

}  // namespace gateway
}  // namespace bcos
