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
 * @brief SyncTreeTopology implementation
 * @file SyncTreeTopology.h
 * @author: yujiechen
 * @date 2023-03-22
 */
#pragma once
#include <bcos-tool/TreeTopology.h>

#define SYNCTREE_LOG(LEVEL)     \
    BCOS_LOG(LEVEL) << LOG_BADGE("SYNCTREE") << LOG_KV("nodeIndex", m_nodeIndex) \
    << LOG_KV("consIndex", m_consIndex) << LOG_KV("nodeId", m_nodeId->shortHex())

namespace bcos
{
namespace sync
{
class SyncTreeTopology : public bcos::tool::TreeTopology
{
public:
    using Ptr = std::shared_ptr<SyncTreeTopology>;

    SyncTreeTopology(bcos::crypto::NodeIDPtr _nodeId, std::uint32_t const _treeWidth = 3)
      : TreeTopology(_nodeId, _treeWidth)
    {
        m_nodeList = std::make_shared<bcos::crypto::NodeIDs>();
    }

    virtual ~SyncTreeTopology() {}
    // update corresponding info when the nodes changed
    virtual void updateNodeListInfo(bcos::crypto::NodeIDListPtr _nodeList);
    // consensus info must be updated with nodeList
    virtual void updateAllNodeInfo(bcos::crypto::NodeIDListPtr _consensusNodes, bcos::crypto::NodeIDListPtr _nodeList);
    // select the nodes by tree topology
    virtual bcos::crypto::NodeIDListPtr selectNodesForBlockSync(bcos::crypto::NodeIDSetPtr _peers);

protected:
    bool getNodeIDByIndex(bcos::crypto::NodeIDPtr& _nodeID, ssize_t const _nodeIndex) const override;
    // update the tree-topology range the nodes located in
    void updateStartAndEndIndex() override;

    // select the child nodes by tree
    void recursiveSelectChildNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
        ssize_t const _parentIndex, bcos::crypto::NodeIDSetPtr _peers,
        std::int64_t const _startIndex) override;
    // select the parent nodes by tree
    void selectParentNodes(bcos::crypto::NodeIDListPtr _selectedNodeList,
                           bcos::crypto::NodeIDSetPtr _peers, std::int64_t const _nodeIndex,
                           std::int64_t const _startIndex, bool const _selectAll = false) override;

private:
    bool locatedInGroup();

protected:
    mutable Mutex m_mutex;
    // the nodeList include both the consensus nodes and the observer nodes
    bcos::crypto::NodeIDListPtr m_nodeList;

    std::atomic_int64_t m_nodeIndex{0};
};
}  // namespace sync
}  // namespace bcos
