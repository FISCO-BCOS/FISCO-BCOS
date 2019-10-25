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
 * @brief : transaction pool
 * @file: TxPool.h
 * @author: yujiechen
 * @date: 2018-09-23
 */
#pragma once
#include "TransactionNonceCheck.h"
#include "TxPoolInterface.h"
#include <libblockchain/BlockChainInterface.h>
#include <libdevcore/ThreadPool.h>
#include <libethcore/Block.h>
#include <libethcore/Common.h>
#include <libethcore/Protocol.h>
#include <libethcore/Transaction.h>
#include <libp2p/P2PInterface.h>
#include <tbb/concurrent_queue.h>
#include <unordered_map>
#include <tbb/concurrent_unordered_set.h>

using namespace dev::eth;
using namespace dev::p2p;

#define TXPOOL_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("TXPOOL")

#if 0
template<typename Key>
	struct tbb_hash_compare {
   	h256::hash hasher;

		size_t operator()(const h256& k) {

			return hasher(k);
		}
		bool operator()(const h256& k1, const h256& k2) {
			return k1 == k2;
		}
	};


template<dev::h256>
class tbb::tbb_hash
{
public:
    tbb_hash() {}
    dev::h256::hash hasher;

    size_t operator()(const dev::h256& key) const
    {
        return hasher(key);
    }
};
}
#endif

namespace dev
{

namespace txpool
{
class TxPool;

struct TxPoolStatus
{
    size_t current;
    size_t dropped;
};

class H256Compare {
    dev::h256::hash hasher;
public:
    size_t operator()(const h256& x) const {
        return hasher(x);
    }
    // True if strings are equal
    bool equal(const h256& x, const h256& y) const {
            return x==y;
    }
};

class TxPoolNonceManager
{
public:
};
struct transactionCompare
{
    bool operator()(dev::eth::Transaction::Ptr _first, dev::eth::Transaction::Ptr _second) const
    {
        return _first->importTime() <= _second->importTime();
    }
};
class TxPool : public TxPoolInterface, public std::enable_shared_from_this<TxPool>
{
public:
    TxPool(std::shared_ptr<dev::p2p::P2PInterface> _p2pService,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        PROTOCOL_ID const& _protocolId, uint64_t const& _limit = 102400, uint64_t workThreads = 16)
      : m_service(_p2pService),
        m_blockChain(_blockChain),
        m_limit(_limit),
        m_protocolId(_protocolId)
    {
        assert(m_service && m_blockChain);
        if (m_protocolId == 0)
            BOOST_THROW_EXCEPTION(InvalidProtocolID() << errinfo_comment("ProtocolID must be > 0"));
        m_groupId = dev::eth::getGroupAndProtocol(m_protocolId).first;
        m_txNonceCheck = std::make_shared<TransactionNonceCheck>(m_blockChain);
        m_txpoolNonceChecker = std::make_shared<CommonTransactionNonceCheck>();
        m_workerPool =
            std::make_shared<dev::ThreadPool>("txPoolWorker-" + std::to_string(_protocolId), workThreads);
        m_txsCache = std::make_shared<tbb::concurrent_queue<dev::eth::Transaction::Ptr>>();
        m_submitThread = std::make_shared<std::thread>();
        m_totalTxsNum = m_blockChain->totalTransactionCount().first;
    }
    void start() override { startSubmitThread(); }
    void stop() override { stopSubmitThread(); }
    void setMaxBlockLimit(unsigned const& limit) { m_txNonceCheck->setBlockLimit(limit); }
    unsigned const& maxBlockLimit() { return m_txNonceCheck->maxBlockLimit(); }
    virtual ~TxPool()
    {
        // stop the submit thread
        clear();
    }

    /**
     * @brief submit a transaction through RPC/web3sdk
     *
     * @param _t : transaction
     * @return std::pair<h256, Address> : maps from transaction hash to contract address
     */
    std::pair<h256, Address> submit(dev::eth::Transaction::Ptr _tx) override;

    std::pair<h256, Address> submitTransactions();
    std::pair<h256, Address> submitTransactions(dev::eth::Transaction::Ptr _tx);

    /**
     * @brief Remove transaction from the queue
     * @param _txHash: Remove bad transaction from the queue
     */
    bool drop(h256 const& _txHash) override;
    bool dropBlockTrans(std::shared_ptr<dev::eth::Block> block) override;
    bool handleBadBlock(dev::eth::Block const& block) override;
    /**
     * @brief Get top transactions from the queue
     *
     * @param _limit : _limit Max number of transactions to return.
     * @param _avoid : Transactions to avoid returning.
     * @param _condition : The function return false to avoid transaction to return.
     * @return Transactions : up to _limit transactions
     */
    std::shared_ptr<dev::eth::Transactions> topTransactions(uint64_t const& _limit) override;
    std::shared_ptr<dev::eth::Transactions> topTransactions(
        uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid = false) override;
    std::shared_ptr<dev::eth::Transactions> topTransactionsCondition(
        uint64_t const& _limit, dev::h512 const& _nodeId) override;

    /// get all transactions(maybe blocksync module need this interface)
    std::shared_ptr<dev::eth::Transactions> pendingList() const override;
    /// get current transaction num
    size_t pendingSize() override;

