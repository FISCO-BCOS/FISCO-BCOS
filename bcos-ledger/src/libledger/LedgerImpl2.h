#pragma once

#include "Ledger.h"
#include "bcos-framework/storage2/StringPool.h"
#include "bcos-task/Task.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Exception.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/protocol/Block.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-executor/src/Common.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-table/src/StateStorageFactory.h>
#include <bcos-tool/bcos-tool/VersionConverter.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Ranges.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
#include <future>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace bcos::ledger
{

// clang-format off
struct NotFoundTransaction : public bcos::error::Exception {};
struct UnexpectedRowIndex : public bcos::error::Exception {};
struct MismatchTransactionCount : public bcos::error::Exception {};
struct MismatchParentHash: public bcos::error::Exception {};
struct NotFoundBlockHeader: public bcos::error::Exception {};
struct GetABIError : public bcos::error::Exception {};
// clang-format on

template <bcos::crypto::hasher::Hasher Hasher, bcos::transaction_executor::StateStorage Storage,
    protocol::IsBlockFactory BlockFactory>
class LedgerImpl2
{
private:
    Storage& m_storage;
    BlockFactory& m_blockFactory;
    transaction_executor::TableNamePool& m_tableNamePool;

public:
    LedgerImpl2(Storage& storage, BlockFactory& blockFactory,
        transaction_executor::TableNamePool& tableNamePool)
      : m_storage(storage), m_blockFactory(blockFactory), m_tableNamePool(tableNamePool)
    {}

    template <bcos::concepts::ledger::DataFlag... Flags>
    task::Task<void> setBlock(protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(INFO) << "setBlock: " << block.blockHeader()->number();

        auto blockNumberStr = boost::lexical_cast<std::string>(block.blockHeader()->number());
        (co_await setBlockData<Flags>(blockNumberStr, block), ...);
        co_return;
    }

    task::Task<bcos::concepts::ledger::Status> getStatus()
    {
        LEDGER_LOG(TRACE) << "getStatus";

        auto it = co_await m_storage.read(
            std::to_array({SYS_KEY_TOTAL_TRANSACTION_COUNT, SYS_KEY_TOTAL_FAILED_TRANSACTION,
                SYS_KEY_CURRENT_NUMBER}) |
            RANGES::views::transform([this](std::string_view key) {
                return transaction_executor::StateKey{
                    storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE), key};
            }));

        bcos::concepts::ledger::Status status;
        int i = 0;
        while (co_await it.next())
        {
            auto&& entry = co_await it.value();
            int64_t value = 0;
            if (entry)
            {
                value = boost::lexical_cast<int64_t>(entry.get());
            }
            switch (i)
            {
            case 0:
                status.total = value;
                break;
            case 1:
                status.failed = value;
                break;
            case 2:
                status.blockNumber = value;
                break;
            default:
                BOOST_THROW_EXCEPTION(
                    UnexpectedRowIndex{} << bcos::error::ErrorMessage{
                        "Unexpected getRows index: " + boost::lexical_cast<std::string>(i)});
                break;
            }
            ++i;
        }

        LEDGER_LOG(TRACE) << "getStatus result: " << status.total << " | " << status.failed << " | "
                          << status.blockNumber;

        co_return status;
    }

    template <bool isTransaction>
    task::Task<void> setTransactions(RANGES::range auto&& hashes, RANGES::range auto&& buffers)
    {
        if (RANGES::size(buffers) != RANGES::size(hashes))
        {
            BOOST_THROW_EXCEPTION(
                MismatchTransactionCount{} << bcos::error::ErrorMessage{"No match count"});
        }
        auto tableNameID =
            isTransaction ?
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_TX) :
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_RECEIPT);

        LEDGER_LOG(INFO) << "setTransactionBuffers: " << tableNameID << " " << RANGES::size(hashes);

        co_await m_storage.write(
            hashes | RANGES::views::transform([this, &tableNameID](auto const& hash) {
                return transaction_executor::StateKey{
                    tableNameID, std::string_view(std::data(hash), RANGES::size(hash))};
            }),
            buffers | RANGES::views::transform([this](auto&& buffer) {
                storage::Entry entry;
                entry.set(std::forward<decltype(buffer)>(buffer));

                return entry;
            }));

        co_return;
    }

    template <std::same_as<bcos::concepts::ledger::HEADER>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData header: " << blockNumberKey;
        auto blockHeader = block.blockHeaderConst();

        // number 2 header
        bcos::storage::Entry number2HeaderEntry;
        std::vector<bcos::byte> number2HeaderBuffer;
        bcos::concepts::serialize::encode(*blockHeader, number2HeaderBuffer);
        number2HeaderEntry.set(std::move(number2HeaderBuffer));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_BLOCK_HEADER),
                blockNumberKey},
            std::move(number2HeaderEntry));

        // number 2 block hash
        bcos::storage::Entry hashEntry;
        hashEntry.set(blockHeader->hash());
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_HASH),
                blockNumberKey},
            std::move(hashEntry));

        // block hash 2 number
        bcos::storage::Entry hash2NumberEntry;
        hash2NumberEntry.set(blockNumberKey);
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_NUMBER),
                std::string_view{
                    block.blockHeader.dataHash.data(), block.blockHeader.dataHash.size()}},
            std::move(hash2NumberEntry));

        // current number
        bcos::storage::Entry numberEntry;
        numberEntry.set(blockNumberKey);
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                SYS_KEY_CURRENT_NUMBER},
            std::move(numberEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::NONCES>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData nonce " << blockNumberKey;

        auto blockNonceList = m_blockFactory.createBlock();
        blockNonceList->setNonceList(block.nonceList());
        bcos::storage::Entry number2NonceEntry;
        std::vector<bcos::byte> number2NonceBuffer;
        bcos::concepts::serialize::encode(*blockNonceList, number2NonceBuffer);
        number2NonceEntry.set(std::move(number2NonceBuffer));

        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_BLOCK_NUMBER_2_NONCES),
                blockNumberKey},
            std::move(number2NonceEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::TRANSACTIONS_METADATA>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData transaction metadata: " << blockNumberKey;

        auto transactionsBlock = m_blockFactory.createBlock();
        if (block.transactionsMetaDataSize() > 0)
        {
            for (size_t i = 0; i < block.transactionsMetaDataSize(); ++i)
            {
                auto originTransactionMetaData = block->transactionMetaData(i);
                auto transactionMetaData =
                    m_blockFactory.createTransactionMetaData(originTransactionMetaData->hash(),
                        std::string(originTransactionMetaData->to()));
                transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
            }
        }
        else if (block.transactionsSize() > 0)
        {
            for (size_t i = 0; i < block->transactionsSize(); ++i)
            {
                auto transaction = block->transaction(i);
                auto transactionMetaData = m_blockFactory->createTransactionMetaData(
                    transaction->hash(), std::string(transaction->to()));
                transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
            }
        }

        if (transactionsBlock.transactionsMetaDataSize() == 0)
        {
            LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
            co_return;
        }

        bcos::storage::Entry number2TransactionHashesEntry;
        std::vector<bcos::byte> number2TransactionHashesBuffer;
        bcos::concepts::serialize::encode(transactionsBlock, number2TransactionHashesBuffer);
        number2TransactionHashesEntry.set(std::move(number2TransactionHashesBuffer));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_TXS),
                blockNumberKey},
            std::move(number2TransactionHashesEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::TRANSACTIONS>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData transactions: " << blockNumberKey;

        if (block.transactionsMetaDataSize() == 0)
        {
            LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
            co_return;
        }

        auto hashes = RANGES::iota_view<uint64_t, uint64_t>(0, block.transactionsMetaDataSize()) |
                      RANGES::views::transform([&block](uint64_t index) {
                          auto metaData = block.transactionMetaData(index);
                          return metaData->hash();
                      });
        auto buffers = RANGES::iota_view<uint64_t, uint64_t>(0, block.transactionsSize()) |
                       RANGES::views::transform([&block](uint64_t index) {
                           auto transaction = block.transaction(index);
                           std::vector<bcos::byte> buffer;
                           bcos::concepts::serialize::encode(*transaction, buffer);

                           return buffer;
                       });
        co_await setTransactions<false>(hashes, buffers);
    }

    template <std::same_as<concepts::ledger::RECEIPTS>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData receipts: " << blockNumberKey;

        if (std::empty(block.transactionsMetaData))
        {
            LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
            co_return;
        }

        size_t totalTransactionCount = 0;
        size_t failedTransactionCount = 0;
        for (uint64_t i = 0; i < block.transactionsMetaDataSize(); ++i)
        {
            auto receipt = block.receipt[i];
            if (receipt.data.status != 0)
            {
                ++failedTransactionCount;
            }
            ++totalTransactionCount;
        }

        auto hashes = RANGES::iota_view<uint64_t, uint64_t>(0, block.transactionsMetaDataSize()) |
                      RANGES::views::transform([&block](uint64_t index) {
                          auto metaData = block.transactionMetaData(index);
                          return metaData->hash();
                      });
        auto buffers = RANGES::iota_view<uint64_t, uint64_t>(0, block.transactionsSize()) |
                       RANGES::views::transform([&block](uint64_t index) {
                           auto receipt = block.receipt(index);
                           std::vector<bcos::byte> buffer;
                           bcos::concepts::serialize::encode(*receipt, buffer);
                           return buffer;
                       });

        co_await setTransactions<false>(hashes, buffers);

        LEDGER_LOG(DEBUG) << LOG_DESC("Calculate tx counts in block")
                          << LOG_KV("number", blockNumberKey)
                          << LOG_KV("totalCount", totalTransactionCount)
                          << LOG_KV("failedCount", failedTransactionCount);

        auto transactionCount = co_await getStatus();
        transactionCount.total += totalTransactionCount;
        transactionCount.failed += failedTransactionCount;

        bcos::storage::Entry totalEntry;
        totalEntry.set(boost::lexical_cast<std::string>(transactionCount.total));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                SYS_KEY_TOTAL_TRANSACTION_COUNT},
            std::move(totalEntry));

        if (transactionCount.failed > 0)
        {
            bcos::storage::Entry failedEntry;
            failedEntry.set(boost::lexical_cast<std::string>(transactionCount.failed));
            co_await storage2::writeOne(m_storage,
                transaction_executor::StateKey{
                    storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                    SYS_KEY_TOTAL_FAILED_TRANSACTION},
                std::move(failedEntry));
        }

        LEDGER_LOG(INFO) << LOG_DESC("setBlock")
                         << LOG_KV("number", block.blockHeader.data.blockNumber)
                         << LOG_KV("totalTxs", transactionCount.total)
                         << LOG_KV("failedTxs", transactionCount.failed)
                         << LOG_KV("incTxs", totalTransactionCount)
                         << LOG_KV("incFailedTxs", failedTransactionCount);
    }

    template <std::same_as<concepts::ledger::ALL>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, bcos::concepts::block::Block auto& block)
    {
        LEDGER_LOG(DEBUG) << "setBlockData all: " << blockNumberKey;

        co_await setBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::TRANSACTIONS_METADATA>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::TRANSACTIONS>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::RECEIPTS>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::NONCES>(blockNumberKey, block);
    }
};

}  // namespace bcos::ledger