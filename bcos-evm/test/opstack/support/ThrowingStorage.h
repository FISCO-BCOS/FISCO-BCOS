// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// ThrowingStorage — 异常注入装饰器(仅测试用)。包一层底层 storage2 存储,读路径算子
// (existsOne/readOne/range)一律抛异常,用于验证 Storage2Ledger 的毒旗错误通道
// (Storage2LedgerTest 组 (e)):syncWait 在调用点重抛 → 桥的 catch 全部异常 → 置位
// poisoned()/记录 firstError() → 读方法返回安全值(nullopt/空字节/零值),不崩溃、
// 不静默降级为"账户不存在"。
//
// 只覆盖 Storage2Ledger 读桥实际用到的算子;writeOne/removeOne 直通不抛,供测试
// fixture 在包装前经由底层存储正常写入种子数据。

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace bcos::evm::test
{

template <class Storage>
class ThrowingStorage
{
public:
    explicit ThrowingStorage(Storage& storage) noexcept : m_storage(storage) {}

    task::Task<std::optional<bcos::storage::Entry>> readOne(auto key, auto&&... args)
    {
        throw std::runtime_error("ThrowingStorage: injected read failure (readOne)");
        // 不可达;保留以满足协程返回类型推导。
        co_return co_await storage2::readOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<bool> existsOne(auto key, auto&&... args)
    {
        throw std::runtime_error("ThrowingStorage: injected read failure (existsOne)");
        co_return co_await storage2::existsOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<typename Storage::Iterator> range(auto&&... args)
    {
        throw std::runtime_error("ThrowingStorage: injected read failure (range)");
        co_return co_await storage2::range(m_storage.get(), std::forward<decltype(args)>(args)...);
    }

    task::Task<void> writeOne(auto key, auto value, auto&&... args)
    {
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...);
    }

    task::Task<void> removeOne(auto key, auto&&... args)
    {
        co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::evm::test
