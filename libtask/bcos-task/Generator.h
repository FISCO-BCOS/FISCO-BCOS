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
 */

#pragma once

#include <version>

#if defined(__cpp_lib_generator) && __cpp_lib_generator >= 202207L
#include <generator>
#include <ranges>
namespace bcos::task
{
template <typename Ref, typename Val = void, typename Alloc = void>
using Generator = std::generator<Ref, Val, Alloc>;
using std::ranges::elements_of;
}  // namespace bcos::task
#else
#include <concepts>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>
namespace bcos::task
{
template <std::ranges::range Range, typename Alloc = std::allocator<std::byte> >
struct elements_of
{
    [[no_unique_address]] Range range;
    [[no_unique_address]] Alloc allocator = Alloc();
};

template <typename Range, typename Alloc = std::allocator<std::byte> >
elements_of(Range&&, Alloc = Alloc()) -> elements_of<Range&&, Alloc>;

namespace detail
{
template <typename Yielded>
struct GeneratorState
{
    std::vector<std::coroutine_handle<> > coroutines;
    std::add_pointer_t<Yielded> value = nullptr;
};

class GeneratorAccess;

template <typename Allocator>
class GeneratorAllocator
{
    struct Block
    {
        alignas(
            __STDCPP_DEFAULT_NEW_ALIGNMENT__) std::byte storage[__STDCPP_DEFAULT_NEW_ALIGNMENT__];
    };

    using Deallocator = void (*)(void*, std::size_t) noexcept;

    static std::size_t blockCount(std::size_t size) noexcept
    {
        return (size + sizeof(Block) - 1) / sizeof(Block);
    }

    static std::uintptr_t alignUp(std::uintptr_t address, std::size_t alignment) noexcept
    {
        return (address + alignment - 1) & ~(alignment - 1);
    }

    static Deallocator* deallocatorAddress(std::uintptr_t address, std::size_t frameSize) noexcept
    {
        return reinterpret_cast<Deallocator*>(alignUp(address + frameSize, alignof(Deallocator)));
    }

    template <typename Alloc>
    static Alloc* allocatorAddress(std::uintptr_t address, std::size_t frameSize) noexcept
    {
        auto afterDeallocator =
            reinterpret_cast<std::uintptr_t>(deallocatorAddress(address, frameSize)) +
            sizeof(Deallocator);
        return reinterpret_cast<Alloc*>(alignUp(afterDeallocator, alignof(Alloc)));
    }

    template <typename Alloc>
    static std::size_t allocationSize(std::size_t frameSize) noexcept
    {
        return frameSize + alignof(Deallocator) - 1 + sizeof(Deallocator) + alignof(Alloc) - 1 +
               sizeof(Alloc);
    }

    template <typename Alloc>
    static void deallocate(void* pointer, std::size_t frameSize) noexcept
    {
        using Traits = std::allocator_traits<Alloc>;
        auto address = reinterpret_cast<std::uintptr_t>(pointer);
        auto* storedAllocator = allocatorAddress<Alloc>(address, frameSize);
        Alloc allocator(std::move(*storedAllocator));
        storedAllocator->~Alloc();
        Traits::deallocate(allocator, static_cast<Block*>(static_cast<void*>(pointer)),
            blockCount(allocationSize<Alloc>(frameSize)));
    }

    template <typename Alloc>
    static void* allocate(const Alloc& allocator, std::size_t frameSize)
    {
        using Rebound = typename std::allocator_traits<Alloc>::template rebind_alloc<Block>;
        using Traits = std::allocator_traits<Rebound>;
        static_assert(std::is_pointer_v<typename Traits::pointer>,
            "Generator allocators must use raw pointers");

        Rebound rebound(allocator);
        auto count = blockCount(allocationSize<Rebound>(frameSize));
        auto* storage = Traits::allocate(rebound, count);
        auto address = reinterpret_cast<std::uintptr_t>(storage);
        *deallocatorAddress(address, frameSize) = &deallocate<Rebound>;
        try
        {
            ::new (static_cast<void*>(allocatorAddress<Rebound>(address, frameSize)))
                Rebound(std::move(rebound));
        }
        catch (...)
        {
            Traits::deallocate(rebound, storage, count);
            throw;
        }
        return storage;
    }

public:
    void* operator new(std::size_t size)
        requires(std::is_void_v<Allocator> || std::default_initializable<Allocator>)
    {
        using Alloc =
            std::conditional_t<std::is_void_v<Allocator>, std::allocator<std::byte>, Allocator>;
        return allocate(Alloc{}, size);
    }

