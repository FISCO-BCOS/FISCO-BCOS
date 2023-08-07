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
#include <utility>

#define SYNCTREE_LOG(LEVEL)                                                      \
    BCOS_LOG(LEVEL) << LOG_BADGE("SYNCTREE") << LOG_KV("nodeIndex", m_nodeIndex) \
                    << LOG_KV("consIndex", m_consIndex) << LOG_KV("nodeId", m_nodeId->shortHex())

namespace bcos::sync
{
class SyncTreeTopology : public bcos::tool::TreeTopology
{
public:
    using Ptr = std::shared_ptr<SyncTreeTopology>;

    explicit SyncTreeTopology(bcos::crypto::NodeIDPtr _nodeId, std::uint32_t const _treeWidth = 3)
      : TreeTopology(std::move(_nodeId), _treeWidth)
    {
        m_nodeList = std::make_shared<bcos::crypto::NodeIDs>();
    }

    ~SyncTreeTopology() override = default;
    SyncTreeTopology(SyncTreeTopology const&) = delete;
    SyncTreeTopology& operator=(SyncTreeTopology const&) = delete;
    SyncTreeTopology(SyncTreeTopology&&) = delete;
    SyncTreeTopology& operator=(SyncTreeTopology&&) = delete;

    // update corresponding info when the nodes changed
    virtual void updateNodeInfo(bcos::crypto::NodeIDs const& _nodeList);
    // consensus info must be updated with nodeList
    virtual void updateAllNodeInfo(
        bcos::crypto::NodeIDs const& _consensusNodes, bcos::crypto::NodeIDs const& _nodeList);
    // select the nodes by tree topology
    [[nodiscard]] virtual bcos::crypto::NodeIDSetPtr selectNodesForBlockSync(
        bcos::crypto::NodeIDSetPtr const& _peers);

protected:
    [[nodiscard]] bcos::crypto::NodeIDPtr getNodeIDByIndex(std::int32_t _nodeIndex) const override;
    // update the tree-topology range the nodes located in
    void updateStartAndEndIndex() override;

    // select the child nodes by tree
    [[nodiscard]] bcos::crypto::NodeIDSetPtr recursiveSelectChildNodes(std::int32_t _parentIndex,
        bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _startIndex) override;
    // select the parent nodes by tree
    [[nodiscard]] bcos::crypto::NodeIDSetPtr selectParentNodes(
        bcos::crypto::NodeIDSetPtr const& _peers, std::int32_t _nodeIndex, std::int32_t _startIndex,
        bool _selectAll) override;

private:
    inline bool locatedInGroup() { return (m_consIndex != -1) || (m_nodeIndex != -1); }

protected:
    mutable Mutex m_mutex;
    // the nodeList include both the consensus nodes and the observer nodes
    bcos::crypto::NodeIDListPtr m_nodeList;

    // index in nodeList
    std::atomic_int32_t m_nodeIndex{0};
};
}  // namespace bcos::sync
