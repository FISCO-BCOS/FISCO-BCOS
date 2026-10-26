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
