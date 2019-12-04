/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : SyncTreeTopology implementation
 * @file: SyncTreeTopology.h
 * @author: yujiechen
 * @date: 2019-09-19
 */
#pragma once
#include "TreeTopology.h"

#define SYNCTREE_LOG(_OBV)                                                 \
    LOG(_OBV) << LOG_BADGE("SYNCTREE") << LOG_KV("nodeIndex", m_nodeIndex) \
              << LOG_KV("consIndex", m_consIndex) << LOG_KV("nodeId", m_nodeId.abridged())

namespace dev
{
namespace sync
{
class SyncTreeTopology : public TreeTopology
{
public:
    using Ptr = std::shared_ptr<SyncTreeTopology>;
    SyncTreeTopology(dev::h512 const& _nodeId, unsigned const& _treeWidth = 3)
      : TreeTopology(_nodeId, _treeWidth)
    {
        m_childOffset = 1;
        m_nodeList = std::make_shared<dev::h512s>();
    }

    virtual ~SyncTreeTopology() {}
    // update corresponding info when the nodes changed
    virtual void updateNodeListInfo(dev::h512s const& _nodeList);
    // select the nodes by tree topology
    std::shared_ptr<dev::h512s> selectNodes(std::shared_ptr<std::set<dev::h512>> _peers) override;

protected:
    bool getNodeIDByIndex(dev::h512& _nodeID, ssize_t const& _nodeIndex) const override;
    // update the tree-topology range the nodes located in
    void updateStartAndEndIndex() override;

    ssize_t getChildNodeIndex(ssize_t const& _parentIndex, ssize_t const& _offset) override;
    ssize_t getParentNodeIndex(ssize_t const& _nodeIndex) override;

    // select the child nodes by tree
    void recursiveSelectChildNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
        ssize_t const& _parentIndex, std::shared_ptr<std::set<dev::h512>> _peers) override;
    // select the parent nodes by tree
    void selectParentNodes(std::shared_ptr<dev::h512s> _selectedNodeList,
        std::shared_ptr<std::set<dev::h512>> _peers, int64_t const& _nodeIndex) override;

private:
    bool locatedInGroup();

protected:
    mutable Mutex m_mutex;
    // the nodeList include both the consensus nodes and the observer nodes
    std::shared_ptr<dev::h512s> m_nodeList;
    std::atomic<int64_t> m_nodeNum = {0};

    std::atomic<int64_t> m_nodeIndex = {0};
};
}  // namespace sync
}  // namespace dev
