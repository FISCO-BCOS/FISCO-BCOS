#pragma once

#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Block.h>
#include <bcos-concepts/Receipt.h>
#include <bcos-concepts/Transaction.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-utilities/Ranges.h>
#include <bits/ranges_base.h>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace bcos::ledger
{

template <bcos::crypto::hasher::Hasher Hasher, bcos::concepts::storage::Storage Storage>
class LedgerImpl : public bcos::concepts::ledger::LedgerBase<LedgerImpl<Hasher, Storage>>
{
    friend bcos::concepts::ledger::LedgerBase<LedgerImpl<Hasher, Storage>>;

public:
    LedgerImpl(Storage storage) : m_storage{std::move(storage)} {}

private:
    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);
        (getBlockData<Flags>(blockNumberStr, block), ...);
    }

    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_setBlock(bcos::concepts::block::Block auto block)
    {
        // if (!std::empty(block.transactions))
        //     return;

        auto blockNumberStr = boost::lexical_cast<std::string>(block.blockHeader.data.blockNumber);
        (setBlockData<Flags>(blockNumberStr, block), ...);
    }

    template <concepts::ledger::DataFlag Flag>
    auto impl_getTransactionsOrReceipts(RANGES::range auto const& hashes, RANGES::range auto& out)
    {
        if (RANGES::size(out) < RANGES::size(hashes))
        {
            BOOST_THROW_EXCEPTION(std::invalid_argument{"Output size too short!"});
        }

        if constexpr (!std::is_same_v<Flag, concepts::ledger::TRANSACTIONS> &&
                      !std::is_same_v<Flag, concepts::ledger::RECEIPTS>)
        {
            static_assert(!sizeof(hashes), "Unspported data flag!");
        }

        constexpr auto tableName = std::is_same_v<Flag, concepts::ledger::TRANSACTIONS> ?
                                       SYS_HASH_2_TX :
                                       SYS_HASH_2_RECEIPT;
        auto entries = m_storage.getRows(std::string_view{tableName}, hashes);

#pragma omp parallel for
        for (auto i = 0u; i < RANGES::size(entries); ++i)
        {
            if (!entries[i]) [[unlikely]]
                BOOST_THROW_EXCEPTION(std::runtime_error{"Get transaction not found"});
            auto field = entries[i]->getField(0);
            bcos::concepts::serialize::decode(field, out[i]);
        }
        return out;
    }

    auto impl_getStatus()
    {
        LEDGER_LOG(INFO) << "GetStatus request";
        constexpr static auto keys = std::to_array({SYS_KEY_TOTAL_TRANSACTION_COUNT,
            SYS_KEY_TOTAL_FAILED_TRANSACTION, SYS_KEY_CURRENT_NUMBER});

        bcos::concepts::ledger::Status status;
        auto entries = m_storage.getRows(SYS_CURRENT_STATE, keys);
        for (auto i = 0u; i < RANGES::size(entries); ++i)
        {
            auto& entry = entries[i];

            int64_t value = 0;
            if (!entry) [[unlikely]]
            {
                LEDGER_LOG(WARNING)
                    << "GetTotalTransactionCount error" << LOG_KV("index", i) << " empty";
            }
            else
            {
                value = boost::lexical_cast<int64_t>(entry->getField(0));
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
                BOOST_THROW_EXCEPTION(std::runtime_error{"Unexpected getRows index"});
                break;
            }
        }

        return status;
    }

    template <bool isTransaction>
    void impl_setTransactionOrReceiptBuffers(
        RANGES::range auto const& hashes, RANGES::range auto buffers)
    {
        auto count = RANGES::size(buffers);
        if (count != RANGES::size(hashes))
        {
            BOOST_THROW_EXCEPTION(std::runtime_error{"No match count"});
        }
        constexpr auto tableName = isTransaction ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;

        for (auto i = 0u; i < count; ++i)
        {
            bcos::storage::Entry entry;
            entry.importFields({std::move(buffers[i])});

            auto&& hash = hashes[i];
            m_storage.setRow(
                tableName, std::string_view(std::data(hash), RANGES::size(hash)), std::move(entry));
        }
    }

    template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::block::Block BlockType>
    void impl_sync(LedgerType& source, bool onlyHeader)
    {
        auto& sourceLedger = bcos::concepts::getRef(source);

        auto status = impl_getStatus();
        auto sourceStatus = sourceLedger.getStatus();

        if (onlyHeader)
        {
            for (auto blockNumber = status.blockNumber; blockNumber <= sourceStatus.blockNumber;
                 ++blockNumber)
            {
                BlockType block;
                sourceLedger.template getBlock<bcos::concepts::ledger::HEADER>(blockNumber, block);
                setBlock<bcos::concepts::ledger::HEADER>(std::move(block));
            }
        }
        else
        {
            for (auto blockNumber = status.blockNumber; blockNumber <= sourceStatus.blockNumber;
                 ++blockNumber)
            {
                BlockType block;
                sourceLedger.template getBlock<bcos::concepts::ledger::ALL>(blockNumber, block);
                setBlock<bcos::concepts::ledger::ALL>(std::move(block));
            }
        }
    }

    template <bcos::concepts::ledger::DataFlag Flag>
    void getBlockData(std::string_view blockNumberKey, bcos::concepts::block::Block auto& block)
    {
        if constexpr (std::is_same_v<Flag, bcos::concepts::ledger::HEADER>)
        {
            auto entry = m_storage.getRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberKey);
            if (!entry) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"});
            }

            auto field = entry->getField(0);
            bcos::concepts::serialize::decode(field, block.blockHeader);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS> ||
                           std::is_same_v<Flag, concepts::ledger::RECEIPTS>)
        {
            if (RANGES::empty(block.transactionsMetaData))
            {
                auto entry = m_storage.getRow(SYS_NUMBER_2_TXS, blockNumberKey);
                if (!entry) [[unlikely]]
                    BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"});

                auto field = entry->getField(0);
                std::remove_reference_t<decltype(block)> metadataBlock;
                bcos::concepts::serialize::decode(field, metadataBlock);
                block.transactionsMetaData = std::move(metadataBlock.transactionsMetaData);
            }

            auto hashesRange =
                block.transactionsMetaData |
                RANGES::views::transform(
                    [](typename decltype(block.transactionsMetaData)::value_type const& metaData) {
                        return std::string_view{metaData.hash.data(), metaData.hash.size()};
                    });
            auto outputSize = RANGES::size(block.transactionsMetaData);

            if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS>)
            {
                block.transactions.resize(outputSize);
                impl_getTransactionsOrReceipts<Flag>(std::move(hashesRange), block.transactions);
            }
            else
            {
                block.receipts.resize(outputSize);
                impl_getTransactionsOrReceipts<Flag>(std::move(hashesRange), block.receipts);
            }
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::NONCES>)
        {}
        else if constexpr (std::is_same_v<Flag, concepts::ledger::ALL>)
        {
            getBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
            getBlockData<concepts::ledger::TRANSACTIONS>(blockNumberKey, block);
            getBlockData<concepts::ledger::RECEIPTS>(blockNumberKey, block);
            getBlockData<concepts::ledger::NONCES>(blockNumberKey, block);
        }
        else
        {
            static_assert(!sizeof(block), "Wrong input flag!");
        }
    }

    template <bcos::concepts::ledger::DataFlag Flag>
    void setBlockData(std::string_view blockNumberKey, bcos::concepts::block::Block auto& block)
    {
        if constexpr (std::is_same_v<Flag, bcos::concepts::ledger::HEADER>)
        {
            // current number
            bcos::storage::Entry numberEntry;
            numberEntry.importFields({std::string(blockNumberKey)});
            m_storage.setRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry));

            // number 2 block hash
            bcos::storage::Entry hashEntry;
            hashEntry.importFields({block.blockHeader.dataHash});
            m_storage.setRow(SYS_NUMBER_2_HASH, blockNumberKey, std::move(hashEntry));

            // block hash 2 number
            bcos::storage::Entry hash2NumberEntry;
            hash2NumberEntry.importFields({std::string(blockNumberKey)});
            m_storage.setRow(SYS_HASH_2_NUMBER,
                std::string_view{
                    block.blockHeader.dataHash.data(), block.blockHeader.dataHash.size()},
                std::move(hash2NumberEntry));

            // number 2 header
            bcos::storage::Entry number2HeaderEntry;
            std::vector<bcos::byte> number2HeaderBuffer;
            bcos::concepts::serialize::encode(block.blockHeader, number2HeaderBuffer);
            number2HeaderEntry.importFields({std::move(number2HeaderBuffer)});
            m_storage.setRow(
                SYS_NUMBER_2_BLOCK_HEADER, blockNumberKey, std::move(number2HeaderEntry));
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::NONCES>)
        {
            std::remove_cvref_t<decltype(block)> blockNonceList;
            blockNonceList.nonceList = std::move(block.nonceList);
            bcos::storage::Entry number2NonceEntry;
            std::vector<bcos::byte> number2NonceBuffer;
            bcos::concepts::serialize::encode(blockNonceList, number2NonceBuffer);
            number2NonceEntry.importFields({std::move(number2NonceBuffer)});
            m_storage.setRow(
                SYS_BLOCK_NUMBER_2_NONCES, blockNumberKey, std::move(number2NonceEntry));
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS> ||
                           std::is_same_v<Flag, concepts::ledger::RECEIPTS>)
        {
            if (std::empty(block.transactionsMetaData))
            {
                block.transactionsMetaData.resize(block.transactions.size());
#pragma omp parallel for
                for (auto i = 0u; i < block.transactions.size(); ++i)
                {
                    if (RANGES::size(block.transactionsMetaData[i].hash) < Hasher::HASH_SIZE)
                    {
                        block.transactionsMetaData[i].hash.resize(Hasher::HASH_SIZE);
                    }

                    bcos::concepts::hash::calculate<Hasher>(
                        block.transactions[i], block.transactionsMetaData[i].hash);
                }
            }

            if (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS>)
            {
                std::remove_cvref_t<decltype(block)> transactionsBlock;
                std::swap(transactionsBlock.transactionsMetaData, block.transactionsMetaData);

                bcos::storage::Entry number2TransactionHashesEntry;
                std::vector<bcos::byte> number2TransactionHashesBuffer;
                bcos::concepts::serialize::encode(
                    transactionsBlock, number2TransactionHashesBuffer);
                number2TransactionHashesEntry.importFields(
                    {std::move(number2TransactionHashesBuffer)});
                m_storage.setRow(
                    SYS_NUMBER_2_TXS, blockNumberKey, std::move(number2TransactionHashesEntry));
                std::swap(block.transactionsMetaData, transactionsBlock.transactionsMetaData);
            }
            else
            {
                if (RANGES::size(block.transactionsMetaData) != RANGES::size(block.receipts))
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error{"Transaction meta data mismatch!"});
                }

                size_t totalTransactionCount = 0;
                size_t failedTransactionCount = 0;
                std::vector<std::vector<bcos::byte>> receiptBuffers(block.receipts.size());
