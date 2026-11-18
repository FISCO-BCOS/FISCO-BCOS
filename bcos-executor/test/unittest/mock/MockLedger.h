#pragma once

#include "MockBlock.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/storage/Serialize.h"
#include "bcos-framework/storage/StorageInterface.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/test/unit_test.hpp>
#include <future>
#include <sstream>

namespace bcos::test
{
using namespace bcos::ledger;

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

    task::Task<std::string> prewriteBlock(bcos::storage::StorageInterface::Ptr,
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr, bool,
        std::optional<bcos::crypto::HashType>, bool) override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return std::string{};
    };

    task::Task<void> preStoreBlockTxs(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override
    {
        co_return;
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

    task::Task<protocol::Block::Ptr> getBlockDataByNumber(protocol::BlockNumber, int32_t) override
    {
        auto block = std::make_shared<MockBlock>();
        if (m_blockHeader)
        {
            block->setBlockHeader(m_blockHeader);
        }
        block->blockHeader()->setNumber(m_blockNumber);
        auto hashImpl = std::make_shared<crypto::Keccak256>();
        block->blockHeader()->calculateHash(*hashImpl);
        co_return std::move(block);
    };

    protocol::BlockNumber m_blockNumber = 0;
    void setBlockNumber(protocol::BlockNumber blockNumber) { m_blockNumber = blockNumber; }

    task::Task<protocol::BlockNumber> getBlockNumber() override { co_return m_blockNumber; };

    task::Task<crypto::HashType> getBlockHashByNumber(protocol::BlockNumber _blockNumber) override
    {
        co_return crypto::HashType(_blockNumber);
    };

    task::Task<protocol::BlockNumber> getBlockNumberByHash(crypto::HashType) override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return -1;
    };

    task::Task<BatchTransactionsResult> getBatchTxsByHashList(crypto::HashListPtr, bool) override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return BatchTransactionsResult{};
    };

    task::Task<TransactionReceiptResult> getTransactionReceiptByHash(
        crypto::HashType, bool) override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return TransactionReceiptResult{};
    };

    task::Task<TransactionCountResult> getTotalTransactionCount() override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return TransactionCountResult{};
    };

    task::Task<std::optional<bcos::storage::Entry>> getCurrentStateByKey(std::string_view) override
    {
        co_return std::nullopt;
    }

    task::Task<SystemConfigEntry> getSystemConfigByKey(std::string_view _key) override
    {
        if (std::string(bcos::ledger::SYSTEM_KEY_COMPATIBILITY_VERSION) == std::string(_key))
        {
            std::stringstream ss;
            ss << bcos::protocol::BlockVersion::MAX_VERSION;
            co_return SystemConfigEntry{ss.str(), m_blockNumber};
        }
        if (std::string(bcos::ledger::SYSTEM_KEY_TX_GAS_LIMIT) == std::string(_key))
        {
            co_return SystemConfigEntry{std::to_string(MockLedger::TX_GAS_LIMIT), m_blockNumber};
        }
        if (std::string(bcos::ledger::SYSTEM_KEY_WEB3_CHAIN_ID) == std::string(_key))
        {
            co_return SystemConfigEntry{"20200", m_blockNumber};
        }
        if (std::string(bcos::ledger::SYSTEM_KEY_AUTH_CHECK_STATUS) == std::string(_key))
        {
            if (m_storage)
            {
                std::promise<storage::Entry> promise;
                m_storage->asyncGetRow(ledger::SYS_CONFIG, ledger::SYSTEM_KEY_AUTH_CHECK_STATUS,
                    [&promise](auto&& e, std::optional<storage::Entry> entry) {
                        promise.set_value(entry.value());
                    });
                auto rawEntry = promise.get_future().get();
                auto entry =
                    bcos::storage::serialize::decode<ledger::SystemConfigEntry>(rawEntry.get());
                co_return entry;
            }
            co_return SystemConfigEntry{"0", m_blockNumber};
        }

        co_return SystemConfigEntry{"0", 0};
    };

    task::Task<consensus::ConsensusNodeList> getNodeListByType(std::string_view) override
    {
        co_return consensus::ConsensusNodeList{};
    };

    task::Task<NonceListResult> getNonceList(protocol::BlockNumber, int64_t) override
    {
        BOOST_CHECK(false);  // Need implementations
        co_return nullptr;
    };

    void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) override {}
};
}  // namespace bcos::test
