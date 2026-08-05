// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// D1 (spec docs/superpowers/specs/2026-08-03-blockhashes-full-history-d1-fix.md): 对齐 op-geth
// GetHashFn (core/evm.go:103-140) 的懒加载 BlockHashes。EVM BLOCKHASH 操作码只查询
// [max(0,N-256), N-1] 窗口 (evmone instructions.hpp:681-691), 窗口外由操作码层返回零。
// 本类型以注入 parentHash 为种子 (高度 N-1, 零 storage 读, 覆盖首块/EIP-2935), 更早祖先
// 按需从 SYS_NUMBER_2_HASH 懒加载。无 reorg 链上 (step 3c) number->hash 单射, 逐高度直查
// 与 op-geth 的 header 回走等价 (前提是表连续, G6)。
//
// 生命周期/线程安全: 构造于 executeOpBlock 内, 与 bridge 同源 storage, 全程存活于本块;
// 依赖 engine 的 x_state 串行执行段 (design §4.4), 无需内部锁。

#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/lexical_cast.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>

namespace bcos::evm::engine::detail
{
/// op-geth GetHashFn 语义的懒加载 BlockHashes: 种子 {N-1: parentHash}, 更早祖先按需查
/// SYS_NUMBER_2_HASH; 缺失返回零; storage 异常/坏值长度记毒旗返回零。
template <class Storage>
class RecentBlockHashes final : public evmone::state::BlockHashes
{
public:
    RecentBlockHashes(Storage& storage, int64_t blockNumber, evmc::bytes32 parentHash,
        std::optional<std::string>* error)
      : m_storage(storage), m_blockNumber(blockNumber), m_parentHash(parentHash), m_error(error)
    {
        // 种子: EIP-2935 系统调用只查 N-1 (system_contracts.cpp:49-53), 首块亦命中此处。
        // 非 noexcept 上下文 (构造), emplace 无需 try。
        m_cache.emplace(m_blockNumber - 1, m_parentHash);
    }

    evmc::bytes32 get_block_hash(int64_t n) const noexcept override
    {
        // G1: 接口 noexcept, 整个 body 包 try/catch —— emplace 与毒旗写都分配内存,
        // bad_alloc 在 noexcept 里不捕获即 terminate。
        try
        {
            if (n >= m_blockNumber || n < 0)
                return evmc::bytes32{};
            if (const auto it = m_cache.find(n); it != m_cache.end())
                return it->second;

            const auto key = boost::lexical_cast<std::string>(n);
            auto entry = bcos::task::syncWait(bcos::storage2::readOne(
                m_storage, bcos::executor_v1::StateKeyView{bcos::ledger::SYS_NUMBER_2_HASH, key}));
            if (!entry.has_value())
                return evmc::bytes32{};  // 缺失行 = op-geth 裁剪/不可达语义 (G6: 表连续前提下)

            // G3: 值长度校验, 仿 Storage2Ledger::fetchAllStorage (Storage2Ledger.h:682-686)。
            const auto value = entry->get();
            if (value.size() != sizeof(evmc::bytes32::bytes))
            {
                poison("RecentBlockHashes: SYS_NUMBER_2_HASH entry length != 32");
                return evmc::bytes32{};
            }
            evmc::bytes32 out{};
            std::memcpy(out.bytes, value.data(), sizeof(out.bytes));
            // emplace 在 try 内 (G1): bad_alloc 时跳到 catch。
            m_cache.emplace(n, out);
            return out;
        }
        catch (...)
        {
            poison("RecentBlockHashes: storage read or cache insert failed");
            return evmc::bytes32{};
        }
    }

private:
    void poison(std::string msg) const noexcept
    {
        // 只记首条, 后续异常不再抛 (仿 Storage2Ledger::poison(), Storage2Ledger.h:768-781)。
        try
        {
            if (m_error != nullptr && !*m_error)
                *m_error = std::move(msg);
        }
        catch (...)
        {}
    }

    Storage& m_storage;
    int64_t m_blockNumber = 0;
    evmc::bytes32 m_parentHash{};
    std::optional<std::string>* m_error = nullptr;
    mutable std::map<int64_t, evmc::bytes32> m_cache;  // 接口 const noexcept → mutable
};
}  // namespace bcos::evm::engine::detail
