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
#include <libnetwork/Common.h>
#include <libp2p/Service.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <libtxpool/TxPoolInterface.h>

namespace dev
{
namespace demo
{
class MockService : public Service
{
public:
    MockService() : Service()
    {
        p2p::NodeID nodeID = h512(100);
        NodeIPEndpoint m_endpoint(bi::address::from_string("127.0.0.1"), 30303, 30310);
        P2PSessionInfo info(nodeID, m_endpoint, std::set<std::string>());
        std::set<std::string> topics;
        std::string topic = "Topic1";
        topics.insert(topic);
        m_sessionInfos.push_back(P2PSessionInfo(nodeID, m_endpoint, topics));
    }

    virtual dev::p2p::P2PSessionInfos sessionInfos() override { return m_sessionInfos; }
    void setSessionInfos(dev::p2p::P2PSessionInfos& sessionInfos) { m_sessionInfos = sessionInfos; }
    void appendSessionInfo(dev::p2p::P2PSessionInfo const& info) { m_sessionInfos.push_back(info); }
    void clearSessionInfo() { m_sessionInfos.clear(); }
    dev::p2p::P2PSessionInfos sessionInfosByProtocolID(PROTOCOL_ID _protocolID) const
    {
        return m_sessionInfos;
    }

    virtual void asyncSendMessageByNodeID(dev::p2p::NodeID nodeID,
        dev::p2p::P2PMessage::Ptr message, CallbackFuncWithSession callback,
        dev::p2p::Options options = dev::p2p::Options()) override
    {
        if (m_asyncSend.count(nodeID))
            m_asyncSend[nodeID]++;
        else
            m_asyncSend[nodeID] = 1;
        m_asyncSendMsgs[nodeID] = message;
    }
    size_t getAsyncSendSizeByNodeID(dev::p2p::NodeID const& nodeID)
    {
        if (!m_asyncSend.count(nodeID))
            return 0;
        return m_asyncSend[nodeID];
    }

    dev::network::Message::Ptr getAsyncSendMessageByNodeID(NodeID const& nodeID)

    {
        auto msg = m_asyncSendMsgs.find(nodeID);
        if (msg == m_asyncSendMsgs.end())
            return nullptr;
        return msg->second;
    }

    void setConnected() { m_connected = true; }
    bool isConnected(dev::p2p::NodeID const& nodeId) const { return m_connected; }

private:
    dev::p2p::P2PSessionInfos m_sessionInfos;
    std::map<dev::p2p::NodeID, size_t> m_asyncSend;
    std::map<dev::p2p::NodeID, dev::p2p::P2PMessage::Ptr> m_asyncSendMsgs;
    bool m_connected;
};

class MockBlockChain : public dev::blockchain::BlockChainInterface
{
public:
    MockBlockChain()
    {
        m_blockNumber = 1;
        m_totalTransactionCount = 0;
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

    virtual int64_t number() override { return m_blockNumber - 1; }
    virtual std::pair<int64_t, int64_t> totalTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber - 1);
    }

    void createTransaction()
    {
        dev::bytes rlpBytes = fromHex(
            "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
            "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
            "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
            "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
            "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
            "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

        dev::RLP rlpObj(rlpBytes);
        dev::bytesConstRef d = rlpObj.data();
        transaction = dev::eth::Transaction(d, eth::CheckTransaction::Everything);
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
        if (_txHash == dev::jsToFixed<32>(
                           "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f"))
            return dev::eth::LocalisedTransaction(transaction, blockHash, 0, 1);
        else
            return dev::eth::LocalisedTransaction(Transaction(), h256(0), -1);
    }

    dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override
    {
        return dev::eth::Transaction();
    }

    virtual dev::eth::TransactionReceipt getTransactionReceiptByHash(
        dev::h256 const& _txHash) override
    {
        if (_txHash ==
            jsToFixed<32>("0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f"))
        {
            dev::eth::LogEntries entries;
            dev::eth::LogEntry entry;
            entry.address = Address(0x2000);
            entry.data = bytes();
            entry.topics = h256s();
            entries.push_back(entry);
            return dev::eth::TransactionReceipt(
                h256(0x3), u256(8), entries, 0, bytes(), Address(0x1000));
        }
        else
        {
            return dev::eth::TransactionReceipt();
        }
    }

    dev::eth::LocalisedTransactionReceipt getLocalisedTxReceiptByHash(
        dev::h256 const& _txHash) override
    {
        if (_txHash == dev::jsToFixed<32>(
                           "0x7536cf1286b5ce6c110cd4fea5c891467884240c9af366d678eb4191e1c31c6f"))
        {
            auto tx = getLocalisedTxByHash(_txHash);
            auto txReceipt = getTransactionReceiptByHash(_txHash);
            return LocalisedTransactionReceipt(txReceipt, _txHash, tx.blockHash(), tx.blockNumber(),
                tx.from(), tx.to(), tx.transactionIndex(), txReceipt.gasUsed(),
                txReceipt.contractAddress());
        }
        else
            return dev::eth::LocalisedTransactionReceipt(
                TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
    }

    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return m_blockChain[_i];
    }

    virtual dev::blockchain::CommitResult commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        m_blockHash[block.blockHeader().hash()] = block.blockHeader().number();
        m_blockChain.push_back(std::make_shared<Block>(block));
        m_blockNumber = block.blockHeader().number() + 1;
        m_totalTransactionCount += block.transactions().size();
        m_onReady();
        return dev::blockchain::CommitResult::OK;
    }

