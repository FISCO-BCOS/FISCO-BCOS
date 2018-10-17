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

#include "SyncData.h"


using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::p2p;
using namespace dev::blockchain;
using namespace dev::txpool;

bool SyncData::hasPeer(NodeID const& _id)
{
    auto peer = m_peersData.find(_id);
    return peer != m_peersData.end();
}

bool SyncData::newSyncPeerData(NodeInfo const& _info)
{
    if (hasPeer(_info.id))
    {
        LOG(WARNING) << "Peer " << _info.id << " is exist, no need to create.";
        return false;
    }

    try
    {
        shared_ptr<SyncPeerData> peer = make_shared<SyncPeerData>(_info);
        m_peersData.insert(pair<NodeID, shared_ptr<SyncPeerData>>(peer->id(), peer));
    }
    catch (Exception const& e)
    {
        LOG(ERROR) << "Create SyncPeer failed!";
        BOOST_THROW_EXCEPTION(InvalidSyncPeerCreation() << errinfo_comment(e.what()));
    }
    return true;
}

std::shared_ptr<SyncPeerData> SyncData::peerData(NodeID const& _id)
{
    auto peer = m_peersData.find(_id);
    if (peer == m_peersData.end())
    {
        LOG(WARNING) << "Peer data not found at id: " << _id;
        return nullptr;
    }
    return peer->second;
}

void SyncData::foreachPeer(std::function<bool(std::shared_ptr<SyncPeerData>)> const& _f) const
{
    for (auto& peer : m_peersData)
    {
        if (!_f(peer.second))
            break;
    }
}
