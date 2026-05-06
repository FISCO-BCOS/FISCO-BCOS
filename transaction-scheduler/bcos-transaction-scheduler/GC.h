#pragma once
#include <oneapi/tbb/task_arena.h>
#include <atomic>
#include <tuple>

namespace bcos::scheduler_v1
{
class GC
{
    inline static std::atomic<size_t> s_pendingCount{0};

public:
    // Cap on in-flight deferred-destruction tasks. Above this we destroy
    // synchronously to bound the backlog when the low-priority GC arena is
    // starved by high-priority work. Sized for "a few blocks worth" of
    // contexts; tune if per-call payload is unusually large or small.
    static constexpr size_t MAX_PENDING_GC = 64;

    static void collect(auto&&... resources)
    {
        static tbb::task_arena arena(1, 1, tbb::task_arena::priority::low);

        // Atomically reserve a slot iff we are strictly under the cap. Using
        // compare_exchange_weak as the loop condition avoids the TOCTOU window
        // of a separate load+fetch_add; on CAS failure cur is updated with the
        // observed value, so a concurrent fill correctly drops us into the
        // synchronous-destruction fallback below.
        size_t cur = s_pendingCount.load(std::memory_order_relaxed);
        while (cur < MAX_PENDING_GC && !s_pendingCount.compare_exchange_weak(cur, cur + 1,
                                           std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }

        if (cur >= MAX_PENDING_GC)
        {
            // Backpressure: destroy synchronously in the caller's scope.
            [[maybe_unused]] auto tuple =
                std::make_tuple(std::forward<decltype(resources)>(resources)...);
            return;
        }

        arena.enqueue([resources = std::make_tuple(
                           std::forward<decltype(resources)>(resources)...)]() noexcept {
            (void)resources;
            s_pendingCount.fetch_sub(1, std::memory_order_relaxed);
        });
    }
};
}  // namespace bcos::scheduler_v1
