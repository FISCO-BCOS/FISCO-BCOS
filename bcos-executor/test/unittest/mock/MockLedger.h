#pragma once

#include "MockBlock.h"
#include "bcos-framework/storage/StorageInterface.h"
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/test/unit_test.hpp>
#include <future>
#include <sstream>

namespace bcos::test
{
class MockLedger : public bcos::ledger::LedgerInterface
{
public:
    using Ptr = std::shared_ptr<MockLedger>;
    static const uint32_t TX_GAS_LIMIT = 3000000666;
    bcos::storage::TransactionalStorageInterface::Ptr m_storage = nullptr;

    MockLedger() = default;
    MockLedger(bcos::storage::TransactionalStorageInterface::Ptr _storage)
      : m_storage(std::move(_storage))
    {}

    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(std::string, Error::Ptr&&)> callback, bool writeTxsAndReceipts) override
    {
        BOOST_CHECK(false);  // Need implementations
    };
    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr,
        bcos::protocol::Block::ConstPtr, std::function<void(Error::UniquePtr&&)> _callback) override
    {
        if (!_callback)
        {
            return;
        }
        _callback(nullptr);
    }

    bcos::Error::Ptr storeTransactionsAndReceipts(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override
    {
        BOOST_CHECK(false);  // Need implementations
        return nullptr;
    };

    protocol::BlockHeader::Ptr m_blockHeader;
    void setBlockHeader(protocol::BlockHeader::Ptr blockHeader)
    {
        if (blockHeader)
        {
            m_blockHeader = blockHeader;
            m_blockNumber = blockHeader->number();
        }
    }
    void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock) override
    {
        auto block = std::make_shared<MockBlock>();
        if (m_blockHeader)
        {
            block->setBlockHeader(m_blockHeader);
        }
        block->blockHeader()->setNumber(m_blockNumber);
        _onGetBlock(nullptr, block);
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
        _onGetBlock(nullptr, crypto::HashType(_blockNumber));
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

    void asyncGetCurrentStateByKey(std::string_view const& _key,
        std::function<void(Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback) override
    {}


    void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> _onGetConfig) override
    {
        if (std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION) == std::string(_key))
        {
            std::stringstream ss;
            ss << bcos::protocol::BlockVersion::MAX_VERSION;

            _onGetConfig(nullptr, ss.str(), m_blockNumber);
            return;
        }
        else if (std::string(bcos::ledger::SYSTEM_KEY_TX_GAS_LIMIT) == std::string(_key))
        {
            _onGetConfig(nullptr, std::to_string(MockLedger::TX_GAS_LIMIT), m_blockNumber);
            return;
        }
        else if (std::string(bcos::ledger::SYSTEM_KEY_WEB3_CHAIN_ID) == std::string(_key))
        {
            _onGetConfig(nullptr, "20200", m_blockNumber);
            return;
        }
        else if (std::string(bcos::ledger::SYSTEM_KEY_AUTH_CHECK_STATUS) == std::string(_key))
        {
            if (m_storage)
            {
                std::promise<storage::Entry> promise;
                m_storage->asyncGetRow(ledger::SYS_CONFIG, ledger::SYSTEM_KEY_AUTH_CHECK_STATUS,
                    [&promise](auto&& e, std::optional<storage::Entry> entry) {
                        promise.set_value(entry.value());
                    });
                auto entry = promise.get_future().get().getObject<ledger::SystemConfigEntry>();
                _onGetConfig(nullptr, std::get<0>(entry), std::get<1>(entry));
                return;
            }
            _onGetConfig(nullptr, "0", m_blockNumber);
            return;
        }


        BOOST_CHECK(false);  // Need implementations
    };


    void asyncGetNodeListByType(std::string_view const& _type,
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

    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}
};
}  // namespace bcos::test