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
#include <libprotocol/LogEntry.h>
#include <libsync/SyncStatus.h>
using namespace std;
using namespace bcos::sync;
using namespace bcos::blockverifier;
using namespace bcos::protocol;
using namespace bcos::p2p;
using namespace bcos::consensus;

/// start the Sealer module
void Sealer::start()
{
    if (m_startConsensus)
    {
        SEAL_LOG(WARNING) << "[Sealer module has already been started]";
        return;
    }
    SEAL_LOG(INFO) << "[Start sealer module]";
    resetSealingBlock();
    m_consensusEngine->reportBlock(*(m_blockChain->getBlockByNumber(m_blockChain->number())));
    m_maxBlockCanSeal = m_consensusEngine->maxBlockTransactions();
    m_syncBlock = false;
    /// start  a thread to execute executeTask()&&taskProcessLoop()
    startWorking();
    m_startConsensus = true;
}

bool Sealer::shouldSeal()
{
    bool sealed = false;
    {
        ReadGuard l(x_sealing);
        sealed = m_sealing.block->isSealed();
    }
    return (!sealed && m_startConsensus &&
            m_consensusEngine->accountType() == NodeAccountType::SealerAccount &&
            !isBlockSyncing());
}

void Sealer::reportNewBlock()
{
    bool t = true;
    if (m_syncBlock.compare_exchange_strong(t, false))
    {
        shared_ptr<bcos::protocol::Block> p_block =
            m_blockChain->getBlockByNumber(m_blockChain->number());
        if (!p_block)
        {
            LOG(ERROR) << "[reportNewBlock] empty block";
            return;
        }
        m_consensusEngine->reportBlock(*p_block);
        WriteGuard l(x_sealing);
        {
            if (shouldResetSealing())
            {
                SEAL_LOG(DEBUG) << "[reportNewBlock] Reset sealing: [number]:  "
                                << m_blockChain->number()
                                << ", sealing number:" << m_sealing.block->blockHeader().number();
                resetSealingBlock();
            }
        }
    }
}

bool Sealer::shouldWait(bool const& wait) const
{
    return !m_syncBlock && wait;
}

void Sealer::executeTask(bool wait)
{
    reportNewBlock();
    if (shouldSeal() && m_startConsensus.load())
    {
        WriteGuard l(x_sealing);
        {
            /// get current transaction num
            uint64_t tx_num = m_sealing.block->getTransactionSize();

            /// add this to in case of unlimited-loop
            if (m_txPool->status().current == 0)
            {
                m_syncTxPool = false;
            }
            else
            {
                m_syncTxPool = true;
            }
            auto maxTxsPerBlock = maxBlockCanSeal();
            /// load transaction from transaction queue
            if (maxTxsPerBlock > tx_num && m_syncTxPool == true && !reachBlockIntervalTime())
                loadTransactions(maxTxsPerBlock - tx_num);
            /// check enough or reach block interval
            if (!checkTxsEnough(maxTxsPerBlock))
            {
                ///< 10 milliseconds to next loop
                boost::unique_lock<boost::mutex> l(x_signalled);
                m_signalled.wait_for(l, boost::chrono::milliseconds(1));
                return;
            }
            if (shouldHandleBlock())
                handleBlock();
        }
    }
    if (shouldWait(wait))
    {
        boost::unique_lock<boost::mutex> l(x_blocksignalled);
        m_blockSignalled.wait_for(l, boost::chrono::milliseconds(10));
    }
}

/**
 * @brief: load transactions from the transaction pool
 * @param transToFetch: max transactions to fetch
 */
void Sealer::loadTransactions(uint64_t const& transToFetch)
{
    /// fetch transactions and update m_transactionSet
    m_sealing.block->appendTransactions(
        m_txPool->topTransactions(transToFetch, m_sealing.m_transactionSet, true));
}

/// check whether the blocksync module is syncing
bool Sealer::isBlockSyncing()
{
    SyncStatus state = m_blockSync->status();
    return (state.state != SyncState::Idle);
}

/**
 * @brief : reset specified sealing block by generating an empty block
 *
 * @param sealing :  the block should be resetted
 * @param filter : the tx hashes of transactions that should't be packeted into sealing block when
 * loadTransactions(used to set m_transactionSet)
 * @param resetNextLeader : reset realing for the next leader or not ? default is false.
 *                          true: reset sealing for the next leader; the block number of the sealing
 * header should be reset to the current block number add 2 false: reset sealing for the current
 * leader; the sealing header should be populated from the current block
 */
void Sealer::resetSealingBlock(Sealing& sealing, h256Hash const& filter, bool resetNextLeader)
{
    resetBlock(sealing.block, resetNextLeader);
    sealing.m_transactionSet = filter;
    sealing.p_execContext = nullptr;
}

/**
 * @brief : reset specified block according to 'resetNextLeader' option
 *
 * @param block : the block that should be resetted
 * @param resetNextLeader: reset the block for the next leader or not ? default is false.
 *                         true: reset block for the next leader; the block number of the block
 * header should be reset to the current block number add 2 false: reset block for the current
 * leader; the block header should be populated from the current block
 */
void Sealer::resetBlock(std::shared_ptr<bcos::protocol::Block> block, bool resetNextLeader)
{
    /// reset block for the next leader:
    /// 1. clear the block; 2. set the block number to current block number add 2
    if (resetNextLeader)
    {
        SEAL_LOG(DEBUG) << "reset nextleader number to:" << (m_blockChain->number() + 2);
        block->resetCurrentBlock();
        block->header().setNumber(m_blockChain->number() + 2);
    }
    /// reset block for current leader:
    /// 1. clear the block; 2. populate header from the highest block
    else
    {
        auto highestBlock = m_blockChain->getBlockByNumber(m_blockChain->number());
        if (!highestBlock)
        {  // impossible so exit
            SEAL_LOG(FATAL) << LOG_DESC("exit because can't get highest block")
                            << LOG_KV("number", m_blockChain->number());
        }
        block->resetCurrentBlock(highestBlock->blockHeader());
        SEAL_LOG(DEBUG) << "resetCurrentBlock to"
                        << LOG_KV("sealingNum", block->blockHeader().number());
    }
}

/**
 * @brief : set some important fields for specified block header (called by PBFTSealer after load
 * transactions finished)
 *
 * @param header : the block header should be setted
 * the resetted fields including to:
 * 1. block import time;
 * 2. sealer list: reset to current leader list
 * 3. sealer: reset to the idx of the block generator
 */
void Sealer::resetSealingHeader(BlockHeader& header)
{
    /// import block
    resetCurrentTime();
    header.setSealerList(m_consensusEngine->consensusList());
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
        return;
    }
    SEAL_LOG(INFO) << "Stop sealer module...";
    m_startConsensus = false;
    doneWorking();
    if (isWorking())
    {
        stopWorking();
        // will not restart worker, so terminate it
        terminate();
    }
}
