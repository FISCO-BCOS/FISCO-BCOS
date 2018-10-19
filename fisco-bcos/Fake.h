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
 * @brief: fake interface
 *         fake Block
 *         fake BlockChainInterface
 *         fake SyncInterface
 *         fake BlockVerifierInterface
 * @file: Fake.h
 * @author: chaychen
 * @date: 2018-10-08
 */

#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libblocksync/SyncInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>

using namespace dev;
using namespace dev::blockchain;
using namespace dev::eth;
using namespace dev::sync;
using namespace dev::blockverifier;

class FakeBlockChain : public BlockChainInterface
{
public:
    FakeBlockChain()
    {
        m_blockNumber = 1;
        bytes m_blockHeaderData = bytes();
        bytes m_blockData = bytes();
        BlockHeader blockHeader;
        blockHeader.setSealer(u256(1));
        blockHeader.setNumber(0);
        blockHeader.setTimestamp(0);
        Block block;
        blockHeader.encode(m_blockHeaderData);
        block.encode(m_blockData, ref(m_blockHeaderData));
        block.decode(ref(m_blockData));
        m_blockHash[block.blockHeaderHash()] = 0;
        m_blockChain.push_back(std::make_shared<Block>(block));
    }

    ~FakeBlockChain() {}

    int64_t number() const { return m_blockNumber - 1; }

    dev::h256 numberHash(int64_t _i) const { return m_blockChain[_i]->headerHash(); }

    std::shared_ptr<dev::eth::Block> getBlockByHash(dev::h256 const& _blockHash) override
    {
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    dev::eth::Transaction getTxByHash(dev::h256 const& _txHash) override { return Transaction(); }

    std::shared_ptr<dev::eth::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }

    void commitBlock(
        dev::eth::Block& block, std::shared_ptr<dev::blockverifier::ExecutiveContext>) override
    {
        /// block.header().setParentHash(m_blockChain[m_blockNumber - 1]->header().hash());
        /// block.header().setNumber(m_blockNumber);
        m_blockHash[block.blockHeader().hash()] = m_blockNumber;
        m_blockChain.push_back(std::make_shared<Block>(block));
        m_blockNumber += 1;
    }

    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
};

class FakeBlockSync : public SyncInterface
{
public:
    FakeBlockSync() { m_status.state = SyncState::Idle; };
    virtual ~FakeBlockSync(){};
    /// start blockSync
    void start(){};
    /// stop blockSync
    void stop(){};

    /// get status of block sync
    /// @returns Synchonization status
    SyncStatus status() const override { return m_status; };
    bool isSyncing() const override { return false; };
    h256 latestBlockSent() override { return h256(); };

    /// for rpc && sdk: broad cast transaction to all nodes
    void broadCastTransactions() override{};
    /// for p2p: broad cast transaction to specified nodes
    void sendTransactions(NodeList const& _nodes) override{};

    /// abort sync and reset all status of blockSyncs
    void reset() override{};
    bool forceSync() override { return true; };

    /// protocol id used when register handler to p2p module
    int16_t const& getProtocolId() const override { return m_protocolID; };
    void setProtocolId(int16_t const _protocolId) override{};

private:
    SyncStatus m_status;
    int16_t m_protocolID = 0;
};

class FakeBlockVerifier : public BlockVerifierInterface
{
public:
    FakeBlockVerifier() { m_executiveContext = std::make_shared<ExecutiveContext>(); };
    virtual ~FakeBlockVerifier(){};
    std::shared_ptr<ExecutiveContext> executeBlock(dev::eth::Block& block) override
    {
        return m_executiveContext;
    };

private:
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};
