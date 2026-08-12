// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpstackExecutorCache — 分叉键缓存的 OpstackExecutor 实例池（接线 Task 6，SEV-9）。
//
// OpstackExecutor 拥有一个 evmc::VM（evmc_create_evmone()）；接线前 execute hook / eth_call 每调
// 现构造（先例 OpBlockScheduler.h:298 `OpstackExecutor executor(m_receiptFactory, m_hashImpl,
// cfg)`）。本缓存按 (forkTimestamps, chainId) 分叉键缓存实例——同一分叉的连续块共享一个 VM。
// 键含解析后的活跃分叉维度（cfg.fork）：单个分叉调度横跨 isthmus/jovian 块，而 executor 的
// m_forkConfig 构造后固定（const& 静态单例），故按活跃分叉各持一例。
//
// 线程安全依赖引擎执行段被状态锁串行（同 OpSchedulerImpl 的 vm 所有权注释）；仍以 mutex 守护
// map（RPC eth_call 路径可能在执行段之外，成本可忽略）。

#include <opstack-executor/OpstackExecutor.h>

#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>

namespace bcos::executor_v1::opstack
{
class OpstackExecutorCache
{
public:
    OpstackExecutorCache(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl)
      : m_receiptFactory(std::move(receiptFactory)), m_hashImpl(std::move(hashImpl))
    {}

    /// 返回 (forkTimestamps, chainId, cfg) 命中的 OpstackExecutor。cfg 是 configAt 解析出的活跃
    /// 分叉配置（isthmus/jovian 静态单例的引用，executor 内部按 const& 持有，安全）。
    OpstackExecutor& get(bcos::evm::opstack::OpForkTimestamps const& forkTimestamps,
        uint64_t chainId, bcos::evm::opstack::OpForkConfig const& cfg)
    {
        std::scoped_lock lock(m_mutex);
        auto key = std::make_tuple(
            forkTimestamps.isthmusTime, forkTimestamps.jovianTime, chainId, cfg.fork);
        auto [it, inserted] = m_executors.emplace(key, nullptr);
        if (inserted)
            it->second = std::make_unique<OpstackExecutor>(m_receiptFactory, m_hashImpl, cfg);
        return *it->second;
    }

private:
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    std::map<std::tuple<uint64_t, uint64_t, uint64_t, bcos::evm::opstack::OpFork>,
        std::unique_ptr<OpstackExecutor>>
        m_executors;
    std::mutex m_mutex;
};
}  // namespace bcos::executor_v1::opstack
