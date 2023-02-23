#pragma once

#include "../protocol/Block.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Trait.h>

namespace bcos::concepts::transaction_scheduler
{

template <class TransactionRangeType>
concept TransactionRange = RANGES::range<TransactionRangeType>;

template <class Impl>
concept TransactionScheduler = requires(Impl impl, const protocol::BlockHeader& blockHeader)
{
    requires RANGES::range<task::AwaitableReturnType<decltype(impl.executeTransactions(
        blockHeader, RANGES::any_view<const protocol::Transaction&>{}))>> &&
        protocol::IsTransactionReceipt<
            RANGES::range_value_t<task::AwaitableReturnType<decltype(impl.executeTransactions(
                blockHeader, RANGES::any_view<const protocol::Transaction&>{}))>>>;
    requires concepts::bytebuffer::ByteBuffer<
        task::AwaitableReturnType<decltype(impl.getCode(std::string_view{}))>>;
    requires concepts::bytebuffer::ByteBuffer<
        task::AwaitableReturnType<decltype(impl.getABI(std::string_view{}))>>;
};

}  // namespace bcos::concepts::transaction_scheduler