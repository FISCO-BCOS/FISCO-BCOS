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
 * @file LocalRouterTable.h
 * @author: octopus
 * @date 2021-12-29
 */
#pragma once
#include "FrontServiceInfo.h"
#include "bcos-gateway/libp2p/P2PSession.h"
#include <bcos-framework/interfaces/crypto/KeyFactory.h>
#include <bcos-framework/interfaces/crypto/KeyInterface.h>
#include <bcos-framework/interfaces/gateway/GatewayInterface.h>
#include <bcos-framework/interfaces/multigroup/GroupInfo.h>
#include <memory>
namespace bcos
{
namespace gateway
{
class LocalRouterTable
{
public:
    using Ptr = std::shared_ptr<LocalRouterTable>;
    using GroupNodeListType = std::map<std::string, std::map<std::string, FrontServiceInfo::Ptr>>;
    LocalRouterTable(bcos::crypto::KeyFactory::Ptr _keyFactory) : m_keyFactory(_keyFactory) {}
    virtual ~LocalRouterTable() {}

    FrontServiceInfo::Ptr getFrontService(
        const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID) const;

    std::vector<FrontServiceInfo::Ptr> getGroupFrontServiceList(const std::string& _groupID) const;
    bcos::crypto::NodeIDs getGroupNodeIDList(const std::string& _groupID) const;
    bool insertNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID,
        bcos::protocol::NodeType _type, bcos::front::FrontServiceInterface::Ptr _frontService);
    bool removeNode(const std::string& _groupID, bcos::crypto::NodeIDPtr _nodeID);

    std::map<std::string, std::set<std::string>> nodeListInfo() const;

    bool updateGroupNodeInfos(bcos::group::GroupInfo::Ptr _groupInfo);
    bool eraseUnreachableNodes();

    // Note: copy to ensure thread-safe
    // groupID => nodeID => FrontServiceInfo
    GroupNodeListType nodeList() const
    {
        ReadGuard l(x_nodeList);
        return m_nodeList;
    }

    bool asyncBroadcastMsg(int16_t _nodeType, const std::string& _groupID,
        bcos::crypto::NodeIDPtr _srcNodeID, bytesConstRef _payload);

    bool sendMessage(const std::string& _groupID, bcos::crypto::NodeIDPtr _srcNodeID,
        bcos::crypto::NodeIDPtr _dstNodeID, bytesConstRef _payload, ErrorRespFunc _errorRespFunc);

private:
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
    // groupID => nodeID => FrontServiceInfo
    GroupNodeListType m_nodeList;
    mutable SharedMutex x_nodeList;
};
}  // namespace gateway
}  // namespace bcos