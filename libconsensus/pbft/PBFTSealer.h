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
 * @brief : implementation of PBFT consensus
 * @file: PBFTConsensus.h
 * @author: yujiechen
 * @date: 2018-09-28
 *
 *
 * @author: yujiechen
 * @file:PBFTSealer.h
 * @date: 2018-10-26
 * @modifications: rename PBFTConsensus.h to PBFTSealer.h
 */
#pragma once
#include "PBFTEngine.h"
#include <libconsensus/Sealer.h>
#include <sstream>
namespace dev
{
namespace consensus
{
class PBFTSealer : public Sealer
{
public:
    PBFTSealer(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync)
      : Sealer(_txPool, _blockChain, _blockSync)
    {}

    void start() override;
    void stop() override;
    /// can reset the sealing block or not?
    bool shouldResetSealing() override
    {
        // in case of that:
        // 1. block n has trigger reportNewBlock and the system is committing the block (n+1), the
        // blockNumber has changed to (n+1)
        // 2. Sealer calls reportNewBlock, and generate a new block with number of block (n+2)
        // 3. block (n+1) commit completed and trigger reportNewBlock again
        // 4. Sealer calls reportNewBlock, and generate a new block with number of block (n+2) again
        if (m_sealing.block->isSealed() && shouldHandleBlock())
        {
            PBFTSEALER_LOG(DEBUG)
                << LOG_DESC("sealing block have already been sealed and should be handled")
                << LOG_KV("sealingNumber", m_sealing.block->blockHeader().number())
                << LOG_KV("curNum", m_blockChain->number());
            return false;
        }
        /// only the leader need reset sealing in PBFT
        return Sealer::shouldResetSealing() &&
               (m_pbftEngine->getLeader().second == m_pbftEngine->nodeIdx());
    }

    void setConsensusEngine(ConsensusInterface::Ptr _consensusEngine) override
    {
        Sealer::setConsensusEngine(_consensusEngine);
        initConsensusEngine();
    }

    void setEnableDynamicBlockSize(bool enableDynamicBlockSize)
    {
        m_enableDynamicBlockSize = enableDynamicBlockSize;
    }

    void setBlockSizeIncreaseRatio(bool blockSizeIncreaseRatio)
    {
        m_blockSizeIncreaseRatio = blockSizeIncreaseRatio;
    }

protected:
    virtual void initConsensusEngine()
    {
        m_pbftEngine = std::dynamic_pointer_cast<PBFTEngine>(m_consensusEngine);

        // called by viewchange procedure to reset block when timeout
        m_pbftEngine->onViewChange(boost::bind(&PBFTSealer::resetBlockForViewChange, this));

        /// called by the next leader to reset block when it receives the prepare block
        m_pbftEngine->onNotifyNextLeaderReset(
            boost::bind(&PBFTSealer::resetBlockForNextLeader, this, _1));

        // resetConfig to update m_sealersNum
        m_pbftEngine->resetConfig();
        /// set thread name for PBFTSealer
        std::string threadName = "PBFTSeal-" + std::to_string(m_pbftEngine->groupId());
        setName(threadName);
    }

    void handleBlock() override;
    bool shouldSeal() override;
    // only the leader can generate the latest block
    bool shouldHandleBlock() override
    {
        return m_sealing.block->blockHeader().number() == (m_blockChain->number() + 1) &&
               (m_pbftEngine->getLeader().first &&
                   m_pbftEngine->getLeader().second == m_pbftEngine->nodeIdx());
    }

    bool reachBlockIntervalTime() override
    {
        return m_pbftEngine->reachBlockIntervalTime() ||
               (m_sealing.block->getTransactionSize() > 0 && m_pbftEngine->reachMinBlockGenTime());
    }
    /// in case of the next leader packeted the number of maxTransNum transactions before the last
    /// block is consensused
    bool canHandleBlockForNextLeader() override
    {
        return m_pbftEngine->canHandleBlockForNextLeader();
    }
    void setBlock();
    void attempIncreaseTimeoutTx();

private:
    void onTimeout(uint64_t const& sealingTxNumber);
    void increaseMaxTxsCanSeal();
    void onCommitBlock(
        uint64_t const& blockNumber, uint64_t const& sealingTxNumber, unsigned const& changeCycle);
    /// reset block when view changes
    void resetBlockForViewChange()
    {
        /// in case of that:
        /// time1: checkTimeout, blockNumber = n - 1
        /// time2: Report block, blockNumber = n
        /// time2: handleBlock, seal a new block, blockNumber(m_sealing) = n + 1, and broadcast the
        /// prepare request time3: callback onViewChange, reset the sealed block time4: seal again,
        /// blockNumber(m_sealing) = n + 1 the result is: generate two block with the same block in
        /// a period solution: if there has been  a higher sealed block, return directly without
        /// reset
        {
            WriteGuard l(x_sealing);
            if (m_sealing.block->isSealed() && shouldHandleBlock())
            {
                PBFTSEALER_LOG(DEBUG)
                    << LOG_DESC("sealing block have already been sealed and should be handled")
                    << LOG_KV("sealingNumber", m_sealing.block->blockHeader().number())
                    << LOG_KV("curNum", m_blockChain->number());
                return;
            }
            PBFTSEALER_LOG(DEBUG) << LOG_DESC("resetSealingBlock for viewchange")
                                  << LOG_KV(
                                         "sealingNumber", m_sealing.block->blockHeader().number())
                                  << LOG_KV("curNum", m_blockChain->number());
            resetSealingBlock();
        }
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }

    /// reset block for the next leader
    void resetBlockForNextLeader(dev::h256Hash const& filter)
    {
        {
            WriteGuard l(x_sealing);
            PBFTSEALER_LOG(DEBUG) << LOG_DESC("resetSealingBlock for nextLeader")
                                  << LOG_KV(
                                         "sealingNumber", m_sealing.block->blockHeader().number())
                                  << LOG_KV("curNum", m_blockChain->number());
            resetSealingBlock(filter, true);
        }
        m_signalled.notify_all();
        m_blockSignalled.notify_all();
    }

protected:
    std::shared_ptr<PBFTEngine> m_pbftEngine;
    /// the minimum number of transactions that caused timeout
    uint64_t m_lastTimeoutTx = 0;
    /// the maximum number of transactions that has been consensused without timeout
    uint64_t m_maxNoTimeoutTx = 0;
    /// timeout counter
    int64_t m_timeoutCount = 0;
    uint64_t m_lastBlockNumber = 0;
    bool m_enableDynamicBlockSize = true;
    float m_blockSizeIncreaseRatio = 0.5;
};
}  // namespace consensus
}  // namespace dev
