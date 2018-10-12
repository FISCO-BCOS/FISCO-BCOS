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
 * @file: Consensus.h
 * @author: yujiechen
 * @date: 2018-09-27
 */
#pragma once
#include "ConsensusInterface.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblocksync/SyncInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcore/Worker.h>
#include <libethcore/Block.h>
#include <libethcore/Protocol.h>
#include <libp2p/Service.h>
#include <libtxpool/TxPool.h>

namespace dev
{
class ConsensusStatus;
namespace consensus
{
class Consensus : public Worker, virtual ConsensusInterface
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
    Consensus(std::shared_ptr<dev::p2p::Service> _service,
        std::shared_ptr<dev::txpool::TxPool> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        int16_t const& _protocolId, h512s const& _minerList)
      : Worker("consensus", 0),
        m_service(_service),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockSync(_blockSync),
        m_blockVerifier(_blockVerifier),
        m_protocolId(_protocolId),
        m_minerList(_minerList)
    {
        assert(m_service && m_txPool && m_blockSync);
        if (m_protocolId == 0)
            BOOST_THROW_EXCEPTION(
                InvalidProtocolID() << errinfo_comment("Protocol id must be larger than 0"));
    }

    virtual ~Consensus() { stop(); }
    /// start the consensus module
    void start() override;
    /// stop the consensus module
    void stop() override;

    /// get miner list
    h512s minerList() const override { return m_minerList; }
    /// set the miner list
    void setMinerList(h512s const& _minerList) override { m_minerList = _minerList; }
    /// append miner
    void appendMiner(h512 const& _miner) override { m_minerList.push_back(_miner); }
    /// get status of consensus
    const ConsensusStatus consensusStatus() const override { return ConsensusStatus(); }

    /// protocol id used when register handler to p2p module
    int16_t const& protocolId() const { return m_protocolId; }
    /// set the max number of transactions in a block
    void setMaxBlockTransactions(uint64_t const& _maxBlockTransactions)
    {
        m_maxBlockTransactions = _maxBlockTransactions;
    }
    /// get the max number of transactions in a block
    uint64_t maxBlockTransactions() const { return m_maxBlockTransactions; }

    /// get account type
    ///@return NodeAccountType::MinerAccount: the node can generate and execute block
    ///@return NodeAccountType::ObserveAccout: the node can only sync block from other nodes
    NodeAccountType accountType() override { return m_accountType; }
    /// set the node account type
    void setNodeAccountType(dev::consensus::NodeAccountType const& _accountType) override
    {
        m_accountType = _accountType;
    }
    ///
    u256 nodeIdx() const override { return m_idx; }
    virtual void setNodeIdx(u256 const& _idx) override { m_idx = _idx; }
    void setExtraData(std::vector<bytes> const& _extra) { m_extraData = _extra; }
    std::vector<bytes> const& extraData() const { return m_extraData; }
    bool const& allowFutureBlocks() const { return m_allowFutureBlocks; }
    void setAllowFutureBlocks(bool isAllowFutureBlocks)
    {
        m_allowFutureBlocks = isAllowFutureBlocks;
    }

protected:
    /// sealing block
    virtual bool shouldSeal();
    virtual bool shouldWait(bool const& wait);
    virtual void reportBlock(BlockHeader const& blockHeader){};
    /// load transactions from transaction pool
    void loadTransactions(uint64_t const& transToFetch);
    virtual uint64_t calculateMaxPackTxNum() { return m_maxBlockTransactions; }
    virtual bool checkTxsEnough(uint64_t maxTxsCanSeal)
    {
        uint64_t tx_num = m_sealing.block.getTransactionSize();
        if (tx_num >= maxTxsCanSeal)
            m_syncTxPool = false;
        return (tx_num >= maxTxsCanSeal);
    }

    virtual void handleBlock() {}
    virtual void doWork(bool wait);
    void doWork() override { doWork(true); }
    bool isBlockSyncing();

    /// functions for usage
    void setSealingRoot(h256 const& trans_root, h256 const& receipt_root, h256 const& state_root)
    {
        /// set transaction root, receipt root and state root
        m_sealing.block.header().setRoots(trans_root, receipt_root, state_root);
    }

    void appendSealingExtraData(bytes const& _extra);
    void ResetSealingHeader();
    void ResetSealingBlock();
    dev::blockverifier::ExecutiveContext::Ptr executeBlock(Block& block);
    virtual void executeBlock() { m_sealing.p_execContext = executeBlock(m_sealing.block); }
    bool blockExists(h256 const& blockHash)
    {
        if (m_blockChain->getBlockByHash(blockHash) == nullptr)
            return false;
        return true;
    }
    virtual void checkBlockValid(Block const& block);
    bool encodeBlock(bytes& blockBytes);
    /// reset timestamp of block header
    void resetCurrentTime()
    {
        uint64_t parentTime =
            m_blockChain->getBlockByNumber(m_blockChain->number())->header().timestamp();
        m_sealing.block.header().setTimestamp(max(parentTime + 1, utcTime()));
    }

protected:
    /// p2p service handler
    std::shared_ptr<dev::p2p::Service> m_service;
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPool> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// handler of the block-sync module
    std::shared_ptr<dev::sync::SyncInterface> m_blockSync;
    /// handler of the block-verifier module
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;
    int16_t m_protocolId;
    /// extra data
    std::vector<bytes> m_extraData;
    /// type of this node (MinerAccount or ObserveAccount)
    NodeAccountType m_accountType;
    /// index of this node
    u256 m_idx = u256(0);
    /// miner list
    h512s m_minerList;
    uint64_t m_maxBlockTransactions = 1000;
    /// allow future blocks or not
    bool m_allowFutureBlocks = true;

    /// current sealing block(include block, transaction set of block and execute context)
    Sealing m_sealing;
    /// lock on m_sealing
    mutable SharedMutex x_sealing;
    /// atomic value represents that whether is calling syncTransactionQueue now
    std::atomic<bool> m_syncTxPool = {false};
    /// signal to notify all thread to work
    std::condition_variable m_signalled;
    /// mutex to access m_signalled
    Mutex x_signalled;
    ///< Has the remote worker recently been reset?
    bool m_remoteWorking = false;
    /// True if we /should/ be sealing.
    bool m_startConsensus = false;
};
}  // namespace consensus
}  // namespace dev
