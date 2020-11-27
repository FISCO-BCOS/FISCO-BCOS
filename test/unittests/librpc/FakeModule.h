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
#include <libconfig/GlobalConfigure.h>
#include <libconsensus/ConsensusInterface.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libledger/LedgerManager.h>
#include <libp2p/Service.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <libutilities/DataConvertUtility.h>
#include <libutilities/TopicInfo.h>
#include <test/tools/libutils/Common.h>
#include <test/unittests/libconsensus/FakePBFTEngine.h>

using namespace std;
using namespace bcos;
using namespace bcos::blockchain;
using namespace bcos::eth;
using namespace bcos::blockverifier;
using namespace bcos::sync;
using namespace bcos::ledger;
using namespace bcos::p2p;

namespace bcos
{
namespace test
{
class FakesService : public Service
{
public:
    FakesService() : Service()
    {
        NodeID nodeID = h512(100);
        NodeIPEndpoint m_endpoint(boost::asio::ip::make_address("127.0.0.1"), 30310);
        bcos::network::NodeInfo node_info;
        node_info.nodeID = nodeID;
        std::set<bcos::TopicItem> topicList;
        P2PSessionInfo info(node_info, m_endpoint, topicList);
        TopicItem item;
        item.topic = "Topic1";
        item.topicStatus = TopicStatus::VERIFY_SUCCESS_STATUS;
        topicList.insert(std::move(item));
        m_sessionInfos.push_back(P2PSessionInfo(node_info, m_endpoint, topicList));
        h512s nodeList;
        nodeList.push_back(
            h512("7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fc"
                 "be637191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61"));
        m_groupID2NodeList[1] = nodeList;
    }

    P2PSessionInfos sessionInfos() override { return m_sessionInfos; }
    NodeID id() const override
    {
        return h512(
            "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fc"
            "be637191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61");
    }
    void setSessionInfos(P2PSessionInfos& sessionInfos) { m_sessionInfos = sessionInfos; }
    void appendSessionInfo(P2PSessionInfo const& info) { m_sessionInfos.push_back(info); }
    void clearSessionInfo() { m_sessionInfos.clear(); }
    P2PSessionInfos sessionInfosByProtocolID(PROTOCOL_ID) const override { return m_sessionInfos; }

    h512s getNodeListByGroupID(GROUP_ID groupID) override { return m_groupID2NodeList[groupID]; }

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

    bcos::network::Message::Ptr getAsyncSendMessageByNodeID(NodeID const& nodeID)
    {
        auto msg = m_asyncSendMsgs.find(nodeID);
        if (msg == m_asyncSendMsgs.end())
            return nullptr;
        return msg->second;
    }

    void setConnected() { m_connected = true; }
    bool isConnected(NodeID const&) const override { return m_connected; }

private:
    P2PSessionInfos m_sessionInfos;
    std::map<NodeID, size_t> m_asyncSend;
    std::map<NodeID, bcos::network::Message::Ptr> m_asyncSendMsgs;
    std::map<GROUP_ID, h512s> m_groupID2NodeList;
    bool m_connected;
};

class MockBlockChain : public BlockChainInterface
{
public:
    MockBlockChain()
    {
        m_blockNumber = 0;
        m_totalTransactionCount = 0;
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
        blockHash = blockHeader.hash();
        std::cout << "* MockBlockChain, genesis block hash is " << *toHexString(blockHash);
        std::shared_ptr<Transaction> p_tx = createTransaction();
        ;
        transactions = std::make_shared<Transactions>();
        transactions->push_back(p_tx);

        block.setBlockHeader(blockHeader);
        block.setTransactions(transactions);

        m_blockHash[blockHash] = 0;
        m_blockChain.push_back(std::make_shared<Block>(block));
    }

    virtual ~MockBlockChain() {}

    int64_t number() override { return m_blockNumber; }
    std::pair<int64_t, int64_t> totalTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber);
    }
    std::pair<int64_t, int64_t> totalFailedTransactionCount() override
    {
        return std::make_pair(m_totalTransactionCount, m_blockNumber - 1);
    }

