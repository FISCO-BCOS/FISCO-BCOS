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
#include <libp2p/Service.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <test/tools/libutils/Common.h>
#include <test/unittests/libconsensus/FakePBFTEngine.h>

using namespace std;
using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::blockverifier;
using namespace dev::sync;
using namespace dev::ledger;
using namespace dev::p2p;

namespace dev
{
namespace test
{
class FakesService : public Service
{
public:
    FakesService() : Service()
    {
        NodeID nodeID = h512(100);
        NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30310);
        SessionInfo info(nodeID, m_endpoint, std::set<std::string>());
        std::set<std::string> topics;
        std::string topic = "Topic1";
        topics.insert(topic);
        m_sessionInfos.push_back(SessionInfo(nodeID, m_endpoint, topics));
    }

    virtual SessionInfos sessionInfos() override { return m_sessionInfos; }
    void setSessionInfos(SessionInfos& sessionInfos) { m_sessionInfos = sessionInfos; }
    void appendSessionInfo(SessionInfo const& info) { m_sessionInfos.push_back(info); }
    void clearSessionInfo() { m_sessionInfos.clear(); }
    SessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const { return m_sessionInfos; }

    void asyncSendMessageByNodeID(NodeID nodeID, P2PMessage::Ptr message,
        CallbackFuncWithSession callback, dev::p2p::Options) override
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
        m_blockNumber = 0;
        m_totalTransactionCount = 0;
        blockHash = h256("0x067150c07dab4facb7160e075548007e067150c07dab4facb7160e075548007e");
        blockHeader.setNumber(m_blockNumber);
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
    virtual std::pair<int64_t, int64_t> totalTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber);
    }
    void setGroupMark(std::string const& groupMark) override {}
    void createTransaction()
    {
#if FISCO_GM
        bytes rlpBytes = fromHex(
            "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d15485174876e7ff8609"
            "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
            "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
            "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
            "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
            "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a039d123931f027fd93ab7e79c62f133fa"
            "649b2aee0a0209066ae8e9b74bd18bf8a0a1a87d38676809abb6bfa178550ff3781351234f5430d7910e42"
            "c4b14a4d461");
#else
        bytes rlpBytes = fromHex(
            "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
            "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
            "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
            "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
            "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
            "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
#endif
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
        return LocalisedTransaction(transaction, blockHash, 0, 0);
    }

    dev::eth::LocalisedTransactionReceipt getLocalisedTxReceiptByHash(
        dev::h256 const& _txHash) override
    {
        if (_txHash ==
            jsToFixed<32>("0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f"))
        {
            auto tx = getLocalisedTxByHash(_txHash);
            auto txReceipt = getTransactionReceiptByHash(_txHash);
            return LocalisedTransactionReceipt(txReceipt, _txHash, tx.blockHash(), tx.blockNumber(),
                tx.from(), tx.to(), tx.transactionIndex(), txReceipt.gasUsed(),
                txReceipt.contractAddress());
        }
        else
            return LocalisedTransactionReceipt(
                TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
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

    CommitResult commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        m_blockHash[block.blockHeader().hash()] = block.blockHeader().number();
        m_blockChain.push_back(std::make_shared<Block>(block));
        m_blockNumber = block.blockHeader().number() + 1;
        m_totalTransactionCount += block.transactions().size();
        m_onReady();
        return CommitResult::OK;
    }

    dev::bytes getCode(dev::Address _address) override { return bytes(); }

    BlockHeader blockHeader;
    Transactions transactions;
    Transaction transaction;
    bytes extraData;
    Block block;
    h256 blockHash;
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
    uint64_t m_totalTransactionCount;
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
    std::shared_ptr<ExecutiveContext> executeBlock(
        dev::eth::Block& block, BlockInfo const& parentBlockInfo) override
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
#if FISCO_GM
        bytes rlpBytes = fromHex(
            "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d15485174876e7ff8609"
            "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
            "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
            "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
            "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
            "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a039d123931f027fd93ab7e79c62f133fa"
            "649b2aee0a0209066ae8e9b74bd18bf8a0a1a87d38676809abb6bfa178550ff3781351234f5430d7910e42"
            "c4b14a4d461");
#else
        bytes rlpBytes = fromHex(
            "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
            "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
            "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
            "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
            "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
            "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
#endif
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
    bool handleBadBlock(Block const& block) override { return true; }
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

class MockBlockSync : public SyncInterface
{
public:
    void start() override {}
    void stop() override {}
    SyncStatus status() const override { return m_syncStatus; }
    std::string const syncInfo() const override
    {
        const std::string syncStatus =
            "{\"isSyncing\":true,\"protocolId\":265,\"genesisHash\":"
            "\"eb8b84af3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c\","
            "\"currentBlockNumber\":2826,\"currentBlockHash\":"
            "\"1f7a714a5b2f31b6d5b609afb98bcea35c65dd42cfa999f39d0733798f371ca9\","
            "\"knownHighestNumber\":3533,\"knownLatestHash\":"
            "\"c8f36c588f40d280258722c75e927e113d1b69b2e9db230201cc199ab3986b39\",\"txPoolSize\":"
            "562,\"peers\":[{\"nodeId\":"
            "\"46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491"
            "cc1c15f46e621ea7304a9b5f0b3fb85ba20a6b1c0fc1\",\"genesisHash\":"
            "\"eb8b84af3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c\",\"blockNumber\":"
            "3533,\"latestHash\":"
            "\"c8f36c588f40d280258722c75e927e113d1b69b2e9db230201cc199ab3986b39\"},{\"nodeId\":"
            "\"7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191"
            "cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61\",\"genesisHash\":"
            "\"eb8b84af3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c\",\"blockNumber\":"
            "3533,\"latestHash\":"
            "\"c8f36c588f40d280258722c75e927e113d1b69b2e9db230201cc199ab3986b39\"},{\"nodeId\":"
            "\"f6f4931f56b9963851f43bb857ed5a6170ec1a4208ddcf1a1f2bb66f6d7e7a5c4749a89b5277d6265b1c"
            "12fdbc89290bed7cccf905eef359989275319b331753\",\"genesisHash\":"
            "\"eb8b84af3f35165d52cb41abe1a9a3d684703aca4966ce720ecd940bd885517c\",\"blockNumber\":"
            "3535,\"latestHash\":"
            "\"e2e1ccebe2ace9f45560b970c38b7edb4f80d0592ac7d345394852ff08ec8c08\"}]}";

        return syncStatus;
    }
    bool isSyncing() const override { return m_isSyncing; }
    PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    void setProtocolId(PROTOCOL_ID const _protocolId) override { m_protocolId = _protocolId; };
    void noteSealingBlockNumber(int64_t _number){};

private:
    SyncStatus m_syncStatus;
    bool m_isSyncing;
    bool m_forceSync;
    Block m_latestSentBlock;
    PROTOCOL_ID m_protocolId;
};

class FakeLedger : public LedgerInterface
{
public:
    FakeLedger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir, std::string const& _configFile)
    {
        /// init blockChain
        initBlockChain();
        /// init blockVerifier
        initBlockVerifier();
        /// init txPool
        initTxPool();
        /// init sync
        initBlockSync();
    }
    virtual bool initLedger() override { return true; };
    virtual void initConfig(std::string const& configPath) override{};
    virtual std::shared_ptr<dev::txpool::TxPoolInterface> txPool() const override
    {
        return m_txPool;
    }
    virtual std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier()
        const override
    {
        return m_blockVerifier;
    }
    virtual std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain() const override
    {
        return m_blockChain;
    }
    virtual std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const override
    {
        FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
        std::shared_ptr<dev::consensus::ConsensusInterface> consensusInterface =
            fake_pbft.consensus();
        return consensusInterface;
    }
    virtual std::shared_ptr<dev::sync::SyncInterface> sync() const override { return m_sync; }
    void initBlockChain() { m_blockChain = std::make_shared<MockBlockChain>(); }
    void initBlockVerifier() { m_blockVerifier = std::make_shared<MockBlockVerifier>(); }
    void initTxPool() { m_txPool = std::make_shared<MockTxPool>(); }
    void initBlockSync() { m_sync = std::make_shared<MockBlockSync>(); }
    virtual dev::GROUP_ID const& groupId() const override { return m_groupId; }
    virtual std::shared_ptr<LedgerParamInterface> getParam() const override { return m_param; }
    virtual void startAll() override {}
    virtual void stopAll() override {}

private:
    std::shared_ptr<LedgerParamInterface> m_param = nullptr;

    std::shared_ptr<dev::p2p::P2PInterface> m_service = nullptr;
    dev::GROUP_ID m_groupId;
    dev::KeyPair m_keyPair;
    std::string m_configFileName = "config";
    std::string m_postfix = ".ini";
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool = nullptr;
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier = nullptr;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain = nullptr;
    std::shared_ptr<dev::sync::SyncInterface> m_sync = nullptr;
};

}  // namespace test
}  // namespace dev
