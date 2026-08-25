#pragma once

#include "GC.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include "bcos-utilities/ITTAPI.h"  // ittapi::Report / ITT_DOMAINS (self-contained)
#include <oneapi/tbb/cache_aligned_allocator.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/partitioner.h>
#include <oneapi/tbb/task_arena.h>
#include <tbb/task_arena.h>
#include <concepts>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/iota.hpp>
#include <type_traits>
#include <bcos-utilities/BoostLog.h>

namespace bcos::scheduler_v1
{

/// Default per-block context type when the executor does not define its own.
/// Used as the 6th executeBlock parameter so executors that accept a
/// BlockContext (e.g. the OP executor) can receive per-block metadata without
/// the callers of BlockContext-less executors passing anything.
struct EmptyBlockContext
{
};

/// SFINAE: use the executor's own BlockContext type when it defines one,
/// otherwise fall back to EmptyBlockContext.
template <class E, class = void>
struct BlockContextOf
{
    using type = EmptyBlockContext;
};
template <class E>
struct BlockContextOf<E, std::void_t<typename E::BlockContext>>
{
    using type = typename E::BlockContext;
};

#define SERIAL_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SERIAL_SCHEDULER")

class SchedulerSerialImpl
{
    GC m_gc;
    std::size_t m_chunkSize;
    bool m_serial;

public:
    constexpr static auto MIN_TRANSACTION_GRAIN_SIZE = 16;

    /// @param chunkSize explicit chunk size (0 = default formula
    ///        max(count/max_concurrency, MIN_TRANSACTION_GRAIN_SIZE))
    /// @param serial     serial mode: chunk forced to 1 and pipeline max_tokens
    ///                   forced to 1, regardless of chunkSize
    explicit SchedulerSerialImpl(
        bcos::IOServicePool::Ptr ioServicePool, std::size_t chunkSize = 0, bool serial = false)
      : m_gc(std::move(ioServicePool)), m_chunkSize(chunkSize), m_serial(serial)
    {}

    /// Execute a block. @p ctx is the per-block context (BlockContextOf — EmptyBlockContext
    /// for executors that define none, OpBlockExecutionContext-like for OP executors) and is
    /// kept as a REFERENCE on purpose: executors write cross-transaction accumulators through
    /// its mutable fields (e.g. cumulativeGasUsed), and the caller reads them back after the
    /// co_await — a by-value parameter would silently cut that channel. Coroutine reference
    /// parameters live in the frame past the full-expression, so ctx must be an lvalue that
    /// outlives the co_await; there is deliberately NO default argument (a `= {}` default
    /// would materialise a temporary bound to the frame's reference — the same footgun
    /// OpstackExecutor::createExecuteContext recorded and removed). BlockContext-less callers
    /// use the 5-parameter overload below.
    template <class Storage, executor_v1::TransactionExecutor<Storage> TransactionExecutor>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage& storage,
        TransactionExecutor& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig,
        typename BlockContextOf<TransactionExecutor>::type const& ctx)
    {
        ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
            ittapi::ITT_DOMAINS::instance().SERIAL_EXECUTE);
        auto count = static_cast<int32_t>(::ranges::size(transactions));

        std::vector<typename TransactionExecutor::template ExecuteContext<Storage>> contexts;
        contexts.reserve(count);

        // serial mode forces chunk=1 regardless of the requested chunkSize
        // (guards against a caller forgetting to pass chunkSize=1); otherwise
        // m_chunkSize==0 selects the default formula.
        auto const chunkSize =
            m_serial ?
                1 :
                (m_chunkSize == 0 ?
                        std::max<size_t>((size_t)(count / tbb::this_task_arena::max_concurrency()),
                            (size_t)SchedulerSerialImpl::MIN_TRANSACTION_GRAIN_SIZE) :
                        m_chunkSize);
        auto chunks = ::ranges::views::iota(0, count) | ::ranges::views::chunk(chunkSize);
        using ChunkRange = ::ranges::range_value_t<decltype(chunks)>;
        ::ranges::range_size_t<decltype(transactions)> chunkIndex = 0;

        std::vector<protocol::TransactionReceipt::Ptr> receipts;
        receipts.reserve(count);