    /// @returns the status of the transaction queue.
    TxPoolStatus status() const override;

    /// protocol id used when register handler to p2p module
    virtual PROTOCOL_ID const& getProtocolId() const override { return m_protocolId; }
    void setTxPoolLimit(uint64_t const& _limit) { m_limit = _limit; }

    /// Set transaction is known by a node
    void setTransactionIsKnownBy(h256 const& _txHash, h512 const& _nodeId) override;

    /// Is the transaction is known by the node ?
    bool isTransactionKnownBy(h256 const& _txHash, h512 const& _nodeId) override
    {
        auto p = m_transactionKnownBy.find(_txHash);
        if (p == m_transactionKnownBy.end())
            return false;
        return p->second.find(_nodeId) != p->second.end();
    }
    void setTransactionsAreKnownBy(
        std::vector<dev::h256> const& _txHashVec, h512 const& _nodeId) override;
    /// Is the transaction is known by someone
    bool isTransactionKnownBySomeone(h256 const& _txHash) override;
    SharedMutex& xtransactionKnownBy() override { return x_transactionKnownBy; }

    /// verify and set the sender of known transactions of sepcified block
    void verifyAndSetSenderForBlock(dev::eth::Block& block) override;
    bool txExists(dev::h256 const& txHash) override;

    bool isFull() override
    {
        // UpgradableGuard l(m_lock);
        return m_txsQueue.size() >= m_limit;
    }

    dev::ThreadPool::Ptr workerPool() { return m_workerPool; }

    uint64_t totalTransactionNum() override { return m_totalTxsNum; }

protected:
    /**
     * @brief : submit a transaction through p2p, Verify and add transaction to the queue
     * synchronously.
     *
     * @param _tx : Trasnaction data.
     * @param _ik : Set to Retry to force re-addinga transaction that was previously dropped.
     * @return ImportResult : Import result code.
     */
    ImportResult import(dev::eth::Transaction::Ptr _tx, IfDropped _ik = IfDropped::Ignore) override;
    ImportResult import(bytesConstRef _txBytes, IfDropped _ik = IfDropped::Ignore) override;
    /// verify transaction
    virtual ImportResult verify(Transaction::Ptr trans, IfDropped _ik = IfDropped::Ignore);
    /// interface for filter check
    virtual u256 filterCheck(Transaction::Ptr) const { return u256(0); };
    void clear();
    bool dropTransactions(std::shared_ptr<Block> block, bool needNotify = false);
    bool removeBlockKnowTrans(dev::eth::Block const& block);

private:
    void startSubmitThread();
    void stopSubmitThread();

    dev::eth::LocalisedTransactionReceipt::Ptr constructTransactionReceipt(
        dev::eth::Transaction::Ptr tx, dev::eth::TransactionReceipt::Ptr receipt,
        dev::eth::Block const& block, unsigned index);

    bool removeTrans(h256 const& _txHash, bool needTriggerCallback = false, std::shared_ptr<dev::eth::Block> block = std::shared_ptr<dev::eth::Block>(), size_t index = 0);
    bool insert(dev::eth::Transaction::Ptr _tx);
    void removeTransactionKnowBy(h256 const& _txHash);
    bool inline txPoolNonceCheck(dev::eth::Transaction::Ptr const& tx)
    {
        if (!m_txpoolNonceChecker->isNonceOk(*tx, true))
        {
            TXPOOL_LOG(WARNING) << LOG_DESC("txPool Nonce Check failed");
            return false;
        }
        return true;
    }

private:
    /// p2p module
    std::shared_ptr<dev::p2p::P2PInterface> m_service;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    std::shared_ptr<TransactionNonceCheck> m_txNonceCheck;
    /// nonce check for txpool
    std::shared_ptr<CommonTransactionNonceCheck> m_txpoolNonceChecker;
    /// Max number of pending transactions
    uint64_t m_limit;
    mutable SharedMutex m_lock;
    /// protocolId
    PROTOCOL_ID m_protocolId;
    GROUP_ID m_groupId;
    /// transaction queue
    using TransactionQueue = std::set<dev::eth::Transaction::Ptr, transactionCompare>;
    TransactionQueue m_txsQueue;
    std::unordered_map<h256, TransactionQueue::iterator> m_txsHash;
    /// hash of dropped transactions
    h256Hash m_dropped;
    /// Transaction is known by some peers
    mutable SharedMutex x_transactionKnownBy;
    std::unordered_map<h256, std::unordered_set<h512>> m_transactionKnownBy;

    tbb::concurrent_unordered_set<h256, H256Compare> m_delTransactions;

    dev::ThreadPool::Ptr m_workerPool;

    std::shared_ptr<tbb::concurrent_queue<dev::eth::Transaction::Ptr>> m_txsCache;
    std::shared_ptr<std::thread> m_submitThread;
    std::atomic_bool m_running = {false};
    std::condition_variable m_signalled;
    Mutex x_signalled;

    // total txsNum
    std::atomic<uint64_t> m_totalTxsNum = {0};
};
}  // namespace txpool
}  // namespace dev
