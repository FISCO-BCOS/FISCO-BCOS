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
    /// start  a thread to execute doWork()&&workLoop()
    startWorking();
    reportBlock(m_blockChain->getBlockByNumber(m_blockChain->number())->blockHeader());
}

bool Consensus::shouldSeal()
{
    bool sealed;
    DEV_READ_GUARDED(x_sealing)
    sealed = m_sealing.block.isSealed();
    bool t = true;
    return (!sealed && m_startConsensus && accountType() == NodeAccountType::MinerAccount &&
            !isBlockSyncing() && m_syncTxPool.compare_exchange_strong(t, false));
}

bool Consensus::shouldWait(bool const& wait)
{
    return !m_syncTxPool && (wait || m_sealing.block.isSealed());
}

void Consensus::doWork(bool wait)
{
    if (shouldSeal())
    {
        DEV_WRITE_GUARDED(x_sealing)
        {
            /// get current transaction num
            uint64_t tx_num = m_sealing.block.getTransactionSize();
            /// obtain the transaction num should be packed
            uint64_t max_blockCanSeal = calculateMaxPackTxNum();
            /// load transaction from transaction queue
            if (max_blockCanSeal > 0 && tx_num < max_blockCanSeal)
                loadTransactions(max_blockCanSeal - tx_num);
            /// check enough
            if (!checkTxsEnough(max_blockCanSeal))
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

void Consensus::ResetSealingHeader()
{
    /// import block
    resetCurrentTime();
    m_sealing.block.header().setSealerList(minerList());
    m_sealing.block.header().setSealer(nodeIdx());
    m_sealing.block.header().setLogBloom(LogBloom());
    m_sealing.block.header().setGasUsed(u256(0));
    m_sealing.block.header().setExtraData(m_extraData);
}

void Consensus::ResetSealingBlock()
{
    m_sealing.block.resetCurrentBlock();
    ResetSealingHeader();
    m_sealing.m_transactionSet.clear();
}

void Consensus::appendSealingExtraData(bytes const& _extra)
{
    m_sealing.block.header().appendExtraDataArray(_extra);
}

/// update m_sealing and receiptRoot
dev::blockverifier::ExecutiveContext::Ptr Consensus::executeBlock(Block& block)
{
    std::unordered_map<Address, dev::eth::PrecompiledContract> contract;
    /// reset execute context
    return m_blockVerifier->executeBlock(block);
}

void Consensus::checkBlockValid(Block const& block)
{
    h256 block_hash = block.blockHeader().hash();
    /// check the timestamp
    if (block.blockHeader().timestamp() > u256(utcTime()) && !m_allowFutureBlocks)
    {
        LOG(ERROR) << "Future timestamp(now disabled) of block_hash = " << block_hash;
        BOOST_THROW_EXCEPTION(DisabledFutureTime() << errinfo_comment("Future time Disabled"));
    }
    /// check the block number
    if (block.blockHeader().number() <= m_blockChain->number())
    {
        LOG(ERROR) << "Old Block Height, block_hash = " << block_hash;
        BOOST_THROW_EXCEPTION(InvalidBlockHeight() << errinfo_comment("Invalid block height"));
    }
    /// check existence of this block (Must non-exist)
    if (blockExists(block_hash))
    {
        LOG(ERROR) << "Block Already Existed, drop now, block_hash = " << block_hash;
        BOOST_THROW_EXCEPTION(ExistedBlock() << errinfo_comment("Block Already Existed, drop now"));
    }
    /// check the existence of the parent block (Must exist)
    if (!blockExists(block.blockHeader().parentHash()))
    {
        LOG(ERROR) << "Parent Block Doesn't Exist, drop now, block_hash = " << block_hash;
        BOOST_THROW_EXCEPTION(ParentNoneExist() << errinfo_comment("Parent Block Doesn't Exist"));
    }
}

bool Consensus::encodeBlock(bytes& blockBytes)
{
    try
    {
        m_sealing.block.encode(blockBytes);
        return true;
    }
    catch (std::exception& e)
    {
        LOG(ERROR) << "ERROR: sealBlock failed";
        return false;
    }
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
