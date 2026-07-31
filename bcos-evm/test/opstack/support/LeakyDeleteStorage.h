// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// LeakyDeleteStorage — 删除失效注入装饰器(仅测试用,终审批 9)。包一层底层 storage2 存储,
// **`removeOne` 是空操作**,其余算子(readOne/existsOne/range/writeOne)一律直通。
//
// 用途:模拟"写回路径漏了删槽"——`Storage2Ledger::applyModifiedEntry` 的契约②零值分支调了
// `removeOne` 却没生效,零值槽行因此留在账户表里。这是 `applyDiff` 删槽后置回读守护**唯一
// 可达的触发方式**(该守护校验的是结果:行删掉了没有)。
//
// 与 support/ThrowingStorage.h 同一装饰器写法(只覆盖桥实际用到的算子);两者的区别是
// ThrowingStorage 打的是读路,本装饰器打的是删除路。
//
// 使用者只有 Storage2LedgerTest((z6):直接断言 applyDiff 抛 + 置毒旗)。
//
// **实测记录(终审批 9 F-1,反直觉,勿再试)**:本装饰器在 `OpSchedulerImplTest` 那一层
// **触发不了**——把它包在 kVectorId 的向量块执行上,`removeOne` 一次都没被调到(该块既无零值
// 槽写入,也无账户删除),用例只会报 "none thrown"。applyDiff 的三条 tripwire 全都依赖
// StateDiff 长成特定形状,而块执行凑不出那个形状。分类层("触发时是 -32603 不是 INVALID")
// 的见证因此改用 support/WriteFailingStorage.h ——"底层存储写入失败"对任何块都成立。

#include <bcos-framework/storage/Entry.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-task/Task.h>
#include <functional>
#include <optional>
#include <utility>

namespace bcos::evm::test
{

template <class Storage>
class LeakyDeleteStorage
{
public:
    explicit LeakyDeleteStorage(Storage& storage) noexcept : m_storage(storage) {}

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
        co_return co_await storage2::range(
            m_storage.get(), std::forward<decltype(args)>(args)...);
    }

    task::Task<void> writeOne(auto key, auto value, auto&&... args)
    {
        co_await storage2::writeOne(m_storage.get(), std::move(key), std::move(value),
            std::forward<decltype(args)>(args)...);
    }

    /// 刻意什么都不做:注入"删除没生效"的写回泄漏。
    task::Task<void> removeOne(auto /*key*/, auto&&... /*args*/) { co_return; }

private:
    std::reference_wrapper<Storage> m_storage;
};

}  // namespace bcos::evm::test
