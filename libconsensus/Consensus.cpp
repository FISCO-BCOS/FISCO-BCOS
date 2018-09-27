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
 * @brief : implementation of consensus
 * @file: Consensus.cpp
 * @author: yujiechen
 * @date: 2018-09-27
 */
#include "Consensus.h"
#include <libethcore/LogEntry.h>
using namespace dev::sync;
using namespace dev::blockmanager;
namespace dev
{
namespace consensus
{
/// start the consensus module
void Consensus::start()
{
    if (m_startSeal)
    {
        LOG(WARNING) << "Consensus module has already been started, return directly";
        return;
    }
    doWork(false);
    startWorking();
}

/// doWork
void Consensus::doWork(bool wait)
{
    bool sealed = false;
    bool condition = true;
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.isSealed();
    /// TODO: m_remoteWorking
    bool t = false;
    if (!sealed && !m_blockSync->isSyncing() && m_syncTxPool.compare_exchange_strong(t, false))
        syncTxPool(m_maxBlockTransactions);
    /// sealing block
    rejigSealing();
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.isSealed();
    /// wait for 1s even the block has been sealed
    if (!m_syncTxPool && (wait || sealed))
    {
        std::unique_lock<std::mutex> l(x_signalled);
        m_signalled.wait_for(l, chrono::seconds(1));
    }
}

/// sync transactions from txPool
void Consensus::syncTxPool(uint64_t const& maxBlockTransactions)
{
    DEV_WRITE_GUARDED(x_sealing)
    {
        /// transaction already has been sealed
        if (m_sealing.isSealed())
        {
            LOG(INFO) << "Skipping txpool sync for a sealed block";
            return;
        }
        /// sealed transaction over maxBlockTransactions
        if (m_sealing.getTransactionSize() >= maxBlockTransactions)
        {
            LOG(INFO) << "Skipping txq sync for full block";
            return;
        }
        m_sealing.appendTransactions(m_txPool->topTransactions(maxBlockTransactions));
        if (m_sealing.getTransactionSize() >= maxBlockTransactions)
            m_syncTxPool = false;
    }
}

void Consensus::submitToEncode()
{
    /// import block
    resetCurrentTime();
    m_sealingHeader.setSealerList(minerList());
    m_sealingHeader.setSealer(nodeIdx());
    m_sealingHeader.setLogBloom(LogBloom());
    m_sealingHeader.setGasUsed(0);
    /// set transaction root, receipt root and state root
    m_sealingHeader.setRoots(
        m_sealing.getTransactionRoot(), hash256(BytesMap()), hash256(BytesMap()));
    m_sealingHeader.setExtraData(m_extraData);
}

/// sealing function
bool Consensus::generateSeal(bytes& _block_data)
{
    if (m_startSeal && getNodeAccountType() == NodeAccountType::MinerAccount && !isBlockSyncing() &&
        shouldSeal())
    {
        DEV_WRITE_GUARDED(x_sealing)
        {
            if (m_sealing.isSealed())
            {
                LOG(WARNING) << "Forbit to seal sealed block ...";
                return false;
            }
            uint64_t txNum = m_sealing.getTransactionSize();
            uint64_t packTxNum = m_maxBlockTransactions;
            updatePackedTxNum(packTxNum);
            bool t = true;
            if (txNum < packTxNum && !m_blockSync->isSyncing() &&
                m_syncTxPool.compare_exchange_strong(t, false))
                syncTxPool(packTxNum);
            txNum = m_sealing.getTransactionSize();
            if (shouldWaitForNextInterval())
            {
                LOG(DEBUG) << "Wait for next interval, tx:" << txNum;
                return false;
            }
            submitToEncode();
            m_sealingHeader = m_sealing.blockHeader();
            try
            {
                m_sealing.encode(_block_data);
            }
            catch (std::exception& e)
            {
                LOG(ERROR) << "ERROR: sealBlock failed";
                return false;
            }
            return true;
        }
    }
    return false;
}

/// check whether the blocksync module is syncing
bool Consensus::isBlockSyncing()
{
    SyncStatus state = m_blockSync->status();
    return (state.state != SyncState::Idle && state.state != SyncState::NewBlocks);
}

/// stop the consensus module
void Consensus::stop()
{
    m_startSeal = false;
    doneWorking();
    stopWorking();
}
}  // namespace consensus
}  // namespace dev
