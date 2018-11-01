/**
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
 *
 * @brief : main Fakes of p2p module
 *
 * @file FakeModule.h
 * @author: caryliao
 * @date 2018-10-26
 */
#pragma once

#include <jsonrpccpp/common/exception.h>
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libconsensus/ConsensusInterface.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libexecutive/ExecutionResult.h>
#include <libledger/LedgerManager.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <test/tools/libutils/Common.h>
#include <test/unittests/libconsensus/FakePBFTEngine.h>

using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::sync;
using namespace dev::ledger;

namespace dev
{
namespace test
{
class MockService : public Service
{
public:
    MockService(std::shared_ptr<Host> _host, std::shared_ptr<P2PMsgHandler> _p2pMsgHandler)
      : Service(_host, _p2pMsgHandler)
    {
        NodeID nodeID = h512(100);
        NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30310);
        SessionInfo info(nodeID, m_endpoint, std::vector<std::string>());
        std::vector<std::string> topics;
        std::string topic = "Topic1";
        topics.push_back(topic);
        m_sessionInfos.push_back(SessionInfo(nodeID, m_endpoint, topics));
    }

    virtual SessionInfos sessionInfos() const override { return m_sessionInfos; }
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
        m_asyncSendMsgs[nodeID] = message;
    }
    size_t getAsyncSendSizeByNodeID(NodeID const& nodeID)
    {
        if (!m_asyncSend.count(nodeID))
            return 0;
        return m_asyncSend[nodeID];
    }

    Message::Ptr getAsyncSendMessageByNodeID(NodeID const& nodeID)
    {
        auto msg = m_asyncSendMsgs.find(nodeID);
        if (msg == m_asyncSendMsgs.end())
            return nullptr;
        return msg->second;
    }

    void setConnected() { m_connected = true; }
    bool isConnected(NodeID const& nodeId) const { return m_connected; }

private:
    SessionInfos m_sessionInfos;
    std::map<NodeID, size_t> m_asyncSend;
    std::map<NodeID, Message::Ptr> m_asyncSendMsgs;
    bool m_connected;
};

class MockBlockChain : public BlockChainInterface
{
public:
    MockBlockChain()
    {
        m_blockNumber = 1;
        blockHash = h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
        blockHeader.setNumber(1);
        blockHeader.setParentHash(h256(0x1));
        blockHeader.setLogBloom(h2048(0x2));
        blockHeader.setRoots(h256(0x3), h256(0x4), h256(0x5));
        blockHeader.setSealer(u256(1));
        extraData = bytes();
        byte b = 10;
        extraData.push_back(b);
        blockHeader.appendExtraDataArray(extraData);
        blockHeader.setGasLimit(u256(9));
        blockHeader.setGasUsed(u256(8));
        blockHeader.setTimestamp(9);

        createTransaction();
        transactions.push_back(transaction);

        block.setBlockHeader(blockHeader);
        block.setTransactions(transactions);

        m_blockHash[blockHash] = 0;
        m_blockChain.push_back(std::make_shared<Block>(block));
    }

    virtual ~MockBlockChain() {}

    virtual int64_t number() override { return m_blockNumber; }

    void createTransaction()
    {
        bytes rlpBytes = fromHex(
            "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
            "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
            "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
            "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
            "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
            "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        transaction = Transaction(d, eth::CheckTransaction::Everything);
    }
    dev::h256 numberHash(int64_t _i) { return blockHash; }

    virtual std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    virtual dev::eth::LocalisedTransaction getLocalisedTxByHash(dev::h256 const& _txHash) override
    {
        return LocalisedTransaction(transaction, blockHash, 0, 1);
    }

    dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override { return Transaction(); }

    virtual dev::eth::TransactionReceipt getTransactionReceiptByHash(
        dev::h256 const& _txHash) override
    {
        LogEntries entries;
        LogEntry entry;
        entry.address = Address(0x2000);
        entry.data = bytes();
        entry.topics = h256s();
        entries.push_back(entry);
        return TransactionReceipt(h256(0x3), u256(8), entries, 0, bytes(), Address(0x1000));
    }

    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }

