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
 *         fake BlockVerifierInterface
 *         fake Ledger
 * @file: Fake.h
 * @author: chaychen
 * @date: 2018-10-08
 */

#pragma once
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libdevcrypto/Common.h>
#include <libprotocol/Block.h>
#include <libprotocol/CommonJS.h>
#include <libprotocol/Transaction.h>
#include <libprotocol/TransactionReceipt.h>
#include <libeventfilter/EventLogFilterManager.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerParam.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <unistd.h>
#include <ctime>

using namespace bcos;
using namespace bcos::blockchain;
using namespace bcos::protocol;
using namespace bcos::sync;
using namespace bcos::blockverifier;
using namespace bcos::ledger;
class FakeBlockChain : public BlockChainInterface
{
public:
    FakeBlockChain()
    {
        m_blockNumber = 1;
        m_totalTransactionCount = 0;
        bytes m_blockHeaderData = bytes();
        bytes m_blockData = bytes();
        BlockHeader blockHeader;
        blockHeader.setSealer(u256(1));
        blockHeader.setNumber(0);
        blockHeader.setTimestamp(0);
        Block block;
        block.setBlockHeader(blockHeader);
        block.encode(m_blockData);
        /// block.decode(ref(m_blockData));
        m_blockHash[block.blockHeaderHash()] = 0;
        m_blockChain.push_back(std::make_shared<Block>(block));
    }

    ~FakeBlockChain() {}

    int64_t number() override
    {
        ReadGuard l(x_blockChain);
        return m_blockChain.size() - 1;
    }

    std::pair<int64_t, int64_t> totalTransactionCount() override
    {
        ReadGuard l(x_blockChain);
        return std::make_pair(m_totalTransactionCount, m_blockChain.size() - 1);
    }

    std::pair<int64_t, int64_t> totalFailedTransactionCount() override
    {
        ReadGuard l(x_blockChain);
        return std::make_pair(m_totalFailedTransactionCount, m_blockChain.size() - 1);
    }

    bcos::h256 numberHash(int64_t _i) override
    {
        ReadGuard l(x_blockChain);
        return m_blockChain[_i]->headerHash();
    }

    std::shared_ptr<bcos::protocol::Block> getBlockByHash(
        bcos::h256 const& _blockHash, int64_t = -1) override
    {
        ReadGuard l(x_blockChain);
        if (m_blockHash.count(_blockHash))
            return m_blockChain[m_blockHash[_blockHash]];
        return nullptr;
    }

