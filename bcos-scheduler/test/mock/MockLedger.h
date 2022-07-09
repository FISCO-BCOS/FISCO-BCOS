#pragma once

#include "bcos-framework/interfaces/ledger/LedgerInterface.h"
#include "bcos-framework/interfaces/protocol/Protocol.h"
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockLedger : public bcos::ledger::LedgerInterface
{
public:
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::Ptr&&)> callback)
    {
        BOOST_CHECK_EQUAL(block->blockHeaderConst()->number(), 100);
        callback(nullptr);
    }
    void asyncPreStoreBlockTxs(bcos::protocol::TransactionsPtr, bcos::protocol::Block::ConstPtr,
        std::function<void(Error::UniquePtr&&)> _callback) override
    {
        if (!_callback)
        {
            return;
        }
        _callback(nullptr);
    }
    void asyncStoreTransactions(std::shared_ptr<std::vector<bytesConstPtr>> _txToStore,
        crypto::HashListPtr _txHashList, std::function<void(Error::Ptr)> _onTxStored)
    {}

    void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock)
    {}

    void asyncGetBlockNumber(std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock)
    {
        _onGetBlock(nullptr, 100);
    }

    void asyncGetBlockHashByNumber(protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock)
    {
        BOOST_CHECK_EQUAL(_blockNumber, 100);
        _onGetBlock(nullptr, h256(110));
    }

    void asyncGetBlockNumberByHash(crypto::HashType const& _blockHash,
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock)
    {}

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            _onGetTx)
    {}

    void asyncGetTransactionReceiptByHash(crypto::HashType const& _txHash, bool _withProof,
        std::function<void(
            Error::Ptr, protocol::TransactionReceipt::ConstPtr, bcos::ledger::MerkleProofPtr)>
            _onGetTx)
    {}

    void asyncGetTotalTransactionCount(std::function<void(Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, protocol::BlockNumber _latestBlockNumber)>
            _callback)
    {}

    void asyncGetSystemConfigByKey(std::string const& _key,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> _onGetConfig)
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

    void asyncGetNodeListByType(std::string const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig)
    {
        if (_type == ledger::CONSENSUS_SEALER)
        {
            _onGetConfig(nullptr, std::make_shared<consensus::ConsensusNodeList>(1));
        }
        else if (_type == ledger::CONSENSUS_OBSERVER)
        {
            _onGetConfig(nullptr, std::make_shared<consensus::ConsensusNodeList>(2));
        }
        else
        {
            BOOST_FAIL("Unknown query type");
        }
    }

    void asyncGetNonceList(protocol::BlockNumber _startNumber, int64_t _offset,
        std::function<void(
            Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
            _onGetList)
    {}
};
#pragma GCC diagnostic pop
}  // namespace bcos::test