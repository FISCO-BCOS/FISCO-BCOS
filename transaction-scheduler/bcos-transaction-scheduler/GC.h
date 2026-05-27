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

        // Optimistically reserve a slot. fetch_add is a single RMW that never
        // retries; if we observe the pre-increment value already at or above
        // the cap, roll back and fall through to synchronous destruction. The
        // queue itself never exceeds the cap because over-reservers never
        // enqueue; only the counter can briefly overshoot before the matching
        // fetch_sub, and that transient is not observable outside this class.
        if (s_pendingCount.fetch_add(1, std::memory_order_relaxed) >= MAX_PENDING_GC)
        {
            s_pendingCount.fetch_sub(1, std::memory_order_relaxed);
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
