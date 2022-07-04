#pragma once
#include "../Block.h"
#include "../storage/Storage.h"
#include <concepts>

namespace bcos::concepts::ledger
{

template <class ArgType>
concept TransactionOrReceipt = bcos::concepts::transaction::Transaction<ArgType> ||
    bcos::concepts::receipt::TransactionReceipt<ArgType>;

// enum LedgerDataFlag
// {
//     ALL,
//     HEADER,
//     TRANSACTIONS,
//     RECEIPTS,
//     NONCES
// };

// clang-format off
struct DataFlagBase {};
struct ALL: public DataFlagBase {};
struct HEADER: public DataFlagBase {};
struct TRANSACTIONS: public DataFlagBase {};
struct RECEIPTS: public DataFlagBase {};
struct NONCES: public DataFlagBase {};
// clang-format on

template <class FlagType>
concept DataFlag = std::derived_from<FlagType, DataFlagBase>;

template <class Impl>
class LedgerBase
{
public:
    template <DataFlag... flags>
    auto getBlock(bcos::concepts::block::BlockNumber auto blockNumber)
    {
        return impl().template impl_getBlock<flags...>(blockNumber);
    }

    void setBlock(
        bcos::concepts::storage::Storage auto& storage, bcos::concepts::block::Block auto block)
    {
        impl().impl_setBlock(storage, std::move(block));
    }

    template <DataFlag flag>
    auto getTransactionsOrReceipts(std::ranges::range auto const& hashes)
    {
        return impl().template impl_getTransactionsOrReceipts<flag>(hashes);
    }

    struct TransactionCount
    {
        int64_t total;
        int64_t failed;
        int64_t blockNumber;
    };
    TransactionCount getTotalTransactionCount() { return impl().impl_getTotalTransactionCount(); }

    template <bcos::crypto::hasher::Hasher Hasher>
    void setTransactionsOrReceipts(std::ranges::range auto const& inputs) requires
        bcos::concepts::ledger::TransactionOrReceipt<std::ranges::range_value_t<decltype(inputs)>>
    {
        auto hashesRange = inputs | std::views::transform([](auto const& input) {
            return bcos::concepts::hash::calculate<Hasher>(input);
        });
        auto buffersRange = inputs | std::views::transform([](auto const& input) {
            return bcos::concepts::serialize::encode(input);
        });

        constexpr auto isTransaction =
            bcos::concepts::transaction::Transaction<std::ranges::range_value_t<decltype(inputs)>>;
        setTransactionOrReceiptBuffers<isTransaction>(hashesRange, std::move(buffersRange));
    }

    template <bool isTransaction>
    void setTransactionOrReceiptBuffers(
        std::ranges::range auto const& hashes, std::ranges::range auto buffers)
    {
        impl().template impl_setTransactionOrReceiptBuffers<isTransaction>(
            hashes, std::move(buffers));
    }

private:
    friend Impl;
    LedgerBase() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Ledger = std::derived_from<Impl, LedgerBase<Impl>>;

}  // namespace bcos::concepts::ledger