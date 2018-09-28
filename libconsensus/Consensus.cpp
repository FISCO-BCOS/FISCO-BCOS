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

bool Consensus::shouldSeal()
{
    bool sealed;
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.sealing_block.isSealed();
    bool t = true;
    return (!sealed && m_startConsensus && getNodeAccountType() == NodeAccountType::MinerAccount &&
            !isBlockSyncing() && m_syncTxPool.compare_exchange_strong(t, false));
}

bool Consensus::shouldWait(bool const& wait)
{
    return !m_syncTxPool && (wait || m_sealing.sealing_block.isSealed());
}

/// doWork
void Consensus::doWork(bool wait)
{
    if (shouldSeal())
    {
        DEV_WRITE_GUARDED(x_sealing)
        {
            /// get current transaction num
            uint64_t tx_num = m_sealing.sealing_block.getTransactionSize();
            /// obtain the transaction num should be packed
            uint64_t max_blockCanSeal = calculateMaxPackTxNum();
            if (max_blockCanSeal < tx_num)
                return;
            /// load transaction from transaction queue
            loadTransactions(max_blockCanSeal, tx_num);
            /// check enough
            if (checkTxsEnough(max_blockCanSeal))
                return;
            handleBlock();
            /// wait for 1s even the block has been sealed
            if (shouldWait(wait))
            {
                std::unique_lock<std::mutex> l(x_signalled);
                m_signalled.wait_for(l, chrono::milliseconds(1));
            }
        }
    }
}

/// sync transactions from txPool
void Consensus::loadTransactions(uint64_t const& maxTransaction, uint64_t const& curTxsNum)
{
    uint64_t trans_toFetch = maxTransaction - curTxsNum;
    m_sealing.sealing_block.appendTransactions(m_txPool->topTransactions(trans_toFetch));
    if (m_sealing.sealing_block.getTransactionSize() >= maxTransaction)
        m_syncTxPool = false;
}

void inline Consensus::ResetSealingHeader()
{
    /// import block
    resetCurrentTime();
    m_sealing.sealing_block.header().setSealerList(minerList());
    m_sealing.sealing_block.header().setSealer(nodeIdx());
    m_sealing.sealing_block.header().setLogBloom(LogBloom());
    m_sealing.sealing_block.header().setGasUsed(0);
    m_sealing.sealing_block.header().setExtraData(m_extraData);
}

void inline Consensus::appendSealingExtraData(bytes const& _extra)
{
    m_sealing.sealing_block.header().appendExtraDataArray(_extra);
}

void inline Consensus::setSealingRoot(
    h256 const& trans_root, h256 const& receipt_root, h256 const& state_root)
{
    /// set transaction root, receipt root and state root
    m_sealing.sealing_block.header().setRoots(trans_root, receipt_root, state_root);
}
/// TODO: update m_sealing and receiptRoot
void Consensus::executeBlock() {}

bool Consensus::encodeBlock(bytes& blockBytes)
{
    try
    {
        m_sealing.sealing_block.encode(blockBytes);
        return true;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR: sealBlock failed";
        return false;
    }
}

/// check whether the blocksync module is syncing
bool inline Consensus::isBlockSyncing()
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
