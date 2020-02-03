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
 * @brief : Implementation of Rotation PBFT Engine
 * @file: RotationPBFTEngine.h
 * @author: yujiechen
 * @date: 2019-09-11
 */
#pragma once
#include "RPBFTReqCache.h"
#include <libconsensus/pbft/PBFTEngine.h>
#include <libdevcore/TreeTopology.h>

#define RPBFTENGINE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("ROTATING-PBFT")

namespace dev
{
namespace consensus
{
class RotatingPBFTEngine : public PBFTEngine
{
public:
    using Ptr = std::shared_ptr<RotatingPBFTEngine>;
    RotatingPBFTEngine(dev::p2p::P2PInterface::Ptr _service,
        dev::txpool::TxPoolInterface::Ptr _txPool,
        dev::blockchain::BlockChainInterface::Ptr _blockChain,
        dev::sync::SyncInterface::Ptr _blockSync,
        dev::blockverifier::BlockVerifierInterface::Ptr _blockVerifier,
        dev::PROTOCOL_ID const& _protocolId, KeyPair const& _keyPair,
        h512s const& _sealerList = h512s())
      : PBFTEngine(_service, _txPool, _blockChain, _blockSync, _blockVerifier, _protocolId,
            _keyPair, _sealerList)
    {
        m_chosedConsensusNodes = std::make_shared<std::set<dev::h512>>();
        // only broadcast PBFT message to the current consensus nodes
        m_broacastTargetsFilter =
            boost::bind(&RotatingPBFTEngine::filterBroadcastTargets, this, _1);
        // only send transactions to the current consensus nodes
        m_blockSync->registerTxsReceiversFilter(
            [&](std::shared_ptr<std::set<dev::network::NodeID>> _peers) {
                std::shared_ptr<dev::p2p::NodeIDs> selectedNode =
                    std::make_shared<dev::p2p::NodeIDs>();
                for (auto const& peer : *_peers)
                {
                    if (m_chosedConsensusNodes->count(peer))
                        selectedNode->push_back(peer);
                }
                return selectedNode;
            });
        m_chosedSealerList = std::make_shared<dev::h512s>();

        m_requestRawPrepareWorker =
            std::make_shared<dev::ThreadPool>("prepRequest-" + std::to_string(m_groupId), 1);
        m_rawPrepareStatusReceiver =
            std::make_shared<dev::ThreadPool>("prepRecv-" + std::to_string(m_groupId), 1);
        m_rawPrepareResponse =
            std::make_shared<dev::ThreadPool>("prepResp-" + std::to_string(m_groupId), 1);
    }

    // create TreeTopology to forward prepare message
    void createTreeTopology(unsigned const& _treeWidth)
    {
        m_treeRouter = std::make_shared<dev::sync::TreeTopology>(m_keyPair.pub(), _treeWidth);
        if (m_chosedSealerList->size() > 0)
        {
            m_treeRouter->updateConsensusNodeInfo(*m_chosedSealerList);
        }
    }

    void setEpochSize(int64_t const& _epochSize)
    {
        _epochSize > m_sealersNum.load() ? m_epochSize = m_sealersNum.load() :
                                           m_epochSize = _epochSize;
        RPBFTENGINE_LOG(INFO) << LOG_KV("configured _epochSize", m_epochSize);
    }

    void setRotatingInterval(int64_t const& _rotatingInterval)
    {
        m_rotatingInterval = _rotatingInterval;
    }

    /// get sealer list
    dev::h512s consensusList() const override
    {
        ReadGuard l(x_chosedSealerList);
        return *m_chosedSealerList;
    }

    void setPrepareStatusBroadcastPercent(unsigned const& _prepareStatusBroadcastPercent)
    {
        m_prepareStatusBroadcastPercent = _prepareStatusBroadcastPercent;
    }

    void createPBFTReqCache() override;

protected:
    // get the currentLeader
    std::pair<bool, IDXTYPE> getLeader() const override;
    bool locatedInChosedConsensensusNodes() const override;
    dev::network::NodeID getSealerByIndex(size_t const& _index) const override;
    dev::network::NodeID getNodeIDByIndex(size_t const& _index) const;

