#pragma once
#include <oneapi/tbb/task_group.h>

namespace bcos::transaction_scheduler
{
class GC
{
private:
    std::unique_ptr<tbb::task_group> m_releaseGroup;

public:
    GC() : m_releaseGroup(std::make_unique<tbb::task_group>()) {}
    GC(const GC&) = delete;
    GC(GC&&) noexcept = default;
    GC& operator=(const GC&) = delete;
    GC& operator=(GC&&) noexcept = default;
    ~GC() noexcept { m_releaseGroup->wait(); }

    void collect(auto&&... resources)
    {
        m_releaseGroup->run([resources = std::make_tuple(
                                 std::forward<decltype(resources)>(resources)...)]() noexcept {});
    }
};
}  // namespace bcos::transaction_scheduler