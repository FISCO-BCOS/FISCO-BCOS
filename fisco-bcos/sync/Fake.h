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
#include <libinitializer/CommonInitializer.h>
#include <libinitializer/InitializerInterface.h>
#include <libinitializer/P2PInitializer.h>
#include <libinitializer/SecureInitializer.h>
#include <libsync/Common.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <unistd.h>
#include <ctime>

using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::blockverifier;
using namespace dev::initializer;
class FakeConcensus : public Worker
{
    // FakeConcensus, only do: fetch tx from txPool and commit newBlock into blockchain
public:
    using BlockHeaderPtr = std::shared_ptr<dev::eth::BlockHeader>;
    using BlockPtr = std::shared_ptr<dev::eth::Block>;
    using TxPtr = std::shared_ptr<dev::eth::Transaction>;
    using SigList = std::vector<std::pair<u256, Signature>>;

public:
    FakeConcensus(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _sync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        unsigned _idleWaitMs = 30)
      : Worker("FakeConcensusForSync", _idleWaitMs),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_sync(_sync),
        m_blockVerifier(_blockVerifier),
        m_totalTxCommit(0),
        m_protocolId(0)
    {}

    virtual ~FakeConcensus(){};

    void start() { startWorking(); }

    /// stop blockSync
    void stop() { stopWorking(); }
    /// doWork every idleWaitMs
    virtual void doWork() override
    {
        if (m_sync->status().state != SyncState::Idle)
            return;

        Transactions const& txs = m_txPool->pendingList();
        int64_t currentNumber = m_blockChain->number();
        h256 const& parentHash = m_blockChain->numberHash(currentNumber);

        m_sync->noteSealingBlockNumber(currentNumber + 1);

        BlockPtr block = newBlock(parentHash, currentNumber + 1, txs);
        BlockInfo parentBlockInfo;
        ExecutiveContext::Ptr exeCtx = m_blockVerifier->executeBlock(*block, parentBlockInfo);
        m_blockChain->commitBlock(*block, exeCtx);
        m_txPool->dropBlockTrans(*block);
        m_totalTxCommit += txs.size();

        SYNCLOG(TRACE) << "[Commit] Conencus block commit "
                          "[blockNumber/txNumber/totalTxCommitThisNode/blockHash/parentHash]: "
                       << currentNumber + 1 << "/" << txs.size() << "/" << m_totalTxCommit << "/"
                       << block->headerHash() << "/" << parentHash << std::endl;
    }

private:
    BlockPtr newBlock(
        dev::h256 _parentHash, int64_t _currentNumner, dev::eth::Transactions const& _txs)
    {
        // Generate block header
        BlockHeaderPtr header = newBlockHeader(_parentHash, _currentNumner);
        bytes blockHeaderBytes;
        header->encode(blockHeaderBytes);

        // Generate block
        BlockPtr block = std::make_shared<Block>();
        block->setSigList(sigList(_txs.size()));
        block->setTransactions(_txs);

        // Add block header into block
        block->setBlockHeader(*header);

        return block;
    }

    BlockHeaderPtr newBlockHeader(dev::h256 _parentHash, int64_t _currentNumner)
    {
        BlockHeaderPtr blockHeader = std::make_shared<BlockHeader>();
        blockHeader->setParentHash(_parentHash);
        blockHeader->setRoots(sha3("transactionRoot"), sha3("receiptRoot"), sha3("stateRoot"));
        blockHeader->setLogBloom(LogBloom(0));
        blockHeader->setNumber(_currentNumner);
        blockHeader->setGasLimit(u256(3000000));
        blockHeader->setGasUsed(u256(100000));
        uint64_t current_time = 100000;  // utcTime();
        blockHeader->setTimestamp(current_time);
        blockHeader->appendExtraDataArray(jsToBytes("0x1020"));
        blockHeader->setSealer(u256(12));
        std::vector<h512> sealer_list;
        for (unsigned int i = 0; i < 10; i++)
        {
            sealer_list.push_back(toPublic(Secret(h256(i))));
        }
        blockHeader->setSealerList(sealer_list);
        return blockHeader;
    }

    SigList sigList(size_t _size)
    {
        SigList retList;
        /// set sig list
        Signature sig;
        h256 block_hash;
        Secret sec = KeyPair::create().secret();
        for (size_t i = 0; i < _size; i++)
        {
            block_hash = sha3(toString("block " + i));
            sig = sign(sec, block_hash);
            retList.push_back(std::make_pair(u256(block_hash), sig));
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
    PROTOCOL_ID m_protocolId;
};

class FakeInitializer : public InitializerInterface
{
public:
    typedef std::shared_ptr<FakeInitializer> Ptr;

    void init(std::string const& _path)
    {
        boost::property_tree::ptree pt;
        boost::property_tree::read_ini(_path, pt);

        m_commonInitializer = std::make_shared<CommonInitializer>();
        m_commonInitializer->initConfig(pt);

        m_secureInitiailizer = std::make_shared<SecureInitializer>();
        m_secureInitiailizer->setDataPath(m_commonInitializer->dataPath());
        m_secureInitiailizer->initConfig(pt);

        m_p2pInitializer = std::make_shared<P2PInitializer>();
        m_p2pInitializer->setSSLContext(m_secureInitiailizer->SSLContext());
        m_p2pInitializer->setKeyPair(m_secureInitiailizer->keyPair());
        m_p2pInitializer->initConfig(pt);
    }

public:
    CommonInitializer::Ptr commonInitializer() { return m_commonInitializer; }
    SecureInitializer::Ptr secureInitiailizer() { return m_secureInitiailizer; }
    P2PInitializer::Ptr p2pInitializer() { return m_p2pInitializer; }

private:
    CommonInitializer::Ptr m_commonInitializer;
    P2PInitializer::Ptr m_p2pInitializer;
    SecureInitializer::Ptr m_secureInitiailizer;
};