    template <typename Alloc, typename... Args>
    void* operator new(std::size_t size, std::allocator_arg_t, const Alloc& alloc, const Args&...)
        requires(std::is_void_v<Allocator> || std::convertible_to<const Alloc&, Allocator>)
    {
        using Selected = std::conditional_t<std::is_void_v<Allocator>, Alloc, Allocator>;
        return allocate(Selected(alloc), size);
    }

    template <typename This, typename Alloc, typename... Args>
    void* operator new(
        std::size_t size, const This&, std::allocator_arg_t, const Alloc& alloc, const Args&...)
        requires(std::is_void_v<Allocator> || std::convertible_to<const Alloc&, Allocator>)
    {
        using Selected = std::conditional_t<std::is_void_v<Allocator>, Alloc, Allocator>;
        return allocate(Selected(alloc), size);
    }

    void operator delete(void* pointer, std::size_t size) noexcept
    {
        auto address = reinterpret_cast<std::uintptr_t>(pointer);
        (*deallocatorAddress(address, size))(pointer, size);
    }
};
}  // namespace detail

template <typename Ref, typename Value = void, typename Allocator = void>
class Generator : public std::ranges::view_interface<Generator<Ref, Value, Allocator> >
{
    using ValueType = std::conditional_t<std::is_void_v<Value>, std::remove_cvref_t<Ref>, Value>;
    using Reference = std::conditional_t<std::is_void_v<Value>, Ref&&, Ref>;
    using RRef = std::conditional_t<std::is_reference_v<Reference>,
        std::remove_reference_t<Reference>&&, Reference>;

    static_assert(
        std::is_object_v<ValueType> && std::same_as<ValueType, std::remove_cv_t<ValueType> >,
        "Generator value must be a cv-unqualified object type");
    static_assert(
        std::is_reference_v<Reference> ||
            (std::is_object_v<Reference> && std::same_as<Reference, std::remove_cv_t<Reference> > &&
                std::copy_constructible<Reference>),
        "Generator reference must be a reference or a copyable cv-unqualified object type");
    static_assert(std::common_reference_with<Reference&&, ValueType&>);
    static_assert(std::common_reference_with<Reference&&, RRef&&>);
    static_assert(std::common_reference_with<RRef&&, const ValueType&>);

    class Iterator;

public:
    using yielded = std::conditional_t<std::is_reference_v<Reference>, Reference, const Reference&>;

    class promise_type : public detail::GeneratorAllocator<Allocator>
    {
        using State = detail::GeneratorState<yielded>;

    public:
        Generator get_return_object() noexcept
        {
            return Generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() const noexcept { return {}; }

        struct final_awaiter
        {
            bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(
                std::coroutine_handle<promise_type> handle) const noexcept
            {
                auto& coroutines = handle.promise().m_state->coroutines;
                coroutines.pop_back();
                if (coroutines.empty())
                {
                    return std::noop_coroutine();
                }
                return coroutines.back();
            }

            void await_resume() const noexcept {}
        };

        final_awaiter final_suspend() const noexcept { return {}; }

        std::suspend_always yield_value(yielded value) noexcept
        {
            m_state->value = std::addressof(value);
            return {};
        }

        struct copy_awaiter
        {
            std::remove_cvref_t<yielded> m_value;
            State& m_state;

            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type>) noexcept
            {
                m_state.value = std::addressof(m_value);
            }
            void await_resume() const noexcept {}
        };

        auto yield_value(const std::remove_reference_t<yielded>& value) noexcept(
            std::is_nothrow_constructible_v<std::remove_cvref_t<yielded>,
                const std::remove_reference_t<yielded>&>)
            requires(std::is_rvalue_reference_v<yielded> &&
                     std::constructible_from<std::remove_cvref_t<yielded>,
                         const std::remove_reference_t<yielded>&>)
        {
            return copy_awaiter{std::remove_cvref_t<yielded>(value), *m_state};
        }

        template <typename R2, typename V2, typename A2, typename Unused>
            requires std::same_as<typename Generator<R2, V2, A2>::yielded, yielded>
        auto yield_value(elements_of<Generator<R2, V2, A2>&&, Unused> element) noexcept
        {
            return yield_sequence_awaiter<Generator<R2, V2, A2> >{std::move(element.range)};
        }

        template <typename R2, typename V2, typename A2, typename Unused>
            requires std::same_as<typename Generator<R2, V2, A2>::yielded, yielded>
        auto yield_value(elements_of<Generator<R2, V2, A2>&, Unused> element) noexcept
        {
            return yield_sequence_awaiter<Generator<R2, V2, A2> >{std::move(element.range)};
        }

