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
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts,
        std::optional<bcos::ledger::Features>) override
    {
        BOOST_CHECK_EQUAL(block->blockHeaderConst()->number(), 100);
        callback("", nullptr);
    }
    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr,
        bcos::protocol::Block::ConstPtr, std::function<void(Error::UniquePtr&&)> _callback) override
    {
        if (!_callback)
        {
            return;
        }
        _callback(nullptr);
    }
    bcos::Error::Ptr storeTransactionsAndReceipts(bcos::protocol::ConstTransactionsPtr blockTxs,
        bcos::protocol::Block::ConstPtr block) override
    {
        return nullptr;
    }

    void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock) override
    {}

    void asyncGetBlockNumber(
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {
        _onGetBlock(nullptr, 100);
    }

    void asyncGetBlockHashByNumber(protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock) override
    {
        BOOST_CHECK_EQUAL(_blockNumber, 100);
        _onGetBlock(nullptr, h256(110));
    }

    void asyncGetBlockNumberByHash(crypto::HashType const& _blockHash,
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {}

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            _onGetTx) override
    {}

    void asyncGetTransactionReceiptByHash(crypto::HashType const& _txHash, bool _withProof,
        std::function<void(
            Error::Ptr, protocol::TransactionReceipt::Ptr, bcos::ledger::MerkleProofPtr)>
            _onGetTx) override
    {}

    void asyncGetTotalTransactionCount(std::function<void(Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, protocol::BlockNumber _latestBlockNumber)>
            _callback) override
    {}

    void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> _onGetConfig) override
    {
        if (_key == ledger::SYSTEM_KEY_TX_COUNT_LIMIT)
        {
            _onGetConfig(nullptr, "100", 100);
        }
        else if (_key == ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD)
        {
            _onGetConfig(nullptr, "300", 100);
        }
        else if (_key == ledger::SYSTEM_KEY_TX_GAS_LIMIT)
        {
            _onGetConfig(nullptr, "300000000", 100);
        }
        else if (_key == ledger::SYSTEM_KEY_COMPATIBILITY_VERSION)
        {
            _onGetConfig(nullptr, bcos::protocol::RC4_VERSION_STR, 100);
        }
        else
        {
            BOOST_FAIL("Unknown query key");
        }
    }

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeList)> _onGetConfig) override
    {
        if (_type == ledger::CONSENSUS_SEALER)
        {
            _onGetConfig(nullptr, consensus::ConsensusNodeList(1));
        }
        else if (_type == ledger::CONSENSUS_OBSERVER)
        {
            _onGetConfig(nullptr, consensus::ConsensusNodeList(2));
        }
        else if (_type == ledger::CONSENSUS_CANDIDATE_SEALER)
        {
            _onGetConfig(nullptr, consensus::ConsensusNodeList(1));
        }
        else if (_type.empty())
        {
            consensus::ConsensusNodeList nodeList(4);
            nodeList[0].type = consensus::Type::consensus_sealer;
            nodeList[1].type = consensus::Type::consensus_observer;
            nodeList[2].type = consensus::Type::consensus_observer;
            nodeList[3].type = consensus::Type::consensus_candidate_sealer;
            _onGetConfig(nullptr, nodeList);
        }
        else
        {
            BOOST_FAIL("Unknown query type");
        }
    }

    void asyncGetNonceList(protocol::BlockNumber _startNumber, int64_t _offset,
        std::function<void(
            Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
            _onGetList) override
    {}
    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}
};
#pragma GCC diagnostic pop
}  // namespace bcos::test