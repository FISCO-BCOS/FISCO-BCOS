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
class FakeConcensus : public bcos::Worker
{
    // FakeConcensus, only do: fetch tx from txPool and commit newBlock into blockchain
public:
    using BlockHeaderPtr = std::shared_ptr<bcos::eth::BlockHeader>;
    using BlockPtr = std::shared_ptr<bcos::eth::Block>;
    using TxPtr = std::shared_ptr<bcos::eth::Transaction>;
    using SigList = std::vector<std::pair<bcos::u256, std::vector<unsigned char>>>;

public:
    FakeConcensus(std::shared_ptr<bcos::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<bcos::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<bcos::sync::SyncInterface> _sync,
        std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> _blockVerifier,
        unsigned _idleWaitMs = 30)
      : bcos::Worker("FakeConcensusForSync", 0),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_sync(_sync),
        m_blockVerifier(_blockVerifier),
        m_totalTxCommit(0),
        m_protocolId(0),
        m_nodeId(0),
        m_blockGenerationInterval(_idleWaitMs)
    {
        m_groupId = bcos::eth::getGroupAndProtocol(m_protocolId).first;
    }

    virtual ~FakeConcensus(){};

    void start() { startWorking(); }

    /// stop blockSync
    void stop() { stopWorking(); }
    /// doWork every idleWaitMs
    virtual void doWork() override
    {
        if (m_sync->status().state != bcos::sync::SyncState::Idle)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_blockGenerationInterval));
            return;
        }

        // seal
        std::shared_ptr<bcos::eth::Transactions> txs = m_txPool->pendingList();

        if (0 == txs->size())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_blockGenerationInterval));
            return;
        }

        int64_t currentNumber = m_blockChain->number();
        bcos::h256 const& parentHash = m_blockChain->numberHash(currentNumber);

        m_sync->noteSealingBlockNumber(currentNumber + 1);

        BlockPtr block = newBlock(parentHash, currentNumber + 1, txs);
        bcos::blockverifier::BlockInfo parentBlockInfo;
        bcos::blockverifier::ExecutiveContext::Ptr exeCtx =
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
    BlockPtr newBlock(bcos::h256 _parentHash, int64_t _currentNumner,
        std::shared_ptr<bcos::eth::Transactions> _txs)
    {
        // Generate block header
        BlockHeaderPtr header = newBlockHeader(_parentHash, _currentNumner);
        bcos::bytes blockHeaderBytes;
        header->encode(blockHeaderBytes);

        // Generate block
        BlockPtr block = std::make_shared<bcos::eth::Block>();
        block->setSigList(sigList(_txs->size()));
        block->setTransactions(_txs);

        // Add block header into block
        block->setBlockHeader(*header);

        return block;
    }

    BlockHeaderPtr newBlockHeader(bcos::h256 _parentHash, int64_t _currentNumner)
    {
        BlockHeaderPtr blockHeader = std::make_shared<bcos::eth::BlockHeader>();
        blockHeader->setParentHash(_parentHash);
        blockHeader->setRoots(bcos::crypto::Hash("transactionRoot"),
            bcos::crypto::Hash("receiptRoot"), bcos::crypto::Hash("stateRoot"));
        blockHeader->setLogBloom(bcos::eth::LogBloom(0));
        blockHeader->setNumber(_currentNumner);
        blockHeader->setGasLimit(bcos::u256(3000000));
        blockHeader->setGasUsed(bcos::u256(100000));
        uint64_t current_time = 100000;  // utcTime();
        blockHeader->setTimestamp(current_time);
        blockHeader->appendExtraDataArray(bcos::jsToBytes("0x1020"));
        blockHeader->setSealer(bcos::u256(12));
        std::vector<bcos::h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(bcos::toPublic(bcos::Secret(bcos::h256(i))));
        }
        blockHeader->setSealerList(sealer_list);
        return blockHeader;
    }

    std::shared_ptr<SigList> sigList(size_t _size)
    {
        std::shared_ptr<SigList> retList = std::make_shared<SigList>();
        /// set sig list
        bcos::h256 block_hash;
        auto keyPair = bcos::KeyPair::create();
        for (size_t i = 0; i < _size; i++)
        {
            block_hash = bcos::crypto::Hash("block " + std::to_string(i));
            auto sig = bcos::crypto::Sign(keyPair, block_hash);
            retList->push_back(std::make_pair(bcos::u256(block_hash), sig->asBytes()));
        }
        return retList;
    }

private:
    /// transaction pool handler
    std::shared_ptr<bcos::txpool::TxPoolInterface> m_txPool;
    /// handler of the block chain module
    std::shared_ptr<bcos::blockchain::BlockChainInterface> m_blockChain;
    /// sync
    std::shared_ptr<bcos::sync::SyncInterface> m_sync;
    /// block verifier
    std::shared_ptr<bcos::blockverifier::BlockVerifierInterface> m_blockVerifier;

    size_t m_totalTxCommit;
    bcos::PROTOCOL_ID m_protocolId;
    bcos::h512 m_nodeId;
    bcos::GROUP_ID m_groupId;
    unsigned m_blockGenerationInterval;
};

class FakeInitializer : public bcos::initializer::InitializerInterface
{
public:
    typedef std::shared_ptr<FakeInitializer> Ptr;

    void init(std::string const& _path)
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);

        m_secureInitializer = std::make_shared<bcos::initializer::SecureInitializer>();
        m_secureInitializer->initConfig(pt);

        m_p2pInitializer = std::make_shared<bcos::initializer::P2PInitializer>();
        m_p2pInitializer->setSSLContext(m_secureInitializer->SSLContext());
        m_p2pInitializer->setKeyPair(m_secureInitializer->keyPair());
        m_p2pInitializer->initConfig(pt);
    }

public:
    bcos::initializer::SecureInitializer::Ptr secureInitializer() { return m_secureInitializer; }
    bcos::initializer::P2PInitializer::Ptr p2pInitializer() { return m_p2pInitializer; }

private:
    bcos::initializer::P2PInitializer::Ptr m_p2pInitializer;
    bcos::initializer::SecureInitializer::Ptr m_secureInitializer;
};
