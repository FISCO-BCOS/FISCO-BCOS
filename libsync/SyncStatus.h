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
#pragma once
#include "Common.h"
#include "DownloadingBlockQueue.h"
#include "RspBlockReq.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Exceptions.h>
#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/StatisticHandler.h>
#include <libtxpool/TxPoolInterface.h>
#include <map>
#include <queue>
#include <set>
#include <vector>


namespace dev
{
namespace sync
{
struct SyncStatus
{
    SyncState state = SyncState::Idle;
    PROTOCOL_ID protocolId;
    int64_t currentBlockNumber;
    int64_t knownHighestNumber;
    h256 knownLatestHash;
};

class SyncPeerStatus
{
public:
    SyncPeerStatus(PROTOCOL_ID, NodeID const& _nodeId, int64_t _number, h256 const& _genesisHash,
        h256 const& _latestHash)
      : nodeId(_nodeId),
        number(_number),
        genesisHash(_genesisHash),
        latestHash(_latestHash),
        reqQueue(_nodeId)
    {}
    SyncPeerStatus(const SyncPeerInfo& _info, PROTOCOL_ID)
      : nodeId(_info.nodeId),
        number(_info.number),
        genesisHash(_info.genesisHash),
        latestHash(_info.latestHash),
        reqQueue(_info.nodeId)
    {}

    void update(const SyncPeerInfo& _info)
    {
        nodeId = _info.nodeId;
        number = _info.number;
        genesisHash = _info.genesisHash;
        latestHash = _info.latestHash;
    }

public:
    NodeID nodeId;
    int64_t number;
    h256 genesisHash;
    h256 latestHash;
    DownloadRequestQueue reqQueue;
    bool isSealer = false;
};

class SyncMasterStatus
{
public:
    using Ptr = std::shared_ptr<SyncMasterStatus>;
    SyncMasterStatus(std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        PROTOCOL_ID const& _protocolId, h256 const& _genesisHash, NodeID const& _nodeId)
      : genesisHash(_genesisHash),
        knownHighestNumber(0),
        knownLatestHash(_genesisHash),
        m_protocolId(_protocolId),
        m_nodeId(_nodeId),
        m_downloadingBlockQueue(_blockChain, _protocolId, _nodeId)
    {}

    SyncMasterStatus(h256 const& _genesisHash)
      : genesisHash(_genesisHash),
        knownHighestNumber(0),
        knownLatestHash(_genesisHash),
        m_protocolId(0),
        m_nodeId(0),
        m_downloadingBlockQueue(nullptr, 0, NodeID())
    {}

    virtual ~SyncMasterStatus() {}

    virtual void initConsensusInfo()
    {
        m_chosedConsensusNodes = std::make_shared<std::map<NodeID, int64_t>>();
        m_candidateConsensusNodes = std::make_shared<std::map<NodeID, int64_t>>();
    }

    bool hasPeer(NodeID const& _id);

    bool newSyncPeerStatus(SyncPeerInfo const& _info);

    void deletePeer(NodeID const& _id);

    std::shared_ptr<NodeIDs> peers();
    std::shared_ptr<std::set<NodeID>> peersSet();

    NodeIDs filterPeers(int64_t const& _neighborSize, std::shared_ptr<NodeIDs> _peers,
        std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow);

    std::shared_ptr<SyncPeerStatus> peerStatus(NodeID const& _id);

    void foreachPeer(std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f) const;

    void foreachPeerRandom(std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f) const;
    // select neighborSize peers
    // call _f for each selected peers
    void forRandomPeers(int64_t const& _neighborSize,
        std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _f);

    /// Select some peers at _percent when _allow(peer)
    NodeIDs randomSelection(
        unsigned _percent, std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow);

    /// Select some peers at _selectSize when _allow(peer)
    NodeIDs randomSelectionSize(
        size_t _maxChosenSize, std::function<bool(std::shared_ptr<SyncPeerStatus>)> const& _allow);

    DownloadingBlockQueue& bq() { return m_downloadingBlockQueue; }

    void setStatHandlerForDownloadingBlockQueue(dev::p2p::StatisticHandler::Ptr _statisticHandler)
    {
        m_downloadingBlockQueue.setStatHandler(_statisticHandler);
    }

    // update m_validSelectedNode when the
    // chosed consensus nodes have been changed to ensure safety when use RPBFT
    virtual void updateConsensusInfo(
        std::shared_ptr<std::set<NodeID>> _chosedConsensusInfo, dev::h512s const& _allSealerList);

    // update m_validSelectedNode when the syncInfo changed
    // to ensure safety for RPBFT
    virtual void updateSafetyInfo(const SyncPeerInfo& _info);

    virtual bool isSafety(std::function<bool(uint64_t const& _validSelectedNode)> const& _f)
    {
        Guard l(m_mutex);
        // safety requirement:
        // 1. at least one consensus node start started
        // 2. the block number of at least (f+1) chosedConsensusNodes is no smaller than the
        // max-block-number of candidateConsensusNodes
        return (m_maxCandidateConsNodesNumber != -1 && _f(m_validSelectedNode));
    }

private:
    int64_t selectPeers(int64_t const& _neighborSize, std::shared_ptr<NodeIDs> _nodeIds);
    // get m_validSelectedNode for safety check
    void calForSafetyEnsurance();

public:
    h256 genesisHash;
    mutable SharedMutex x_known;
    int64_t knownHighestNumber;
    h256 knownLatestHash;
    SyncState state = SyncState::Idle;

private:
    PROTOCOL_ID m_protocolId;
    NodeID m_nodeId;
    mutable SharedMutex x_peerStatus;
    std::map<NodeID, std::shared_ptr<SyncPeerStatus>> m_peersStatus;

    mutable Mutex m_mutex;
    // map between node id of chosed consensus nodes and the blockNumber
    std::shared_ptr<std::map<NodeID, int64_t>> m_chosedConsensusNodes;
    // map between node id of the candidated consensus nodes and the block number
    std::shared_ptr<std::map<NodeID, int64_t>> m_candidateConsensusNodes;
    // the number of chosed consensus nodes whose block number are no smaller than max block number
    // of the candidate consensus node
    std::atomic<int64_t> m_validSelectedNode = {-1};
    // the max block number of all the candidated consensus node
    std::atomic<int64_t> m_maxCandidateConsNodesNumber = {-1};

    DownloadingBlockQueue m_downloadingBlockQueue;
};

}  // namespace sync
}  // namespace dev