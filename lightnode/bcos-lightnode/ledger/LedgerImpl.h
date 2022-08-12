#pragma once

#include "../Log.h"
#include <bcos-concepts/Basic.h>
#include <bcos-concepts/Block.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-concepts/Hash.h>
#include <bcos-concepts/Receipt.h>
#include <bcos-concepts/Transaction.h>
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-concepts/storage/Storage.h>
#include <bcos-crypto/hasher/Hasher.h>
#include <bcos-crypto/merkle/Merkle.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Ranges.h>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <atomic>
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
        LEDGER_LOG(INFO) << "getBlock: " << blockNumber;

        auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);
        (getBlockData<Flags>(blockNumberStr, block), ...);
    }

    template <bcos::concepts::ledger::DataFlag... Flags>
    void impl_setBlock(bcos::concepts::block::Block auto block)
    {
        LEDGER_LOG(INFO) << "setBlock: " << block.blockHeader.data.blockNumber;

        auto blockNumberStr = boost::lexical_cast<std::string>(block.blockHeader.data.blockNumber);
        (setBlockData<Flags>(blockNumberStr, block), ...);
    }

    void impl_getBlockNumberByHash(
        bcos::concepts::bytebuffer::ByteBuffer auto const& hash, std::integral auto& number)
    {
        LEDGER_LOG(INFO) << "getBlockNumberByHash request";

        auto entry = storage().getRow(SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash));

        if (!entry)
        {
            number = -1;
            return;
        }

        try
        {
            number = boost::lexical_cast<bcos::protocol::BlockNumber>(entry->getField(0));
        }
        catch (boost::bad_lexical_cast& e)
        {
            // Ignore the exception
            LEDGER_LOG(INFO) << "Cast blockNumber failed, may be empty, set to default value -1"
                             << LOG_KV("blockNumber str", entry->getField(0));
            number = -1;
        }
    }

    void impl_getBlockHashByNumber(
        std::integral auto number, bcos::concepts::bytebuffer::ByteBuffer auto& hash)
    {
        LEDGER_LOG(INFO) << "getBlockHashByNumber request" << LOG_KV("blockNumber", number);

        auto key = boost::lexical_cast<std::string>(number);
        auto entry = storage().getRow(SYS_NUMBER_2_HASH, key);
        if (!entry)
        {
            LEDGER_LOG(WARNING) << "Not found block number: " << number;
            return;
        }

        auto hashStr = entry->getField(0);
        bcos::concepts::bytebuffer::assignTo(hashStr, hash);
    }

    void impl_getTransactionsOrReceipts(RANGES::range auto const& hashes, RANGES::range auto& out)
    {
        bcos::concepts::resizeTo(out, RANGES::size(hashes));
        using DataType = RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>;

        constexpr auto tableName =
            bcos::concepts::transaction::Transaction<DataType> ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;

        LEDGER_LOG(INFO) << "getTransactionOrReceipts: " << tableName << " "
                         << RANGES::size(hashes);
        auto entries = storage().getRows(std::string_view{tableName}, hashes);

        bcos::concepts::resizeTo(out, RANGES::size(hashes));
        tbb::parallel_for(tbb::blocked_range<size_t>(0u, RANGES::size(entries)),
            [&entries, &out](const tbb::blocked_range<size_t>& range) {
                for (auto index = range.begin(); index != range.end(); ++index)
                {
                    if (!entries[index]) [[unlikely]]
                        BOOST_THROW_EXCEPTION(std::runtime_error{"Get transaction not found"});

                    auto field = entries[index]->getField(0);
                    bcos::concepts::serialize::decode(field, out[index]);
                }
            });
    }

    auto impl_getStatus()
    {
        LEDGER_LOG(INFO) << "getStatus";
        constexpr static auto keys = std::to_array({SYS_KEY_TOTAL_TRANSACTION_COUNT,
            SYS_KEY_TOTAL_FAILED_TRANSACTION, SYS_KEY_CURRENT_NUMBER});

        bcos::concepts::ledger::Status status;
        auto entries = storage().getRows(SYS_CURRENT_STATE, keys);
        for (auto i = 0u; i < RANGES::size(entries); ++i)
        {
            auto& entry = entries[i];

            int64_t value = 0;
            if (entry) [[likely]]
                value = boost::lexical_cast<int64_t>(entry->getField(0));

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

        LEDGER_LOG(INFO) << "setTransactionOrReceiptBuffers: " << tableName << " "
                         << RANGES::size(hashes);

        for (auto i = 0u; i < count; ++i)
        {
            bcos::storage::Entry entry;
            entry.importFields({std::move(buffers[i])});

            auto const& hash = hashes[i];
            storage().setRow(
                tableName, std::string_view(std::data(hash), RANGES::size(hash)), std::move(entry));
        }
    }

    template <bcos::concepts::ledger::Ledger LedgerType, bcos::concepts::block::Block BlockType>
    void impl_sync(LedgerType& source, bool onlyHeader)
    {
        auto& sourceLedger = bcos::concepts::getRef(source);

        auto status = impl_getStatus();
        auto sourceStatus = sourceLedger.getStatus();

        LEDGER_LOG(INFO) << "Sync block from remote: " << onlyHeader << " | " << status.blockNumber
                         << " | " << sourceStatus.blockNumber;

        std::optional<BlockType> parentBlock;
        for (auto blockNumber = status.blockNumber + 1; blockNumber <= sourceStatus.blockNumber;
             ++blockNumber)
        {
            LEDGER_LOG(DEBUG) << "Syncing block header: " << blockNumber;
            BlockType block;
            if (onlyHeader)
                sourceLedger.template getBlock<bcos::concepts::ledger::HEADER>(blockNumber, block);
            else
                sourceLedger.template getBlock<bcos::concepts::ledger::ALL>(blockNumber, block);

            if (blockNumber > 0)  // Ignore verify genesis block
            {
                if (!parentBlock)
                {
                    parentBlock = BlockType();
                    impl_getBlock<bcos::concepts::ledger::HEADER>(blockNumber - 1, *parentBlock);
                }

                std::array<std::byte, Hasher::HASH_SIZE> parentHash;
                bcos::concepts::hash::calculate<Hasher>(*parentBlock, parentHash);

                if (RANGES::empty(block.blockHeader.data.parentInfo) ||
                    (block.blockHeader.data.parentInfo[0].blockNumber !=
                        parentBlock->blockHeader.data.blockNumber) ||
                    !bcos::concepts::bytebuffer::equalTo(
                        block.blockHeader.data.parentInfo[0].blockHash, parentHash))
                {
                    LEDGER_LOG(ERROR) << "No match parentHash!";
                    BOOST_THROW_EXCEPTION(std::runtime_error{"No match parentHash!"});
                }
            }

            if (onlyHeader)
                impl_setBlock<bcos::concepts::ledger::HEADER>(block);
            else
                impl_setBlock<bcos::concepts::ledger::ALL>(block);

            parentBlock = std::move(block);
        }
    }

    template <bcos::concepts::ledger::DataFlag Flag>
    void getBlockData(std::string_view blockNumberKey, bcos::concepts::block::Block auto& block)
    {
        LEDGER_LOG(DEBUG) << "getBlockData: " << blockNumberKey << " " << typeid(Flag).name();

        if constexpr (std::is_same_v<Flag, bcos::concepts::ledger::HEADER>)
        {
            auto entry = storage().getRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberKey);
            if (!entry) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found header!"});
            }

            auto field = entry->getField(0);
            bcos::concepts::serialize::decode(field, block.blockHeader);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS_METADATA>)
        {
            auto entry = storage().getRow(SYS_NUMBER_2_TXS, blockNumberKey);
            if (!entry) [[unlikely]]
            {
                LEDGER_LOG(INFO) << "GetBlock not found transaction meta data!";
                return;
            }

            auto field = entry->getField(0);
            std::remove_reference_t<decltype(block)> metadataBlock;
            bcos::concepts::serialize::decode(field, metadataBlock);
            block.transactionsMetaData = std::move(metadataBlock.transactionsMetaData);
            block.transactionsMerkle = std::move(metadataBlock.transactionsMerkle);
            block.receiptsMerkle = std::move(metadataBlock.receiptsMerkle);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS> ||
                           std::is_same_v<Flag, concepts::ledger::RECEIPTS>)
        {
            if (RANGES::empty(block.transactionsMetaData))
            {
                LEDGER_LOG(INFO) << "GetBlock not found transaction meta data!";
                return;
            }

            auto hashesRange = block.transactionsMetaData | RANGES::views::transform([
            ](typename decltype(block.transactionsMetaData)::value_type const& metaData) -> auto& {
                return metaData.hash;
            });
            auto outputSize = RANGES::size(block.transactionsMetaData);

            if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS>)
            {
                block.transactions.resize(outputSize);
                impl_getTransactionsOrReceipts(std::move(hashesRange), block.transactions);
            }
            else
            {
                block.receipts.resize(outputSize);
                impl_getTransactionsOrReceipts(std::move(hashesRange), block.receipts);
            }
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::NONCES>)
        {
            // TODO: add get nonce logic
            auto entry = storage().getRow(SYS_BLOCK_NUMBER_2_NONCES, blockNumberKey);
            if (!entry)
            {
                LEDGER_LOG(INFO) << "GetBlock not found nonce data!";
                return;
            }

            std::remove_reference_t<decltype(block)> nonceBlock;
            auto field = entry->getField(0);
            bcos::concepts::serialize::decode(field, nonceBlock);
            block.nonceList = std::move(nonceBlock.nonceList);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::ALL>)
        {
            getBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
            getBlockData<concepts::ledger::TRANSACTIONS_METADATA>(blockNumberKey, block);
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
        LEDGER_LOG(DEBUG) << "setBlockData: " << blockNumberKey << " " << typeid(Flag).name();

        if constexpr (std::is_same_v<Flag, bcos::concepts::ledger::HEADER>)
        {
            // current number
            bcos::storage::Entry numberEntry;
            numberEntry.importFields({std::string(blockNumberKey)});
            storage().setRow(SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER, std::move(numberEntry));

            // number 2 block hash
            bcos::storage::Entry hashEntry;
            hashEntry.importFields({block.blockHeader.dataHash});
            storage().setRow(SYS_NUMBER_2_HASH, blockNumberKey, std::move(hashEntry));

            // block hash 2 number
            bcos::storage::Entry hash2NumberEntry;
            hash2NumberEntry.importFields({std::string(blockNumberKey)});
            storage().setRow(SYS_HASH_2_NUMBER,
                std::string_view{
                    block.blockHeader.dataHash.data(), block.blockHeader.dataHash.size()},
                std::move(hash2NumberEntry));

            // number 2 header
            bcos::storage::Entry number2HeaderEntry;
            std::vector<bcos::byte> number2HeaderBuffer;
            bcos::concepts::serialize::encode(block.blockHeader, number2HeaderBuffer);
            number2HeaderEntry.importFields({std::move(number2HeaderBuffer)});
            storage().setRow(
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
            storage().setRow(
                SYS_BLOCK_NUMBER_2_NONCES, blockNumberKey, std::move(number2NonceEntry));
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS_METADATA>)
        {
            if (RANGES::empty(block.transactionsMetaData) && !RANGES::empty(block.transactions))
            {
                block.transactionsMetaData.resize(block.transactions.size());
                tbb::parallel_for(tbb::blocked_range<size_t>(0, block.transactions.size()),
                    [&block](const tbb::blocked_range<size_t>& range) {
                        for (auto i = range.begin(); i < range.end(); ++i)
                        {
                            bcos::concepts::hash::calculate<Hasher>(
                                block.transactions[i], block.transactionsMetaData[i].hash);
                        }
                    });
            }

            if (std::empty(block.transactionsMetaData))
            {
                LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
                return;
            }

            std::remove_cvref_t<decltype(block)> transactionsBlock;
            std::swap(block.transactionsMetaData, transactionsBlock.transactionsMetaData);

            bcos::storage::Entry number2TransactionHashesEntry;
            std::vector<bcos::byte> number2TransactionHashesBuffer;
            bcos::concepts::serialize::encode(transactionsBlock, number2TransactionHashesBuffer);
            number2TransactionHashesEntry.importFields({std::move(number2TransactionHashesBuffer)});
            storage().setRow(
                SYS_NUMBER_2_TXS, blockNumberKey, std::move(number2TransactionHashesEntry));
            std::swap(transactionsBlock.transactionsMetaData, block.transactionsMetaData);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::TRANSACTIONS>)
        {
            if (std::empty(block.transactionsMetaData))
            {
                LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
                return;
            }

            std::vector<std::vector<bcos::byte>> transactionBuffers(block.transactions.size());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, block.transactions.size()),
                [&block, &transactionBuffers](const tbb::blocked_range<size_t>& range) {
                    for (auto i = range.begin(); i < range.end(); ++i)
                    {
                        auto& transaction = block.transactions[i];
                        bcos::concepts::serialize::encode(transaction, transactionBuffers[i]);
                    }
                });

            auto hashView = block.transactionsMetaData |
                            RANGES::views::transform([](auto& metaData) { return metaData.hash; });
            impl_setTransactionOrReceiptBuffers<false>(hashView, std::move(transactionBuffers));
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::RECEIPTS>)
        {
            if (std::empty(block.transactionsMetaData))
            {
                LEDGER_LOG(INFO) << "setBlockData not found transaction meta data!";
                return;
            }

            std::atomic_size_t totalTransactionCount = 0;
            std::atomic_size_t failedTransactionCount = 0;
            std::vector<std::vector<bcos::byte>> receiptBuffers(block.receipts.size());
            tbb::parallel_for(tbb::blocked_range<size_t>(0, block.receipts.size()),
                [&block, &totalTransactionCount, &failedTransactionCount, &receiptBuffers](
                    const tbb::blocked_range<size_t>& range) {
                    for (auto i = range.begin(); i < range.end(); ++i)
                    {
                        auto& receipt = block.receipts[i];
                        if (receipt.data.status != 0)
                        {
                            ++failedTransactionCount;
                        }
                        ++totalTransactionCount;

                        bcos::concepts::serialize::encode(receipt, receiptBuffers[i]);
                    }
                });

            auto hashView = block.transactionsMetaData |
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
            storage().setRow(
                SYS_CURRENT_STATE, SYS_KEY_TOTAL_TRANSACTION_COUNT, std::move(totalEntry));

            if (transactionCount.failed > 0)
            {
                bcos::storage::Entry failedEntry;
                failedEntry.importFields(
                    {boost::lexical_cast<std::string>(transactionCount.failed)});
                storage().setRow(
                    SYS_CURRENT_STATE, SYS_KEY_TOTAL_FAILED_TRANSACTION, std::move(failedEntry));
            }

            LEDGER_LOG(INFO) << LOG_DESC("setBlock")
                             << LOG_KV("number", block.blockHeader.data.blockNumber)
                             << LOG_KV("totalTxs", transactionCount.total)
                             << LOG_KV("failedTxs", transactionCount.failed)
                             << LOG_KV("incTxs", totalTransactionCount)
                             << LOG_KV("incFailedTxs", failedTransactionCount);
        }
        else if constexpr (std::is_same_v<Flag, concepts::ledger::ALL>)
        {
            setBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
            setBlockData<concepts::ledger::TRANSACTIONS_METADATA>(blockNumberKey, block);
            setBlockData<concepts::ledger::TRANSACTIONS>(blockNumberKey, block);
            setBlockData<concepts::ledger::RECEIPTS>(blockNumberKey, block);
            setBlockData<concepts::ledger::NONCES>(blockNumberKey, block);
        }
        else
        {
            static_assert(!sizeof(block), "Wrong input flag!");
        }
    }

    void impl_setupGenesisBlock(bcos::concepts::block::Block auto block)
    {
        try
        {
            decltype(block) currentBlock;

            impl_getBlock<concepts::ledger::HEADER>(0, currentBlock);
            return;
        }
        catch (...)
        {
            LEDGER_LOG(INFO) << "Not found genesis block, may be not initialized";
        }

        impl_setBlock<concepts::ledger::HEADER>(std::move(block));
    }

    auto& storage() { return bcos::concepts::getRef(m_storage); }

    Storage m_storage;
    crypto::merkle::Merkle<Hasher> m_merkle;  // Use the default width 2
};

}  // namespace bcos::ledger