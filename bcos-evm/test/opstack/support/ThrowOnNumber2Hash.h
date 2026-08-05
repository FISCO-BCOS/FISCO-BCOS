// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// ThrowOnNumber2Hash — 选择性异常注入装饰器(仅测试用)。只对 SYS_NUMBER_2_HASH 表的
// readOne 抛错, 其余表直通 —— 用于验证 RecentBlockHashes 的 hashErr 毒旗通道在
// bridge 未 poison 时独立置位 (G2 分类阶梯: storage 故障 -> -32603 非 INVALID)。
// 不能用 ThrowingStorage (它全表抛, bridge 的账户读会先 poison)。

#include <bcos-framework/ledger/LedgerTypeDef.h>
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
class ThrowOnNumber2Hash
{
public:
    explicit ThrowOnNumber2Hash(Storage& storage) noexcept : m_storage(storage) {}

    task::Task<std::optional<bcos::storage::Entry>> readOne(auto key, auto&&... args)
    {
        // key 是 StateKeyView (RecentBlockHashes 恒传 StateKeyView); 其表名成员是 m_table
        // (StateKey.h:60, 非 .table; StateKey 的成员是 m_tableAndKey/m_split)。
        if (key.m_table == bcos::ledger::SYS_NUMBER_2_HASH)
            throw std::runtime_error("ThrowOnNumber2Hash: injected SYS_NUMBER_2_HASH read failure");
        co_return co_await storage2::readOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<bool> existsOne(auto key, auto&&... args)
    {
        co_return co_await storage2::existsOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

    task::Task<typename Storage::Iterator> range(auto&&... args)
    {
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
