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
 * @brief: fake block-chain
 * @file: FakeBlockChain.h
 * @author: yujiechen
 * @date: 2018-09-25
 */
#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libledger/LedgerParamInterface.h>
#include <libp2p/P2PMessageFactory.h>
#include <libp2p/Service.h>
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libprotocol/FakeBlock.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;
using namespace bcos::p2p;
using namespace bcos::blockchain;

namespace bcos
{
namespace test
{
class FakeService : public bcos::p2p::Service
{
public:
    FakeService() : Service()
    {
        m_messageFactory = std::make_shared<bcos::p2p::P2PMessageFactory>();
    }
    void setSessionInfos(bcos::p2p::P2PSessionInfos& sessionInfos)
    {
        m_sessionInfos = sessionInfos;
    }
    void appendSessionInfo(P2PSessionInfo const& info) { m_sessionInfos.push_back(info); }
    void clearSessionInfo() { m_sessionInfos.clear(); }
    P2PSessionInfos sessionInfosByProtocolID(PROTOCOL_ID) const override { return m_sessionInfos; }

    void asyncSendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message, CallbackFuncWithSession,
        bcos::p2p::Options) override
    {
        if (m_asyncSend.count(nodeID))
            m_asyncSend[nodeID]++;
        else
            m_asyncSend[nodeID] = 1;
        m_asyncSendMsgs[nodeID] = message;
    }
    size_t getAsyncSendSizeByNodeID(NodeID const& nodeID)
    {
        if (!m_asyncSend.count(nodeID))
            return 0;
        return m_asyncSend[nodeID];
    }

    P2PMessage::Ptr getAsyncSendMessageByNodeID(NodeID const& nodeID)
    {
        auto msg = m_asyncSendMsgs.find(nodeID);
        if (msg == m_asyncSendMsgs.end())
            return nullptr;
        return msg->second;
    }

    void clearMessageByNodeID(NodeID const& nodeID)
    {
        if (m_asyncSendMsgs.count(nodeID))
        {
            m_asyncSendMsgs.erase(nodeID);
        }
        m_asyncSend.erase(nodeID);
    }

    std::shared_ptr<bcos::p2p::P2PSession> getP2PSessionByNodeId(NodeID const&) override
    {
        return std::make_shared<bcos::p2p::P2PSession>();
    }
    void setConnected() { m_connected = true; }
    bool isConnected(NodeID const&) const override { return m_connected; }
    std::shared_ptr<bcos::p2p::P2PMessageFactory> p2pMessageFactory() override
    {
        return m_messageFactory;
    }

private:
    P2PSessionInfos m_sessionInfos;
    std::map<NodeID, size_t> m_asyncSend;
    std::map<NodeID, P2PMessage::Ptr> m_asyncSendMsgs;
    std::shared_ptr<bcos::p2p::P2PMessageFactory> m_messageFactory;
    bool m_connected = false;
};
class FakeTxPool : public TxPool
{
public:
    FakeTxPool(std::shared_ptr<bcos::p2p::Service> _p2pService,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        uint64_t const& _limit = 102400,
        int32_t const& _protocolId = bcos::protocol::ProtocolID::TxPool)
      : TxPool(_p2pService, _blockChain, _protocolId, _limit)
    {}
    std::pair<h256, Address> submitTransactions(bcos::protocol::Transaction::Ptr _tx) override
    {
        return TxPool::submitTransactions(_tx);
    };

    ImportResult import(
        bcos::protocol::Transaction::Ptr _tx, IfDropped _ik = IfDropped::Ignore) override
    {
        return TxPool::import(_tx, _ik);
    }
};

class FakeBlockChain : public BlockChainInterface
{
public:
    FakeBlockChain(
        uint64_t _blockNum, size_t const& transSize = 5, KeyPair const& keyPair = KeyPair::create())
      : m_blockNumber(_blockNum)
    {
        m_keyPair = keyPair;
        FakeTheBlockChain(_blockNum, transSize);
    }

    ~FakeBlockChain() {}
    /// fake the block chain
    void FakeTheBlockChain(uint64_t _blockNum, size_t const& trans_size)
    {
        m_blockChain.clear();
        m_blockHash.clear();
        for (uint64_t blockHeight = 0; blockHeight < _blockNum; blockHeight++)
        {
            FakeBlock fake_block(trans_size, m_keyPair);
            if (blockHeight > 0)
            {
                fake_block.m_block->header().setParentHash(
                    m_blockChain[blockHeight - 1]->headerHash());
                fake_block.m_block->header().setNumber(blockHeight);
            }
            m_blockHash[fake_block.m_block->header().hash()] = blockHeight;
            m_blockChain.push_back(std::make_shared<Block>(*fake_block.m_block));
        }
    }

