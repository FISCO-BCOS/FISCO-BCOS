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
 * @file: Consensus.h
 * @author: yujiechen
 * @date: 2018-09-27
 *
 * @ author: yujiechen
 * @ date: 2018-10-26
 * @ file: Sealer.h
 * @ modification: rename Consensus.h to Sealer.h
 */
#pragma once
#include "ConsensusEngineBase.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/Worker.h>
#include <libethcore/Block.h>
#include <libethcore/Exceptions.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPool.h>
#include <libtxpool/TxPoolInterface.h>
namespace dev
{
namespace consensus
{
class Sealer : public Worker, public std::enable_shared_from_this<Sealer>
{
public:
    /**
     * @param _service: p2p service module
     * @param _txPool: transaction pool module
     * @param _blockChain: block chain module
     * @param _blockSync: block sync module
     * @param _blockVerifier: block verifier module
     * @param _protocolId: protocolId
     * @param _minerList: miners to generate and execute block
     */
    Sealer(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync)
      : Worker("Sealer", 0),
        m_txPool(_txPool),
        m_blockSync(_blockSync),
        m_blockChain(_blockChain),
        m_consensusEngine(nullptr)
    {
        assert(m_txPool && m_blockSync && m_blockChain);
        /// register a handler to be called once new transactions imported
        m_tqReady = m_txPool->onReady([=]() { this->onTransactionQueueReady(); });
        m_blockSubmitted = m_blockChain->onReady([=]() { this->onBlockChanged(); });
    }

    virtual ~Sealer() noexcept { stop(); }
    /// start the Sealer module
    virtual void start();
    /// stop the Sealer module
    virtual void stop();
    /// Magically called when m_tq needs syncing. Be nice and don't block.
    virtual void onTransactionQueueReady()
    {
        m_syncTxPool = true;
        m_signalled.notify_all();
    }
    virtual void onBlockChanged()
    {
        m_syncBlock = true;
        m_signalled.notify_all();
    }

    /// set the max number of transactions in a block
    void setMaxBlockTransactions(uint64_t const& _maxBlockTransactions)
    {
        m_maxBlockTransactions = _maxBlockTransactions;
    }
    /// get the max number of transactions in a block
    uint64_t maxBlockTransactions() const { return m_maxBlockTransactions; }
    void setExtraData(std::vector<bytes> const& _extra) { m_extraData = _extra; }
    std::vector<bytes> const& extraData() const { return m_extraData; }

    bool inline shouldResetSealing()
    {
        return (m_sealing.block.isSealed() ||
                m_sealing.block.blockHeader().number() <= m_blockChain->number());
    }
    /// return the pointer of ConsensusInterface to access common interfaces
    std::shared_ptr<dev::consensus::ConsensusInterface> const consensusEngine()
    {
        return m_consensusEngine;
    }

protected:
    void reportNewBlock();
    /// sealing block
    virtual bool shouldSeal();
    virtual bool shouldWait(bool const& wait) const;
    /// load transactions from transaction pool
    void loadTransactions(uint64_t const& transToFetch);
    virtual uint64_t calculateMaxPackTxNum() { return m_maxBlockTransactions; }
    virtual bool checkTxsEnough(uint64_t maxTxsCanSeal)
    {
        uint64_t tx_num = m_sealing.block.getTransactionSize();
        bool enough = (tx_num >= maxTxsCanSeal) || reachBlockIntervalTime();
        if (enough)
        {
            SEAL_LOG(DEBUG) << "[#checkTxsEnough] Tx enough: [txNum]: " << tx_num << std::endl;
            m_syncTxPool = false;
        }
        return enough;
    }

    virtual bool reachBlockIntervalTime() { return false; }
    virtual void handleBlock() {}
    virtual void doWork(bool wait);
    void doWork() override { doWork(true); }
    bool isBlockSyncing();
    inline void resetSealingBlock() { resetSealingBlock(m_sealing); }
    void resetSealingBlock(Sealing& sealing);
    void resetBlock(dev::eth::Block& block);
    void resetSealingHeader(dev::eth::BlockHeader& header);
    /// reset timestamp of block header
    void resetCurrentTime()
    {
        uint64_t parentTime =
            m_blockChain->getBlockByNumber(m_blockChain->number())->header().timestamp();
        m_sealing.block.header().setTimestamp(std::max(parentTime + 1, utcTime()));
    }

protected:
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block-sync module
    std::shared_ptr<dev::sync::SyncInterface> m_blockSync;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::shared_ptr<dev::consensus::ConsensusInterface> m_consensusEngine;

    uint64_t m_maxBlockTransactions = 1000;
    /// current sealing block(include block, transaction set of block and execute context)
    Sealing m_sealing;
    /// lock on m_sealing
    mutable SharedMutex x_sealing;
    /// extra data
    std::vector<bytes> m_extraData;

    /// atomic value represents that whether is calling syncTransactionQueue now
    /// signal to notify all thread to work
    std::condition_variable m_signalled;
    /// mutex to access m_signalled
    Mutex x_signalled;
    std::atomic<bool> m_syncTxPool = {false};
    /// a new block has been submitted to the blockchain
    std::atomic<bool> m_syncBlock = {false};

    ///< Has the remote worker recently been reset?
    bool m_remoteWorking = false;
    /// True if we /should/ be sealing.
    bool m_startConsensus = false;

    /// handler
    Handler<> m_tqReady;
    Handler<> m_blockSubmitted;
};
}  // namespace consensus
}  // namespace dev