    void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        m_blockHash[block.blockHeader().hash()] = block.blockHeader().number();
        m_blockChain.push_back(std::make_shared<Block>(block));
        m_blockNumber = block.blockHeader().number() + 1;
        m_onReady();
    }

    BlockHeader blockHeader;
    Transactions transactions;
    Transaction transaction;
    bytes extraData;
    Block block;
    h256 blockHash;
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
};

class MockBlockVerifier : public BlockVerifierInterface
{
public:
    MockBlockVerifier()
    {
        m_executiveContext = std::make_shared<ExecutiveContext>();
        std::srand(std::time(nullptr));
    };
    virtual ~MockBlockVerifier(){};
    std::shared_ptr<ExecutiveContext> executeBlock(dev::eth::Block& block) override
    {
        usleep(1000 * (block.getTransactionSize()));
        return m_executiveContext;
    };
    virtual std::pair<dev::executive::ExecutionResult, dev::eth::TransactionReceipt>
    executeTransaction(
        const dev::eth::BlockHeader& blockHeader, dev::eth::Transaction const& _t) override
    {
        dev::executive::ExecutionResult res;
        dev::eth::TransactionReceipt reciept;
        return std::make_pair(res, reciept);
    }

private:
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

class MockTxPool : public TxPoolInterface
{
public:
    MockTxPool()
    {
        bytes rlpBytes = fromHex(
            "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
            "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
            "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
            "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
            "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
            "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        transaction = Transaction(d, eth::CheckTransaction::Everything);
        transactions.push_back(transaction);
    };
    virtual ~MockTxPool(){};
    virtual dev::eth::Transactions pendingList() const override { return transactions; };
    virtual size_t pendingSize() override { return 1; }
    virtual dev::eth::Transactions topTransactions(
        uint64_t const& _limit, h256Hash& _avoid, bool _updateAvoid = false) override
    {
        return transactions;
    }
    virtual dev::eth::Transactions topTransactions(uint64_t const& _limit) override
    {
        return transactions;
    }
    virtual bool drop(h256 const& _txHash) override { return true; }
    virtual bool dropBlockTrans(dev::eth::Block const& block) override { return true; }
    virtual PROTOCOL_ID const& getProtocolId() const override { return protocolId; }
    virtual TxPoolStatus status() const override
    {
        TxPoolStatus status;
        status.current = 0;
        status.dropped = 0;
        return status;
    }
    virtual std::pair<h256, Address> submit(dev::eth::Transaction& _tx) override
    {
        return make_pair(_tx.sha3(), toAddress(_tx.from(), _tx.nonce()));
    }
    virtual dev::eth::ImportResult import(
        dev::eth::Transaction& _tx, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return ImportResult::Success;
    }
    virtual dev::eth::ImportResult import(
        bytesConstRef _txBytes, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return ImportResult::Success;
    }

private:
    Transactions transactions;
    Transaction transaction;
    PROTOCOL_ID protocolId = 0;
};

class FakeLedger : public Ledger
{
public:
    FakeLedger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir, std::string const& _configFile)
      : Ledger(service, _groupId, _keyPair, _baseDir, _configFile)
    {
        /// init blockChain
        initBlockChain();
        /// init blockVerifier
        initBlockVerifier();
        /// init txPool
        initTxPool();
    }
    virtual void initLedger(
        std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile)
        override{};
    /// init blockverifier related
    void initBlockChain() override { m_blockChain = std::make_shared<MockBlockChain>(); }
    void initBlockVerifier() override { m_blockVerifier = std::make_shared<MockBlockVerifier>(); }
    void initTxPool() override { m_txPool = std::make_shared<MockTxPool>(); }
    virtual std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const override
    {
        FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
        std::shared_ptr<dev::consensus::ConsensusInterface> consensusInterface =
            fake_pbft.consensus();
        return consensusInterface;
    }
};

}  // namespace test
}  // namespace dev