    std::shared_ptr<std::vector<bcos::eth::NonceKeyType>> getNonces(int64_t) override
    {
        return std::make_shared<std::vector<bcos::eth::NonceKeyType>>();
    }
    bool checkAndBuildGenesisBlock(
        std::shared_ptr<LedgerParamInterface> initParam, bool = true) override
    {
        m_initParam = initParam;
        return true;
    }
    bcos::h512s sealerList() override { return m_initParam->mutableConsensusParam().sealerList; };
    bcos::h512s observerList() override
    {
        return m_initParam->mutableConsensusParam().observerList;
    };

    std::string getSystemConfigByKey(std::string const&, int64_t = -1) override
    {
        return "300000000";
    };

    Transaction::Ptr createTransaction()
    {
        bytes rlpBytes;
        if (g_BCOSConfig.SMCrypto())
        {
            rlpBytes = *fromHexString(
                "f90114a003eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd598411e1"
                "a300"
                "84b2d05e008201f594bab78cea98af2320ad4ee81bba8a7473e0c8c48d80a48fff0fc400000000"
                "0000"
                "00000000000000000000000000000000000000000000000000040101a48fff0fc4000000000000"
                "0000"
                "000000000000000000000000000000000000000000000004b8408234c544a9f3ce3b401a92cc71"
                "7560"
                "2ce2a1e29b1ec135381c7d2a9e8f78f3edc9c06ee55252857c9a4560cb39e9d70d40f4331cace4"
                "d2b3"
                "121b967fa7a829f0a00f16d87c5065ad5c3b110ef0b97fe9a67b62443cb8ddde60d4e001a64429"
                "dc6e"
                "a03d2569e0449e9a900c236541afb9d8a8d5e1a36844439c7076f6e75ed624256f");
        }
        else
        {
            rlpBytes = *fromHexString(
                "f8d3a003922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c8411e1a3"
                "0084"
                "11e1a3008201f894d6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f80a466c991390000000000"
                "0000"
                "000000000000000000000000000000000000000000000000040101a466c9913900000000000000"
                "0000"
                "00000000000000000000000000000000000000000000041ba08e0d3fae10412c584c977721aeda"
                "88df"
                "932b2a019f084feda1e0a42d199ea979a016c387f79eb85078be5db40abe1670b8b480a12c7eab"
                "719b"
                "edee212b7972f775");
        }
        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        transaction = std::make_shared<Transaction>(d, eth::CheckTransaction::Everything);
        std::cout << "* MockBlockChain: hash of the transaction: " << transaction->hash()
                  << std::endl;
        return transaction;
    }
    bcos::h256 numberHash(int64_t) override { return blockHash; }

