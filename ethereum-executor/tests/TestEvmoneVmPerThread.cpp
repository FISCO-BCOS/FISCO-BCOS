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
 * @file TestEvmoneVmPerThread.cpp
 * @brief The negative control for the shared-VM race, as a committed test.
 *
 * An evmone VM pools ExecutionState objects in an unsynchronised vector indexed by EVM call
 * depth, so two threads must never drive one VM. That property is what keeps the
 * `std::length_error`, the null-EthAccount SIGSEGV and the wrong gas estimates from coming
 * back, and it is invisible to the compiler: reverting `thread_local` to a shared instance
 * still builds and still passes every single-threaded test, because the block pipeline is
 * serial by construction.
 *
 * These two assertions are the whole invariant:
 *   - one thread always gets the same VM (the pool is reused across calls, no per-transaction
 *     evmc_create_evmone)
 *   - two threads never get the same VM
 *
 * If someone narrows `thread_local` back to a function-local `static`, the second assertion
 * fails and no concurrency timing is needed to see it.
 */

#include <ethereum-executor/EvmoneVm.h>
#include <boost/test/unit_test.hpp>
#include <thread>

using bcos::executor_v1::eth::threadLocalEvmoneVm;

BOOST_AUTO_TEST_SUITE(EvmoneVmPerThreadTest)

/// The pool is per thread and reused: repeated calls on one thread must not build a new VM.
BOOST_AUTO_TEST_CASE(SameThreadGetsTheSameVm)
{
    auto* first = std::addressof(threadLocalEvmoneVm());
    auto* second = std::addressof(threadLocalEvmoneVm());
    BOOST_CHECK_EQUAL(first, second);
    // A VM wrapping a null vm would silently answer every execution with a failure; catch the
    // one construction failure that evmc_create_evmone can report.
    BOOST_CHECK(threadLocalEvmoneVm().get_raw_pointer() != nullptr);
}

/// The invariant the shared-VM bug violated. This is the assertion that fails when
/// threadLocalEvmoneVm's `thread_local` is removed.
BOOST_AUTO_TEST_CASE(DifferentThreadsGetDifferentVms)
{
    auto* mainVm = std::addressof(threadLocalEvmoneVm());

    void* otherVm = nullptr;
    void* otherVmAgain = nullptr;
    std::thread worker{[&] {
        otherVm = std::addressof(threadLocalEvmoneVm());
        otherVmAgain = std::addressof(threadLocalEvmoneVm());
    }};
    worker.join();

    BOOST_REQUIRE(otherVm != nullptr);
    BOOST_CHECK_NE(mainVm, otherVm);           // two threads, two VMs
    BOOST_CHECK_EQUAL(otherVm, otherVmAgain);  // and the worker's is stable too
}

/// Several threads at once: each gets its own, and no two coincide. A shared VM would collapse
/// every one of these to the same address.
BOOST_AUTO_TEST_CASE(ConcurrentThreadsEachGetTheirOwn)
{
    constexpr int kThreads = 8;
    void* seen[kThreads] = {};
    std::thread workers[kThreads];
    for (int i = 0; i < kThreads; ++i)
    {
        workers[i] = std::thread{[&seen, i] { seen[i] = std::addressof(threadLocalEvmoneVm()); }};
    }
    auto* mainVm = std::addressof(threadLocalEvmoneVm());
    for (auto& w : workers)
        w.join();

    for (int i = 0; i < kThreads; ++i)
    {
        BOOST_REQUIRE(seen[i] != nullptr);
        BOOST_CHECK_NE(seen[i], mainVm);
        for (int j = 0; j < i; ++j)
            BOOST_CHECK_NE(seen[i], seen[j]);
    }
}

BOOST_AUTO_TEST_SUITE_END()
