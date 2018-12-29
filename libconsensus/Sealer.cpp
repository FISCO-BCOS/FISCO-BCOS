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
 * @brief : implementation of Consensus
 * @file: Consensus.cpp
 * @author: yujiechen
 * @date: 2018-09-27
 *
 * @ author: yujiechen
 * @ date: 2018-10-26
 * @ file : Sealer.cpp
 * @ modification: rename Consensus.cpp to Sealer.cpp
 */
#include "Sealer.h"
#include <libethcore/LogEntry.h>
#include <libsync/SyncStatus.h>
using namespace dev::sync;
using namespace dev::blockverifier;
using namespace dev::eth;
using namespace dev::p2p;
namespace dev
{
namespace consensus
{
/// start the Sealer module
void Sealer::start()
{
    if (m_startConsensus)
    {
        SEAL_LOG(WARNING) << "[#Sealer module has already been started]" << std::endl;
        return;
    }
    SEAL_LOG(INFO) << "[#Start sealer module]" << std::endl;
    resetSealingBlock();
    m_consensusEngine->reportBlock(*(m_blockChain->getBlockByNumber(m_blockChain->number())));
    m_syncBlock = false;
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    m_startConsensus = true;
}

bool Sealer::shouldSeal()
{
    bool sealed;
    {
        DEV_READ_GUARDED(x_sealing)
        sealed = m_sealing.block.isSealed();
    }
    return (!sealed && m_startConsensus &&
            m_consensusEngine->accountType() == NodeAccountType::MinerAccount && !isBlockSyncing());
}

void Sealer::reportNewBlock()
{
    bool t = true;
    if (m_syncBlock.compare_exchange_strong(t, false))
    {
        std::shared_ptr<dev::eth::Block> p_block =
            m_blockChain->getBlockByNumber(m_blockChain->number());
        if (!p_block)
        {
            LOG(ERROR) << "[#reportNewBlock] empty block";
            return;
        }
        m_consensusEngine->reportBlock(*p_block);
        DEV_WRITE_GUARDED(x_sealing)
        {
            if (shouldResetSealing())
            {
                SEAL_LOG(DEBUG) << "[#reportNewBlock] Reset sealing: [number]:  "
                                << m_blockChain->number();
                resetSealingBlock();
            }
        }
    }
}

bool Sealer::shouldWait(bool const& wait) const
{
    return !m_syncBlock && wait;
}

void Sealer::doWork(bool wait)
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
            if (m_txPool->status().current > 0)
            {
                m_syncTxPool = true;
                m_signalled.notify_all();
                m_blockSignalled.notify_all();
            }
            else
                m_syncTxPool = false;
            bool t = true;
            /// load transaction from transaction queue
            if (m_syncTxPool == true && !reachBlockIntervalTime())
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
        }
    }
    if (shouldWait(wait))
    {
        std::unique_lock<std::mutex> l(x_blocksignalled);
        m_blockSignalled.wait_for(l, std::chrono::milliseconds(10));
    }
}

/**
 * @brief: load transactions from the transaction pool
 * @param transToFetch: max transactions to fetch
 */
void Sealer::loadTransactions(uint64_t const& transToFetch)
{
    /// fetch transactions and update m_transactionSet
    m_sealing.block.appendTransactions(
        m_txPool->topTransactions(transToFetch, m_sealing.m_transactionSet, true));
}

/// check whether the blocksync module is syncing
bool Sealer::isBlockSyncing()
{
    SyncStatus state = m_blockSync->status();
    return (state.state != SyncState::Idle);
}

void Sealer::resetSealingBlock(Sealing& sealing)
{
    resetBlock(sealing.block);
    sealing.m_transactionSet.clear();
    sealing.p_execContext = nullptr;
}

void Sealer::resetBlock(Block& block)
{
    block.resetCurrentBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->header());
}

void Sealer::resetSealingHeader(BlockHeader& header)
{
    /// import block
    resetCurrentTime();
    header.setSealerList(m_consensusEngine->minerList());
    header.setSealer(m_consensusEngine->nodeIdx());
    header.setLogBloom(LogBloom());
    header.setGasUsed(u256(0));
    header.setExtraData(m_extraData);
}

/// stop the Sealer module
void Sealer::stop()
{
    if (m_startConsensus == false)
    {
        SEAL_LOG(WARNING) << "[#Sealer module has already been stopped]" << std::endl;
        return;
    }
    SEAL_LOG(INFO) << "[#Stop sealer module...]" << std::endl;
    m_startConsensus = false;
    doneWorking();
    if (isWorking())
        stopWorking();
}
}  // namespace consensus
}  // namespace dev
