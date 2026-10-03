#pragma once

#include "Entry.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <atomic>
#include <range/v3/range/concepts.hpp>
#include <range/v3/range/primitives.hpp>

namespace bcos::storage
{

namespace detail
{
inline executor_v1::StateKeyView toStateKeyView(auto&& stateKey)
{
    if constexpr (requires { stateKey.get(); })
    {
        return stateKey;
    }
    else
    {
        return executor_v1::StateKeyView{stateKey};
    }
}

// Legacy storages (RocksDBStorage) invoke the completion callback INLINE, before
// the async initiation returns. If the callback then resumes the awaiting
// coroutine on the spot, the resume nests inside the initiating frame — and a
// loop of N such writes/reads (genesis import of thousands of alloc accounts)
// stacks N nested resumes until the thread stack overflows. The two-phase state
// below makes await_suspend DECLINE to suspend when the completion already
// landed inline: the initiating frames unwind first and the coroutine continues
// at await_resume with no stack growth. A genuinely asynchronous completion
// (another thread) still suspends and is resumed by the callback — exactly one
// of the two paths runs, ordered by the two exchanges.
class InlineResumeGuard
{
public:
    // Called by the completion callback after storing the result. Returns true
    // when the caller must resume m_handle (the coroutine really suspended).
    bool completionLanded() { return m_state.exchange(kDone) == kSuspended; }
    // Called at the end of await_suspend. Returns true when the coroutine
    // should actually suspend (completion has not landed yet).
    bool shouldSuspend() { return m_state.exchange(kSuspended) != kDone; }

private:
    static constexpr int kInitiating = 0;
    static constexpr int kSuspended = 1;
    static constexpr int kDone = 2;
    std::atomic<int> m_state{kInitiating};
};
}  // namespace detail

inline task::Task<std::optional<Entry>> tag_invoke(
    storage2::tag_t<storage2::readOne> /*unused*/, StorageInterface& storage, auto stateKey)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        std::variant<std::monostate, std::optional<Entry>, std::exception_ptr> m_result;
        std::coroutine_handle<> m_handle{};
        detail::InlineResumeGuard m_guard{};

        constexpr static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            m_handle = handle;
            m_storage.asyncGetRow(
                m_table, m_key, [this](Error::UniquePtr error, std::optional<Entry> entry) mutable {
                    if (error)
                    {
                        m_result.emplace<std::exception_ptr>(std::make_exception_ptr(*error));
                    }
                    else
                    {
                        m_result.emplace<std::optional<Entry>>(std::move(entry));
                    }
                    if (m_guard.completionLanded())
                    {
                        m_handle.resume();
                    }
                });
            return m_guard.shouldSuspend();
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

    auto keyView = detail::toStateKeyView(std::forward<decltype(stateKey)>(stateKey));
    auto [table, key] = keyView.get();
    Awaitable awaitable{.m_storage = storage, .m_table = table, .m_key = key, .m_result = {}};
    co_return co_await awaitable;
}

task::Task<std::vector<std::optional<Entry>>> tag_invoke(
    storage2::tag_t<storage2::readSome> /*unused*/, StorageInterface& storage,
    ::ranges::input_range auto keys)
{
    std::vector<std::optional<Entry>> values;
    if constexpr (::ranges::sized_range<decltype(keys)>)
    {
        values.reserve(decltype(values)::size_type(::ranges::size(keys)));
    }
    for (auto&& key : keys)
    {
        values.emplace_back(co_await storage2::readOne(storage, std::forward<decltype(key)>(key)));
    }

    co_return values;
}

inline task::Task<void> tag_invoke(storage2::tag_t<storage2::writeSome> /*unused*/,
    StorageInterface& storage, ::ranges::input_range auto keyValues)
{
    for (auto&& [key, value] : keyValues)
    {
        co_await storage2::writeOne(
            storage, std::forward<decltype(key)>(key), std::forward<decltype(value)>(value));
    }
}

inline task::Task<void> tag_invoke(storage2::tag_t<storage2::writeOne> /*unused*/,
    StorageInterface& storage, auto stateKey, Entry entry)
{
    struct Awaitable
    {
        StorageInterface& m_storage;
        std::string_view m_table;
        std::string_view m_key;
        Entry m_entry;
        std::variant<std::monostate, std::exception_ptr> m_result;
        std::coroutine_handle<> m_handle{};
        detail::InlineResumeGuard m_guard{};

        constexpr static bool await_ready() noexcept { return false; }
        bool await_suspend(std::coroutine_handle<> handle)
        {
            m_handle = handle;
            m_storage.asyncSetRow(
                m_table, m_key, std::move(m_entry), [this](Error::UniquePtr error) mutable {
                    if (error)
                    {
                        m_result.emplace<std::exception_ptr>(std::make_exception_ptr(*error));
                    }
                    if (m_guard.completionLanded())
                    {
                        m_handle.resume();
                    }
                });
            return m_guard.shouldSuspend();
        }
        void await_resume()
        {
            if (std::holds_alternative<std::exception_ptr>(m_result))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(m_result));
            }
        }
    };

    auto keyView = detail::toStateKeyView(std::forward<decltype(stateKey)>(stateKey));
    auto [table, key] = keyView.get();
    Awaitable awaitable{.m_storage = storage,
        .m_table = table,
        .m_key = key,
        .m_entry = std::move(entry),
        .m_result = {}};
    co_await awaitable;
}

}  // namespace bcos::storage