#pragma omp parallel for
                for (auto i = 0u; i < block.receipts.size(); ++i)
                {
                    auto& hash = block.transactionsMetaData[i].hash;
                    auto& receipt = block.receipts[i];
                    if (receipt.data.status != 0)
                    {
#pragma omp atomic
                        ++failedTransactionCount;
                    }
#pragma omp atomic
                    ++totalTransactionCount;

                    bcos::concepts::serialize::encode(receipt, receiptBuffers[i]);
                }
                auto hashView =
                    block.transactionsMetaData |
                    RANGES::views::transform([](auto& metaData) { return metaData.hash; });
                impl_setTransactionOrReceiptBuffers<false>(hashView, std::move(receiptBuffers));

                LEDGER_LOG(DEBUG) << LOG_DESC("Calculate tx counts in block")
                                  << LOG_KV("number", blockNumberKey)
                                  << LOG_KV("totalCount", totalTransactionCount)
                                  << LOG_KV("failedCount", failedTransactionCount);

                auto transactionCount = impl_getStatus();
                transactionCount.total += totalTransactionCount;
                transactionCount.failed += failedTransactionCount;

                bcos::storage::Entry totalEntry;
                totalEntry.importFields({boost::lexical_cast<std::string>(transactionCount.total)});
                m_storage.setRow(
                    SYS_CURRENT_STATE, SYS_KEY_TOTAL_TRANSACTION_COUNT, std::move(totalEntry));

                if (transactionCount.failed > 0)
                {
                    bcos::storage::Entry failedEntry;
                    failedEntry.importFields(
                        {boost::lexical_cast<std::string>(transactionCount.failed)});
                    m_storage.setRow(SYS_CURRENT_STATE, SYS_KEY_TOTAL_FAILED_TRANSACTION,
                        std::move(failedEntry));
                }

                LEDGER_LOG(INFO) << LOG_DESC("setBlock")
                                 << LOG_KV("number", block.blockHeader.data.blockNumber)
                                 << LOG_KV("totalTxs", transactionCount.total)
                                 << LOG_KV("failedTxs", transactionCount.failed)
                                 << LOG_KV("incTxs", totalTransactionCount)
                                 << LOG_KV("incFailedTxs", failedTransactionCount);
            }
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::ALL>)
        {
            setBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
            setBlockData<concepts::ledger::TRANSACTIONS>(blockNumberKey, block);
            setBlockData<concepts::ledger::RECEIPTS>(blockNumberKey, block);
            setBlockData<concepts::ledger::NONCES>(blockNumberKey, block);
        }
        else
        {
            static_assert(!sizeof(block), "Wrong input flag!");
        }
    }

    Storage m_storage;
};

}  // namespace bcos::ledger