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
 * @brief Type-erased wrapper around any type satisfying MemPool concept
 */

#pragma once

#include "bcos-framework/mempool/MemPool.h"
#include "bcos-framework/protocol/Transaction.h"
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <type_traits>
#include <vector>

namespace bcos::mempool
{

/// Type-erased wrapper around any type satisfying MemPool<StateStorage>.
///
/// Uses the "PIMPL with virtual Concept/Model" pattern to erase the concrete
/// MemPool implementation type while still providing the full MemPool API.
/// The stored implementation must satisfy MemPool<T, StateStorage> and is
/// held via reference_wrapper — the caller is responsible for ensuring the
/// referenced object outlives this wrapper.
///
/// @tparam StateStorage  The state storage type used by seal/remove.
///
/// Usage:
/// @code
///   MemPoolImpl impl;
///   AnyMemPool<MapStateStorage> any(impl);
///   any.add(myTransactions);
///   any.seal(100, state, std::back_inserter(output));
/// @endcode
template <class StateStorage>
class AnyMemPool
{
private:
    /// Virtual interface for type-erased mempool operations.
    struct Concept
    {
        virtual ~Concept() = default;
        virtual void add(std::vector<protocol::Transaction::Ptr> transactions) = 0;
        virtual void seal(int64_t limit, StateStorage& state,
            std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out) = 0;
        virtual void remove(StateStorage& state) = 0;
        virtual void remove(std::vector<bcos::crypto::HashType> hashes) = 0;
        virtual std::vector<protocol::Transaction::Ptr> get(
            std::vector<bcos::crypto::HashType> hashes) = 0;
    };

    /// Concrete model wrapping a type-erased MemPool implementation.
    /// Uses reference_wrapper — the caller must ensure the referenced
    /// object outlives this Model.
    template <class T>
        requires MemPool<T, StateStorage>
    struct Model final : Concept
    {
        std::reference_wrapper<T> m_mempool;

        explicit Model(T& mempool) : m_mempool(mempool) {}

        void add(std::vector<protocol::Transaction::Ptr> transactions) override
        {
            m_mempool.get().add(std::move(transactions));
        }

        void seal(int64_t limit, StateStorage& state,
            std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out) override
        {
            m_mempool.get().seal(limit, state, out);
        }

        void remove(StateStorage& state) override { m_mempool.get().remove(state); }

        void remove(std::vector<bcos::crypto::HashType> hashes) override
        {
            m_mempool.get().remove(std::move(hashes));
        }

        std::vector<protocol::Transaction::Ptr> get(
            std::vector<bcos::crypto::HashType> hashes) override
        {
            return m_mempool.get().get(std::move(hashes));
        }
    };

    std::unique_ptr<Concept> m_impl;

public:
    AnyMemPool() = delete;
    ~AnyMemPool() = default;

    AnyMemPool(const AnyMemPool&) = delete;
    AnyMemPool& operator=(const AnyMemPool&) = delete;

    [[nodiscard]] AnyMemPool(AnyMemPool&& other) noexcept : m_impl(std::move(other.m_impl)) {}
    [[nodiscard]] AnyMemPool& operator=(AnyMemPool&& other) noexcept
    {
        if (this != &other)
        {
            m_impl = std::move(other.m_impl);
        }
        return *this;
    }

    /// Construct from a reference to any type satisfying MemPool<T, StateStorage>.
    /// The referenced object must outlive this AnyMemPool.
    template <class T>
        requires MemPool<std::remove_cvref_t<T>, StateStorage> &&
                 (!std::same_as<std::remove_cvref_t<T>, AnyMemPool>)
    explicit AnyMemPool(T& mempool)
      : m_impl(std::make_unique<Model<std::remove_cvref_t<T>>>(mempool))
    {}

    /// Returns true if this wrapper holds a mempool implementation.
    [[nodiscard]] explicit operator bool() const noexcept { return m_impl != nullptr; }

    void add(std::vector<protocol::Transaction::Ptr> transactions)
    {
        assert(m_impl && "AnyMemPool must be initialized before use");
        m_impl->add(std::move(transactions));
    }

    void seal(int64_t limit, StateStorage& state,
        std::back_insert_iterator<std::vector<protocol::Transaction::Ptr>> out)
    {
        assert(m_impl && "AnyMemPool must be initialized before use");
        m_impl->seal(limit, state, out);
    }

    void remove(StateStorage& state)
    {
        assert(m_impl && "AnyMemPool must be initialized before use");
        m_impl->remove(state);
    }

    void remove(std::vector<bcos::crypto::HashType> hashes)
    {
        assert(m_impl && "AnyMemPool must be initialized before use");
        m_impl->remove(std::move(hashes));
    }

    std::vector<protocol::Transaction::Ptr> get(std::vector<bcos::crypto::HashType> hashes)
    {
        assert(m_impl && "AnyMemPool must be initialized before use");
        return m_impl->get(std::move(hashes));
    }
};

/// AnyMemPool itself satisfies the MemPool concept (reflexive check).
/// This is verified at compile-time when AnyMemPool is instantiated with a
/// concrete StateStorage type, e.g.:
///   static_assert(MemPool<AnyMemPool<MyStorage>, MyStorage>);

}  // namespace bcos::mempool
