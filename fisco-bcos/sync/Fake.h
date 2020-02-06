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
 * @brief: fake Concencus
 * @file: Fake.h
 * @author: jimmyshi
 * @date: 2018-10-27
 */

#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libinitializer/Common.h>
#include <libinitializer/InitializerInterface.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libsync/Common.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <unistd.h>
#include <ctime>
class FakeConcensus : public dev::Worker
{
    // FakeConcensus, only do: fetch tx from txPool and commit newBlock into blockchain
public:
    using BlockHeaderPtr = std::shared_ptr<dev::eth::BlockHeader>;
    using BlockPtr = std::shared_ptr<dev::eth::Block>;
    using TxPtr = std::shared_ptr<dev::eth::Transaction>;
    using SigList = std::vector<std::pair<dev::u256, dev::Signature>>;

public:
    FakeConcensus(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _sync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        unsigned _idleWaitMs = 30)
      : dev::Worker("FakeConcensusForSync", 0),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_sync(_sync),
        m_blockVerifier(_blockVerifier),
        m_totalTxCommit(0),
        m_protocolId(0),
        m_nodeId(0),
        m_blockGenerationInterval(_idleWaitMs)
    {
        m_groupId = dev::eth::getGroupAndProtocol(m_protocolId).first;
    }

    virtual ~FakeConcensus(){};

    void start() { startWorking(); }

    /// stop blockSync
    void stop() { stopWorking(); }
    /// doWork every idleWaitMs
    virtual void doWork() override
    {
        if (m_sync->status().state != dev::sync::SyncState::Idle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_blockGenerationInterval));
            return;
        }

        // seal
        std::shared_ptr<dev::eth::Transactions> txs = m_txPool->pendingList();

        if (0 == txs->size())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_blockGenerationInterval));
            return;
        }

        int64_t currentNumber = m_blockChain->number();
        dev::h256 const& parentHash = m_blockChain->numberHash(currentNumber);

        m_sync->noteSealingBlockNumber(currentNumber + 1);

        BlockPtr block = newBlock(parentHash, currentNumber + 1, txs);
        dev::blockverifier::BlockInfo parentBlockInfo;
        dev::blockverifier::ExecutiveContext::Ptr exeCtx =
            m_blockVerifier->executeBlock(*block, parentBlockInfo);

        // consensus process waiting time simulation
        std::this_thread::sleep_for(std::chrono::milliseconds(m_blockGenerationInterval));

        // commit
        m_blockChain->commitBlock(block, exeCtx);
        m_txPool->dropBlockTrans(block);
        m_totalTxCommit += txs->size();

        SYNC_LOG(TRACE) << LOG_BADGE("Commit") << LOG_DESC("Consensus block commit: ")
                        << LOG_KV("blockNumber", currentNumber + 1)
                        << LOG_KV("txNumber", txs->size())
                        << LOG_KV("totalTxCommitThisNode", m_totalTxCommit)
                        << LOG_KV("blockHash", block->headerHash())
                        << LOG_KV("parentHash", parentHash);
    }

private:
    BlockPtr newBlock(
        dev::h256 _parentHash, int64_t _currentNumner, std::shared_ptr<dev::eth::Transactions> _txs)
    {
        // Generate block header
        BlockHeaderPtr header = newBlockHeader(_parentHash, _currentNumner);
        dev::bytes blockHeaderBytes;
        header->encode(blockHeaderBytes);

        // Generate block
        BlockPtr block = std::make_shared<dev::eth::Block>();
        block->setSigList(sigList(_txs->size()));
        block->setTransactions(_txs);

        // Add block header into block
        block->setBlockHeader(*header);

        return block;
    }

    BlockHeaderPtr newBlockHeader(dev::h256 _parentHash, int64_t _currentNumner)
    {
        BlockHeaderPtr blockHeader = std::make_shared<dev::eth::BlockHeader>();
        blockHeader->setParentHash(_parentHash);
        blockHeader->setRoots(
            dev::sha3("transactionRoot"), dev::sha3("receiptRoot"), dev::sha3("stateRoot"));
        blockHeader->setLogBloom(dev::eth::LogBloom(0));
        blockHeader->setNumber(_currentNumner);
        blockHeader->setGasLimit(dev::u256(3000000));
        blockHeader->setGasUsed(dev::u256(100000));
        uint64_t current_time = 100000;  // utcTime();
        blockHeader->setTimestamp(current_time);
        blockHeader->appendExtraDataArray(dev::jsToBytes("0x1020"));
        blockHeader->setSealer(dev::u256(12));
        std::vector<dev::h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(dev::toPublic(dev::Secret(dev::h256(i))));
        }
        blockHeader->setSealerList(sealer_list);
        return blockHeader;
    }

    std::shared_ptr<SigList> sigList(size_t _size)
    {
        std::shared_ptr<SigList> retList = std::make_shared<SigList>();
        /// set sig list
        dev::Signature sig;
        dev::h256 block_hash;
        auto keyPair = dev::KeyPair::create();
        for (size_t i = 0; i < _size; i++)
        {
            block_hash = dev::sha3("block " + std::to_string(i));
            sig = dev::sign(keyPair, block_hash);
            retList->push_back(std::make_pair(dev::u256(block_hash), sig));
        }
        return retList;
    }

private:
    /// transaction pool handler
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain;
    /// sync
    std::shared_ptr<dev::sync::SyncInterface> m_sync;
    /// block verifier
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;

    size_t m_totalTxCommit;
    dev::PROTOCOL_ID m_protocolId;
    dev::h512 m_nodeId;
    dev::GROUP_ID m_groupId;
    unsigned m_blockGenerationInterval;
};

class FakeInitializer : public dev::initializer::InitializerInterface
{
public:
    typedef std::shared_ptr<FakeInitializer> Ptr;

    void init(std::string const& _path)
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);

        m_secureInitializer = std::make_shared<dev::initializer::SecureInitializer>();
        m_secureInitializer->initConfig(pt);

        m_p2pInitializer = std::make_shared<dev::initializer::P2PInitializer>();
        m_p2pInitializer->setSSLContext(m_secureInitializer->SSLContext());
        m_p2pInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_p2pInitializer->initConfig(pt);
    }

public:
    dev::initializer::SecureInitializer::Ptr secureInitializer() { return m_secureInitializer; }
    dev::initializer::P2PInitializer::Ptr p2pInitializer() { return m_p2pInitializer; }

private:
    dev::initializer::P2PInitializer::Ptr m_p2pInitializer;
    dev::initializer::SecureInitializer::Ptr m_secureInitializer;
};