    void setGroupMark(std::string const& groupMark) override {}

    dev::bytes getCode(dev::Address _address) override { return bytes(); }

    dev::eth::BlockHeader blockHeader;
    dev::eth::Transactions transactions;
    dev::eth::Transaction transaction;
    dev::bytes extraData;
    dev::eth::Block block;
    dev::h256 blockHash;
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
    uint64_t m_totalTransactionCount;
};

class MockBlockVerifier : public dev::blockverifier::BlockVerifierInterface
{
public:
    MockBlockVerifier()
    {
        m_executiveContext = std::make_shared<dev::blockverifier::ExecutiveContext>();
        std::srand(std::time(nullptr));
    };
    virtual ~MockBlockVerifier(){};
    std::shared_ptr<dev::blockverifier::ExecutiveContext> executeBlock(
        dev::eth::Block& block, dev::blockverifier::BlockInfo const& parentBlockInfo) override
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
    std::shared_ptr<dev::blockverifier::ExecutiveContext> m_executiveContext;
};

class MockTxPool : public dev::txpool::TxPoolInterface
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

        dev::RLP rlpObj(rlpBytes);
        dev::bytesConstRef d = rlpObj.data();
        transaction = dev::eth::Transaction(d, eth::CheckTransaction::Everything);
        transactions.push_back(transaction);
    };
    virtual ~MockTxPool(){};
    virtual dev::eth::Transactions pendingList() const override { return transactions; };
    virtual size_t pendingSize() override { return 1; }
    virtual dev::eth::Transactions topTransactions(
        uint64_t const& _limit, dev::h256Hash& _avoid, bool _updateAvoid = false) override
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
    virtual dev::PROTOCOL_ID const& getProtocolId() const override { return protocolId; }
    virtual dev::txpool::TxPoolStatus status() const override
    {
        dev::txpool::TxPoolStatus status;
        status.current = 0;
        status.dropped = 0;
        return status;
    }
    virtual std::pair<dev::h256, dev::Address> submit(dev::eth::Transaction& _tx) override
    {
        return std::make_pair(_tx.sha3(), toAddress(_tx.from(), _tx.nonce()));
    }
    virtual dev::eth::ImportResult import(
        dev::eth::Transaction& _tx, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return dev::eth::ImportResult::Success;
    }
    virtual dev::eth::ImportResult import(
        dev::bytesConstRef _txBytes, dev::eth::IfDropped _ik = dev::eth::IfDropped::Ignore) override
    {
        return dev::eth::ImportResult::Success;
    }

private:
    dev::eth::Transactions transactions;
    dev::eth::Transaction transaction;
    dev::PROTOCOL_ID protocolId = 0;
};

class MockBlockSync : public dev::sync::SyncInterface
{
public:
    void start() override {}
    void stop() override {}
    dev::sync::SyncStatus status() const override { return m_syncStatus; }
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
    dev::PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    void setProtocolId(dev::PROTOCOL_ID const _protocolId) override { m_protocolId = _protocolId; };
    void noteSealingBlockNumber(int64_t _number){};

private:
    dev::sync::SyncStatus m_syncStatus;
    bool m_isSyncing;
    bool m_forceSync;
    dev::eth::Block m_latestSentBlock;
    dev::PROTOCOL_ID m_protocolId;
};

