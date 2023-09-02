#pragma once
#include "MultiLayerStorage.h"
#include "bcos-concepts/ledger/Ledger.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-task/Task.h"
#include <oneapi/tbb/combinable.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_pipeline.h>

namespace bcos::transaction_scheduler
{

task::Task<void> tag_invoke(tag_t<commit> /*unused*/, auto& scheduler, auto& storage,
    concepts::ledger::IsLedger auto& ledger, protocol::Block& block,
    std::vector<protocol::Transaction::ConstPtr> const& transactions)
{
    if (block.blockHeaderConst()->number() != 0)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().SET_BLOCK);

        auto& lastImmutable = storage;
        co_await ledger
            .template setBlock<concepts::ledger::HEADER, concepts::ledger::TRANSACTIONS_METADATA,
                concepts::ledger::RECEIPTS, concepts::ledger::NONCES>(*lastImmutable, block);

        std::vector<bcos::h256> hashes(RANGES::size(transactions));
        std::vector<std::vector<bcos::byte>> buffers(RANGES::size(transactions));
        tbb::parallel_for(
            tbb::blocked_range(0LU, RANGES::size(transactions)), [&](auto const& range) {
                for (auto i = range.begin(); i != range.end(); ++i)
                {
                    auto& transaction = transactions[i];
                    hashes[i] = transaction->hash();
                    bcos::concepts::serialize::encode(*transaction, buffers[i]);
                }
            });

        co_await ledger.template setTransactions<true>(
            *lastImmutable, std::move(hashes), std::move(buffers));
    }

    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().MERGE_STATE);
        // co_await scheduler.multiLayerStorage().mergeAndPopImmutableBack(); //TODO: Move to
        // baseline scheduler
    }
}

// task::Task<protocol::TransactionReceipt::Ptr> call(
//     protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction)
// {
//     auto view = m_multiLayerStorage.fork(false);
//     view.newTemporaryMutable();
//     co_return co_await transaction_executor::execute(
//         m_executor, view, blockHeader, transaction, 0);
// }

}  // namespace bcos::transaction_scheduler