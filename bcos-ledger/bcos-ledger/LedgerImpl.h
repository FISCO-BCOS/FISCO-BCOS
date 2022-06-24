#pragma once
#include <bcos-framework/concepts/Basic.h>
#include <bcos-framework/concepts/Block.h>
#include <bcos-framework/concepts/Storage.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-tars-protocol/impl/TarsSerializable.h>
#include <boost/iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <boost/lexical_cast.hpp>
#include <bitset>
#include <tuple>

namespace bcos::ledger
{

// All get block flags
// clang-format off
struct GETBLOCK_FLAGS {};
struct BLOCK: public GETBLOCK_FLAGS {};
struct BLOCK_HEADER: public GETBLOCK_FLAGS {};
struct BLOCK_TRANSACTIONS: public GETBLOCK_FLAGS {};
struct BLOCK_RECEIPTS: public GETBLOCK_FLAGS {};
// clang-format on

template <bcos::storage::Storage Storage, bcos::concepts::block::Block Block>
class LedgerImpl
{
public:
    LedgerImpl(Storage storage) : m_storage{std::move(storage)} {}

    template <class Flag, class... Flags>
    requires std::derived_from<Flag, GETBLOCK_FLAGS>
    auto getBlock(bcos::concepts::block::BlockNumber auto blockNumber)
    {
        auto blockNumberStr = boost::lexical_cast<std::string>(blockNumber);

        if constexpr (std::is_same_v<Flag, BLOCK_HEADER>)
        {
            auto entry = m_storage.getRow(SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr);
            if (!entry) [[unlikely]] { BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"}); }

            auto field = entry->getField(0);
            decltype(Block::blockHeader) blockHeader;
            bcos::concepts::serialize::decode(blockHeader, field);

            return std::tuple_cat(std::tuple{std::move(blockHeader)}, std::tuple{getBlock<Flags...>(blockNumber)});
        }
        else if constexpr (std::is_same_v<Flag, BLOCK_TRANSACTIONS> || std::is_same_v<Flag, BLOCK_RECEIPTS>)
        {
            auto entry = m_storage.getRow(SYS_NUMBER_2_TXS, blockNumberStr);
            if (!entry) [[unlikely]]
                BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"});

            auto field = entry->getField(0);
            Block block;
            bcos::concepts::serialize::decode(block, field);

            struct
            {
                auto operator()(typename decltype(block.transactionsMetaData)::value_type const& metaData) const
                {
                    return std::string_view{metaData.hash.data(), metaData.hash.size()};
                }
            } GetMetaHashFunc;

            auto begin = boost::make_transform_iterator(block.transactionsMetaData.cbegin(), GetMetaHashFunc);
            auto end = boost::make_transform_iterator(block.transactionsMetaData.cend(), GetMetaHashFunc);
            auto range = std::ranges::subrange{begin, end};

            constexpr auto isTransaction = std::is_same_v<Flag, BLOCK_TRANSACTIONS>;
            auto outputs = getTransactionOrReceipts<isTransaction>(std::move(range));
            return std::tuple_cat(std::tuple{std::move(outputs)}, std::tuple{getBlock<Flags...>(blockNumber)});
        }
        else if constexpr (std::is_same_v<Flag, BLOCK>)
        {
            auto [header, transactions, receipts] =
                getBlock<BLOCK_HEADER, BLOCK_TRANSACTIONS, BLOCK_RECEIPTS>(blockNumber);
            Block block;
            block.blockHeader = std::move(header);
            block.transactions = std::move(transactions);
            block.receipts = std::move(receipts);

            return std::tuple_cat(std::tuple{std::move(block)}, std::tuple{getBlock<Flags...>(blockNumber)});
        }
        else { static_assert(!sizeof(blockNumber), "Wrong input flag!"); }
    }

private:
    auto getBlock(bcos::concepts::block::BlockNumber auto) { return std::tuple{}; }

    template <bool isTransaction>
    auto getTransactionOrReceipts(std::ranges::range auto&& hashes)
    {
        using OutputItemType =
            std::conditional_t<isTransaction, std::ranges::range_value_t<decltype(Block::transactions)>,
                std::ranges::range_value_t<decltype(Block::receipts)>>;
        constexpr auto tableName = isTransaction ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;
        auto entries = m_storage.getRows(std::string_view{tableName}, hashes);
        std::vector<OutputItemType> outputs(std::size(entries));

        for (auto i = 0u; i < std::size(entries); ++i)  // TODO: can be parallel
        {
            if (!entries[i]) [[unlikely]]
                BOOST_THROW_EXCEPTION(std::runtime_error{"GetBlock not found!"});
            auto field = entries[i]->getField(0);
            bcos::concepts::serialize::decode(outputs[i], field);
        }
        return outputs;
    }

    Storage m_storage;
};
}  // namespace bcos::ledger