class MockConsensus : public dev::consensus::ConsensusInterface
{
public:
    MockConsensus(){};
    virtual ~MockConsensus(){};
    virtual void start(){};
    virtual void stop(){};
    virtual dev::h512s minerList() const { return m_minerList; };
    virtual void appendMiner(dev::h512 const& _miner){};
    /// get status of consensus
    virtual const std::string consensusStatus() const
    {
        std::string status =
            "[\n    {\n        \"nodeNum\" : 0,\n        \"f\" : 0,\n        "
            "\"consensusedBlockNumber\" : 0,\n        \"highestBlockHeader.blockNumber\" : 0,\n    "
            "    \"highestBlockHeader.blockHash\" : "
            "\"9a4d03c01903850ad99ee7994f51b50f729f6b7c29b04581b0fa7bb138c02a8e\",\n        "
            "\"protocolId\" : 8,\n        \"accountType\" : 98376458,\n        \"minerList\" : "
            "\"43686ea74ed41a4a37b7b5e28a27d0aee21a85477798bf4742754b50537477fc9fb5cc99d0e9c1f451be"
            "a635cb4b9513fa964cc09a98153e59f25650170d3183#\",\n        \"allowFutureBlocks\" : "
            "true,\n        \"connectedNodes\" : 0,\n        \"currentView\" : 0,\n        "
            "\"toView\" : 0,\n        \"leaderFailed\" : false,\n        \"cfgErr\" : false,\n     "
            "   \"omitEmptyBlock\" : true\n    },\n    {\n        \"prepareCache.block_hash\" : "
            "\"0000000000000000000000000000000000000000000000000000000000000000\",\n        "
            "\"prepareCache.height\" : -1,\n        \"prepareCache.idx\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\",\n "
            "       \"prepareCache.view\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\"\n  "
            "  },\n    {\n        \"rawPrepareCache.block_hash\" : "
            "\"0000000000000000000000000000000000000000000000000000000000000000\",\n        "
            "\"rawPrepareCache.height\" : -1,\n        \"rawPrepareCache.idx\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\",\n "
            "       \"rawPrepareCache.view\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\"\n  "
            "  },\n    {\n        \"committedPrepareCache.block_hash\" : "
            "\"0000000000000000000000000000000000000000000000000000000000000000\",\n        "
            "\"committedPrepareCache.height\" : -1,\n        \"committedPrepareCache.idx\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\",\n "
            "       \"committedPrepareCache.view\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\"\n  "
            "  },\n    {\n        \"futureCache.block_hash\" : "
            "\"0000000000000000000000000000000000000000000000000000000000000000\",\n        "
            "\"futureCache.height\" : -1,\n        \"futureCache.idx\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\",\n "
            "       \"futureCache.view\" : "
            "\"115792089237316195423570985008687907853269984665640564039457584007913129639935\"\n  "
            "  },\n    {\n        \"signCache.cachedBlockSize\" : \"0\"\n    },\n    {\n        "
            "\"commitCache.cachedBlockSize\" : \"0\"\n    },\n    {\n        "
            "\"viewChangeCache.cachedBlockSize\" : \"0\"\n    }\n]";
        return status;
    };

    /// protocol id used when register handler to p2p module
    virtual dev::PROTOCOL_ID const& protocolId() const { return protocal_id; };

    /// get node account type
    virtual dev::consensus::NodeAccountType accountType()
    {
        return dev::consensus::NodeAccountType::MinerAccount;
    };
    /// set the node account type
    virtual void setNodeAccountType(dev::consensus::NodeAccountType const&){};
    virtual dev::consensus::IDXTYPE nodeIdx() const { return 0; };
    /// update the context of PBFT after commit a block into the block-chain
    virtual void reportBlock(dev::eth::Block const& block){};

private:
    dev::h512s m_minerList;
    dev::PROTOCOL_ID protocal_id = 0;
};

class FakeLedger : public dev::ledger::LedgerInterface
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
        std::shared_ptr<dev::consensus::ConsensusInterface> consensusInterface =
            std::make_shared<MockConsensus>();
        return consensusInterface;
    }
    virtual std::shared_ptr<dev::sync::SyncInterface> sync() const override { return m_sync; }
    void initBlockChain() { m_blockChain = std::make_shared<MockBlockChain>(); }
    void initBlockVerifier() { m_blockVerifier = std::make_shared<MockBlockVerifier>(); }
    void initTxPool() { m_txPool = std::make_shared<MockTxPool>(); }
    void initBlockSync() { m_sync = std::make_shared<MockBlockSync>(); }
    virtual dev::GROUP_ID const& groupId() const override { return m_groupId; }
    virtual std::shared_ptr<dev::ledger::LedgerParamInterface> getParam() const override
    {
        return m_param;
    }
    virtual void startAll() override {}
    virtual void stopAll() override {}

private:
    std::shared_ptr<dev::ledger::LedgerParamInterface> m_param = nullptr;

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

}  // namespace demo
}  // namespace dev
