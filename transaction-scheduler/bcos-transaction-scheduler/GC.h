#pragma once
#include <oneapi/tbb/task_arena.h>
#include <atomic>

namespace bcos::scheduler_v1
{
class GC
{
    inline static std::atomic<size_t> s_pendingCount{0};

public:
    static constexpr size_t MAX_PENDING_GC = 64;

    static void collect(auto&&... resources)
    {
        static tbb::task_arena arena(1, 1, tbb::task_arena::priority::low);

        if (s_pendingCount.load(std::memory_order_relaxed) >= MAX_PENDING_GC)
        {
            // Synchronous destruction as backpressure
            [[maybe_unused]] auto tuple =
                std::make_tuple(std::forward<decltype(resources)>(resources)...);
            return;
        }
        s_pendingCount.fetch_add(1, std::memory_order_relaxed);
        arena.enqueue([resources = std::make_tuple(
                           std::forward<decltype(resources)>(resources)...)]() noexcept {
            (void)resources;
            s_pendingCount.fetch_sub(1, std::memory_order_relaxed);
        });
    }
};
}  // namespace bcos::scheduler_v1