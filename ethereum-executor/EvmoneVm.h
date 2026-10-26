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
 * @file EvmoneVm.h
 * @brief The evmone VM belonging to the calling thread.
 *
 * A single evmone VM must not be driven by two threads: it pools ExecutionState objects in an
 * unsynchronised vector indexed by EVM call depth (VM::get_execution_state resize()s it on
 * demand) and each pooled slot remembers the host context of the frame that last reset it. Two
 * threads through one VM race on that vector, and one thread's EVM can end up calling into the
 * other's host, corrupting its EthereumState journal.
 *
 * One VM per thread keeps that pool alive across calls (no per-transaction evmc_create_evmone)
 * while only ever indexing it with the current thread's own frames.
 *
 * The VM is therefore always taken *by the code that executes the EVM*, never passed down into
 * it: a `evmc::VM&` handed across a coroutine boundary is bound on the caller's thread and used
 * on whichever thread the coroutine happens to run on, which nothing enforces to be the same.
 * This header exists on its own (rather than in EthereumExecutor.h) so that every layer that
 * drives the EVM -- EthereumTransition.h, EthSystemCalls.h, the OP executor -- can take the VM
 * without depending on an executor class.
 *
 * Taking it inside the synchronous span is necessary but not by itself sufficient, and the
 * remaining premise is worth naming because it is what an asynchronous storage would break:
 * holding the VM across an await is only safe while that await never actually suspends. The read
 * path the Ethereum lane runs on (EthereumState over the inline and blocking storages) reaches
 * task::tbb::syncWait (libtask/bcos-task/TBBWait.h), which calls oneapi::tbb::task::suspend only
 * when the awaited task has not already finished -- and for these storages it always has, so the
 * span runs to completion on the thread that entered it. A storage whose reads complete on
 * another task would suspend there, resume on a different TBB worker, and the frame would
 * continue on a thread that does not own this VM. Reusing the VM is therefore conditional on
 * that property, not merely on the acquisition site.
 *
 * Cost: one VM per *live* thread, held until that thread exits, so the total is
 * (TBB worker threads + RPC IO threads) x per-VM footprint. The per-VM footprint is not a
 * constant: the pool's slots are constructed lazily and each live slot retains the
 * ExecutionState memory buffer of the largest contract ever executed on that thread (a reset
 * clears the size, not the capacity). Plan against the high-water mark for the target
 * deployment rather than the empty-pool figure. Callers cannot influence which thread serves
 * them, but eth_call is unauthenticated, so how large a thread's high-water mark grows is
 * caller-influenced.
 *
 * Measured on the 3.18.0 tree (x86-64, evmone 0.21):
 *   - one pooled slot costs 36.5 KiB even when idle: a 32 KiB stack (StackSpace is heap
 *     allocated, 1024 x 32 B), the 4 KiB initial Memory buffer, and 432 B of ExecutionState;
 *   - a frame that grows memory to 1 MiB leaves +1092 KiB resident after it returns;
 *   - a depth-64 chain leaves +38.2 KiB per frame (2484 KiB for 65 slots), i.e. the fixed
 *     stack dominates until contracts use memory.
 * Slots are indexed by call depth, so a thread keeps one per depth it has ever reached (EVM
 * cap: 1024), and each keeps its largest memory ever.
 *
 * Threads: [thread_pool] tbb_thread_count (default 0 = all cores, N) plus the shared IO pool
 * (default N + 1), so at most 2N + 1 threads can hold a VM -- 33 on a 16-core node.
 *   - benign: depth <= 10, a few KiB of memory per frame -> ~0.5 MiB per thread, ~17 MiB total;
 *   - one crafted eth_call under the 50M op-geth rpc.gascap that spreads ~49M gas of memory
 *     expansion over a 1024-deep self-call chain retains ~136 MiB per thread (272 MiB with the
 *     capacity doubling), ~8.8 GiB for the node. The peak is not new -- the pre-reuse code paid
 *     the same transiently -- but it is now sticky per thread instead of returning to the
 *     allocator, and multiplied by however many threads have served such a call.
 */
#pragma once

#include <evmone/evmone.h>
#include <evmc/evmc.hpp>

namespace bcos::executor_v1::eth
{
/// The EVM instance for the calling thread. Only valid on the thread that took it.
inline evmc::VM& threadLocalEvmoneVm()
{
    thread_local evmc::VM vm{evmc_create_evmone()};
    return vm;
}
}  // namespace bcos::executor_v1::eth
