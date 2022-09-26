#pragma once
#include "../Block.h"
#include "../storage/Storage.h"
#include <bcos-concepts/Coroutine.h>
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
struct TRANSACTIONS_METADATA: public DataFlagBase {};
struct TRANSACTIONS: public DataFlagBase {};
struct RECEIPTS: public DataFlagBase {};
struct NONCES: public DataFlagBase {};
// clang-format on
template <class FlagType>
concept DataFlag = std::derived_from<FlagType, DataFlagBase>;

struct Status
{
    int64_t total = 0;
    int64_t failed = 0;
    int64_t blockNumber = 0;
};

// All method in ledger is uncacheed
template <class Impl>
class LedgerBase
{
public:
    template <DataFlag... Flags>
    auto getBlock(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block) -> bcos::coroutine::Awaitable auto
    {
        return impl().template impl_getBlock<Flags...>(blockNumber, block);
    }

    template <DataFlag... Flags>
    auto setBlock(bcos::concepts::block::Block auto block) -> bcos::coroutine::Awaitable auto
    {
        return impl().template impl_setBlock<Flags...>(std::move(block));
    }

    auto getBlockNumberByHash(bcos::concepts::bytebuffer::ByteBuffer auto const& hash,
        std::integral auto& number) -> bcos::coroutine::Awaitable auto
    {
        return impl().impl_getBlockNumberByHash(hash, number);
    }

    auto getBlockHashByNumber(std::integral auto number,
        bcos::concepts::bytebuffer::ByteBuffer auto& hash) -> bcos::coroutine::Awaitable auto
    {
        return impl().impl_getBlockHashByNumber(number, hash);
    }

    auto getTransactions(RANGES::range auto const& hashes, RANGES::range auto& out)
        -> bcos::coroutine::Awaitable auto requires
        TransactionOrReceipt<RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>>
    {
        return impl().impl_getTransactions(hashes, out);
    }

    auto getStatus() -> bcos::coroutine::Awaitable auto { return impl().impl_getStatus(); }

    template <bcos::crypto::hasher::Hasher Hasher>
    auto setTransactions(RANGES::range auto const& inputs) -> bcos::coroutine::Awaitable
        auto requires bcos::concepts::ledger::TransactionOrReceipt<
            RANGES::range_value_t<std::remove_cvref_t<decltype(inputs)>>>
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
        return setTransactionOrReceiptBuffers<isTransaction>(hashesRange, std::move(buffersRange));
    }

    template <bool isTransaction>
    auto setTransactionOrReceiptBuffers(RANGES::range auto const& hashes,
        RANGES::range auto buffers) -> bcos::coroutine::Awaitable auto
    {
        return impl().template impl_setTransactions<isTransaction>(hashes, std::move(buffers));
    }

    template <class LedgerType, bcos::concepts::block::Block BlockType>
    requires std::derived_from<LedgerType, LedgerBase<LedgerType>> ||
        std::derived_from<typename LedgerType::element_type,
            LedgerBase<typename LedgerType::element_type>>
    auto sync(LedgerType& source, bool onlyHeader) -> bcos::coroutine::Awaitable auto
    {
        return impl().template impl_sync<LedgerType, BlockType>(source, onlyHeader);
    }

    auto setupGenesisBlock(bcos::concepts::block::Block auto block) -> bcos::coroutine::Awaitable
        auto
    {
        return impl().template impl_setupGenesisBlock(std::move(block));
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Ledger = std::derived_from<Impl, LedgerBase<Impl>> ||
    std::derived_from<typename Impl::element_type, LedgerBase<typename Impl::element_type>>;

}  // namespace bcos::concepts::ledger