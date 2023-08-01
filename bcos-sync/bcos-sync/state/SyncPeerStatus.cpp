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

#include <utility>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::crypto;
using namespace bcos::protocol;

PeerStatus::PeerStatus(BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockNumber _number,
    HashType const& _hash, HashType const& _genesisHash)
  : m_nodeId(std::move(_nodeId)),
    m_number(_number),
    m_hash(_hash),
    m_genesisHash(_genesisHash),
    m_downloadRequests(std::make_shared<DownloadRequestQueue>(_config, m_nodeId))
{}

PeerStatus::PeerStatus(BlockSyncConfig::Ptr _config, PublicPtr _nodeId)
  : PeerStatus(std::move(_config), std::move(_nodeId), 0, HashType(), HashType())
{}

PeerStatus::PeerStatus(
    BlockSyncConfig::Ptr _config, PublicPtr _nodeId, BlockSyncStatusInterface::ConstPtr _status)
  : PeerStatus(std::move(_config), std::move(_nodeId), _status->number(), _status->hash(),
        _status->genesisHash())
{}

bool PeerStatus::update(BlockSyncStatusInterface::ConstPtr _status)
{
    UpgradableGuard l(x_mutex);
    if (m_hash == _status->hash() && _status->number() == m_number &&
        m_archivedNumber == _status->archivedBlockNumber())
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
    m_archivedNumber = _status->archivedBlockNumber();
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

bool SyncPeerStatus::hasPeer(PublicPtr const& _peer) const
{
    Guard lock(x_peersStatus);
    return m_peersStatus.contains(_peer);
}

PeerStatus::Ptr SyncPeerStatus::peerStatus(bcos::crypto::PublicPtr const& _peer) const
{
    Guard lock(x_peersStatus);
    auto iter = m_peersStatus.find(_peer);
    if (iter == m_peersStatus.end())
    {
        return nullptr;
    }
    return iter->second;
}

PeerStatus::Ptr SyncPeerStatus::insertEmptyPeer(const PublicPtr& _peer)
{
    Guard lock(x_peersStatus);
    // create and insert the new peer status
    auto peerStatus = std::make_shared<PeerStatus>(m_config, _peer);
    m_peersStatus.insert(std::make_pair(_peer, peerStatus));
    return peerStatus;
}

bool SyncPeerStatus::updatePeerStatus(
    PublicPtr _peer, BlockSyncStatusInterface::ConstPtr _peerStatus)
{
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
    PeerStatus::Ptr peerStatus{nullptr};
    {
        Guard lock(x_peersStatus);
        // update the existed peer status
        auto iter = m_peersStatus.find(_peer);
        if (iter != m_peersStatus.end())
        {
            peerStatus = iter->second;
        }
    }
    if (peerStatus != nullptr)
    {
        if (peerStatus->update(_peerStatus))
        {
            updateKnownMaxBlockInfo(_peerStatus);
        }
        return true;
    }
    // create and insert the new peer status
    peerStatus = std::make_shared<PeerStatus>(m_config, _peer, _peerStatus);
    {
        Guard lock(x_peersStatus);
        m_peersStatus.insert({_peer, peerStatus});
    }
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
    Guard lock(x_peersStatus);
    auto peer = m_peersStatus.find(_peer);
    if (peer != m_peersStatus.end())
    {
        m_peersStatus.erase(peer);
    }
}

void SyncPeerStatus::foreachPeerRandom(std::function<bool(PeerStatus::Ptr)> const& _func) const
{
    // Get nodeid list
    NodeIDs nodeIds;
    {
        Guard lock(x_peersStatus);
        if (m_peersStatus.empty())
        {
            return;
        }
        for (const auto& peer : m_peersStatus)
        {
            nodeIds.emplace_back(peer.first);
        }

        // Random nodeid list
        for (size_t i = nodeIds.size() - 1; i > 0; --i)
        {
            size_t select = rand() % (i + 1);
            swap(nodeIds[i], nodeIds[select]);
        }
    }

    // access _f() according to the random list
    for (const auto& nodeId : nodeIds)
    {
        // check the peers status in case peersStatus changed during the loop
        auto const& peer = peerStatus(nodeId);
        if (peer && !_func(peer))
        {
            break;
        }
    }
}

void SyncPeerStatus::foreachPeer(std::function<bool(PeerStatus::Ptr)> const& _func) const
{
    // copy the peers status in case peersStatus changed during the loop
    auto&& peersStatus = peersStatsCopy();
    for (const auto& peer : peersStatus)
    {
        if (peer.second && !_func(peer.second))
        {
            break;
        }
    }
}

std::shared_ptr<NodeIDs> SyncPeerStatus::peers()
{
    auto nodeIds = std::make_shared<NodeIDs>();
    Guard lock(x_peersStatus);
    for (auto& peer : m_peersStatus)
    {
        nodeIds->emplace_back(peer.first);
    }
    return nodeIds;
}