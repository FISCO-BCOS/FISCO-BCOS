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
#include "RPBFTMsgFactory.h"
#include <libconsensus/pbft/PBFTEngine.h>

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
                    if (m_chosedConsensusNodes.count(peer))
                        selectedNode->push_back(peer);
                }
                return selectedNode;
            });
    }

    void setGroupSize(int64_t const& _groupSize)
    {
        _groupSize > m_sealersNum.load() ? m_groupSize = m_sealersNum.load() :
                                           m_groupSize = _groupSize;
        RPBFTENGINE_LOG(INFO) << LOG_KV("configured _groupSize", m_groupSize);
    }

    void setRotatingInterval(int64_t const& _rotatingInterval)
    {
        m_rotatingInterval = _rotatingInterval;
    }

    bool shouldRecvTxs() const override
    {
        // only the real consensusNode is far syncing, forbid send transactions to the node
        // the real observer node can receive messages
        return m_blockSync->isFarSyncing() && m_locatedInConsensusNodes;
    }

protected:
    // get the currentLeader
    std::pair<bool, IDXTYPE> getLeader() const override;
    bool locatedInChosedConsensensusNodes() const override;
    // TODO: select nodes with VRF algorithm
    IDXTYPE VRFSelection() const;

    void resetConfig() override;
    virtual void resetChosedConsensusNodes();
    virtual void chooseConsensusNodes();
    virtual void updateConsensusInfoForTreeRouter();
    virtual void resetLocatedInConsensusNodes();
    IDXTYPE minValidNodes() const override { return std::min(m_groupSize, m_sealersNum) - m_f; }

    virtual ssize_t filterBroadcastTargets(dev::network::NodeID const& _nodeId);

    virtual bool updateGroupSize();
    virtual bool updateRotatingInterval();
    void createPBFTMsgFactory() override { m_pbftMsgFactory = std::make_shared<RPBFTMsgFactory>(); }

    // get the disconnected node list
    void getForwardNodes(dev::h512s& _forwardNodes, dev::p2p::P2PSessionInfos const& _sessions);

    void forwardMsg(
        std::string const& _key, PBFTMsgPacket::Ptr _pbftMsgPacket, PBFTMsg const&) override;

    PBFTMsgPacket::Ptr createPBFTMsgPacket(bytesConstRef _data, PACKET_TYPE const& _packetType,
        unsigned const& _ttl, dev::h512s const& _forwardNodes) override;

    void broadcastMsg(dev::h512s const& _targetNodes, bytesConstRef _data,
        unsigned const& _packetType, unsigned const& _ttl) override;

protected:
    // configured group size
    std::atomic<int64_t> m_groupSize = {-1};
    // the interval(measured by block number) to adjust the sealers
    std::atomic<int64_t> m_rotatingInterval = {-1};

    std::atomic<int64_t> m_startNodeIdx = {-1};
    std::atomic<int64_t> m_rotatingRound = {0};
    std::atomic<int64_t> m_sealersNum = {0};

    std::atomic_bool m_locatedInConsensusNodes = {true};

    std::atomic_bool m_chosedConsNodeChanged = {false};
    mutable SharedMutex x_chosedConsensusNodes;
    std::set<dev::h512> m_chosedConsensusNodes;

    // used to record the rotatingIntervalEnableNumber changed or not
    dev::eth::BlockNumber m_rotatingIntervalEnableNumber = {-1};
    bool m_rotatingIntervalUpdated = false;
};
}  // namespace consensus
}  // namespace dev
