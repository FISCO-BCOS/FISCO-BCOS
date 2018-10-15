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
 * @brief : Sync implementation
 * @author: jimmyshi
 * @date: 2018-09-21
 */

#pragma once
#include "Common.h"
#include "SyncInterface.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/Worker.h>
#include <libethcore/Exceptions.h>
#include <libp2p/P2PInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <queue>
#include <vector>

namespace dev
{
namespace sync
{
class DownloadingBlockQueuePiority
{
public:
    bool operator()(
        const std::shared_ptr<dev::eth::Block>& left, const std::shared_ptr<dev::eth::Block>& right)
    {
        if (!left || !right)
            BOOST_THROW_EXCEPTION(dev::eth::InvalidBlockDownloadQueuePiorityInput());
        return left->header().number() > right->header().number();
    }
};

class SyncMaster : public SyncInterface
{
public:
    SyncMaster(dev::blockchain::BlockChainInterface& _blockChain,
        dev::txpool::TxPoolInterface& _txPool, int16_t const _protocolId, unsigned _idleWaitMs);

    template <typename T1, typename T2>
    SyncMaster(T1 _blockChain, T2 _txPool, int16_t const _protocolId, unsigned _idleWaitMs = 30)
      : m_blockChain(dynamic_cast<dev::blockchain::BlockChainInterface&>(_blockChain)),
        m_txPool(dynamic_cast<dev::txpool::TxPoolInterface&>(_txPool)),
        m_protocolId(_protocolId),
        SyncInterface(_protocolId, _idleWaitMs){};

    virtual ~SyncMaster(){};
    /// start blockSync
    virtual void start() override;
    /// stop blockSync
    virtual void stop() override;

    virtual void doWork() override;

    /// get status of block sync
    /// @returns Synchonization status
    virtual SyncStatus status() const override;
    virtual bool isSyncing() const override;
    // virtual h256 latestBlockSent() override;

    /// for rpc && sdk: broad cast transaction to all nodes
    // virtual void broadCastTransactions() override;
    /// for p2p: broad cast transaction to specified nodes
    // virtual void sendTransactions(NodeList const& _nodes) override;

    /// abort sync and reset all status of blockSyncs
    // virtual void reset() override;
    // virtual bool forceSync() override;

    /// protocol id used when register handler to p2p module
    virtual int16_t const& protocolId() const override { return m_protocolId; };
    virtual void setProtocolId(int16_t const _protocolId) override { m_protocolId = _protocolId; };

private:
    // Outside data
    dev::blockchain::BlockChainInterface& m_blockChain;
    dev::txpool::TxPoolInterface& m_txPool;

    // Internal data
    // std::map<dev::p2p::NodeID, std::shared_ptr<SyncPeer>> m_peers;
    std::priority_queue<std::shared_ptr<dev::eth::Block>,
        std::vector<std::shared_ptr<dev::eth::Block>>, DownloadingBlockQueuePiority>
        m_downloadingBlockQueue;
    unsigned m_startingBlock = 0;  ///< Last block number for the start of sync
    unsigned m_highestBlock = 0;   ///< Highest block number seen

    // Internal coding variable
    mutable RecursiveMutex x_sync;

    // sync state
    SyncState m_state = SyncState::Idle;

    // settings
    int64_t m_maxBlockDownloadQueueSize = 5;
    int16_t m_protocolId;
};

}  // namespace sync
}  // namespace dev