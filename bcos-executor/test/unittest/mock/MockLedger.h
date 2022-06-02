#pragma once

#include <bcos-framework/interfaces/ledger/LedgerInterface.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
class MockLedger : public bcos::ledger::LedgerInterface
{
public:
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::Block::ConstPtr block, std::function<void(Error::Ptr&&)> callback) override
    {
        BOOST_CHECK(false);  // Need implementations
    };

    void asyncStoreTransactions(std::shared_ptr<std::vector<bytesConstPtr>> _txToStore,
        crypto::HashListPtr _txHashList, std::function<void(Error::Ptr)> _onTxStored) override
    {
        BOOST_CHECK(false);  // Need implementations
    };

    void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock) override
    {
        BOOST_CHECK(false);  // Need implementations
    };

    protocol::BlockNumber m_blockNumber = 0;
    void setBlockNumber(protocol::BlockNumber blockNumber) { m_blockNumber = blockNumber; }

    void asyncGetBlockNumber(
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {
        _onGetBlock(nullptr, m_blockNumber);
    };

    void asyncGetBlockHashByNumber(protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock) override
    {
        BOOST_CHECK(false);  // Need implemz'xentations
    };

    void asyncGetBlockNumberByHash(crypto::HashType const& _blockHash,
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {
        BOOST_CHECK(false);  // Need implementations
    };

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            _onGetTx) override
    {
        BOOST_CHECK(false);  // Need implementations
    };


    void asyncGetTransactionReceiptByHash(crypto::HashType const& _txHash, bool _withProof,
        std::function<void(
            Error::Ptr, protocol::TransactionReceipt::ConstPtr, bcos::ledger::MerkleProofPtr)>
            _onGetTx) override
    {
        BOOST_CHECK(false);  // Need implementations
    };


    void asyncGetTotalTransactionCount(std::function<void(Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, protocol::BlockNumber _latestBlockNumber)>
            _callback) override
    {
        BOOST_CHECK(false);  // Need implementations
    };


    void asyncGetSystemConfigByKey(std::string const& _key,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> _onGetConfig) override
    {
        BOOST_CHECK(false);  // Need implementations
    };


    void asyncGetNodeListByType(std::string const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig) override
    {
        BOOST_CHECK(false);  // Need implementations
    };

    void asyncGetNonceList(protocol::BlockNumber _startNumber, int64_t _offset,
        std::function<void(
            Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
            _onGetList) override
    {
        BOOST_CHECK(false);  // Need implementations
    };
};
}  // namespace bcos::test