    int64_t number() override { return m_blockNumber - 1; }
    void setBlockNumber(int64_t const& number) { m_blockNumber = number; }
    std::shared_ptr<std::vector<bcos::protocol::NonceKeyType>> getNonces(
        int64_t _blockNumber) override
    {
        std::shared_ptr<std::vector<bcos::protocol::NonceKeyType>> nonceVector =
            std::make_shared<std::vector<bcos::protocol::NonceKeyType>>();
        auto pBlock = getBlockByNumber(_blockNumber);
        for (auto trans : *(pBlock->transactions()))
        {
            nonceVector->push_back(
                boost::lexical_cast<bcos::protocol::NonceKeyType>(trans->nonce()));
        }
        return nonceVector;
    }
    std::pair<int64_t, int64_t> totalTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber - 1);
    }

    std::pair<int64_t, int64_t> totalFailedTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber - 1);
    }

    bcos::h256 numberHash(int64_t _i) override
    {
        if ((size_t)_i >= m_blockChain.size())
        {
            return h256();
        }
        return m_blockChain[_i]->headerHash();
    }

    std::shared_ptr<bcos::protocol::Block> getBlockByHash(
        bcos::h256 const& _blockHash, int64_t = -1) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    std::shared_ptr<bcos::protocol::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }

    std::shared_ptr<bcos::bytes> getBlockRLPByNumber(int64_t _i) override
    {
        auto blockHash = numberHash(_i);
        if (m_blockHash.count(blockHash))
            return m_blockChain[m_blockHash[blockHash]]->rlpP();
        return nullptr;
    }

    bcos::protocol::Transaction::Ptr getTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<Transaction>();
    }
    bcos::protocol::LocalisedTransaction::Ptr getLocalisedTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<LocalisedTransaction>();
    }
    bcos::protocol::TransactionReceipt::Ptr getTransactionReceiptByHash(bcos::h256 const&) override
    {
        return std::make_shared<TransactionReceipt>();
    }

    bcos::protocol::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        bcos::h256 const&) override
    {
        return std::make_shared<LocalisedTransactionReceipt>(
            TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
    }
    std::pair<LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(bcos::h256 const&) override
    {
        return std::make_pair(std::make_shared<LocalisedTransaction>(),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    std::pair<bcos::protocol::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        bcos::h256 const& _txHash, bcos::protocol::LocalisedTransaction&) override
    {
        (void)_txHash;
        return std::make_pair(std::make_shared<LocalisedTransactionReceipt>(
                                  bcos::protocol::TransactionException::None),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    CommitResult commitBlock(std::shared_ptr<bcos::protocol::Block> block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext>) override
    {
        block->header().setParentHash(m_blockChain[m_blockNumber - 1]->header().hash());
        block->header().setNumber(m_blockNumber);
        std::shared_ptr<Block> p_block = std::make_shared<Block>(*block);
        m_blockChain.push_back(p_block);
        m_blockHash[p_block->blockHeader().hash()] = m_blockNumber;
        m_blockNumber += 1;
        m_totalTransactionCount += block->transactions()->size();
        return CommitResult::OK;
    }

    bcos::bytes getCode(bcos::Address) override { return bytes(); }
    bool checkAndBuildGenesisBlock(
        std::shared_ptr<bcos::ledger::LedgerParamInterface>, bool = true) override
    {
        return true;
    }
    std::string getSystemConfigByKey(std::string const&, int64_t) override { return "300000000"; };
    bcos::h512s sealerList() override { return m_sealerList; }
    bcos::h512s observerList() override { return m_observerList; }
    void setSealerList(bcos::h512s const& sealers) { m_sealerList = sealers; }
    void setObserverList(bcos::h512s const& observers) { m_observerList = observers; }

    std::map<h256, int64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    int64_t m_blockNumber;
    int64_t m_totalTransactionCount;
    KeyPair m_keyPair;
    bcos::h512s m_sealerList = bcos::h512s();
    bcos::h512s m_observerList = bcos::h512s();
};
class TxPoolFixture
{
public:
    TxPoolFixture(uint64_t blockNum = 5, size_t const& transSize = 5,
        Secret const& sec = KeyPair::create().secret())
    {
        FakeTxPoolFunc(blockNum, transSize, sec);
    }
    void FakeTxPoolFunc(uint64_t _blockNum, size_t const& transSize, Secret const& sec)
    {
        /// fake block manager
        m_blockChain = std::make_shared<FakeBlockChain>(_blockNum, transSize, sec);
        /// fake service of p2p module
        m_topicService = std::make_shared<FakeService>();
        std::shared_ptr<BlockChainInterface> blockChain =
            std::shared_ptr<BlockChainInterface>(m_blockChain);
        /// fake txpool
        PROTOCOL_ID protocol = getGroupProtoclID(1, bcos::protocol::ProtocolID::TxPool);
        m_txPool = std::make_shared<FakeTxPool>(m_topicService, blockChain, 1024000, protocol);
    }

    void setSessionData(std::string const&)
    {
#if 0
        for (auto session : m_host->sessions())
            dynamic_cast<FakeSessionForTest*>(session.second.get())->setDataContent(data_content);
#endif
    }
    std::shared_ptr<FakeTxPool> m_txPool;
    std::shared_ptr<FakeService> m_topicService;
    std::shared_ptr<FakeBlockChain> m_blockChain;
    Secret sec;
    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
};
}  // namespace test
}  // namespace bcos
