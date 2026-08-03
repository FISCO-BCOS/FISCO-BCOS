// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// LedgerRootTest.cpp — 真账本桥 Task 5(design §6):跨后端建根测试。brief 编号 (o)-(s)
// 中的 (o) 已被 Storage2LedgerTest.cpp 里 Task 4 审查修复占用的测试名占用,墓碑跳过/键
// 分类两组(brief (p)/(q))落在 Storage2LedgerTest.cpp(见该文件"真账本桥 Task 5"小节),
// 本文件承接三后端同根/大规模/建根逐字段矩阵三组,顺延为 (r)-(t)(映射见任务报告)。
//
// 覆盖 bcos::evm::stateRootOf<Ledger> 泛型模板(adapter/StateRootCompute.h):secure-trie
// 账户树(键 keccak(addr),叶 rlp(nonce, balance, storageRoot, codeHash))+ 每账户存储树
// (accountStorageRoot,与 OpBlockSeal.cpp::opStorageRoot 同构造)。

#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/MemoryLedger.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/utils/mpt_hash.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <optional>
#include <string>
#include <vector>

using namespace bcos::evm::ledger;
using namespace evmc::literals;

namespace
{
using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;

evmc_bytes32 slotKey(uint8_t last)
{
    evmc_bytes32 key{};
    key.bytes[31] = last;
    return key;
}
}  // namespace

// (r) 三后端同根(design §6/§7 "三后端同根"):同一状态(带码带槽账户 / 多槽账户 / 完全
//     空账户)在 TestState(既有 mpt_hash 引擎,不经 stateRootOf 的 deprecated 包装,直接调
//     vendored 引擎本身)/ MemoryLedger / Storage2Ledger 三个后端分别建根,三根逐字节相等。
TEST(LedgerRoot, ThreeBackendsSameRootWithCodeAndSlots)
{
    evmone::test::TestState ts;
    {
        auto& a = ts[0x01_address];
        a.nonce = 3;
        a.balance = 100;
        a.code = evmc::bytes{0x60, 0x00, 0x60, 0x01};
        a.storage[slotKey(1)] = slotKey(7);
    }
    {
        auto& a = ts[0x02_address];
        a.nonce = 0;
        a.balance = 50;
        a.storage[slotKey(1)] = slotKey(11);
        a.storage[slotKey(2)] = slotKey(22);
        a.storage[slotKey(3)] = slotKey(33);
    }
    ts[0x03_address];  // 完全空账户(KEEP 契约:存在但字段全默认)。

    const auto want = evmone::state::mpt_hash(ts);

    MemoryLedger memLedger;
    seedFromTestState(memLedger, ts);
    EXPECT_EQ(bcos::evm::stateRootOf(memLedger), want);

    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);
    seedFromTestState(bridge, ts);
    ASSERT_FALSE(bridge.poisoned());
    EXPECT_EQ(bcos::evm::stateRootOf(bridge), want);
    EXPECT_FALSE(bridge.poisoned());
}

