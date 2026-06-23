#pragma once
#include <bcos-utilities/IOServicePool.h>
#include <boost/asio/defer.hpp>
#include <atomic>
#include <tuple>

namespace bcos::scheduler_v1
{
class GC
{
    // Shared ownership so that in-flight deferred lambdas can safely
    // decrement the counter even after the GC object itself is destroyed.
    std::shared_ptr<std::atomic<size_t>> m_pendingCount = std::make_shared<std::atomic<size_t>>(0);
    bcos::IOServicePool::Ptr m_ioServicePool;

public:
    // Cap on in-flight deferred-destruction tasks. Above this we destroy
    // synchronously to bound the backlog when the io_context pool is
    // starved. Sized for "a few blocks worth" of contexts; tune if
    // per-call payload is unusually large or small.
    static constexpr size_t MAX_PENDING_GC = 64;

    explicit GC(bcos::IOServicePool::Ptr ioServicePool) : m_ioServicePool(std::move(ioServicePool))
    {}

    void collect(auto&&... resources)
    {
        auto& counter = *m_pendingCount;
        // Optimistically reserve a slot. fetch_add is a single RMW that never
        // retries; if we observe the pre-increment value already at or above
        // the cap, roll back and fall through to synchronous destruction. The
        // queue itself never exceeds the cap because over-reservers never
        // enqueue; only the counter can briefly overshoot before the matching
        // fetch_sub, and that transient is not observable outside this class.
        if (counter.fetch_add(1, std::memory_order_relaxed) >= MAX_PENDING_GC)
        {
            counter.fetch_sub(1, std::memory_order_relaxed);
            // Backpressure: destroy synchronously in the caller's scope.
            [[maybe_unused]] auto tuple =
                std::make_tuple(std::forward<decltype(resources)>(resources)...);
            return;
        }

        auto& ioService = m_ioServicePool->getIOService();
        auto pendingCount = m_pendingCount;
        boost::asio::post(*ioService,
            [pendingCount, resources = std::make_tuple(
                               std::forward<decltype(resources)>(resources)...)]() noexcept {
                (void)resources;
                pendingCount->fetch_sub(1, std::memory_order_relaxed);
            });
    }
};
}  // namespace bcos::scheduler_v1
