#pragma once

namespace bcos::utilities
{

struct NullLock
{
    template <class Mutex>
    NullLock(const Mutex& /*unused*/) noexcept
    {}
    NullLock() noexcept = default;
    void unlock() const noexcept {}
};
}  // namespace bcos::utilities