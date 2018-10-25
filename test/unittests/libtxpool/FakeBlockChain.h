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
#include <libtxpool/TxPool.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <test/unittests/libethcore/FakeBlock.h>
#include <test/unittests/libp2p/FakeHost.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
using namespace dev::txpool;
using namespace dev::blockchain;

namespace dev
{
namespace test
{
class FakeService : public Service
{
public:
    FakeService(std::shared_ptr<Host> _host, std::shared_ptr<P2PMsgHandler> _p2pMsgHandler)
      : Service(_host, _p2pMsgHandler)
    {}
    void setSessionInfos(SessionInfos& sessionInfos) { m_sessionInfos = sessionInfos; }
    void appendSessionInfo(SessionInfo const& info) { m_sessionInfos.push_back(info); }
    void clearSessionInfo() { m_sessionInfos.clear(); }
    SessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const { return m_sessionInfos; }

    void asyncSendMessageByNodeID(NodeID const& nodeID, Message::Ptr message,
        CallbackFunc callback = [](P2PException e, Message::Ptr msg) {},
        dev::p2p::Options const& options = dev::p2p::Options()) override
    {
        if (m_asyncSend.count(nodeID))
            m_asyncSend[nodeID]++;
        else
            m_asyncSend[nodeID] = 1;
    }
    size_t getAsyncSendSizeByNodeID(NodeID const& nodeID)
    {
        if (!m_asyncSend.count(nodeID))
            return 0;
        return m_asyncSend[nodeID];
    }
    void setConnected() { m_connected = true; }
    bool isConnected(NodeID const& nodeId) const { return m_connected; }

private:
    SessionInfos m_sessionInfos;
    std::map<NodeID, size_t> m_asyncSend;
    bool m_connected;
};
class FakeTxPool : public TxPool
{
public:
    FakeTxPool(std::shared_ptr<dev::p2p::Service> _p2pService,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        uint64_t const& _limit = 102400, int32_t const& _protocolId = dev::eth::ProtocolID::TxPool)
      : TxPool(_p2pService, _blockChain, _protocolId, _limit)
    {}
    ImportResult import(bytesConstRef _txBytes, IfDropped _ik = IfDropped::Ignore)
    {
        return TxPool::import(_txBytes, _ik);
    }
};

class FakeBlockChain : public BlockChainInterface
{
public:
    FakeBlockChain(uint64_t _blockNum, size_t const& transSize = 5) : m_blockNumber(_blockNum)
    {
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
            FakeBlock fake_block(trans_size);
            if (blockHeight > 0)
            {
                fake_block.m_blockHeader.setParentHash(m_blockChain[blockHeight - 1]->headerHash());
                fake_block.m_blockHeader.setNumber(blockHeight);
                fake_block.reEncodeDecode();
            }
            m_blockHash[fake_block.m_blockHeader.hash()] = blockHeight;
            m_blockChain.push_back(std::make_shared<Block>(fake_block.m_block));
        }
    }

    int64_t number() const { return m_blockNumber - 1; }

    dev::h256 numberHash(int64_t _i) const { return m_blockChain[_i]->headerHash(); }

    std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    virtual std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i)
    {
        return getBlockByHash(numberHash(_i));
    }

    dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override { return Transaction(); }
    dev::eth::TransactionReceipt getTransactionReceiptByHash(dev::h256 const& _txHash) override
    {
        return TransactionReceipt();
    }

    virtual void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>)
    {
        block.header().setParentHash(m_blockChain[m_blockNumber - 1]->header().hash());
        block.header().setNumber(m_blockNumber);
        std::shared_ptr<Block> p_block = std::make_shared<Block>(block);
        m_blockChain.push_back(p_block);
        m_blockHash[p_block->blockHeader().hash()] = m_blockNumber;
        m_blockNumber += 1;
    }
    std::map<h256, int64_t> m_blockHash;
    std::vector<std::shared_ptr<Block> > m_blockChain;
    int64_t m_blockNumber;
};
class TxPoolFixture
{
public:
    TxPoolFixture(uint64_t blockNum = 5, size_t const& transSize = 5)
    {
        FakeTxPoolFunc(blockNum, transSize);
    }

    void FakeTxPoolFunc(uint64_t _blockNum, size_t const& transSize)
    {
        /// fake block manager
        m_blockChain = std::make_shared<FakeBlockChain>(_blockNum, transSize);
        /// fake host of p2p module
        FakeHost* hostPtr = createFakeHostWithSession(clientVersion, listenIp, listenPort);
        m_host = std::shared_ptr<Host>(hostPtr);
        /// create p2pHandler
        m_p2pHandler = std::make_shared<P2PMsgHandler>();
        /// fake service of p2p module
        m_topicService = std::make_shared<FakeService>(m_host, m_p2pHandler);
        std::shared_ptr<BlockChainInterface> blockChain =
            std::shared_ptr<BlockChainInterface>(m_blockChain);
        /// fake txpool
        m_txPool = std::make_shared<FakeTxPool>(m_topicService, blockChain);
    }

    void setSessionData(std::string const& data_content)
    {
        for (auto session : m_host->sessions())
            dynamic_cast<FakeSessionForTest*>(session.second.get())->setDataContent(data_content);
    }
    std::shared_ptr<FakeTxPool> m_txPool;
    std::shared_ptr<FakeService> m_topicService;
    std::shared_ptr<P2PMsgHandler> m_p2pHandler;
    std::shared_ptr<FakeBlockChain> m_blockChain;
    std::shared_ptr<Host> m_host;
    std::string clientVersion = "2.0";
    std::string listenIp = "127.0.0.1";
    uint16_t listenPort = 30304;
};
}  // namespace test
}  // namespace dev
