#pragma once
#include "../protocol/Block.h"
#include "../storage/Storage.h"
#include <bcos-framework/protocol/Block.h>
#include <bcos-task/Trait.h>
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
        bcos::concepts::block::Block auto& block)
    {
        return impl().template impl_getBlock<Flags...>(blockNumber, block);
    }

    template <DataFlag... Flags>
    auto getBlockByNodeList(bcos::concepts::block::BlockNumber auto blockNumber,
        bcos::concepts::block::Block auto& block, bcos::crypto::NodeIDs const& nodeList)
    {
        return impl().template impl_getBlockByNodeList<Flags...>(blockNumber, block, nodeList);
    }

    template <DataFlag... Flags>
    auto setBlock(bcos::concepts::block::Block auto block)
    {
        return impl().template impl_setBlock<Flags...>(std::move(block));
    }

    auto getBlockNumberByHash(
        bcos::concepts::bytebuffer::ByteBuffer auto const& hash, std::integral auto& number)
    {
        return impl().impl_getBlockNumberByHash(hash, number);
    }

    auto getBlockHashByNumber(
        std::integral auto number, bcos::concepts::bytebuffer::ByteBuffer auto& hash)
    {
        return impl().impl_getBlockHashByNumber(number, hash);
    }

    auto getABI(std::string contractAddress) { return impl().impl_getABI(contractAddress); }


    auto getTransactions(RANGES::range auto const& hashes, RANGES::range auto& out)
        requires TransactionOrReceipt<RANGES::range_value_t<std::remove_cvref_t<decltype(out)>>>
    {
        return impl().impl_getTransactions(hashes, out);
    }

    auto getStatus() { return impl().impl_getStatus(); }

    auto getAllPeersStatus() { return impl().impl_getAllPeersStatus(); }

    template <class LedgerType, bcos::concepts::block::Block BlockType>
        requires std::derived_from<LedgerType, LedgerBase<LedgerType>> ||
                 std::derived_from<typename LedgerType::element_type,
                     LedgerBase<typename LedgerType::element_type>>
    auto sync(LedgerType& source, bool onlyHeader)
    {
        return impl().template impl_sync<LedgerType, BlockType>(source, onlyHeader);
    }

    auto setupGenesisBlock(bcos::concepts::block::Block auto block)
    {
        return impl().impl_setupGenesisBlock(std::move(block));
    }
    auto checkGenesisBlock(bcos::concepts::block::Block auto block)
    {
        return impl().impl_checkGenesisBlock(std::move(block));
    }

private:
    friend Impl;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Ledger =
    std::derived_from<Impl, LedgerBase<Impl>> ||
    std::derived_from<typename Impl::element_type, LedgerBase<typename Impl::element_type>>;

template <class Impl>
concept IsLedger = true;

}  // namespace bcos::concepts::ledger
