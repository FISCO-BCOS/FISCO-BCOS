#pragma once
#include "../Block.h"
#include "../storage/Storage.h"
#include <bcos-utilities/Ranges.h>
#include <concepts>

namespace bcos::concepts::ledger
{

template <class ArgType>
concept TransactionOrReceipt = bcos::concepts::transaction::Transaction<ArgType> ||
    bcos::concepts::receipt::TransactionReceipt<ArgType>;

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

struct Status
{
    uint64_t total = 0;
    uint64_t failed = 0;
    uint64_t blockNumber = 0;
};

// All method in ledger is uncacheed
template <class Impl>
class LedgerBase
{
public:
    template <DataFlag... Flags>
    void getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block)
    {
        impl().template impl_getBlock<Flags...>(blockNumber, block);
    }

    template <DataFlag... Flags>
    void setBlock(bcos::concepts::block::Block auto block)
    {
        impl().template impl_setBlock<Flags...>(std::move(block));
    }

    void getTransactionsOrReceipts(
        RANGES::range auto const& hashes, RANGES::range auto& out) requires
        TransactionOrReceipt<RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>>
    {
        impl().impl_getTransactionsOrReceipts(hashes, out);
    }

    Status getStatus() { return impl().impl_getStatus(); }

    template <bcos::crypto::hasher::Hasher Hasher>
    void setTransactionsOrReceipts(RANGES::range auto const& inputs) requires bcos::concepts::
        ledger::TransactionOrReceipt<RANGES::range_value_t<std::remove_cvref_t<decltype(inputs)>>>
    {
        auto hashesRange = inputs | RANGES::views::transform([](auto const& input) {
            decltype(input.dataHash) hash(Hasher::HASH_SIZE);
            bcos::concepts::hash::calculate<Hasher>(input, hash);
            return hash;
        });
        auto buffersRange = inputs | RANGES::views::transform([](auto const& input) {
            std::vector<bcos::byte> buffer;
            bcos::concepts::serialize::encode(input, buffer);
            return buffer;
        });

        constexpr auto isTransaction =
            bcos::concepts::transaction::Transaction<RANGES::range_value_t<decltype(inputs)>>;
        setTransactionOrReceiptBuffers<isTransaction>(hashesRange, std::move(buffersRange));
    }

    template <bool isTransaction>
    void setTransactionOrReceiptBuffers(
        RANGES::range auto const& hashes, RANGES::range auto buffers)
    {
        impl().template impl_setTransactionOrReceiptBuffers<isTransaction>(
            hashes, std::move(buffers));
    }

    template <class LedgerType, bcos::concepts::block::Block BlockType>
    requires std::derived_from<LedgerType, LedgerBase<LedgerType>> ||
        std::derived_from<typename LedgerType::element_type,
            LedgerBase<typename LedgerType::element_type>>
    void sync(LedgerType& source, bool onlyHeader)
    {
        impl().template impl_sync<LedgerType, BlockType>(source, onlyHeader);
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Ledger = std::derived_from<Impl, LedgerBase<Impl>> ||
    std::derived_from<typename Impl::element_type, LedgerBase<typename Impl::element_type>>;

}  // namespace bcos::concepts::ledger