        template <std::ranges::input_range Range, typename Alloc>
            requires std::convertible_to<std::ranges::range_reference_t<Range>, yielded>
        auto yield_value(elements_of<Range, Alloc> element)
        {
            auto nested =
                [](std::allocator_arg_t, Alloc, std::ranges::iterator_t<Range> iterator,
                    std::ranges::sentinel_t<Range> sentinel) -> Generator<yielded, void, Alloc> {
                for (; iterator != sentinel; ++iterator)
                {
                    co_yield static_cast<yielded>(*iterator);
                }
            };
            return yield_value(elements_of(nested(std::allocator_arg, element.allocator,
                std::ranges::begin(element.range), std::ranges::end(element.range))));
        }

        void return_void() const noexcept {}

        void unhandled_exception()
        {
            if (m_state->coroutines.size() == 1)
            {
                throw;
            }
            m_exception = std::current_exception();
        }

        void await_transform() = delete;

    private:
        friend Generator;
        friend Iterator;
        friend detail::GeneratorAccess;

        template <typename>
        struct yield_sequence_awaiter;

        State* m_state = nullptr;
        std::exception_ptr m_exception;
    };

    Generator(const Generator&) = delete;
    Generator(Generator&& other) noexcept
      : m_coroutine(std::exchange(other.m_coroutine, {})),
        m_active(std::exchange(other.m_active, nullptr))
    {}

    ~Generator()
    {
        if (m_coroutine)
        {
            m_coroutine.destroy();
        }
    }

    Generator& operator=(Generator other) noexcept
    {
        std::swap(m_coroutine, other.m_coroutine);
        std::swap(m_active, other.m_active);
        return *this;
    }

    Iterator begin()
    {
        auto& state = *m_active;
        m_coroutine.promise().m_state = &state;
        state.coroutines.push_back(m_coroutine);
        m_coroutine.resume();
        return Iterator{m_coroutine};
    }

    std::default_sentinel_t end() const noexcept { return std::default_sentinel; }

private:
    template <typename, typename, typename>
    friend class Generator;
    friend detail::GeneratorAccess;

    explicit Generator(std::coroutine_handle<promise_type> coroutine) noexcept
      : m_coroutine(coroutine), m_active(new detail::GeneratorState<yielded>())
    {}

    std::coroutine_handle<promise_type> m_coroutine;
    std::unique_ptr<detail::GeneratorState<yielded> > m_active;
};

template <typename Ref, typename Value, typename Allocator>
class Generator<Ref, Value, Allocator>::Iterator
{
public:
    using value_type = ValueType;
    using difference_type = std::ptrdiff_t;

    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator(Iterator&& other) noexcept : m_coroutine(std::exchange(other.m_coroutine, {})) {}

    Iterator& operator=(Iterator&& other) noexcept
    {
        m_coroutine = std::exchange(other.m_coroutine, {});
        return *this;
    }

    Reference operator*() const noexcept(std::is_nothrow_copy_constructible_v<Reference>)
    {
        return static_cast<Reference>(*m_coroutine.promise().m_state->value);
    }

    Iterator& operator++()
    {
        m_coroutine.promise().m_state->coroutines.back().resume();
        return *this;
    }

    void operator++(int) { ++*this; }

    friend bool operator==(const Iterator& iterator, std::default_sentinel_t) noexcept
    {
        return iterator.m_coroutine.done();
    }

private:
    friend Generator;

    explicit Iterator(std::coroutine_handle<promise_type> coroutine) noexcept
      : m_coroutine(coroutine)
    {}

    std::coroutine_handle<promise_type> m_coroutine;
};

namespace detail
{
class GeneratorAccess
{
public:
    template <typename Ref, typename Value, typename Allocator, typename Yielded>
    static std::coroutine_handle<> start(
        Generator<Ref, Value, Allocator>& generator, GeneratorState<Yielded>& state) noexcept
    {
        auto coroutine = generator.m_coroutine;
        coroutine.promise().m_state = &state;
        state.coroutines.push_back(coroutine);
        return coroutine;
    }

    template <typename Ref, typename Value, typename Allocator>
    static void rethrow(Generator<Ref, Value, Allocator>& generator)
    {
        if (generator.m_coroutine.promise().m_exception)
        {
            std::rethrow_exception(generator.m_coroutine.promise().m_exception);
        }
    }
};
}  // namespace detail

template <typename Ref, typename Value, typename Allocator>
template <typename NestedGenerator>
struct Generator<Ref, Value, Allocator>::promise_type::yield_sequence_awaiter
{
    NestedGenerator m_generator;

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) noexcept
    {
        return detail::GeneratorAccess::start(m_generator, *handle.promise().m_state);
    }

    void await_resume() { detail::GeneratorAccess::rethrow(m_generator); }
};
}  // namespace bcos::task
#endif
