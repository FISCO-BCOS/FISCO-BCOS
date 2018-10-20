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
    auto peer = m_peersStatus.find(_id);
    return peer != m_peersStatus.end();
}

bool SyncMasterStatus::newSyncPeerStatus(NodeInfo const& _info)
{
    if (hasPeer(_info.nodeId))
    {
        LOG(WARNING) << "Peer " << _info.nodeId << " is exist, no need to create.";
        return false;
    }

    try
    {
        shared_ptr<SyncPeerStatus> peer = make_shared<SyncPeerStatus>(_info);

        Guard l(x_peerStatus);
        m_peersStatus.insert(pair<NodeID, shared_ptr<SyncPeerStatus>>(peer->nodeId, peer));
    }
    catch (Exception const& e)
    {
        LOG(ERROR) << "Create SyncPeer failed!";
        BOOST_THROW_EXCEPTION(InvalidSyncPeerCreation() << errinfo_comment(e.what()));
    }
    return true;
}

std::shared_ptr<SyncPeerStatus> SyncMasterStatus::peerStatus(NodeID const& _id)
{
    auto peer = m_peersStatus.find(_id);
    if (peer == m_peersStatus.end())
    {
        LOG(WARNING) << "Peer data not found at id: " << _id;
        return nullptr;
    }
    return peer->second;
}

void SyncMasterStatus::foreachPeer(
    std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f) const
{
    for (auto& peer : m_peersStatus)
    {
        if (!_f(peer.second))
            break;
    }
}

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
    return move(chosen);
}
