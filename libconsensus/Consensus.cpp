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
using namespace dev::blockverifier;
using namespace dev::eth;
using namespace dev::p2p;
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
    resetSealingBlock();
    m_consensusEngine->reportBlock(
        m_blockChain->getBlockByNumber(m_blockChain->number())->blockHeader());
    m_syncBlock = false;
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    m_startConsensus = true;
}

bool Consensus::shouldSeal()
{
    bool sealed;
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.block.isSealed();
    return (!sealed && m_startConsensus &&
            m_consensusEngine->accountType() == NodeAccountType::MinerAccount && !isBlockSyncing());
}

void Consensus::reportNewBlock()
{
    bool t = true;
    if (m_syncBlock.compare_exchange_strong(t, false))
    {
        LOG(DEBUG) << "#### reportNewBlock compare_exchange_strong";
        DEV_WRITE_GUARDED(x_sealing)
        {
            m_consensusEngine->reportBlock(
                m_blockChain->getBlockByNumber(m_blockChain->number())->blockHeader());
            if (shouldResetSealing())
            {
                LOG(DEBUG) << "##### resetSealingBlock";
                resetSealingBlock();
            }
        }
    }
}

bool Consensus::shouldWait(bool const& wait) const
{
    return !m_syncBlock && wait;
}

void Consensus::doWork(bool wait)
{
    reportNewBlock();
    if (shouldSeal())
    {
        DEV_WRITE_GUARDED(x_sealing)
        {
            /// get current transaction num
            uint64_t tx_num = m_sealing.block.getTransactionSize();
            /// obtain the transaction num should be packed
            uint64_t max_blockCanSeal = calculateMaxPackTxNum();
            bool t = true;
            /// load transaction from transaction queue
            if (m_syncTxPool.compare_exchange_strong(t, false) || checkTxsEnough(max_blockCanSeal))
            {
                /// LOG(DEBUG) << "### load Transactions, tx_num:" << tx_num;
                /// LOG(DEBUG) << "### load Transactions, max_blockCanSeal:" << max_blockCanSeal;
                loadTransactions(max_blockCanSeal - tx_num);
                /// check enough or reach block interval
                if (!checkTxsEnough(max_blockCanSeal))
                {
                    ///< 10 milliseconds to next loop
                    std::unique_lock<std::mutex> l(x_signalled);
                    m_signalled.wait_for(l, std::chrono::milliseconds(1));
                    return;
                }
                handleBlock();
                /// wait for 1s even the block has been sealed
            }
        }
    }
    if (shouldWait(wait))
    {
        std::unique_lock<std::mutex> l(x_signalled);
        m_signalled.wait_for(l, std::chrono::milliseconds(10));
    }
}

/**
 * @brief: load transactions from the transaction pool
 * @param transToFetch: max transactions to fetch
 */
void Consensus::loadTransactions(uint64_t const& transToFetch)
{
    /// fetch transactions and update m_transactionSet
    m_sealing.block.appendTransactions(
        m_txPool->topTransactions(transToFetch, m_sealing.m_transactionSet, true));
}

/// check whether the blocksync module is syncing
bool Consensus::isBlockSyncing()
{
    SyncStatus state = m_blockSync->status();
    return (state.state != SyncState::Idle && state.state != SyncState::NewBlocks);
}

void Consensus::resetSealingBlock(Sealing& sealing)
{
    resetBlock(sealing.block);
    sealing.m_transactionSet.clear();
    sealing.p_execContext = nullptr;
}

void Consensus::resetBlock(Block& block)
{
    block.resetCurrentBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->header());
    /// LOG(DEBUG) << "##### m_blockChain->getBlockByNumber(m_blockChain->number():"
    ///           << m_blockChain->number();
    /// LOG(DEBUG) << "##### block number after reset:" << block.header().number();
}

void Consensus::resetSealingHeader(BlockHeader& header)
{
    /// import block
    resetCurrentTime();
    header.setSealerList(m_consensusEngine->minerList());
    header.setSealer(m_consensusEngine->nodeIdx());
    header.setLogBloom(LogBloom());
    header.setGasUsed(u256(0));
    header.setExtraData(m_extraData);
}

/// stop the consensus module
void Consensus::stop()
{
    if (m_startConsensus == false)
    {
        LOG(WARNING) << "Consensus module has already been stopped, return now";
        return;
    }
    m_startConsensus = false;
    doneWorking();
    stopWorking();
}
}  // namespace consensus
}  // namespace dev
