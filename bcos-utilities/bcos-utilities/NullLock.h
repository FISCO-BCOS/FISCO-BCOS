#pragma once

namespace bcos::utilities
{

struct NullLock
{
    template <class Mutex>
    NullLock(const Mutex& /*unused*/) noexcept
    {}
    template <class Mutes>
    NullLock(const Mutes& /*unused*/, bool /*unused*/) noexcept
    {}
    void upgrade_to_writer() const noexcept {}
    NullLock() noexcept = default;
    void unlock() const noexcept {}
};
}  // namespace bcos::utilities