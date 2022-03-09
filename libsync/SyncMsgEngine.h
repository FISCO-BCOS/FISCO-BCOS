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
 * @brief : The implementation of callback from p2p
 * @author: jimmyshi
 * @date: 2018-10-17
 */

#pragma once
#include "Common.h"
#include "DownloadingTxsQueue.h"
#include "NodeTimeMaintenance.h"
#include "RspBlockReq.h"
#include "SyncMsgPacket.h"
#include "SyncMsgPacketFactory.h"
#include "SyncStatus.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/ThreadPool.h>
#include <libdevcore/Worker.h>
#include <libethcore/Exceptions.h>
#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/P2PMessage.h>
#include <libtxpool/TxPoolInterface.h>

namespace dev
{
namespace sync
{
class SyncMsgEngine : public std::enable_shared_from_this<SyncMsgEngine>
{
public:
    SyncMsgEngine(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<SyncMasterStatus> _syncStatus,
        std::shared_ptr<DownloadingTxsQueue> _txQueue, PROTOCOL_ID const& _protocolId,
        NodeID const& _nodeId, h256 const& _genesisHash)
      : m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_syncStatus(_syncStatus),
        m_txQueue(_txQueue),
        m_protocolId(_protocolId),
        m_groupId(dev::eth::getGroupAndProtocol(_protocolId).first),
        m_nodeId(_nodeId),
        m_genesisHash(_genesisHash)
    {
        m_service->registerHandlerByProtoclID(
            m_protocolId, boost::bind(&SyncMsgEngine::messageHandler, this, boost::placeholders::_1,
                              boost::placeholders::_2, boost::placeholders::_3));
        m_txsWorker = std::make_shared<dev::ThreadPool>("SyncMsgE-" + std::to_string(m_groupId), 1);
        m_txsSender =
            std::make_shared<dev::ThreadPool>("TxsSender-" + std::to_string(m_groupId), 1);
        m_txsReceiver =
            std::make_shared<dev::ThreadPool>("txsRecv-" + std::to_string(m_groupId), 1);
    }

    virtual void stop();
    virtual ~SyncMsgEngine() { stop(); }

    void messageHandler(dev::p2p::NetworkException _e,
        std::shared_ptr<dev::p2p::P2PSession> _session, dev::p2p::P2PMessage::Ptr _msg);

    bool blockNumberFarBehind() const;

    void onNotifyWorker(std::function<void()> const& _f) { m_onNotifyWorker = _f; }
    void onNotifySyncTrans(std::function<void()> const& _f) { m_onNotifySyncTrans = _f; }

    void setSyncMsgPacketFactory(SyncMsgPacketFactory::Ptr _syncMsgPacketFactory)
    {
        m_syncMsgPacketFactory = _syncMsgPacketFactory;
    }

    void setNodeTimeMaintenance(NodeTimeMaintenance::Ptr _nodeTimeMaintenance)
    {
        if (m_nodeTimeMaintenance)
        {
            return;
        }
        m_timeAlignWorker =
            std::make_shared<dev::ThreadPool>("alignTime-" + std::to_string(m_groupId), 1);
        m_nodeTimeMaintenance = _nodeTimeMaintenance;
    }

    NodeTimeMaintenance::Ptr nodeTimeMaintenance() { return m_nodeTimeMaintenance; }

private:
    bool checkSession(std::shared_ptr<dev::p2p::P2PSession> _session);
    bool checkMessage(dev::p2p::P2PMessage::Ptr _msg);
    bool checkGroupPacket(SyncMsgPacket const& _packet);

protected:
    virtual bool interpret(
        SyncMsgPacket::Ptr _packet, dev::p2p::P2PMessage::Ptr _msg, dev::h512 const& _peer);

    void onPeerStatus(SyncMsgPacket const& _packet);
    void onPeerTransactions(SyncMsgPacket::Ptr _packet, dev::p2p::P2PMessage::Ptr _msg);
    void onPeerBlocks(SyncMsgPacket const& _packet);
    void onPeerRequestBlocks(SyncMsgPacket const& _packet);

    void onPeerTxsStatus(
        std::shared_ptr<SyncMsgPacket> _packet, dev::h512 const& _peer, dev::p2p::P2PMessage::Ptr);
    void onReceiveTxsRequest(std::shared_ptr<SyncMsgPacket> _txsReqPacket, dev::h512 const& _peer,
        dev::p2p::P2PMessage::Ptr);

protected:
    // Outside data
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::shared_ptr<SyncMasterStatus> m_syncStatus;
    std::shared_ptr<DownloadingTxsQueue> m_txQueue;

    // Internal data
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    NodeID m_nodeId;  ///< Nodeid of this node
    h256 m_genesisHash;
    std::function<void()> m_onNotifyWorker = nullptr;
    std::function<void()> m_onNotifySyncTrans = nullptr;

    // TODO: Simplify worker threads
    std::shared_ptr<dev::ThreadPool> m_txsWorker;
    std::shared_ptr<dev::ThreadPool> m_txsSender;
    std::shared_ptr<dev::ThreadPool> m_txsReceiver;
    std::shared_ptr<dev::ThreadPool> m_timeAlignWorker;

    NodeTimeMaintenance::Ptr m_nodeTimeMaintenance;

    // factory used to create sync related packet
    SyncMsgPacketFactory::Ptr m_syncMsgPacketFactory;
};

class DownloadBlocksContainer
{
public:
    DownloadBlocksContainer(
        std::shared_ptr<dev::p2p::P2PInterface> _service, PROTOCOL_ID _protocolId, NodeID _nodeId)
      : m_service(_service), m_protocolId(_protocolId), m_nodeId(_nodeId), m_blockRLPsBatch()
    {
        m_groupId = dev::eth::getGroupAndProtocol(_protocolId).first;
    }
    ~DownloadBlocksContainer() { clearBatchAndSend(); }

    void batchAndSend(BlockPtr _block);
    void batchAndSend(std::shared_ptr<bytes> _blockRLP);

private:
    void clearBatchAndSend();
    void sendBigBlock(bytes const& _blockRLP);

private:
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    NodeID m_nodeId;
    std::vector<dev::bytes> m_blockRLPsBatch;
    size_t m_currentBatchSize = 0;
};

}  // namespace sync
}  // namespace dev
