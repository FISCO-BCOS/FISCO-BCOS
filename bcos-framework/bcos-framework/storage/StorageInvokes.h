#pragma once

#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"

namespace bcos::storage
{

inline task::Task<std::optional<Entry>> tag_invoke(storage2::tag_t<storage2::readOne> /*unused*/,
    StorageInterface& storage, std::tuple<std::string_view, std::string_view> stateKey)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        std::variant<std::monostate, std::optional<Entry>, std::exception_ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
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

    auto [table, key] = stateKey;
    auto value =
        co_await Awaitable{.m_storage = storage, .m_table = table, .m_key = key, .m_result = {}};
    co_return value;
}

inline task::Task<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
    StorageInterface& storage, std::tuple<std::string_view, std::string_view> stateKey, Entry entry)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        Entry m_entry;
        std::variant<std::monostate, std::exception_ptr> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
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

    auto [table, key] = stateKey;
    co_await Awaitable{.m_storage = storage,
        .m_table = table,
        .m_key = key,
        .m_entry = std::move(entry),
        .m_result = {}};
}

}  // namespace bcos::storage