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
#include "bcos-framework/protocol/Transaction.h"
#include <proxy/proxy.h>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <type_traits>
#include <vector>

namespace bcos::mempool
{

/// Dispatch structs — each maps to a member function of the underlying
/// MemPool implementation.  Two dispatch structs both target ::remove so
/// that overload resolution can distinguish remove(StateStorage&) from
/// remove(vector<HashType>).
PRO_DEF_MEM_DISPATCH(MemAdd, add);
PRO_DEF_MEM_DISPATCH(MemSeal, seal);
PRO_DEF_MEM_DISPATCH(MemRemoveState, remove);
PRO_DEF_MEM_DISPATCH(MemRemoveHashes, remove);
PRO_DEF_MEM_DISPATCH(MemGet, get);

/// Facade declaring the MemPool<StateStorage> interface for proxy.
///
/// @tparam StateStorage  The state storage type used by seal/remove.
template <class StateStorage>
struct AnyMemPoolFacade
  : pro::facade_builder ::add_convention<MemAdd,
        void(std::vector<protocol::Transaction::Ptr>)>::add_convention<MemSeal,
        void(int64_t, StateStorage&,
            std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>>)>::
        template add_convention<MemRemoveState,
            void(StateStorage&)>::template add_convention<MemRemoveHashes,
            void(std::vector<bcos::crypto::HashType>)>::template add_convention<MemGet,
            std::vector<protocol::Transaction::Ptr>(std::vector<bcos::crypto::HashType>)>::
            template support_relocation<
                pro::constraint_level::nothrow>::
                template support_destruction<pro::constraint_level::nothrow>::build
{
};

/// Type-erased owning wrapper around any type satisfying MemPool<StateStorage>.
///
/// Backed by pro::proxy<AnyMemPoolFacade<StateStorage>>.  This thin wrapper
/// adapts proxy's operator-> dispatch style to the direct-call style required
/// by the MemPool concept.
///
/// Usage (for non-movable types, using in-place construction):
/// @code
///   AnyMemPool<StateStorage> any(std::in_place_type<MemPoolImpl>, ...);
///   any.add(myTransactions);
/// @endcode
///
/// Note: AnyMemPool is move-only (copy is deleted) because the facade does
/// not require copyability from stored types — this allows wrapping
/// non-copyable implementations like MemPoolImpl (which contains std::mutex).
template <class StateStorage>
class AnyMemPool
{
public:
    using ProxyType = pro::proxy<AnyMemPoolFacade<StateStorage>>;
    using OutIter = std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>>;
    using TxVec = std::vector<protocol::Transaction::Ptr>;
    using HashVec = std::vector<bcos::crypto::HashType>;

    AnyMemPool() = default;
    ~AnyMemPool() = default;

    AnyMemPool(const AnyMemPool&) = delete;
    AnyMemPool& operator=(const AnyMemPool&) = delete;

    AnyMemPool(AnyMemPool&&) noexcept = default;
    AnyMemPool& operator=(AnyMemPool&&) noexcept = default;

    /// Construct from any type satisfying MemPool<T, StateStorage>.
    /// The value is copied/moved into the proxy (owning semantics).
    template <class T>
        requires MemPool<std::remove_cvref_t<T>, StateStorage> &&
                 (!std::same_as<std::remove_cvref_t<T>, AnyMemPool>)
    explicit AnyMemPool(T&& mempool)
      : m_impl(pro::make_proxy<AnyMemPoolFacade<StateStorage>, std::remove_cvref_t<T>>(
            std::forward<T>(mempool)))
    {}

    /// Construct in-place from constructor arguments (for non-movable types).
    template <class T, class... Args>
        requires MemPool<T, StateStorage> &&
                 std::is_constructible_v<T, Args...>
    explicit AnyMemPool(std::in_place_type_t<T>, Args&&... args)
      : m_impl(pro::make_proxy<AnyMemPoolFacade<StateStorage>, T>(std::forward<Args>(args)...))
    {}

    /// Returns true if this wrapper holds a mempool implementation.
    [[nodiscard]] explicit operator bool() const noexcept { return m_impl.has_value(); }

    void add(TxVec transactions)
    {
        assert(m_impl.has_value() && "AnyMemPool must be initialized before use");
        m_impl->add(std::move(transactions));
    }
    void seal(int64_t limit, StateStorage& state, OutIter out)
    {
        assert(m_impl.has_value() && "AnyMemPool must be initialized before use");
        m_impl->seal(limit, state, out);
    }
    void remove(StateStorage& state)
    {
        assert(m_impl.has_value() && "AnyMemPool must be initialized before use");
        pro::proxy_invoke<false, MemRemoveState, void(StateStorage&)>(m_impl, state);
    }
    void remove(HashVec hashes)
    {
        assert(m_impl.has_value() && "AnyMemPool must be initialized before use");
        pro::proxy_invoke<false, MemRemoveHashes, void(HashVec)>(m_impl, std::move(hashes));
    }
    TxVec get(HashVec hashes)
    {
        assert(m_impl.has_value() && "AnyMemPool must be initialized before use");
        return m_impl->get(std::move(hashes));
    }

    /// Access the underlying proxy for advanced operations (e.g. proxy_cast).
    [[nodiscard]] ProxyType& proxy() noexcept { return m_impl; }
    [[nodiscard]] const ProxyType& proxy() const noexcept { return m_impl; }

private:
    ProxyType m_impl;
};

}  // namespace bcos::mempool
