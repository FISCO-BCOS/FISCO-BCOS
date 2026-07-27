// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// LedgerSeed — 统一播种(design §5):把向量 pre(evmone::test::TestState)合成一枚创世
// StateDiff,走同一条 applyDiff 路径落账。MemoryLedger 与 Storage2Ledger 通用(applyDiff 是
// 两者共有的写回接口),不为每个后端各写一套播种逻辑——序列化/ensure-exists/契约②③的落地
// 全部委托给各自的 applyDiff 实现,本文件只负责把 TestState 的 map 形状转成 StateDiff 形状。
//
// 字段映射:
//   - nonce/balance 直接拷贝;
//   - code:pre 账户 code 为空 → StateDiff::Entry::code 留 std::nullopt(契约③"仅 has_value()
//     时覆写",空 code 不落一条空覆写,与"账户存在但无码"的默认态自然重合);非空才显式携带。
//   - storage:TestState 的 storage map 本就不含零值槽(loader 按 trie "0 ≡ 缺席" 规约在解析时
//     剔除,见 T8nReplayHarness.h 对 pre 解析的注释),故逐对原样搬进 modified_storage,落地
//     后由 applyDiff 的契约②处理(非零値写入,不会误触发删槽分支)。
//   - deleted_accounts 恒空:播种只创世,不产生删除。
//
// 完全空账户(EIP-161 touch-delete 向量前置)：nonce=0/balance=0/code=nullopt/
// modified_storage=空 的 Entry 依然进入 modified_accounts,applyDiff 的 ensure-exists
// 契约(design §5 rev.2 补)保证其被无条件落账,而不是被"无字段可写"的错误优化跳过。

#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

namespace bcos::evm::ledger
{

template <class Ledger>
void seedFromTestState(Ledger& ledger, const evmone::test::TestState& pre)
{
    evmone::state::StateDiff diff;
    diff.modified_accounts.reserve(pre.size());
    for (const auto& [addr, account] : pre)
    {
        evmone::state::StateDiff::Entry entry;
        entry.addr = addr;
        entry.nonce = account.nonce;
        entry.balance = account.balance;
        if (!account.code.empty())
            entry.code = account.code;
        entry.modified_storage.reserve(account.storage.size());
        for (const auto& [key, value] : account.storage)
            entry.modified_storage.emplace_back(key, value);
        diff.modified_accounts.push_back(std::move(entry));
    }
    ledger.applyDiff(diff);
}

}  // namespace bcos::evm::ledger