    void sendPrepareMsgFromLeader(PrepareReq::Ptr _prepareReq, bytesConstRef _data,
        dev::PACKET_TYPE const& _p2pPacketType = 0) override;
    // forward the received prepare message by tree
    virtual void forwardReceivedPrepareMsgByTree(std::shared_ptr<dev::p2p::P2PSession> _session,
        dev::p2p::P2PMessage::Ptr _prepareMsg, PBFTMsgPacket::Ptr _pbftMsg);

    void onRecvPBFTMessage(dev::p2p::NetworkException _exception,
        std::shared_ptr<dev::p2p::P2PSession> _session,
        dev::p2p::P2PMessage::Ptr _message) override;

    bool handlePartiallyPrepare(std::shared_ptr<dev::p2p::P2PSession> _session,
        dev::p2p::P2PMessage::Ptr _message) override;

    // fault-tolerant logic for broadcast-prepare-by-tree
    virtual void sendRawPrepareStatusRandomly(PBFTMsg::Ptr _rawPrepareReq);
    virtual void onReceiveRawPrepareStatus(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message);

    // receive the requested rawPreparePacket
    virtual void onReceiveRawPrepareRequest(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message);

    virtual void onReceiveRawPrepareResponse(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message);

    // TODO: select nodes with VRF algorithm
    IDXTYPE VRFSelection() const;

    void resetConfig() override;
    virtual void resetChosedConsensusNodes();
    virtual void chooseConsensusNodes();
    virtual void updateConsensusInfo();
    virtual void resetLocatedInConsensusNodes();
    IDXTYPE minValidNodes() const override { return std::min(m_epochSize, m_sealersNum) - m_f; }

    virtual ssize_t filterBroadcastTargets(dev::network::NodeID const& _nodeId);

    virtual bool updateEpochSize();
    virtual bool updateRotatingInterval();

    void handleP2PMessage(dev::p2p::NetworkException _exception,
        std::shared_ptr<dev::p2p::P2PSession> _session,
        dev::p2p::P2PMessage::Ptr _message) override;

private:
    PBFTMsg::Ptr decodeP2PMsgIntoPBFTMsg(
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _message);

    // request RawPrepare packet to the _targetNode
    void requestRawPreparePacket(
        dev::h512 const& _targetNode, PBFTMsg::Ptr _requestedRawPrepareStatus);

protected:
    // configured epoch size
    std::atomic<int64_t> m_epochSize = {-1};
    // the interval(measured by block number) to adjust the sealers
    std::atomic<int64_t> m_rotatingInterval = {-1};

    std::atomic<int64_t> m_startNodeIdx = {-1};
    std::atomic<int64_t> m_rotatingRound = {0};
    std::atomic<int64_t> m_sealersNum = {0};

    std::atomic_bool m_locatedInConsensusNodes = {true};

    std::atomic_bool m_chosedConsNodeChanged = {false};
    mutable SharedMutex x_chosedConsensusNodes;
    std::shared_ptr<std::set<dev::h512>> m_chosedConsensusNodes;

    mutable SharedMutex x_chosedSealerList;
    std::shared_ptr<dev::h512s> m_chosedSealerList;

    // used to record the rotatingIntervalEnableNumber changed or not
    dev::eth::BlockNumber m_rotatingIntervalEnableNumber = {-1};
    bool m_rotatingIntervalUpdated = false;

    std::shared_ptr<dev::sync::TreeTopology> m_treeRouter;
    std::shared_ptr<RPBFTReqCache> m_rpbftReqCache;

    dev::ThreadPool::Ptr m_requestRawPrepareWorker;
    dev::ThreadPool::Ptr m_rawPrepareStatusReceiver;
    dev::ThreadPool::Ptr m_rawPrepareResponse;

    unsigned m_prepareStatusBroadcastPercent = 33;
};
}  // namespace consensus
}  // namespace dev
