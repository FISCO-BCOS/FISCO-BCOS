#pragma once
#include <bcos-framework/concepts/Block.h>
#include <bcos-framework/concepts/storage/Storage.h>
#include <boost/type_traits.hpp>
#include <boost/type_traits/function_traits.hpp>

namespace bcos::concepts::ledger
{

template <class ArgType>
concept TransactionOrReceipt = bcos::concepts::transaction::Transaction<ArgType> ||
    bcos::concepts::receipt::TransactionReceipt<ArgType>;

// clang-format off
struct GETBLOCK_FLAGS {};
struct BLOCK_ALL: public GETBLOCK_FLAGS {};
struct BLOCK_HEADER: public GETBLOCK_FLAGS {};
struct BLOCK_TRANSACTIONS: public GETBLOCK_FLAGS {};
struct BLOCK_RECEIPTS: public GETBLOCK_FLAGS {};
struct BLOCK_NONCES: public GETBLOCK_FLAGS {};
// clang-format on

template <class GetBlockFlagType>
concept GetBlockFlag = std::derived_from<GetBlockFlagType, GETBLOCK_FLAGS>;

template <class Impl>
class LedgerBase
{
public:
    template <class... Flags>
    auto getBlock(bcos::concepts::block::BlockNumber auto blockNumber)
    {
        return impl().template impl_getBlock<Flags...>(blockNumber);
    }

    void setBlock(
        bcos::concepts::storage::Storage auto& storage, bcos::concepts::block::Block auto block)
    {
        impl().impl_setBlock(storage, std::move(block));
    }

    template <bool isTransaction>
    auto getTransactionsOrReceipts(std::ranges::range auto const& hashes)
    {
        return impl().template impl_getTransactionsOrReceipts<isTransaction>(hashes);
    }

    struct TransactionCount
    {
        int64_t total;
        int64_t failed;
        int64_t blockNumber;
    };
    TransactionCount getTotalTransactionCount() { return impl().impl_getTotalTransactionCount(); }

    template <std::ranges::range Inputs>
    requires bcos::concepts::ledger::TransactionOrReceipt<std::ranges::range_value_t<Inputs>>
    void setTransactionsOrReceipts(Inputs const& inputs)
    {
        impl().impl_setTransactionsOrReceipts(inputs);
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