// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// CountingStorage — 读写计数装饰器(仅测试用)。包一层底层 storage2 存储,统计
// Storage2Ledger 读桥实际发出的 existsOne/readOne/range 调用次数,用作负缓存
// (Storage2LedgerTest 组 (g))与接线完整性探针(Task 6/7)的机器判据:
// "第二次读同一地址,storage 调用计数零增量"。writeCount 统计 writeOne/removeOne
// 调用次数,同为接线完整性探针(Task 6 E-b gate)的机器判据来源
// ("storage2_writes" RecordProperty)。
//
// 只转发 Storage2Ledger 读桥与 EVMAccount 写路径实际用到的算子(existsOne/readOne/
// range/writeOne/removeOne);未覆盖的算子按需追加,不做穷尽泛化。

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

namespace bcos::evm::test
{

template <class Storage>
class CountingStorage
{
public:
    explicit CountingStorage(Storage& storage) noexcept : m_storage(storage) {}

    /// existsOne/readOne/range 累计调用次数(桥读路径关心的算子)。
    std::size_t readCount{0};
    /// writeOne/removeOne 累计调用次数(桥写路径关心的算子)。
    std::size_t writeCount{0};

    task::Task<std::optional<bcos::storage::Entry>> readOne(auto key, auto&&... args)
    {
        ++readCount;
        co_return co_await storage2::readOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<bool> existsOne(auto key, auto&&... args)
    {
        ++readCount;
        co_return co_await storage2::existsOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<typename Storage::Iterator> range(auto&&... args)
    {
        ++readCount;
        co_return co_await storage2::range(m_storage.get(), std::forward<decltype(args)>(args)...);
    }

    task::Task<void> writeOne(auto key, auto value, auto&&... args)
    {
        ++writeCount;
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...);
    }

    task::Task<void> removeOne(auto key, auto&&... args)
    {
        ++writeCount;
        co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::evm::test
