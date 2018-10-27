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
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <unistd.h>
#include <ctime>

using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::blockverifier;


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
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> _blockVerifier,
        unsigned _idleWaitMs = 30)
      : Worker("FakeConcensusForSync", _idleWaitMs),
        m_txPool(_txPool),
        m_blockChain(_blockChain),
        m_blockVerifier(_blockVerifier)
    {}

    virtual ~FakeConcensus(){};

    void start() { startWorking(); }

    /// stop blockSync
    void stop() { stopWorking(); }
    /// doWork every idleWaitMs
    virtual void doWork() override
    {
        Transactions const& txs = m_txPool->pendingList();
        int64_t currentNumber = m_blockChain->number();
        h256 const& parentHash = m_blockChain->numberHash(currentNumber);
        BlockPtr block = newBlock(parentHash, currentNumber, txs);
        ExecutiveContext::Ptr exeCtx = m_blockVerifier->executeBlock(*block);
        m_blockChain->commitBlock(*block, exeCtx);
        m_txPool->dropBlockTrans(*block);

        LOG(INFO) << "[SYNC] Block commit: " << currentNumber + 1 << " with " << txs.size()
                  << " transactions";
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
        BlockPtr block = make_shared<Block>();
        block->setSigList(sigList(_txs.size()));
        block->setTransactions(_txs);

        // Add block header into block
        bytes blockBytes;
        block->encode(blockBytes, ref(blockHeaderBytes));
        block->decode(ref(blockBytes));

        return block;
    }

    BlockHeaderPtr newBlockHeader(dev::h256 _parentHash, int64_t _currentNumner)
    {
        BlockHeaderPtr blockHeader = make_shared<BlockHeader>();
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
    /// block verifier
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier;
};