// (s) 大规模(design §7):1 账户 1024 槽 + 32KB code——visitAccounts 槽计数 1024、建根与
//     同数据 MemoryLedger 建根相等、applyDiff 删除后遍历零残留。
TEST(LedgerRoot, LargeAccountThousandSlotsAndLargeCode)
{
    constexpr std::size_t kSlotCount = 1024;
    constexpr std::size_t kCodeSize = 32 * 1024;

    evmone::state::StateDiff diff;
    evmone::state::StateDiff::Entry entry;
    entry.addr = 0x01_address;
    entry.nonce = 1;
    entry.balance = 1;
    entry.code = evmc::bytes(kCodeSize, 0xAB);
    entry.modified_storage.reserve(kSlotCount);
    for (std::size_t i = 0; i < kSlotCount; ++i)
    {
        evmc_bytes32 key{};
        key.bytes[30] = static_cast<uint8_t>(i >> 8);
        key.bytes[31] = static_cast<uint8_t>(i);
        entry.modified_storage.emplace_back(key, slotKey(1));
    }
    diff.modified_accounts.push_back(std::move(entry));

    MemoryLedger memLedger;
    memLedger.applyDiff(diff);
    const auto want = bcos::evm::stateRootOf(memLedger);

    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);
    bridge.applyDiff(diff);
    ASSERT_FALSE(bridge.poisoned());

    std::size_t accountCount = 0;
    std::size_t slotCount = 0;
    bool ok = bridge.visitAccounts([&](const auto& av) {
        ++accountCount;
        slotCount = av.storage.size();
        EXPECT_EQ(av.code().size(), kCodeSize);
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    EXPECT_EQ(accountCount, 1U);
    EXPECT_EQ(slotCount, kSlotCount);
    EXPECT_EQ(bcos::evm::stateRootOf(bridge), want);

    // 删除后遍历零残留。
    evmone::state::StateDiff deleteDiff;
    deleteDiff.deleted_accounts.push_back(0x01_address);
    bridge.applyDiff(deleteDiff);

    std::size_t remaining = 0;
    ok = bridge.visitAccounts([&](const auto&) {
        ++remaining;
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    EXPECT_EQ(remaining, 0U);
}

// (s2) 终审批 D-6:默认 get_or_insert 且最终为空的账户不得落账。构造一条"新建且 EIP-161 空"
// 的 diff 条目(nonce=0 ∧ balance=0 ∧ 无码),applyDiff 必须守卫翻红(毒旗 → -32603),而不是把
// 空账户表行写进账本——该守卫把"未来新增不 bump nonce 的创建路径"固定为失败(§6.4 D-6)。
TEST(LedgerRoot, ApplyDiffRejectsNewlyCreatedEmptyAccount)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmone::state::StateDiff diff;
    evmone::state::StateDiff::Entry entry;
    entry.addr = 0x04_address;  // 全新地址,账本无该表
    entry.nonce = 0;
    entry.balance = 0;
    // 无 code、无 storage。
    diff.modified_accounts.push_back(std::move(entry));

    EXPECT_THROW(bridge.applyDiff(diff), std::runtime_error);
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_NE(std::string(bridge.firstError()).find("EIP-161-empty"), std::string::npos);

    // 对照 1:同地址给一笔 balance(非空)→ 合法落账,守卫不误伤。
    MutableStorage storage2;
    Storage2Ledger<MutableStorage> bridge2(storage2);
    evmone::state::StateDiff okDiff;
    evmone::state::StateDiff::Entry okEntry;
    okEntry.addr = 0x04_address;
    okEntry.nonce = 0;
    okEntry.balance = 1;
    okDiff.modified_accounts.push_back(std::move(okEntry));
    bridge2.applyDiff(okDiff);
    ASSERT_FALSE(bridge2.poisoned());

    // 对照 2:同一条"新建空账户"diff 走播种模式(seeding=true)不触发——seedFromTestState
    // 经同一条 applyDiff 落账 pre 中的完全空账户(EIP-161 touch-delete 向量前置,KEEP 契约),
    // 那是创世快照而非块执行,守卫放行(D-6)。
    MutableStorage storage3;
    Storage2Ledger<MutableStorage> bridge3(storage3);
    evmone::state::StateDiff seedDiff;
    evmone::state::StateDiff::Entry seedEntry;
    seedEntry.addr = 0x04_address;
    seedEntry.nonce = 0;
    seedEntry.balance = 0;
    seedDiff.modified_accounts.push_back(std::move(seedEntry));
    bridge3.applyDiff(seedDiff, /*seeding=*/true);
    ASSERT_FALSE(bridge3.poisoned());
}

// (t) 建根逐字段矩阵(design §7 探针 4,常驻 gtest):基线根 vs 分别篡改
//     nonce/balance/code(→codeHash)/单槽后的根,四例均不等于基线。
TEST(LedgerRoot, FieldMatrixEachMutationChangesRoot)
{
    auto buildBaseline = []() {
        MemoryLedger ledger;
        evmone::state::StateDiff diff;
        diff.modified_accounts.push_back(
            {0x01_address, 3, 100, evmc::bytes{0x60, 0x00}, {{slotKey(1), slotKey(7)}}});
        ledger.applyDiff(diff);
        return ledger;
    };

    const auto baseline = bcos::evm::stateRootOf(buildBaseline());

    {  // nonce 篡改
        auto ledger = buildBaseline();
        evmone::state::StateDiff diff;
        diff.modified_accounts.push_back({0x01_address, 4, 100, std::nullopt, {}});
        ledger.applyDiff(diff);
        EXPECT_NE(bcos::evm::stateRootOf(ledger), baseline);
    }
    {  // balance 篡改
        auto ledger = buildBaseline();
        evmone::state::StateDiff diff;
        diff.modified_accounts.push_back({0x01_address, 3, 101, std::nullopt, {}});
        ledger.applyDiff(diff);
        EXPECT_NE(bcos::evm::stateRootOf(ledger), baseline);
    }
    {  // code 篡改(→codeHash 变)
        auto ledger = buildBaseline();
        evmone::state::StateDiff diff;
        diff.modified_accounts.push_back({0x01_address, 3, 100, evmc::bytes{0x60, 0x01}, {}});
        ledger.applyDiff(diff);
        EXPECT_NE(bcos::evm::stateRootOf(ledger), baseline);
    }
    {  // 单槽篡改
        auto ledger = buildBaseline();
        evmone::state::StateDiff diff;
        diff.modified_accounts.push_back(
            {0x01_address, 3, 100, std::nullopt, {{slotKey(1), slotKey(8)}}});
        ledger.applyDiff(diff);
        EXPECT_NE(bcos::evm::stateRootOf(ledger), baseline);
    }
}
