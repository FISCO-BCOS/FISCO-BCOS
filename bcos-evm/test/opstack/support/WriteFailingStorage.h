// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// WriteFailingStorage — 写回失败注入装饰器(仅测试用,终审批 9 F-1)。与 support/ThrowingStorage.h
// **镜像对称**:那个打读路(existsOne/readOne/range 抛,写路直通),这个打写路
// (writeOne/removeOne 抛,读路直通)。
//
// 用途:给 `Storage2Ledger::applyDiff` 的毒旗通道提供一个**与 diff 内容无关**的触发点。
// applyDiff 的其余 tripwire(/sys/ 路由、ghost-delete、契约②零值写回泄漏)都要求 StateDiff
// 长成特定形状,在 `OpSchedulerImpl` 这一层要靠真实块执行凑出那个形状——**实测凑不出**:
// 用 support/LeakyDeleteStorage.h 包住 `OpSchedulerImplTest` 的向量执行,`removeOne` 一次都
// 没被调到(该块既无零值槽写、也无账户删除),注入完全不触发、用例报 "none thrown"。
// 而"底层存储写入自身失败"这一条对**任何**块都成立(applyDiff 必写账户 nonce/balance),
// 因此它是分类层唯一稳的写路径见证,同时也是 applyDiff 注释里列举的本地故障之一。
//
// 读路刻意直通,是为了保证**毒旗的唯一来源是 applyDiff**:若读路也抛,毒旗会在块执行早期
// 就被读方法的 noexcept catch 置位,用例就分不清"写路的 catch 置的旗"还是"读路置的旗",
// 见证归属会糊掉(既有用例 (d) ThrowingStorageIsStorageError 覆盖的正是读路那一半)。

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
class WriteFailingStorage
{
public:
    explicit WriteFailingStorage(Storage& storage) noexcept : m_storage(storage) {}

    task::Task<std::optional<bcos::storage::Entry>> readOne(auto key, auto&&... args)
    {
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
        throw std::runtime_error("WriteFailingStorage: injected write failure (writeOne)");
        // 不可达;保留以满足协程返回类型推导(与 ThrowingStorage.h 同一写法)。
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...);
    }

    task::Task<void> removeOne(auto key, auto&&... args)
    {
        throw std::runtime_error("WriteFailingStorage: injected write failure (removeOne)");
        co_await storage2::removeOne(
            m_storage.get(), std::move(key), std::forward<decltype(args)>(args)...);
    }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::evm::test
