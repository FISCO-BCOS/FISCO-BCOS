#pragma once
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"

namespace bcos::ledger::account
{

inline constexpr struct Create
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} create{};

inline constexpr struct Code
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} code{};

inline constexpr struct SetCode
{
    task::Task<void> operator()(auto& account, auto&& code, auto&&... args) const
    {
        co_await tag_invoke(*this, account, std::forward<decltype(code)>(code),
            std::forward<decltype(args)>(args)...);
    }
} setCode{};

inline constexpr struct CodeHash
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} codeHash{};

inline constexpr struct ABI
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} abi{};

inline constexpr struct SetABI
{
    task::Task<void> operator()(auto& account, auto&& code, auto&&... args) const
    {
        co_await tag_invoke(*this, account, std::forward<decltype(code)>(code),
            std::forward<decltype(args)>(args)...);
    }
} setABI{};

inline constexpr struct Balance
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} balance{};

inline constexpr struct SetBalance
{
    task::Task<void> operator()(auto& account, auto&& balance, auto&&... args) const
    {
        co_await tag_invoke(*this, account, std::forward<decltype(balance)>(balance),
            std::forward<decltype(args)>(args)...);
    }
} setBalance{};

inline constexpr struct Storage
{
    auto operator()(auto& account, auto&& key, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, account,
            std::forward<decltype(key)>(key), std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(key)>(key),
            std::forward<decltype(args)>(args)...);
    }
} storage{};

inline constexpr struct SetStorage
{
    auto operator()(auto& account, auto&& key, auto&& value, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, account,
            std::forward<decltype(key)>(key), std::forward<decltype(value)>(value),
            std::forward<decltype(args)>(args)...))>>
    {
        co_await tag_invoke(*this, account, std::forward<decltype(key)>(key),
            std::forward<decltype(value)>(value), std::forward<decltype(args)>(args)...);
    }
} setStorage{};

inline constexpr struct Path
{
    auto operator()(auto& account, auto&&... args) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, account, std::forward<decltype(args)>(args)...))>>
    {
        co_return co_await tag_invoke(*this, account, std::forward<decltype(args)>(args)...);
    }
} path{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;

}  // namespace bcos::ledger::account