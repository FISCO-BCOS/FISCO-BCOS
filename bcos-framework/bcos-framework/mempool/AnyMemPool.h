/**
 *  Copyright (C) 2025 FISCO BCOS.
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
 * @file AnyMemPool.h
 * @brief Type-erased wrapper around any type satisfying MemPool concept,
 *        using Microsoft proxy library's facade for zero-overhead dispatch.
 */

#pragma once

#include "bcos-framework/mempool/MemPool.h"
#include <proxy/proxy.h>
#include <vector>

namespace bcos::mempool
{

/// Dispatch structs — each maps to a member function of the underlying
/// MemPool implementation.  The two remove overloads are given distinct
/// names (removeByState / removeByHashes) so that pro::proxy's operator->
/// can dispatch them without ambiguity.
PRO_DEF_MEM_DISPATCH(MemAdd, add);
PRO_DEF_MEM_DISPATCH(MemSeal, seal);
PRO_DEF_MEM_DISPATCH(MemRemoveState, removeByState);
PRO_DEF_MEM_DISPATCH(MemRemoveHashes, removeByHashes);
PRO_DEF_MEM_DISPATCH(MemGet, get);

/// Facade declaring the MemPool<StateStorage> interface for proxy.
///
/// @tparam StateStorage  The state storage type used by seal/remove.
template <class StateStorage>
struct AnyMemPoolFacade
  : pro::facade_builder ::add_convention<MemAdd,
        void(std::vector<protocol::Transaction::Ptr>)>::add_convention<MemSeal,
        void(int64_t, StateStorage&,
            std::back_insert_iterator<
                std::vector<protocol::Transaction::Ptr>>)>::template add_convention<MemRemoveState,
        void(StateStorage&)>::template add_convention<MemRemoveHashes,
        void(std::vector<bcos::crypto::HashType>)>::template add_convention<MemGet,
        std::vector<protocol::Transaction::Ptr>(std::vector<bcos::crypto::HashType>)>::
        template support_relocation<pro::constraint_level::nothrow>::template support_destruction<
            pro::constraint_level::nothrow>::build
{
};

/// Type-erased owning wrapper around any type satisfying MemPool<StateStorage>.
///
/// This is a template alias for pro::proxy<AnyMemPoolFacade<StateStorage>>.
/// Users interact with the pool through operator->.  The two remove overloads
/// are named removeByState / removeByHashes, so no ambiguity arises.
///
/// Usage:
/// @code
///   auto pool = pro::make_proxy<AnyMemPoolFacade<StateStorage>, MemPoolImpl>(...);
///   pool->add(myTransactions);
///   pool->seal(limit, state, std::back_inserter(out));
///   auto txs = pool->get(myHashes);
///   pool->removeByState(state);
///   pool->removeByHashes(myHashes);
/// @endcode
template <class StateStorage>
using AnyMemPool = pro::proxy<AnyMemPoolFacade<StateStorage>>;

}  // namespace bcos::mempool
