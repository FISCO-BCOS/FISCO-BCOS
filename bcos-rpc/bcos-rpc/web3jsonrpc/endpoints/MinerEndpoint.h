/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-rpc/groupmgr/GroupManager.h>
#include <json/json.h>

namespace bcos::rpc
{

/// OP Stack miner namespace (DA throttling handshake from the batcher).
class MinerEndpoint
{
public:
    explicit MinerEndpoint(NodeService::Ptr nodeService) : m_nodeService(std::move(nodeService)) {}
    virtual ~MinerEndpoint() = default;

    task::Task<void> setMaxDASize(const Json::Value&, Json::Value&);

private:
    NodeService::Ptr m_nodeService;
};

}  // namespace bcos::rpc
