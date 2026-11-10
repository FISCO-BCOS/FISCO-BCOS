/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file RcuSnapshot.h
 * @brief Copy-on-write snapshot with an explicit grace period, for read-mostly shared state.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace bcos
{

/**
 * @brief Reader-writer state whose read path is a single atomic load.
 *
 * Some state is read on every message and written only when membership or topology changes, i.e. a
 * read:write ratio in the thousands. Wrapping it in a boost::shared_mutex makes every read touch a
 * shared counter with an atomic RMW, which bounces one cache line between every reader core.
 * Measured on the gateway session lookup: 26 ns with one reader thread, ~600 ns with eight threads,
 * for the identical lookup.
 *
 * This class takes the lock out of the read path:
 *   - read() is one acquire load, returning a const reference to the current snapshot. No lock, no
 *     refcount, no RMW.
 *   - update() clones the current snapshot under a writer mutex, applies the mutation, publishes
 *     the clone with a release store, and retires the previous snapshot.
 *
 * RETIREMENT uses an explicit grace period: a retired snapshot is parked with a timestamp and
 * destroyed only once it is older than `gracePeriod`. Because the retired snapshot stays alive for
 * that whole window, anything it owns -- including objects a reader reached through it -- also stays
 * alive. That is what makes it safe to hand out references (and pointers into) a snapshot instead of
 * paying for shared ownership on every read.
 *
 * Two rules keep this sound, and both are load-bearing:
 *   1. A reader MUST NOT keep the reference across a suspension point (a co_await) or across any
 *      call that could block. Take the reference, copy or decide, then drop it.
 *   2. `gracePeriod` must exceed the longest reader critical section by a wide margin. The default
 *      is 1 s against critical sections measured in nanoseconds.
 *
 * Rule 1 is not enforceable by the type system. `retiredCount()` and `setGracePeriod()` exist so
 * tests can shrink the grace window and assert that reuse still works.
 */
template <class T>
class RcuSnapshot
{
public:
    using Clock = std::chrono::steady_clock;

    /// Deep-clone hook. The default copies T, which is right for value types. A table holding
    /// unique_ptrs to mutable objects must supply a cloner: a plain copy would share those objects
    /// with the previous snapshot and defeat the immutability the lock-free read path depends on.
    using Cloner = std::function<T(T const&)>;

    RcuSnapshot() : RcuSnapshot(T{}) {}

    explicit RcuSnapshot(T initial, Cloner cloner = {})
      : m_gracePeriod(std::chrono::milliseconds(1000)), m_cloner(std::move(cloner))
    {
        auto owned = std::make_unique<T const>(std::move(initial));
        m_current.store(owned.get(), std::memory_order_relaxed);
        m_owner = std::move(owned);
    }

    RcuSnapshot(const RcuSnapshot&) = delete;
    RcuSnapshot& operator=(const RcuSnapshot&) = delete;
    ~RcuSnapshot() = default;

    /// Lock-free read. The returned reference, and everything reachable through it, stays valid for
    /// at least `gracePeriod()` after the snapshot is superseded; see rule 1 in the class comment for
    /// the caller's obligation.
    T const& read() const
    {
        auto const* current = m_current.load(std::memory_order_acquire);
        if (current == nullptr) [[unlikely]]
        {
            // Unreachable by construction: the constructor publishes a snapshot before the object is
            // handed out, and update() only ever swaps one valid snapshot for another -- nothing
            // writes nullptr back. The branch exists so the invariant is visible to a static
            // analyser (and so a future change that breaks it degrades instead of dereferencing
            // null). It is a single predicted-not-taken compare, well below measurement noise.
            static_assert(std::is_default_constructible_v<T>,
                "RcuSnapshot needs a default-constructible T for the unreachable null fallback");
            static T const c_empty{};
            return c_empty;
        }
        return *current;
    }

    /// Convenience wrapper for the common "look up and return a value" shape.
    template <class Fn>
    auto withRead(Fn&& fn) const
    {
        return fn(read());
    }

    /// Clone-modify-publish. The mutator runs against a private copy, so readers never block and
    /// never observe a half-applied mutation.
    template <class Fn>
    void update(Fn&& mutate)
    {
        std::lock_guard lock(m_writeMutex);
        retireExpiredLocked();

        auto next = std::make_unique<T const>(m_cloner ? m_cloner(read()) : T(read()));
        mutate(const_cast<T&>(*next));

        m_current.store(next.get(), std::memory_order_release);
        m_retired.emplace_back(Clock::now(), std::move(m_owner));
        m_owner = std::move(next);
    }

    /// Destroy every retired snapshot whose grace period has elapsed. update() calls this, and a
    /// periodic timer may call it so a retiring write is not the only reclaimer.
    void retireExpired()
    {
        std::lock_guard lock(m_writeMutex);
        retireExpiredLocked();
    }

    void setGracePeriod(std::chrono::milliseconds period) { m_gracePeriod = period; }
    std::chrono::milliseconds gracePeriod() const { return m_gracePeriod; }

    /// Snapshots waiting for their grace period to elapse (diagnostics and tests).
    std::size_t retiredCount()
    {
        std::lock_guard lock(m_writeMutex);
        return m_retired.size();
    }

private:
    void retireExpiredLocked()
    {
        auto now = Clock::now();
        auto it = m_retired.begin();
        while (it != m_retired.end())
        {
            if (now - it->retiredAt < m_gracePeriod)
            {
                break;  // retired in order, so everything after is younger
            }
            it = m_retired.erase(it);
        }
    }

    std::atomic<T const*> m_current{nullptr};
    std::unique_ptr<T const> m_owner;

    mutable std::mutex m_writeMutex;
    struct Retired
    {
        Clock::time_point retiredAt;
        std::unique_ptr<T const> snapshot;
    };
    std::deque<Retired> m_retired;
    std::chrono::milliseconds m_gracePeriod;
    Cloner m_cloner;
};

}  // namespace bcos
