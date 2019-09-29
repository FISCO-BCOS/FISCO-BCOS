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
 * @brief : implementation of sync transaction
 * @author: yujiechen
 * @date: 2019-09-16
 */

#pragma once
#include "Common.h"
#include "DownloadingTxsQueue.h"
#include "SyncStatus.h"
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Common.h>
#include <libethcore/Exceptions.h>
#include <libnetwork/Common.h>
#include <libnetwork/Session.h>
#include <libp2p/P2PInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <vector>


namespace dev
{
namespace sync
{
class SyncTransaction : public Worker
{
public:
    using Ptr = std::shared_ptr<SyncTransaction>;
    SyncTransaction(std::shared_ptr<dev::p2p::P2PInterface> _service,
        std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<DownloadingTxsQueue> _txsQueue, PROTOCOL_ID const& _protocolId,
        NodeID const& _nodeId, std::shared_ptr<SyncMasterStatus> _syncStatus)
      : Worker("Sync-" + std::to_string(_protocolId), 0),
        m_service(_service),
        m_txPool(_txPool),
        m_txQueue(_txsQueue),
        m_protocolId(_protocolId),
        m_groupId(dev::eth::getGroupAndProtocol(_protocolId).first),
        m_nodeId(_nodeId),
        m_syncStatus(_syncStatus)
    {
        // signal registration
        m_tqReady = m_txPool->onReady([&]() { this->noteNewTransactions(); });
        /// set thread name
        std::string threadName = "SyncTrans-" + std::to_string(m_groupId);
        setName(threadName);
    }

    virtual ~SyncTransaction() { stop(); };
    /// start blockSync
    virtual void start();
    /// stop blockSync
    virtual void stop();
    /// doWork every idleWaitMs
    virtual void doWork() override;
    virtual void workLoop() override;
    void noteNewTransactions()
    {
        m_newTransactions = true;
        m_signalled.notify_all();
    }

    int64_t protocolId() { return m_protocolId; }

    NodeID nodeId() { return m_nodeId; }
    std::shared_ptr<SyncMasterStatus> syncStatus() { return m_syncStatus; }

    void printSendedTxsInfo()
    {
        SYNC_LOG(DEBUG) << LOG_DESC("CONS: printSendedTxsInfo")
                        << LOG_KV("sendedTxsCount", m_sendedTxsCount)
                        << LOG_KV("sendedTxsBytes", m_sendedTxsBytes);
    }

private:
    /// p2p service handler
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;

    std::shared_ptr<DownloadingTxsQueue> m_txQueue;

    // Internal data
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    NodeID m_nodeId;  ///< Nodeid of this node

    /// Block queue and peers
    std::shared_ptr<SyncMasterStatus> m_syncStatus;

    // Internal coding variable
    /// mutex to access m_signalled
    Mutex x_signalled;

    /// signal to notify all thread to work
    std::condition_variable m_signalled;

    // sync state
    std::atomic_bool m_newTransactions = {false};
    bool m_needMaintainTransactions = true;

    // settings
    dev::eth::Handler<> m_tqReady;

    std::function<dev::p2p::NodeIDs(std::set<NodeID> const&)> fp_broadCastNodesFilter = nullptr;

    std::atomic<uint64_t> m_sendedTxsCount = {0};
    std::atomic<uint64_t> m_sendedTxsBytes = {0};

public:
    void maintainTransactions();
    void maintainDownloadingTransactions();
};

}  // namespace sync
}  // namespace dev
