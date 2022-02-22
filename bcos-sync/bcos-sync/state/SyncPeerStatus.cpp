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
 * @brief matain the sync status
 * @file SyncPeerStatus.cpp
 * @author: jimmyshi
 * @date 2021-05-24
 */
#include "SyncPeerStatus.h"

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::protocol;

PeerStatus::PeerStatus(BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockNumber _number,
    HashType const& _hash, HashType const& _gensisHash)
  : m_nodeId(_nodeId),
    m_number(_number),
    m_hash(_hash),
    m_genesisHash(_gensisHash),
    m_downloadRequests(std::make_shared<DownloadRequestQueue>(_config, m_nodeId))
{}

PeerStatus::PeerStatus(BlockSyncConfig::Ptr _config, PublicPtr _nodeId)
  : PeerStatus(_config, _nodeId, 0, HashType(), HashType())
{}

PeerStatus::PeerStatus(
    BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockSyncStatusInterface::ConstPtr _status)
  : PeerStatus(_config, _nodeId, _status->number(), _status->hash(), _status->genesisHash())
{}

bool PeerStatus::update(BlockSyncStatusInterface::ConstPtr _status)
{
    UpgradableGuard l(x_mutex);
    if (m_hash == _status->hash() && _status->number() == m_number)
    {
        return false;
    }
    if (m_genesisHash != HashType() && _status->genesisHash() != m_genesisHash)
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("Status")
                             << LOG_DESC(
                                    "Receive invalid status packet with different genesis hash")
                             << LOG_KV("peer", m_nodeId->shortHex())
                             << LOG_KV("genesisHash", _status->genesisHash().abridged())
                             << LOG_KV("storedGenesisHash", m_genesisHash.abridged());
        return false;
    }
    UpgradeGuard ul(l);
    m_number = _status->number();
    m_hash = _status->hash();
    if (m_genesisHash == HashType())
    {
        m_genesisHash = _status->genesisHash();
    }
    BLKSYNC_LOG(DEBUG) << LOG_DESC("updatePeerStatus") << LOG_KV("peer", m_nodeId->shortHex())
                       << LOG_KV("number", _status->number())
                       << LOG_KV("hash", _status->hash().abridged())
                       << LOG_KV("genesisHash", _status->genesisHash().abridged());
    return true;
}

bool SyncPeerStatus::hasPeer(PublicPtr _peer)
{
    ReadGuard l(x_peersStatus);
    return m_peersStatus.count(_peer);
}

PeerStatus::Ptr SyncPeerStatus::peerStatus(bcos::crypto::PublicPtr _peer)
{
    ReadGuard l(x_peersStatus);
    if (!m_peersStatus.count(_peer))
    {
        return nullptr;
    }
    return m_peersStatus[_peer];
}

PeerStatus::Ptr SyncPeerStatus::insertEmptyPeer(PublicPtr _peer)
{
    WriteGuard l(x_peersStatus);
    // create and insert the new peer status
    auto peerStatus = std::make_shared<PeerStatus>(m_config, _peer);
    m_peersStatus.insert(std::make_pair(_peer, peerStatus));
    return peerStatus;
}

bool SyncPeerStatus::updatePeerStatus(
    PublicPtr _peer, BlockSyncStatusInterface::ConstPtr _peerStatus)
{
    WriteGuard l(x_peersStatus);
    // check the status
    if (_peerStatus->genesisHash() != m_config->genesisHash())
    {
        BLKSYNC_LOG(WARNING) << LOG_BADGE("updatePeerStatus")
                             << LOG_DESC(
                                    "Receive invalid status packet with different genesis hash")
                             << LOG_KV("peer", _peer->shortHex())
                             << LOG_KV("genesisHash", _peerStatus->genesisHash().abridged())
                             << LOG_KV("expectedGenesisHash", m_config->genesisHash().abridged());
        return false;
    }
    // update the existed peer status
    if (m_peersStatus.count(_peer))
    {
        auto status = m_peersStatus[_peer];
        if (status->update(_peerStatus))
        {
            updateKnownMaxBlockInfo(_peerStatus);
        }
        return true;
    }
    // create and insert the new peer status
    auto peerStatus = std::make_shared<PeerStatus>(m_config, _peer, _peerStatus);
    m_peersStatus.insert(std::make_pair(_peer, peerStatus));
    BLKSYNC_LOG(DEBUG) << LOG_DESC("updatePeerStatus: new peer")
                       << LOG_KV("peer", _peer->shortHex())
                       << LOG_KV("number", _peerStatus->number())
                       << LOG_KV("hash", _peerStatus->hash().abridged())
                       << LOG_KV("genesisHash", _peerStatus->genesisHash().abridged())
                       << LOG_KV("node", m_config->nodeID()->shortHex());
    updateKnownMaxBlockInfo(_peerStatus);
    return true;
}

void SyncPeerStatus::updateKnownMaxBlockInfo(BlockSyncStatusInterface::ConstPtr _peerStatus)
{
    if (_peerStatus->genesisHash() != m_config->genesisHash())
    {
        return;
    }
    if (_peerStatus->number() <= m_config->knownHighestNumber())
    {
        return;
    }
    m_config->setKnownHighestNumber(_peerStatus->number());
    m_config->setKnownLatestHash(_peerStatus->hash());
}

void SyncPeerStatus::deletePeer(PublicPtr _peer)
{
    WriteGuard l(x_peersStatus);
    auto peer = m_peersStatus.find(_peer);
    if (peer != m_peersStatus.end())
    {
        m_peersStatus.erase(peer);
    }
}

void SyncPeerStatus::foreachPeerRandom(std::function<bool(PeerStatus::Ptr)> const& _f) const
{
    ReadGuard l(x_peersStatus);
    if (m_peersStatus.empty())
    {
        return;
    }

    // Get nodeid list
    NodeIDs nodeIds;
    for (auto& peer : m_peersStatus)
    {
        nodeIds.emplace_back(peer.first);
    }

    // Random nodeid list
    for (size_t i = nodeIds.size() - 1; i > 0; --i)
    {
        size_t select = rand() % (i + 1);
        swap(nodeIds[i], nodeIds[select]);
    }

    // access _f() according to the random list
    for (auto nodeId : nodeIds)
    {
        auto const& peer = m_peersStatus.find(nodeId);
        if (peer == m_peersStatus.end())
        {
            continue;
        }
        if (peer->second && !_f(peer->second))
        {
            break;
        }
    }
}

void SyncPeerStatus::foreachPeer(std::function<bool(PeerStatus::Ptr)> const& _f) const
{
    ReadGuard l(x_peersStatus);
    for (auto peer : m_peersStatus)
    {
        if (peer.second && !_f(peer.second))
        {
            break;
        }
    }
}

std::shared_ptr<NodeIDs> SyncPeerStatus::peers()
{
    auto nodeIds = std::make_shared<NodeIDs>();
    ReadGuard l(x_peersStatus);
    for (auto& peer : m_peersStatus)
        nodeIds->emplace_back(peer.first);
    return nodeIds;
}