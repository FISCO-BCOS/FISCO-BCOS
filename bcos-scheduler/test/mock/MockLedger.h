#pragma once

#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/Protocol.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockLedger : public bcos::ledger::LedgerInterface
{
public:
    task::Task<std::string> prewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        bool writeTxsAndReceipts, std::optional<bcos::crypto::HashType>, bool writeNonces) override
    {
        (void)writeTxsAndReceipts;
        (void)writeNonces;
        BOOST_CHECK_EQUAL(block->blockHeader()->number(), 100);
        co_return std::string{};
    }

    task::Task<void> preStoreBlockTxs(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override
    {
        co_return;
    }

    bcos::Error::Ptr storeTransactionsAndReceipts(bcos::protocol::ConstTransactionsPtr blockTxs,
        bcos::protocol::Block::ConstPtr block) override
    {
        return nullptr;
    }

    task::Task<protocol::Block::Ptr> getBlockDataByNumber(protocol::BlockNumber, int32_t) override
    {
        co_return nullptr;
    }

    task::Task<protocol::BlockNumber> getBlockNumber() override { co_return 100; }

    task::Task<crypto::HashType> getBlockHashByNumber(protocol::BlockNumber _blockNumber) override
    {
        BOOST_CHECK_EQUAL(_blockNumber, 100);
        co_return h256(110);
    }

    task::Task<protocol::BlockNumber> getBlockNumberByHash(crypto::HashType) override
    {
        co_return -1;
    }

    task::Task<BatchTransactionsResult> getBatchTxsByHashList(crypto::HashListPtr, bool) override
    {
        co_return BatchTransactionsResult{};
    }

    task::Task<TransactionReceiptResult> getTransactionReceiptByHash(
        crypto::HashType, bool) override
    {
        co_return TransactionReceiptResult{};
    }

    task::Task<TransactionCountResult> getTotalTransactionCount() override
    {
        co_return TransactionCountResult{};
    }

    task::Task<std::optional<bcos::storage::Entry>> getCurrentStateByKey(std::string_view) override
    {
        co_return std::nullopt;
    }

    task::Task<SystemConfigEntry> getSystemConfigByKey(std::string_view _key) override
    {
        if (_key == ledger::SYSTEM_KEY_TX_COUNT_LIMIT)
        {
            co_return SystemConfigEntry{"100", 100};
        }
        if (_key == ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD)
        {
            co_return SystemConfigEntry{"300", 100};
        }
        if (_key == ledger::SYSTEM_KEY_TX_GAS_LIMIT)
        {
            co_return SystemConfigEntry{"300000000", 100};
        }
        if (_key == ledger::SYSTEM_KEY_COMPATIBILITY_VERSION)
        {
            co_return SystemConfigEntry{bcos::protocol::RC4_VERSION_STR, 100};
        }
        BOOST_FAIL("Unknown query key");
        co_return SystemConfigEntry{"", 0};
    }

    task::Task<consensus::ConsensusNodeList> getNodeListByType(std::string_view _type) override
    {
        if (_type == ledger::CONSENSUS_SEALER)
        {
            co_return consensus::ConsensusNodeList(1);
        }
        if (_type == ledger::CONSENSUS_OBSERVER)
        {
            co_return consensus::ConsensusNodeList(2);
        }
        if (_type == ledger::CONSENSUS_CANDIDATE_SEALER)
        {
            co_return consensus::ConsensusNodeList(1);
        }
        if (_type.empty())
        {
            consensus::ConsensusNodeList nodeList(4);
            nodeList[0].type = consensus::Type::consensus_sealer;
            nodeList[1].type = consensus::Type::consensus_observer;
            nodeList[2].type = consensus::Type::consensus_observer;
            nodeList[3].type = consensus::Type::consensus_candidate_sealer;
            co_return std::move(nodeList);
        }
        BOOST_FAIL("Unknown query type");
        co_return consensus::ConsensusNodeList{};
    }

    task::Task<NonceListResult> getNonceList(protocol::BlockNumber, int64_t) override
    {
        co_return nullptr;
    }

    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}
};
#pragma GCC diagnostic pop
}  // namespace bcos::test
