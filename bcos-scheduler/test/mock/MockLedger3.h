#pragma once

#include "bcos-framework/bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/bcos-framework/protocol/Block.h"
#include "bcos-framework/bcos-framework/protocol/Protocol.h"
#include "bcos-framework/bcos-framework/protocol/Transaction.h"
#include "bcos-framework/bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/bcos-framework/storage/StorageInterface.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <gsl/span>
#include <map>


using namespace bcos::ledger;


namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
class MockLedger3 : public bcos::ledger::LedgerInterface
{
public:
    MockLedger3() : LedgerInterface() {}
    using Ptr = std::shared_ptr<MockLedger3>;
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::Ptr&&)> callback) override
    {
        auto blockNumber = block->blockHeader()->number();
        SCHEDULER_LOG(DEBUG) << LOG_KV("blockNumber", blockNumber);
        if (blockNumber == 1024)
        {
            callback(BCOS_ERROR_PTR(LedgerError::CollectAsyncCallbackError, "PrewriteBlock error"));
            return;
        }
        callback(nullptr);
    }

    void asyncStoreTransactions(std::shared_ptr<std::vector<bytesConstPtr>> _txToStore,
        crypto::HashListPtr _txHashList, std::function<void(Error::Ptr)> _onTxStored) override
    {}

    void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock) override
    {}

    void asyncGetBlockNumber(
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {
        _onGetBlock(nullptr, 5);
    }

    void asyncGetBlockHashByNumber(protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock) override
    {
        BOOST_CHECK_EQUAL(_blockNumber, 5);
        _onGetBlock(nullptr, h256(5));
    }

    void asyncGetBlockNumberByHash(crypto::HashType const& _blockHash,
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) override
    {}

    void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
            _onGetTx) override
    {}

    void asyncGetTransactionReceiptByHash(crypto::HashType const& _txHash, bool _withProof,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::ConstPtr, MerkleProofPtr)>
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
            _onGetConfig(nullptr, "100", 5);
        }
        else if (_key == ledger::SYSTEM_KEY_CONSENSUS_LEADER_PERIOD)
        {
            _onGetConfig(nullptr, "300", 5);
        }
        else if (_key == ledger::SYSTEM_KEY_TX_GAS_LIMIT)
        {
            _onGetConfig(nullptr, "300000000", 5);
        }
        else if (_key == ledger::SYSTEM_KEY_COMPATIBILITY_VERSION)
        {
            _onGetConfig(nullptr, bcos::protocol::RC4_VERSION_STR, 5);
        }
        else
        {
            BOOST_FAIL("Unknown query key");
        }
    }

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig) override
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
            _onGetList) override
    {}

    void asyncPreStoreBlockTxs(bcos::protocol::TransactionsPtr _blockTxs,
        bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::UniquePtr&&)> _callback) override
    {}

    void commitSuccess(bool _success)
    {
        if (_success)
        {
            ++commitBlockNumber;
            SCHEDULER_LOG(DEBUG) << "---- mockLedger -----"
                                 << LOG_KV("CommitBlock success, commitBlockNumber",
                                        commitBlockNumber);
        }
        else
        {
            SCHEDULER_LOG(DEBUG) << "---- mockLedger -----"
                                 << LOG_KV(
                                        "CommitBlock failed, commitBlockNumber", commitBlockNumber);
        }
    }

private:
    bcos::protocol::BlockNumber commitBlockNumber = 5;
};

#pragma GCC diagnostic pop
}  // namespace bcos::test