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
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : Sync peer
 * @author: jimmyshi
 * @date: 2018-10-16
 */

#include "SyncStatus.h"


using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;

bool SyncMasterStatus::hasPeer(NodeID const& _id)
{
    ReadGuard l(x_peerStatus);
    auto peer = m_peersStatus.find(_id);
    return peer != m_peersStatus.end();
}

bool SyncMasterStatus::newSyncPeerStatus(SyncPeerInfo const& _info)
{
    if (hasPeer(_info.nodeId))
    {
        SYNC_LOG(WARNING) << LOG_BADGE("Status")
                          << LOG_DESC("Peer status is exist, no need to create");
        return false;
    }

    try
    {
        shared_ptr<SyncPeerStatus> peer = make_shared<SyncPeerStatus>(_info, this->m_protocolId);

        WriteGuard l(x_peerStatus);
        m_peersStatus.insert(pair<NodeID, shared_ptr<SyncPeerStatus>>(peer->nodeId, peer));
    }
    catch (Exception const& e)
    {
        SYNC_LOG(ERROR) << LOG_BADGE("Status") << LOG_DESC("Create SyncPeer failed!");
        BOOST_THROW_EXCEPTION(InvalidSyncPeerCreation() << errinfo_comment(e.what()));
    }
    return true;
}

void SyncMasterStatus::deletePeer(NodeID const& _id)
{
    WriteGuard l(x_peerStatus);
    auto peer = m_peersStatus.find(_id);
    if (peer != m_peersStatus.end())
        m_peersStatus.erase(peer);
}

std::shared_ptr<NodeIDs> SyncMasterStatus::peers()
{
    std::shared_ptr<NodeIDs> nodeIds = std::make_shared<NodeIDs>();
    ReadGuard l(x_peerStatus);
    for (auto& peer : m_peersStatus)
        nodeIds->emplace_back(peer.first);
    return nodeIds;
}

// get all the peers maintained in SyncStatus
std::shared_ptr<std::set<NodeID>> SyncMasterStatus::peersSet()
{
    std::shared_ptr<std::set<NodeID>> nodeIdSet = std::make_shared<std::set<NodeID>>();
    ReadGuard l(x_peerStatus);
    for (auto& peer : m_peersStatus)
        nodeIdSet->insert(peer.first);
    return nodeIdSet;
}

std::shared_ptr<SyncPeerStatus> SyncMasterStatus::peerStatus(NodeID const& _id)
{
    ReadGuard l(x_peerStatus);
    auto peer = m_peersStatus.find(_id);
    if (peer == m_peersStatus.end())
    {
        SYNC_LOG(WARNING) << LOG_BADGE("Status") << LOG_DESC("Peer data not found")
                          << LOG_KV("nodeId", _id.abridged());
        return nullptr;
    }
    return peer->second;
}

/**
 * @brief : filter the nodes that the txs should be sent to from a given node list
 *
 * @param nodes: the complete set that the given tx should be sent to
 * @param _allow : the filter function,
 *  if the function return true, then chose to broadcast transaction to the corresponding node
 *  if the function return false, then ignore the corresponding node
 * @return NodeIDs : the filtered node list that the txs should be sent to
 */
NodeIDs SyncMasterStatus::filterPeers(int64_t const& _neighborSize, std::shared_ptr<NodeIDs> _peers,
    std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow)
{
    int64_t selectedSize = _peers->size();
    if (selectedSize > _neighborSize)
    {
        selectedSize = selectPeers(_neighborSize, _peers);
    }
    NodeIDs chosen;
    for (auto const& peer : (*_peers))
    {
        if (m_peersStatus.count(peer) && _allow(m_peersStatus[peer]))
        {
            chosen.push_back(peer);
            if ((int64_t)chosen.size() == selectedSize)
            {
                break;
            }
        }
    }
    return chosen;
}

void SyncMasterStatus::foreachPeer(
    std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f) const
{
    ReadGuard l(x_peerStatus);
    for (auto& peer : m_peersStatus)
    {
        if (!_f(peer.second))
            break;
    }
}

void SyncMasterStatus::foreachPeerRandom(
    std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f) const
{
    ReadGuard l(x_peerStatus);
    if (m_peersStatus.empty())
        return;

    // Get nodeid list
    NodeIDs nodeIds;
    for (auto& peer : m_peersStatus)
        nodeIds.emplace_back(peer.first);

    // Random nodeid list
    for (size_t i = nodeIds.size() - 1; i > 0; --i)
    {
        size_t select = rand() % (i + 1);
        swap(nodeIds[i], nodeIds[select]);
    }

    // access _f() according to the random list
    for (NodeID const& nodeId : nodeIds)
    {
        auto const& peer = m_peersStatus.find(nodeId);
        if (peer == m_peersStatus.end())
            continue;
        if (!_f(peer->second))
            break;
    }
}

int64_t SyncMasterStatus::selectPeers(
    int64_t const& _neighborSize, std::shared_ptr<NodeIDs> _nodeIds)
{
    int64_t nodeSize = _nodeIds->size();
    int64_t selectedSize = _neighborSize > nodeSize ? nodeSize : _neighborSize;
    for (auto i = 0; i < selectedSize; i++)
    {
        int64_t randomValue = std::rand() % selectedSize;
        swap((*_nodeIds)[i], (*_nodeIds)[randomValue]);
    }
    return selectedSize;
}

void SyncMasterStatus::forRandomPeers(
    int64_t const& _neighborSize, std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f)
{
    std::shared_ptr<NodeIDs> nodeIds = std::make_shared<NodeIDs>();
    ReadGuard l(x_peerStatus);
    for (auto& peer : m_peersStatus)
    {
        nodeIds->emplace_back(peer.first);
    }

    int64_t selectedSize = selectPeers(_neighborSize, nodeIds);
    // call _f for the selected nodes
    for (int i = 0; i < selectedSize; i++)
    {
        _f(m_peersStatus[(*nodeIds)[i]]);
    }
}

// TODO: return reference
NodeIDs SyncMasterStatus::randomSelection(
    unsigned _percent, std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow)
{
    NodeIDs chosen;
    NodeIDs allowed;

    size_t peerCount = 0;
    foreachPeer([&](std::shared_ptr<SyncPeerStatus> _p) {
        if (_allow(_p))
        {
            allowed.push_back(_p->nodeId);
        }
        ++peerCount;
        return true;
    });

    // TODO optimize it by swap (no need two vectors)
    size_t chosenSize = (peerCount * _percent + 99) / 100;
    chosen.reserve(chosenSize);
    for (unsigned i = chosenSize; i && allowed.size(); i--)
    {
        unsigned n = rand() % allowed.size();
        chosen.push_back(std::move(allowed[n]));
        allowed.erase(allowed.begin() + n);
    }
    return chosen;
}
// TODO: return reference
NodeIDs SyncMasterStatus::randomSelectionSize(
    size_t _maxChosenSize, std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow)
{
    NodeIDs chosen;
    NodeIDs allowed;

    size_t peerCount = 0;
    foreachPeer([&](std::shared_ptr<SyncPeerStatus> _p) {
        if (_allow(_p))
        {
            allowed.push_back(_p->nodeId);
        }
        ++peerCount;
        return true;
    });

    chosen.reserve(_maxChosenSize);
    for (unsigned i = _maxChosenSize; i && allowed.size(); i--)
    {
        unsigned n = rand() % allowed.size();
        chosen.push_back(std::move(allowed[n]));
        allowed.erase(allowed.begin() + n);
    }
    return chosen;
}