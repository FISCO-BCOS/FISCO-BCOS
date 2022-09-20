#pragma once

#include "../Basic.h"
#include <bcos-framework/storage/Entry.h>
#include <bcos-utilities/Ranges.h>
#include <coroutine>
#include <type_traits>

namespace bcos::concepts::storage
{
template <class Value, class Executor>
class Task
{
public:
    class promise_type
    {
    public:
        Task get_return_object()
        {
            return std::coroutine_handle<promise_type>::from_promise(*this);
        }

        std::suspend_always initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }

        void unhandled_exception() { m_value = std::current_exception(); }

        void return_value(std::convertible_to<Value> auto&& value)
        {
            m_value.template emplace<Value>(
                std::forward<std::remove_cvref_t<decltype(value)>>(value));
        }

    private:
        std::variant<std::monostate, Value, std::exception_ptr> m_value;
    };

    class Awaitable
    {
    public:
        bool await_ready() const noexcept { return false; }

        template <class Promise>
        void await_suspend(std::coroutine_handle<Promise> handle) const noexcept
        {
            // Put handle into executor
        }

        Value await_resume() const noexcept { return std::move(m_handle.promise().m_value); }
    };

private:
};

template <class Impl>
class StorageBase2
{
public:
    using Coroutine = typename Impl::Coroutine;

    auto getRow(std::string_view table, std::string_view key)
    {
        return impl().impl_getRow(table, key);
    }

    std::vector<std::optional<bcos::storage::Entry>> getRows(
        std::string_view table, RANGES::range auto&& keys)
    {
        return impl().impl_getRows(table, std::forward<decltype(keys)>(keys));
    }

    void setRow(std::string_view table, std::string_view key, bcos::storage::Entry entry)
    {
        impl().impl_setRow(table, key, std::move(entry));
    }

    void createTable(std::string tableName) { impl().impl_createTable(std::move(tableName)); }

private:
    friend Impl;
    StorageBase2() = default;
    auto& impl() { return static_cast<Impl&>(*this); }
};

template <class Impl>
concept Storage2 = std::derived_from<Impl, StorageBase2<Impl>>;

}  // namespace bcos::concepts::storage