    std::shared_ptr<bcos::eth::Block> getBlockByHash(
        bcos::h256 const& _blockHash, int64_t = -1) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    std::shared_ptr<bcos::bytes> getBlockRLPByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i))->rlpP();
    }

    bcos::eth::LocalisedTransaction::Ptr getLocalisedTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<LocalisedTransaction>(transaction, blockHash, 0, 0);
    }

    bcos::eth::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        bcos::h256 const& _txHash) override
    {
        if (_txHash == transaction->hash())
        {
            auto tx = getLocalisedTxByHash(_txHash);
            auto txReceipt = getTransactionReceiptByHash(_txHash);
            return std::make_shared<LocalisedTransactionReceipt>(*txReceipt, _txHash,
                tx->blockHash(), tx->blockNumber(), tx->tx()->from(), tx->tx()->to(),
                tx->transactionIndex(), txReceipt->gasUsed(), txReceipt->contractAddress());
        }
        else
            return std::make_shared<LocalisedTransactionReceipt>(
                TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
    }

    bcos::eth::Transaction::Ptr getTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<Transaction>();
    }

    bcos::eth::TransactionReceipt::Ptr getTransactionReceiptByHash(bcos::h256 const&) override
    {
        LogEntries entries;
        LogEntry entry;
        entry.address = Address(0x2000);
        entry.data = bytes();
        entry.topics = h256s();
        entries.push_back(entry);
        return std::make_shared<TransactionReceipt>(
            h256(0x3), u256(8), entries, eth::TransactionException::None, bytes(), Address(0x1000));
    }

    std::shared_ptr<bcos::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }

    std::shared_ptr<
        std::pair<std::shared_ptr<bcos::eth::BlockHeader>, bcos::eth::Block::SigListPtrType>>
    getBlockHeaderInfo(int64_t _blockNumber) override
    {
        auto block = getBlockByNumber(_blockNumber);
        if (!block)
        {
            return nullptr;
        }
        auto result = std::make_shared<
            std::pair<std::shared_ptr<bcos::eth::BlockHeader>, bcos::eth::Block::SigListPtrType>>();
        result->first = std::make_shared<BlockHeader>(block->blockHeader());
        result->second = nullptr;
        return result;
    }

    std::shared_ptr<
        std::pair<std::shared_ptr<bcos::eth::BlockHeader>, bcos::eth::Block::SigListPtrType>>
    getBlockHeaderInfoByHash(bcos::h256 const& _blockHash) override
    {
        auto block = getBlockByHash(_blockHash);
        if (!block)
        {
            return nullptr;
        }
        auto result = std::make_shared<
            std::pair<std::shared_ptr<bcos::eth::BlockHeader>, bcos::eth::Block::SigListPtrType>>();
        result->first = std::make_shared<BlockHeader>(block->blockHeader());
        // fake sigList
        auto hash = block->header().hash();
        for (int i = 0; i < 9; i++)
        {
            auto keyPair = KeyPair::create();
            auto signature = bcos::crypto::Sign(keyPair, hash)->asBytes();
            sigList.push_back(std::make_pair(i, signature));
        }
        result->second = std::make_shared<bcos::eth::Block::SigListType>(sigList);
        return result;
    }

    std::pair<LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(bcos::h256 const& _txHash) override
    {
        (void)_txHash;
        return std::make_pair(std::make_shared<LocalisedTransaction>(),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    std::pair<bcos::eth::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        bcos::h256 const&, bcos::eth::LocalisedTransaction&) override
    {
        return std::make_pair(
            std::make_shared<LocalisedTransactionReceipt>(bcos::eth::TransactionException::None),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    CommitResult commitBlock(std::shared_ptr<bcos::eth::Block> block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext>) override
    {
        m_blockHash[block->blockHeader().hash()] = block->blockHeader().number();
        m_blockChain.push_back(std::make_shared<Block>(*block));
        m_blockNumber = block->blockHeader().number() + 1;
        m_totalTransactionCount += block->transactions()->size();
        m_onReady(m_blockNumber);
        return CommitResult::OK;
    }

    bcos::bytes getCode(bcos::Address) override { return bytes(); }

    BlockHeader blockHeader;
    std::shared_ptr<Transactions> transactions;
    Transaction::Ptr transaction = std::make_shared<Transaction>();
    bytes extraData;
    Block block;
    h256 blockHash;
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
    uint64_t m_totalTransactionCount;

    std::shared_ptr<LedgerParamInterface> m_initParam;

    std::vector<std::pair<u256, std::vector<unsigned char>>> sigList;
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
        bcos::eth::Block& block, BlockInfo const&) override
    {
        usleep(1000 * (block.getTransactionSize()));
        return m_executiveContext;
    };
    bcos::eth::TransactionReceipt::Ptr executeTransaction(
        const bcos::eth::BlockHeader&, bcos::eth::Transaction::Ptr) override
    {
        bcos::eth::TransactionReceipt::Ptr receipt = std::make_shared<TransactionReceipt>();
        return receipt;
    }

private:
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

class MockTxPool : public TxPoolInterface
{
public:
    MockTxPool()
    {
        bytes rlpBytes;
        if (g_BCOSConfig.SMCrypto())
        {
            rlpBytes = *fromHexString(
                "f90114a003eebc46c9c0e3b84799097c5a6ccd6657a9295c11270407707366d0750fcd598411e1"
                "a300"
                "84b2d05e008201f594bab78cea98af2320ad4ee81bba8a7473e0c8c48d80a48fff0fc400000000"
                "0000"
                "00000000000000000000000000000000000000000000000000040101a48fff0fc4000000000000"
                "0000"
                "000000000000000000000000000000000000000000000004b8408234c544a9f3ce3b401a92cc71"
                "7560"
                "2ce2a1e29b1ec135381c7d2a9e8f78f3edc9c06ee55252857c9a4560cb39e9d70d40f4331cace4"
                "d2b3"
                "121b967fa7a829f0a00f16d87c5065ad5c3b110ef0b97fe9a67b62443cb8ddde60d4e001a64429"
                "dc6e"
                "a03d2569e0449e9a900c236541afb9d8a8d5e1a36844439c7076f6e75ed624256f");
        }
        else
        {
            rlpBytes = *fromHexString(
                "f8d3a003922ee720bb7445e3a914d8ab8f507d1a647296d563100e49548d83fd98865c8411e1a3"
                "0084"
                "11e1a3008201f894d6c8a04b8826b0a37c6d4aa0eaa8644d8e35b79f80a466c991390000000000"
                "0000"
                "000000000000000000000000000000000000000000000000040101a466c9913900000000000000"
                "0000"
                "00000000000000000000000000000000000000000000041ba08e0d3fae10412c584c977721aeda"
                "88df"
                "932b2a019f084feda1e0a42d199ea979a016c387f79eb85078be5db40abe1670b8b480a12c7eab"
                "719b"
                "edee212b7972f775");
        }
        RLP rlpObj(rlpBytes);
        bytesConstRef d = rlpObj.data();
        transaction = std::make_shared<Transaction>(d, eth::CheckTransaction::Everything);
        transactions = std::make_shared<bcos::eth::Transactions>();
        std::shared_ptr<Transaction> tx =
            std::make_shared<Transaction>(d, eth::CheckTransaction::Everything);
        transactions->push_back(tx);
    };
    virtual ~MockTxPool(){};
    std::shared_ptr<bcos::eth::Transactions> pendingList() const override { return transactions; };
    size_t pendingSize() override { return 1; }
    std::shared_ptr<bcos::eth::Transactions> topTransactions(
        uint64_t const&, h256Hash&, bool = false) override
    {
        return transactions;
    }
    std::shared_ptr<bcos::eth::Transactions> topTransactions(uint64_t const&) override
    {
        return transactions;
    }
    bool drop(h256 const&) override { return true; }
    bool dropBlockTrans(std::shared_ptr<bcos::eth::Block>) override { return true; }
    bool handleBadBlock(Block const&) override { return true; }
    PROTOCOL_ID const& getProtocolId() const override { return protocolId; }
    TxPoolStatus status() const override
    {
        TxPoolStatus status;
        status.current = 1;
        status.dropped = 0;
        return status;
    }
    std::pair<h256, Address> submit(bcos::eth::Transaction::Ptr _tx) override
    {
        return make_pair(_tx->hash(), toAddress(_tx->from(), _tx->nonce()));
    }
    std::pair<h256, Address> submitTransactions(bcos::eth::Transaction::Ptr _tx) override
    {
        return make_pair(_tx->hash(), toAddress(_tx->from(), _tx->nonce()));
    }
    bcos::eth::ImportResult import(
        bcos::eth::Transaction::Ptr, bcos::eth::IfDropped = bcos::eth::IfDropped::Ignore) override
    {
        return ImportResult::Success;
    }
    bcos::eth::ImportResult import(
        bytesConstRef, bcos::eth::IfDropped = bcos::eth::IfDropped::Ignore)
    {
        return ImportResult::Success;
    }

private:
    std::shared_ptr<Transactions> transactions;
    Transaction::Ptr transaction = std::make_shared<Transaction>();
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
    bool blockNumberFarBehind() const override { return false; }
    PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    void setProtocolId(PROTOCOL_ID const _protocolId) override { m_protocolId = _protocolId; };
    void noteSealingBlockNumber(int64_t) override{};

    void registerConsensusVerifyHandler(std::function<bool(bcos::eth::Block const&)>) override{};

private:
    SyncStatus m_syncStatus;
    bool m_isSyncing;
    Block m_latestSentBlock;
    PROTOCOL_ID m_protocolId;
};

class FakeLedger : public LedgerInterface
{
public:
    FakeLedger(std::shared_ptr<bcos::p2p::P2PInterface>, bcos::GROUP_ID const&,
        bcos::KeyPair const& keyPair, std::string const&, std::string const&)
      : LedgerInterface(keyPair)
    {
        /// init blockChain
        initBlockChain();
        /// init blockVerifier
        initBlockVerifier();
        /// init txPool
        initTxPool();
        /// init sync
        initBlockSync();
        /// init
        initLedgerParam();
    }
    bool initLedger(std::shared_ptr<LedgerParamInterface>) override { return true; };
    std::shared_ptr<bcos::txpool::TxPoolInterface> txPool() const override { return m_txPool; }
    std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> blockVerifier() const override
    {
        return m_blockVerifier;
    }
    std::shared_ptr<bcos::blockchain::BlockChainInterface> blockChain() const override
    {
        return m_blockChain;
    }
    std::shared_ptr<bcos::consensus::ConsensusInterface> consensus() const override
    {
        FakeConsensus<FakePBFTEngine> fake_pbft(1, ProtocolID::PBFT);
        std::shared_ptr<bcos::consensus::ConsensusInterface> consensusInterface =
            fake_pbft.consensus();
        return consensusInterface;
    }
    virtual std::shared_ptr<bcos::sync::SyncInterface> sync() const override { return m_sync; }
    virtual void initBlockChain()
    {
        m_blockChain = std::make_shared<MockBlockChain>();
        bcos::h512s sealerList;
        sealerList.push_back(bcos::h512(
            "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fc"
            "be637191cc2aebf4746846c0db2604adebf9c70c7f418d4d5a61"));
        // init the genesis param
        auto initParam = std::make_shared<bcos::ledger::LedgerParam>();
        initParam->mutableGenesisMark() = "std";
        initParam->mutableConsensusParam().sealerList = sealerList;
        initParam->mutableConsensusParam().observerList = bcos::h512s();
        initParam->mutableConsensusParam().consensusType = "";
        initParam->mutableStorageParam().type = "";
        initParam->mutableStateParam().type = "";
        initParam->mutableConsensusParam().maxTransactions = 1000;
        initParam->mutableTxParam().txGasLimit = 300000000;
        initParam->mutableGenesisParam().timeStamp = 0;
        m_blockChain->checkAndBuildGenesisBlock(initParam);
    }

    std::shared_ptr<bcos::event::EventLogFilterManager> getEventLogFilterManager() override
    {
        // just for compile, do nothing
        return nullptr;
    }
    virtual void initBlockVerifier() { m_blockVerifier = std::make_shared<MockBlockVerifier>(); }
    virtual void initTxPool() { m_txPool = std::make_shared<MockTxPool>(); }
    virtual void initBlockSync() { m_sync = std::make_shared<MockBlockSync>(); }
    virtual void initLedgerParam()
    {
        m_param = std::make_shared<LedgerParam>();
        m_param->mutableConsensusParam().consensusType = "pbft";
    }
    bcos::GROUP_ID const& groupId() const override { return m_groupId; }
    std::shared_ptr<LedgerParamInterface> getParam() const override { return m_param; }
    void startAll() override {}
    void stopAll() override {}

private:
    std::shared_ptr<LedgerParamInterface> m_param = nullptr;

    std::shared_ptr<bcos::p2p::P2PInterface> m_service = nullptr;
    bcos::GROUP_ID m_groupId;
    bcos::KeyPair m_keyPair;
    std::string m_configFileName = "config";
    std::string m_postfix = ".ini";
    std::shared_ptr<bcos::txpool::TxPoolInterface> m_txPool = nullptr;
    std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> m_blockVerifier = nullptr;
    std::shared_ptr<bcos::blockchain::BlockChainInterface> m_blockChain = nullptr;
    std::shared_ptr<bcos::sync::SyncInterface> m_sync = nullptr;
};

}  // namespace test
}  // namespace bcos
