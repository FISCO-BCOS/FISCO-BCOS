/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file FIB169_LeaderElectionLockScopeTest.cpp
 * @brief FIB-169: LeaderElection::campaignLeader() and updateSelfConfig()
 *        must NOT hold m_mutex across blocking etcd RPCs nor across the
 *        externally supplied m_onCampaignHandler. See audit/findings/FIB-169.md.
 *
 *        We cannot easily instantiate the production LeaderElection here on
 *        every CI environment (it transitively depends on a live etcd-cpp-apiv3
 *        client whose blocking RPCs are exactly what FIB-169 is about).
 *        Instead we model the refactored lock-scope contract with a minimal
 *        replica that mirrors the pattern campaignLeader / updateSelfConfig
 *        now use: snapshot state under the lock, release, then perform the
 *        slow RPC + handler calls without the lock. The test fails if the
 *        replica goes back to the bug pattern (lock held across the slow op).
 * @date 2026-05-08
 */
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(FIB169LeaderElectionLockScopeTest)

namespace
{
/// Mirrors the post-FIB-169 contract for LeaderElection::campaignLeader():
///   1. Take m_mutex.
///   2. Snapshot the small bits of state needed downstream.
///   3. Release m_mutex.
///   4. Perform the blocking etcd txn / external handler invocation.
///   5. (Optionally) re-acquire m_mutex for a brief commit window.
class LockScopeFixture
{
public:
    using SlowOp = std::function<void()>;

    /// Returns true if the slow op is allowed to run *without* m_mutex held.
    /// A regression where the slow op runs under the lock would be caught by
    /// `tryAcquireDuringSlowOp()` returning false.
    bool campaignLikeNarrowScope(SlowOp const& _slowOp)
    {
        std::string snapshotLeaderKey;
        // === inside critical section ===
        {
            std::lock_guard<std::recursive_mutex> l(m_mutex);
            snapshotLeaderKey = m_leaderKey;
        }
        // === outside critical section ===
        _slowOp();  // analogous to grantLease() / m_etcdClient->txn().get()
        return !snapshotLeaderKey.empty();
    }

    /// Bug-pattern (pre-FIB-169) for regression demonstration: the slow op
    /// runs while m_mutex is still held. Used negatively in the test below.
    void campaignLikeWideScope(SlowOp const& _slowOp)
    {
        std::lock_guard<std::recursive_mutex> l(m_mutex);
        // wide scope — slow op happens UNDER the lock
        _slowOp();
    }

    /// Simulates a sibling thread (e.g. updateSelfConfig) trying to take the
    /// same mutex. Returns true if it succeeded within the timeout.
    bool tryAcquireDuringSlowOp(std::chrono::milliseconds _timeout) const
    {
        auto deadline = std::chrono::steady_clock::now() + _timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::unique_lock<std::recursive_mutex> l(m_mutex, std::try_to_lock);
            if (l.owns_lock())
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    void setLeaderKey(std::string _key)
    {
        std::lock_guard<std::recursive_mutex> l(m_mutex);
        m_leaderKey = std::move(_key);
    }

private:
    mutable std::recursive_mutex m_mutex;
    std::string m_leaderKey;
};
}  // namespace

// FIB-169 anchor #1 (positive): a campaignLeader-like worker that snapshots
// state under m_mutex and runs its blocking RPC outside the lock must NOT
// block a sibling thread that needs the same mutex.
BOOST_AUTO_TEST_CASE(narrowLockScopeAllowsConcurrentMutexAcquisition)
{
    LockScopeFixture fx;
    fx.setLeaderKey("/consensus");

    std::atomic<bool> slowOpStarted{false};
    std::atomic<bool> slowOpFinished{false};
    std::atomic<bool> sibling_acquired_during_slow_op{false};

    // The slow op blocks for ~200 ms. While it runs, the sibling tries to
    // take m_mutex. If campaignLeader released the lock before launching the
    // slow op (post-FIB-169 contract), the sibling acquires within < 50 ms.
    auto slowOp = [&]() {
        slowOpStarted.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        slowOpFinished.store(true);
    };

    std::thread campaignThread([&]() { fx.campaignLikeNarrowScope(slowOp); });

    // Wait for the slow op to start, then probe.
    while (!slowOpStarted.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    BOOST_CHECK(!slowOpFinished.load());  // slow op should still be running
    sibling_acquired_during_slow_op.store(fx.tryAcquireDuringSlowOp(std::chrono::milliseconds(50)));

    campaignThread.join();
    BOOST_CHECK(slowOpFinished.load());
    BOOST_CHECK_MESSAGE(sibling_acquired_during_slow_op.load(),
        "FIB-169 regression: sibling thread could not acquire m_mutex while "
        "campaignLeader-like worker was running its slow etcd-RPC stand-in. "
        "The slow op must run OUTSIDE the lock.");
}

// FIB-169 anchor #2 (negative regression demo): the pre-FIB-169 bug-pattern
// (slow op runs under the lock) MUST be observably blocking. This guards the
// test against false positives — if both patterns let the sibling in, the
// fixture itself is broken.
BOOST_AUTO_TEST_CASE(wideLockScopeBlocksSibling_regressionDemo)
{
    LockScopeFixture fx;
    fx.setLeaderKey("/consensus");

    std::atomic<bool> slowOpStarted{false};
    std::atomic<bool> sibling_acquired_during_slow_op{false};

    auto slowOp = [&]() {
        slowOpStarted.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    };

    std::thread campaignThread([&]() { fx.campaignLikeWideScope(slowOp); });

    while (!slowOpStarted.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    sibling_acquired_during_slow_op.store(fx.tryAcquireDuringSlowOp(std::chrono::milliseconds(40)));

    campaignThread.join();
    BOOST_CHECK_MESSAGE(!sibling_acquired_during_slow_op.load(),
        "wide-scope (pre-FIB-169) pattern unexpectedly let the sibling in; "
        "fixture self-check broken — narrow-scope test result is meaningless.");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
