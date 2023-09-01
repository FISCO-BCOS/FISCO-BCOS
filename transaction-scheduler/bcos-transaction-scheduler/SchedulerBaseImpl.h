#pragma once
#include "MultiLayerStorage.h"
#include "bcos-framework/protocol/Transaction.h"
#include <bcos-concepts/ledger/Ledger.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-task/Task.h>
#include <oneapi/tbb/combinable.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_pipeline.h>

namespace bcos::transaction_scheduler
{

template <class MultiLayerStorage, class Executor>
class SchedulerBaseImpl
{
private:
    MultiLayerStorage& m_multiLayerStorage;
    Executor& m_executor;

public:
    SchedulerBaseImpl(MultiLayerStorage& multiLayerStorage, Executor& executor)
      : m_multiLayerStorage(multiLayerStorage), m_executor(executor)
    {}

    void start() { m_multiLayerStorage.newMutable(); }
    task::Task<bcos::h256> finish(
        protocol::BlockHeader const& blockHeader, crypto::Hash const& hashImpl)
    {
        auto& mutableStorage = m_multiLayerStorage.mutableStorage();
        auto it = co_await mutableStorage.seek(storage2::STORAGE_BEGIN);

        static constexpr int HASH_CHUNK_SIZE = 32;
        auto range = it.range() | RANGES::views::chunk(HASH_CHUNK_SIZE);

        tbb::combinable<bcos::h256> combinableHash;
        tbb::task_group taskGroup;
        for (auto&& subrange : range)
        {
            taskGroup.run([subrange = std::forward<decltype(subrange)>(subrange), &combinableHash,
                              &blockHeader, &hashImpl]() {
                auto& entryHash = combinableHash.local();

                for (auto const& keyValue : subrange)
                {
                    auto& [key, entry] = keyValue;
                    auto& [tableName, keyName] = *key;
                    if (entry)
                    {
                        entryHash ^=
                            entry->hash(tableName, keyName, hashImpl, blockHeader.version());
                    }
                    else
                    {
                        storage::Entry deleteEntry;
                        deleteEntry.setStatus(storage::Entry::DELETED);
                        entryHash ^=
                            deleteEntry.hash(tableName, keyName, hashImpl, blockHeader.version());
                    }
                }
            });
        }
        taskGroup.wait();
        m_multiLayerStorage.pushMutableToImmutableFront();

        co_return combinableHash.combine(
            [](const bcos::h256& lhs, const bcos::h256& rhs) -> bcos::h256 { return lhs ^ rhs; });
    }
    task::Task<void> commit(concepts::ledger::IsLedger auto& ledger, protocol::Block& block,
        std::vector<protocol::Transaction::ConstPtr> const& transactions)
    {
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SET_BLOCK);

            auto lastImmutable = m_multiLayerStorage.lastImmutableStorage();
            co_await ledger.template setBlock<concepts::ledger::HEADER,
                concepts::ledger::TRANSACTIONS_METADATA, concepts::ledger::RECEIPTS,
                concepts::ledger::NONCES>(*lastImmutable, block);

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
            co_await m_multiLayerStorage.mergeAndPopImmutableBack();
        }
    }
    task::Task<void> commit() { co_await m_multiLayerStorage.mergeAndPopImmutableBack(); }

    task::Task<protocol::TransactionReceipt::Ptr> call(
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction)
    {
        auto view = m_multiLayerStorage.fork(false);
        view.newTemporaryMutable();
        co_return co_await transaction_executor::execute(
            m_executor, view, blockHeader, transaction, 0);
    }

    MultiLayerStorage& multiLayerStorage() const& { return m_multiLayerStorage; }
    Executor& executor() const& { return m_executor; }
};

}  // namespace bcos::transaction_scheduler