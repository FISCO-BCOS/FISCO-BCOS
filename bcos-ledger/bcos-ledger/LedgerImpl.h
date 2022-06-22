#pragma once
#include <bcos-framework/concepts/Basic.h>
#include <bcos-framework/concepts/Block.h>
#include <bcos-framework/concepts/Storage.h>
#include <bitset>
#include <tuple>

namespace bcos::ledger
{

// clang-format off
struct GETBLOCK_FLAGS {};
struct BLOCK: public GETBLOCK_FLAGS {};
struct BLOCK_HEADER: public GETBLOCK_FLAGS {};
struct BLOCK_TRANSACTIONS: public GETBLOCK_FLAGS {};
struct BLOCK_RECEIPTS: public GETBLOCK_FLAGS {};
// clang-format on

template <bcos::storage::Storage Storage>
class LedgerImpl
{
public:
    LedgerImpl(Storage storage) : m_storage{std::move(storage)} {}

    template <class Flag, class... Flags>
    requires std::derived_from<Flag, GETBLOCK_FLAGS>
    auto getBlock(bcos::concepts::Hash auto const& blockHash)
    {
        return std::tuple_cat(std::tuple{typeid(Flag).name()}, std::tuple{getBlock<Flags...>(blockHash)});
    }

private:
    auto getBlock([[maybe_unused]] bcos::concepts::Hash auto const& blockHash) { return std::tuple{}; }

    Storage m_storage;
};
}  // namespace bcos::ledger