    std::shared_ptr<bcos::bytes> getBlockRLPByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i))->rlpP();
    }

    bcos::protocol::LocalisedTransaction::Ptr getLocalisedTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<LocalisedTransaction>();
    }
    bcos::protocol::Transaction::Ptr getTxByHash(bcos::h256 const&) override
    {
        return std::make_shared<Transaction>();
    }
    bcos::protocol::TransactionReceipt::Ptr getTransactionReceiptByHash(bcos::h256 const&) override
    {
        return std::make_shared<TransactionReceipt>();
    }

    bcos::protocol::LocalisedTransactionReceipt::Ptr getLocalisedTxReceiptByHash(
        bcos::h256 const&) override
    {
        return std::make_shared<LocalisedTransactionReceipt>(
            TransactionReceipt(), h256(0), h256(0), -1, Address(), Address(), -1, 0);
    }

    std::shared_ptr<bcos::protocol::Block> getBlockByNumber(int64_t _i) override
    {
        return getBlockByHash(numberHash(_i));
    }
    std::pair<bcos::protocol::LocalisedTransactionReceipt::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionReceiptByHashWithProof(
        bcos::h256 const&, bcos::protocol::LocalisedTransaction&) override
    {
        return std::make_pair(
            std::make_shared<LocalisedTransactionReceipt>(bcos::protocol::TransactionException::None),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    std::pair<LocalisedTransaction::Ptr,
        std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>>
    getTransactionByHashWithProof(bcos::h256 const&) override
    {
        return std::make_pair(std::make_shared<LocalisedTransaction>(),
            std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>>());
    }
    CommitResult commitBlock(std::shared_ptr<bcos::protocol::Block> block,
        std::shared_ptr<bcos::blockverifier::ExecutiveContext>) override
    {
        if (block->blockHeader().number() == number() + 1)
        {
            WriteGuard l(x_blockChain);
            {
                m_blockHash[block->blockHeader().hash()] = block->blockHeader().number();
                m_blockChain.push_back(std::make_shared<Block>(*block));
                m_blockNumber = block->blockHeader().number() + 1;
                m_totalTransactionCount += block->transactions()->size();
            }
            m_onReady(m_blockNumber);
        }
        return CommitResult::OK;
    }

    std::shared_ptr<std::vector<bcos::protocol::NonceKeyType>> getNonces(int64_t) override
    {
        return std::make_shared<std::vector<bcos::protocol::NonceKeyType>>();
    }

    bool checkAndBuildGenesisBlock(
        std::shared_ptr<bcos::ledger::LedgerParamInterface>, bool = true) override
    {
        return true;
    }

    bcos::h512s sealerList() override { return bcos::h512s(); };
    bcos::h512s observerList() override { return bcos::h512s(); };
    std::string getSystemConfigByKey(std::string const&, int64_t = -1) override
    {
        return "300000000";
    };

    bcos::bytes getCode(bcos::Address) override { return bytes(); }

private:
    std::map<h256, uint64_t> m_blockHash;
    std::vector<std::shared_ptr<Block>> m_blockChain;
    uint64_t m_blockNumber;
    uint64_t m_totalTransactionCount;
    uint64_t m_totalFailedTransactionCount;
    mutable SharedMutex x_blockChain;
};

class FakeBlockSync : public SyncInterface
{
public:
    FakeBlockSync() { m_status.state = SyncState::Idle; };
    virtual ~FakeBlockSync(){};
    /// start blockSync
    void start() override{};
    /// stop blockSync
    void stop() override{};

    /// get status of block sync
    /// @returns Synchonization status
    SyncStatus status() const override { return m_status; };
    bool isSyncing() const override { return false; };

    /// protocol id used when register handler to p2p module
    PROTOCOL_ID const& protocolId() const override { return m_protocolID; };
    void setProtocolId(PROTOCOL_ID const) override{};

    void registerConsensusVerifyHandler(std::function<bool(bcos::protocol::Block const&)>) override{};

private:
    SyncStatus m_status;
    PROTOCOL_ID m_protocolID = 0;
};

class FakeBlockVerifier : public BlockVerifierInterface
{
public:
    FakeBlockVerifier()
    {
        m_executiveContext = std::make_shared<ExecutiveContext>();
        std::srand(std::time(nullptr));
    };
    virtual ~FakeBlockVerifier(){};
    std::shared_ptr<ExecutiveContext> executeBlock(
        bcos::protocol::Block& block, BlockInfo const&) override
    {
        /// execute time: 1000
        /// usleep(1000 * (block.getTransactionSize()));
        fakeExecuteResult(block);
        return m_executiveContext;
    };
    /// fake the transaction receipt of the whole block
    void fakeExecuteResult(bcos::protocol::Block& block)
    {
        std::shared_ptr<TransactionReceipts> receipts = std::make_shared<TransactionReceipts>();
        for (unsigned index = 0; index < block.getTransactionSize(); index++)
        {
            TransactionReceipt::Ptr receipt = std::make_shared<TransactionReceipt>(u256(0),
                u256(100), LogEntries(), protocol::TransactionException::None, bytes(),
                (*block.transactions())[index]->receiveAddress());
            receipts->push_back(receipt);
        }
        block.setTransactionReceipts(receipts);
    }

    bcos::protocol::TransactionReceipt::Ptr executeTransaction(
        const bcos::protocol::BlockHeader&, bcos::protocol::Transaction::Ptr) override
    {
        bcos::protocol::TransactionReceipt::Ptr receipt =
            std::make_shared<bcos::protocol::TransactionReceipt>();
        return receipt;
    }

private:
    std::shared_ptr<ExecutiveContext> m_executiveContext;
};

class FakeLedger : public Ledger
{
public:
    FakeLedger(std::shared_ptr<bcos::p2p::P2PInterface> service, bcos::GROUP_ID const& _groupId,
        bcos::KeyPair const& _keyPair, std::string const&)
      : Ledger(service, _groupId, _keyPair)
    {}
    /// init the ledger(called by initializer)
    bool initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams) override
    {
        m_param = _ledgerParams;
        /// init dbInitializer
        m_dbInitializer = std::make_shared<bcos::ledger::DBInitializer>(m_param, m_groupId);
        /// init blockChain
        initBlockChain();
        /// intit blockVerifier
        initBlockVerifier();
        /// init txPool
        initTxPool();
        /// init sync
        Ledger::initSync();
        /// init consensus
        Ledger::consensusInitFactory();
        // init EventLogFilterManager
        Ledger::initEventLogFilterManager();
        return true;
    }

    /// init blockverifier related
    bool initBlockVerifier() override
    {
        m_blockVerifier = std::make_shared<FakeBlockVerifier>();
        return true;
    }
    bool initBlockChain() override
    {
        m_blockChain = std::make_shared<FakeBlockChain>();
        return true;
    }

    bool initEventLogFilterManager() override
    {
        m_eventLogFilterManger =
            std::make_shared<bcos::event::EventLogFilterManager>(nullptr, 0, 0);
        return true;
    }

    /// init the blockSync
    /// void initSync() override { m_sync = std::make_shared<FakeBlockSync>(); }
};
