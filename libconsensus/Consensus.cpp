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
    if (m_startConsensus)
    {
        LOG(WARNING) << "Consensus module has already been started, return directly";
        return;
    }
    startWorking();
}

/// doWork
void Consensus::doWork(bool wait)
{
    bool sealed = false;
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.isSealed();
    /// TODO: m_remoteWorking
    bool t = false;
    if (!sealed && m_startConsensus && getNodeAccountType() == NodeAccountType::MinerAccount &&
        shouldSeal())
    {
        uint64_t tx_num;
        /// obtain the transaction num should be packed
        uint64_t max_block_can_seal = m_maxBlockTransactions;
        calculateMaxPackTxNum(max_block_can_seal);
        /// get current transaction num
        DEV_READ_GUARDED(x_sealing)
        tx_num = m_sealing.getTransactionSize();
        /// generate block
        bytes block_data;
        bool ret = generateBlock(block_data, tx_num, max_block_can_seal);
        if (ret == true)
        {
            handleBlock(m_sealingHeader, ref(block_data));
        }
    }
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
void Consensus::loadTransactions(uint64_t startIndex, uint64_t const& maxTransaction)
{
    DEV_WRITE_GUARDED(x_sealing)
    {
        /// transaction already has been sealed
        if (m_sealing.isSealed())
        {
            LOG(INFO) << "Skipping txpool sync for a sealed block";
            return;
        }
        m_sealing.appendTransactions(
            m_txPool->topTransactions(maxTransaction - startIndex + 1, startIndex));
        if (m_sealing.getTransactionSize() >= maxTransaction)
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
bool Consensus::generateBlock(
    bytes& _block_data, uint64_t const& tx_num, uint64_t const& max_block_can_seal)
{
    DEV_WRITE_GUARDED(x_sealing)
    {
        bool t = true;
        if (tx_num < max_block_can_seal && !m_blockSync->isSyncing() &&
            m_syncTxPool.compare_exchange_strong(t, false))
            loadTransactions(tx_num, max_block_can_seal);
        if (shouldWaitForNextInterval())
        {
            LOG(DEBUG) << "Wait for next interval, tx:" << m_sealing.getTransactionSize();
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
    }
    return true;
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
    m_startConsensus = false;
    doneWorking();
    stopWorking();
}
}  // namespace consensus
}  // namespace dev