        // 四级流水线，3个线程
        // Four-stage pipeline, with 3 threads
        static tbb::task_arena arena(3, 1, tbb::task_arena::priority::high);
        arena.execute([&]() {
            tbb::parallel_pipeline(m_serial ? 1 : SchedulerSerialImpl::MIN_TRANSACTION_GRAIN_SIZE,
                tbb::make_filter<void, ChunkRange>(tbb::filter_mode::serial_in_order,
                    [&](tbb::flow_control& control) -> ChunkRange {
                        return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                            if (chunkIndex >= ::ranges::size(chunks))
                            {
                                control.stop();
                                co_return {};
                            }

                            ittapi::Report report(ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                ittapi::ITT_DOMAINS::instance().STAGE_1);

                            auto range = chunks[chunkIndex++];
                            for (auto i : range)
                            {
                                if constexpr (requires {
                                                  executor.createExecuteContext(storage,
                                                      blockHeader, transactions[i], i, ledgerConfig,
                                                      false, ctx);
                                              })
                                {
                                    contexts.emplace_back(
                                        co_await executor.createExecuteContext(storage, blockHeader,
                                            transactions[i], i, ledgerConfig, false, ctx));
                                }
                                else
                                {
                                    // Catch mismatched executor at compile time: BlockContext
                                    // declared but createExecuteContext does not accept it.
                                    static_assert(
                                        std::same_as<
                                            typename BlockContextOf<TransactionExecutor>::type,
                                            EmptyBlockContext>,
                                        "executor declares a BlockContext but "
                                        "createExecuteContext does not accept it; "
                                        "the context would be silently dropped");
                                    contexts.emplace_back(
                                        co_await executor.createExecuteContext(storage, blockHeader,
                                            transactions[i], i, ledgerConfig, false));
                                }
                            }
                            co_return range;
                        }());
                    }) &
                    tbb::make_filter<ChunkRange, ChunkRange>(tbb::filter_mode::serial_in_order,
                        [&](ChunkRange range) {
                            return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                                ittapi::Report report(
                                    ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                    ittapi::ITT_DOMAINS::instance().STAGE_2);
                                for (auto i : range)
                                {
                                    auto& context = contexts[i];
                                    co_await context.prepare();
                                }
                                co_return range;
                            }());
                        }) &
                    tbb::make_filter<ChunkRange, ChunkRange>(tbb::filter_mode::serial_in_order,
                        [&](ChunkRange range) {
                            return task::tbb::syncWait([&]() -> task::Task<ChunkRange> {
                                ittapi::Report report(
                                    ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                    ittapi::ITT_DOMAINS::instance().STAGE_3);
                                for (auto i : range)
                                {
                                    auto& context = contexts[i];
                                    co_await context.execute();
                                }
                                co_return range;
                            }());
                        }) &
                    tbb::make_filter<ChunkRange, void>(
                        tbb::filter_mode::serial_in_order, [&](ChunkRange range) {
                            task::tbb::syncWait([&]() -> task::Task<void> {
                                ittapi::Report report(
                                    ittapi::ITT_DOMAINS::instance().SERIAL_SCHEDULER,
                                    ittapi::ITT_DOMAINS::instance().STAGE_4);
                                for (auto i : range)
                                {
                                    auto& context = contexts[i];
                                    receipts.emplace_back(co_await context.finish());
                                }
                            }());
                        }));
        });

        m_gc.collect(std::move(contexts));
        co_return receipts;
    }

    /// BlockContext-less convenience overload: forwards a NAMED static EmptyBlockContext
    /// instead of a default argument, so no temporary is ever bound to the coroutine's
    /// reference parameter (see the 6-parameter overload for the lifetime rule). Constrained
    /// to executors that define no BlockContext of their own — value-initializing a real
    /// block context here (null blockHashes, chainId 0, ...) would compile and be nonsense.
    template <class Storage, executor_v1::TransactionExecutor<Storage> TransactionExecutor>
        requires std::same_as<typename BlockContextOf<TransactionExecutor>::type, EmptyBlockContext>
    task::Task<std::vector<protocol::TransactionReceipt::Ptr>> executeBlock(Storage& storage,
        TransactionExecutor& executor, protocol::BlockHeader const& blockHeader,
        ::ranges::input_range auto const& transactions, ledger::LedgerConfig const& ledgerConfig)
    {
        static EmptyBlockContext const emptyContext = {};
        co_return co_await executeBlock(
            storage, executor, blockHeader, transactions, ledgerConfig, emptyContext);
    }
};


}  // namespace bcos::scheduler_v1