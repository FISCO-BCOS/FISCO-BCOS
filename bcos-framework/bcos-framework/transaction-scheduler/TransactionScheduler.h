#pragma once

#include "../protocol/Block.h"
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-task/Task.h>
#include <bcos-task/Trait.h>
#include <bcos-utilities/Ranges.h>

namespace bcos::concepts::transaction_scheduler
{

class TransactionScheduler
{
public:
    TransactionScheduler() noexcept = default;
    TransactionScheduler(TransactionScheduler const&) noexcept = default;
    TransactionScheduler(TransactionScheduler&&) noexcept = default;
    TransactionScheduler& operator=(TransactionScheduler const&) noexcept = default;
    TransactionScheduler& operator=(TransactionScheduler&&) noexcept = default;
    virtual ~TransactionScheduler() noexcept = default;

    virtual task::Task<std::vector<protocol::TransactionReceipt::Ptr>> execute(
        protocol::Block const& blockHeader,
        RANGES::any_view<protocol::Transaction const&,
            RANGES::category::random_access | RANGES::category::sized>
            transactions) = 0;
};

}  // namespace bcos::concepts::transaction_scheduler