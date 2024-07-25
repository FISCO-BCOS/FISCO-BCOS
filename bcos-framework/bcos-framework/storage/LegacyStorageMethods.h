#pragma once

#include "Entry.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"

namespace bcos::storage
{

inline task::Task<std::optional<Entry>> tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
    StorageInterface& storage, transaction_executor::StateKeyView stateKey)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        std::variant<std::monostate, std::optional<Entry>, std::exception_ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_storage.asyncGetRow(m_table, m_key,
                [this, handle](Error::UniquePtr error, std::optional<Entry> entry) mutable {
                    if (error)
                    {
                        m_result.emplace<std::exception_ptr>(std::make_exception_ptr(*error));
                    }
                    else
                    {
                        m_result.emplace<std::optional<Entry>>(std::move(entry));
                    }
                    handle.resume();
                });
        }
        std::optional<Entry> await_resume()
        {
            if (std::holds_alternative<std::exception_ptr>(m_result))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(m_result));
            }
            return std::move(std::get<std::optional<Entry>>(m_result));
        }
    };

    auto [table, key] = stateKey.getTableAndKey();
    Awaitable awaitable{.m_storage = storage, .m_table = table, .m_key = key, .m_result = {}};
    co_return co_await awaitable;
}

inline task::Task<std::vector<std::optional<Entry>>> tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, StorageInterface& storage,
    RANGES::input_range auto&& keys)
{
    // 这里调用StorageInterface的asyncGetRows接口效率更高,但是keys可能包含不同的table,aysncGetRows每次只能传一个table,因此留给未来优化
    // The asyncGetRows interface of StorageInterface is more efficient, but keys may contain
    // different tables, and aysncGetRows can only be used by one table at a time, so it is left to
    // future optimization
    std::vector<std::optional<Entry>> values;
    if constexpr (RANGES::sized_range<decltype(keys)>)
    {
        values.reserve(decltype(values)::size_type(RANGES::size(keys)));
    }
    for (auto&& key : keys)
    {
        values.emplace_back(co_await storage2::readOne(storage, std::forward<decltype(key)>(key)));
    }

    co_return values;
}

inline task::Task<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
    StorageInterface& storage, RANGES::input_range auto&& keys, RANGES::input_range auto&& values)
    requires std::is_same_v<RANGES::range_value_t<decltype(keys)>,
                 transaction_executor::StateKey> &&
             std::is_same_v<RANGES::range_value_t<decltype(values)>, Entry>
{
    for (auto&& [key, value] : RANGES::views::zip(
             std::forward<decltype(keys)>(keys), std::forward<decltype(values)>(values)))
    {
        co_await storage2::writeOne(
            storage, std::forward<decltype(key)>(key), std::forward<decltype(value)>(value));
    }
}


inline task::Task<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
    StorageInterface& storage, transaction_executor::StateKey stateKey, Entry entry)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        Entry m_entry;
        std::variant<std::monostate, std::exception_ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_storage.asyncSetRow(
                m_table, m_key, std::move(m_entry), [this, handle](Error::UniquePtr error) mutable {
                    if (error)
                    {
                        m_result.emplace<std::exception_ptr>(std::make_exception_ptr(*error));
                    }
                    handle.resume();
                });
        }
        void await_resume()
        {
            if (std::holds_alternative<std::exception_ptr>(m_result))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(m_result));
            }
        }
    };

    auto view = transaction_executor::StateKeyView(stateKey);
    auto [table, key] = view.getTableAndKey();
    Awaitable awaitable{.m_storage = storage,
        .m_table = table,
        .m_key = key,
        .m_entry = std::move(entry),
        .m_result = {}};
    co_await awaitable;
}

}  // namespace bcos::storage
