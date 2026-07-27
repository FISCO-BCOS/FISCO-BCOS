// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Storage2Ledger 单元测试(design §7 组 (a)-(g)):存在性 / 空账户归一化 / 往返读 /
// has_storage / 毒旗(异常注入)/ nonce 溢出毒旗 / 负缓存命中计数。fixture 用单层
// MemoryStorage<StateKey, StateValue, ORDERED>(仿
// transaction-executor/tests/TestMemoryStorage.h:8-9),经 EVMAccount 写入种子数据,
// EVMAccount(storage, addr, /*binaryAddress=*/false)——E-b 前置 feature_raw_address=off。

#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/FixedBytes.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <stdexcept>
#include <string>

#include "support/CountingStorage.h"
#include "support/ThrowingStorage.h"

using namespace bcos::evm::ledger;
using namespace evmc::literals;

namespace
{
using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;
using Account = bcos::ledger::account::EVMAccount<MutableStorage>;

evmc_bytes32 slotKey(uint8_t last)
{
    evmc_bytes32 key{};
    key.bytes[31] = last;
    return key;
}
}  // namespace

// (a) 存在性:EVMAccount::create() 后 has_value;未 create 的地址 nullopt。
TEST(Storage2Ledger, ExistenceAfterCreate)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());

    Storage2Ledger<MutableStorage> bridge(storage);
    EXPECT_TRUE(bridge.get_account(0x01_address).has_value());
    EXPECT_FALSE(bridge.get_account(0x02_address).has_value());
    EXPECT_FALSE(bridge.poisoned());
}

// (b) 空账户归一化:create() 但零字段 → Account{0,0,keccak(空),has_storage=false}。
TEST(Storage2Ledger, EmptyAccountNormalization)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());

    Storage2Ledger<MutableStorage> bridge(storage);
    auto result = bridge.get_account(0x01_address);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->nonce, 0U);
    EXPECT_EQ(result->balance, intx::uint256{0});
    EXPECT_EQ(result->code_hash, evmone::keccak256(evmc::bytes_view{}));
    EXPECT_FALSE(result->has_storage);
    EXPECT_FALSE(bridge.poisoned());

    // 已 create() 但从未 setCode() → get_account_code 应返回空字节,且不毒旗
    // (审查补钉:此前无直接断言覆盖该路径,见 fetchCode 对缺失/空 CODE_HASH 的处理)。
    EXPECT_TRUE(bridge.get_account_code(0x01_address).empty());
    EXPECT_FALSE(bridge.poisoned());
}

// (c) 往返读:EVMAccount 写 balance=7/nonce="5"/setCode → 桥读逐字段相等。
TEST(Storage2Ledger, RoundTripFields)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    bcos::task::syncWait(acc.setBalance(bcos::u256(7)));
    bcos::task::syncWait(acc.setNonce("5"));

    evmc::bytes code{0x60, 0x00};
    auto codeHash = evmone::keccak256(code);
    bcos::h256 codeHashValue(
        reinterpret_cast<const bcos::byte*>(codeHash.bytes), sizeof(codeHash.bytes));
    bcos::task::syncWait(
        acc.setCode(bcos::bytes(code.begin(), code.end()), std::string{}, codeHashValue));

    Storage2Ledger<MutableStorage> bridge(storage);
    auto result = bridge.get_account(0x01_address);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->nonce, 5U);
    EXPECT_EQ(result->balance, intx::uint256{7});
    EXPECT_EQ(result->code_hash, codeHash);
    EXPECT_EQ(bridge.get_account_code(0x01_address), code);
    EXPECT_FALSE(bridge.poisoned());
}

// (d) has_storage:EVMAccount::setStorage 一槽 → true。
TEST(Storage2Ledger, HasStorageTrueAfterSetStorage)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    auto key = slotKey(0x01);
    auto value = slotKey(0x2a);
    bcos::task::syncWait(acc.setStorage(key, value));

    Storage2Ledger<MutableStorage> bridge(storage);
    auto result = bridge.get_account(0x01_address);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->has_storage);

    auto readBack = bridge.get_storage(0x01_address, evmc::bytes32(key));
    EXPECT_EQ(std::memcmp(readBack.bytes, value.bytes, sizeof(value.bytes)), 0);
    EXPECT_FALSE(bridge.poisoned());
}

// (e) 毒旗:Storage2Ledger<ThrowingStorage> 读 → 返回 nullopt/零值,poisoned()==true,
//     firstError() 非空;新实例 poisoned()==false(实例隔离)。
TEST(Storage2Ledger, PoisonOnInjectedStorageException)
{
    MutableStorage rawStorage;
    Account acc(rawStorage, 0x01_address, false);
    bcos::task::syncWait(acc.create());

    bcos::evm::test::ThrowingStorage<MutableStorage> throwing(rawStorage);
    Storage2Ledger<decltype(throwing)> bridge(throwing);

    EXPECT_FALSE(bridge.get_account(0x01_address).has_value());
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_FALSE(bridge.firstError().empty());

    EXPECT_TRUE(bridge.get_account_code(0x01_address).empty());
    EXPECT_EQ(bridge.get_storage(0x01_address, evmc::bytes32{}), evmc::bytes32{});

    // 实例隔离:新桥实例不受前一被下毒实例影响。
    Storage2Ledger<MutableStorage> freshBridge(rawStorage);
    EXPECT_FALSE(freshBridge.poisoned());
    EXPECT_TRUE(freshBridge.get_account(0x01_address).has_value());
}

// (f) nonce 溢出:EVMAccount::setNonce("18446744073709551616")(2^64)→ 桥读 poisoned()。
TEST(Storage2Ledger, NonceOverflowPoisons)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    bcos::task::syncWait(acc.setNonce("18446744073709551616"));  // 2^64

    Storage2Ledger<MutableStorage> bridge(storage);
    auto result = bridge.get_account(0x01_address);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_FALSE(bridge.firstError().empty());
}

// (g) 负缓存:Storage2Ledger<CountingStorage> 连读两次不存在地址,第二次 storage
//     调用计数零增量。
TEST(Storage2Ledger, NegativeCacheAvoidsRepeatStorageCalls)
{
    MutableStorage rawStorage;
    bcos::evm::test::CountingStorage<MutableStorage> counting(rawStorage);
    Storage2Ledger<decltype(counting)> bridge(counting);

    EXPECT_FALSE(bridge.get_account(0x03_address).has_value());
    auto afterFirst = counting.readCount;
    EXPECT_GT(afterFirst, 0U);

    EXPECT_FALSE(bridge.get_account(0x03_address).has_value());
    EXPECT_EQ(counting.readCount, afterFirst);
    EXPECT_FALSE(bridge.poisoned());
}

// ── applyDiff 写回 + 写穿失效(design §5/§7,组 (h)-(n))────────────────────────

// (h) 写穿-负缓存:桥读不存在地址(种负缓存)→ applyDiff 建该账户 → 立即再读 has_value。
TEST(Storage2Ledger, WriteThroughNegativeCacheThenCreate)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    EXPECT_FALSE(bridge.get_account(0x01_address).has_value());
    EXPECT_FALSE(bridge.poisoned());

    evmone::state::StateDiff diff;
    diff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 0,
        .balance = {},
        .code = std::nullopt,
        .modified_storage = {}});
    bridge.applyDiff(diff);

    EXPECT_TRUE(bridge.get_account(0x01_address).has_value());
    EXPECT_FALSE(bridge.poisoned());
}

// (i) 写穿-正缓存:桥读槽值(种正缓存)→ applyDiff 覆写 → 再读得新值;删槽(写零)→
//     再读零且 has_storage 翻 false。
TEST(Storage2Ledger, WriteThroughPositiveCacheOverwriteAndDelete)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    auto key = slotKey(0x01);
    auto initialValue = slotKey(0x11);
    bcos::task::syncWait(acc.setStorage(key, initialValue));

    Storage2Ledger<MutableStorage> bridge(storage);
    auto readBack = bridge.get_storage(0x01_address, evmc::bytes32(key));
    EXPECT_EQ(std::memcmp(readBack.bytes, initialValue.bytes, sizeof(initialValue.bytes)), 0);
    auto accountBefore = bridge.get_account(0x01_address);
    ASSERT_TRUE(accountBefore.has_value());
    EXPECT_TRUE(accountBefore->has_storage);

    auto newValue = slotKey(0x22);
    evmone::state::StateDiff overwriteDiff;
    overwriteDiff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 0,
        .balance = {},
        .code = std::nullopt,
        .modified_storage = {{evmc::bytes32(key), evmc::bytes32(newValue)}}});
    bridge.applyDiff(overwriteDiff);

    auto afterOverwrite = bridge.get_storage(0x01_address, evmc::bytes32(key));
    EXPECT_EQ(std::memcmp(afterOverwrite.bytes, newValue.bytes, sizeof(newValue.bytes)), 0);

    evmone::state::StateDiff deleteSlotDiff;
    deleteSlotDiff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 0,
        .balance = {},
        .code = std::nullopt,
        .modified_storage = {{evmc::bytes32(key), evmc::bytes32{}}}});
    bridge.applyDiff(deleteSlotDiff);

    auto afterDelete = bridge.get_storage(0x01_address, evmc::bytes32(key));
    EXPECT_EQ(afterDelete, evmc::bytes32{});
    auto accountAfterDelete = bridge.get_account(0x01_address);
    ASSERT_TRUE(accountAfterDelete.has_value());
    EXPECT_FALSE(accountAfterDelete->has_storage);
    EXPECT_FALSE(bridge.poisoned());
}

// (j) 删除三表失效(CREATE2 同址重生):建账户+槽+code → 桥读全部(种满三缓存)→
//     applyDiff 删除 → applyDiff 同址重建不同 code/槽 → 再读全部得新值,无陈旧残留。
TEST(Storage2Ledger, DeleteInvalidatesAllThreeCachesForCreate2Respawn)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmc::bytes codeA{0x60, 0x01};
    auto slotA = slotKey(0x01);
    auto valueA = slotKey(0xaa);
    evmone::state::StateDiff createDiff;
    createDiff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 1,
        .balance = intx::uint256{7},
        .code = codeA,
        .modified_storage = {{evmc::bytes32(slotA), evmc::bytes32(valueA)}}});
    bridge.applyDiff(createDiff);

    ASSERT_TRUE(bridge.get_account(0x01_address).has_value());
    EXPECT_EQ(bridge.get_account_code(0x01_address), codeA);
    auto slotABack = bridge.get_storage(0x01_address, evmc::bytes32(slotA));
    EXPECT_EQ(std::memcmp(slotABack.bytes, valueA.bytes, sizeof(valueA.bytes)), 0);

    evmone::state::StateDiff deleteDiff;
    deleteDiff.deleted_accounts.push_back(0x01_address);
    bridge.applyDiff(deleteDiff);

    evmc::bytes codeB{0x60, 0x02, 0x00};
    auto slotB = slotKey(0x02);
    auto valueB = slotKey(0xbb);
    evmone::state::StateDiff recreateDiff;
    recreateDiff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 9,
        .balance = intx::uint256{42},
        .code = codeB,
        .modified_storage = {{evmc::bytes32(slotB), evmc::bytes32(valueB)}}});
    bridge.applyDiff(recreateDiff);

    auto accountAfter = bridge.get_account(0x01_address);
    ASSERT_TRUE(accountAfter.has_value());
    EXPECT_EQ(accountAfter->nonce, 9U);
    EXPECT_EQ(accountAfter->balance, intx::uint256{42});
    EXPECT_EQ(accountAfter->code_hash, evmone::keccak256(codeB));
    EXPECT_TRUE(accountAfter->has_storage);
    EXPECT_EQ(bridge.get_account_code(0x01_address), codeB);

    // 旧槽(slotA)物理已删且缓存已失效,应读零而非陈旧 valueA(无陈旧残留)。
    auto slotAAfter = bridge.get_storage(0x01_address, evmc::bytes32(slotA));
    EXPECT_EQ(slotAAfter, evmc::bytes32{});
    auto slotBAfter = bridge.get_storage(0x01_address, evmc::bytes32(slotB));
    EXPECT_EQ(std::memcmp(slotBAfter.bytes, valueB.bytes, sizeof(valueB.bytes)), 0);
    EXPECT_FALSE(bridge.poisoned());
}

// (k) 往返-写向:桥 applyDiff 写 balance/nonce/code/槽 → EVMAccount 读逐字段相等
//     (abi 豁免:不断言 abi()——design §5)。
TEST(Storage2Ledger, RoundTripWriteDirectionVsEVMAccount)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmc::bytes code{0x60, 0x00, 0x60, 0x01};
    auto slot = slotKey(0x05);
    auto value = slotKey(0x99);
    evmone::state::StateDiff diff;
    diff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 3,
        .balance = intx::uint256{123},
        .code = code,
        .modified_storage = {{evmc::bytes32(slot), evmc::bytes32(value)}}});
    bridge.applyDiff(diff);

    Account acc(storage, 0x01_address, false);
    EXPECT_TRUE(bcos::task::syncWait(acc.exists()));

    auto nonce = bcos::task::syncWait(acc.nonce());
    ASSERT_TRUE(nonce.has_value());
    EXPECT_EQ(*nonce, "3");

    auto balance = bcos::task::syncWait(acc.balance());
    EXPECT_EQ(balance, bcos::u256(123));

    auto codeEntry = bcos::task::syncWait(acc.code());
    ASSERT_TRUE(codeEntry.has_value());
    auto codeView = codeEntry->get();
    EXPECT_EQ(evmc::bytes(codeView.begin(), codeView.end()), code);

    auto codeHash = bcos::task::syncWait(acc.codeHash());
    auto expectedHash = evmone::keccak256(code);
    bcos::h256 expectedHashValue(
        reinterpret_cast<const bcos::byte*>(expectedHash.bytes), sizeof(expectedHash.bytes));
    EXPECT_EQ(codeHash, expectedHashValue);

    auto storedSlot = bcos::task::syncWait(acc.storage(slot));
    EXPECT_EQ(std::memcmp(storedSlot.bytes, value.bytes, sizeof(value.bytes)), 0);
    EXPECT_FALSE(bridge.poisoned());
}

// (l) SYS_CODE_BINARY 不删:两账户同 code → 删其一 → 另一 get_account_code 仍完整。
TEST(Storage2Ledger, DeleteNeverRemovesSharedCodeBinary)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmc::bytes sharedCode{0x60, 0x0a, 0x60, 0x0b};
    evmone::state::StateDiff createDiff;
    createDiff.modified_accounts.push_back({.addr = 0x01_address,
        .nonce = 0,
        .balance = {},
        .code = sharedCode,
        .modified_storage = {}});
    createDiff.modified_accounts.push_back({.addr = 0x02_address,
        .nonce = 0,
        .balance = {},
        .code = sharedCode,
        .modified_storage = {}});
    bridge.applyDiff(createDiff);

    ASSERT_EQ(bridge.get_account_code(0x01_address), sharedCode);
    ASSERT_EQ(bridge.get_account_code(0x02_address), sharedCode);

    evmone::state::StateDiff deleteDiff;
    deleteDiff.deleted_accounts.push_back(0x01_address);
    bridge.applyDiff(deleteDiff);

    EXPECT_FALSE(bridge.get_account(0x01_address).has_value());
    EXPECT_TRUE(bridge.get_account_code(0x01_address).empty());
    // 另一账户的 code 仍完整(SYS_CODE_BINARY 行永不删除,design §5)。
    ASSERT_TRUE(bridge.get_account(0x02_address).has_value());
    EXPECT_EQ(bridge.get_account_code(0x02_address), sharedCode);
    EXPECT_FALSE(bridge.poisoned());
}

// (m) 空账户播种:seedFromTestState(pre 含空账户)→ 桥 get_account(空账户).has_value()。
TEST(Storage2Ledger, SeedFromTestStateEmptyAccountEnsuresExists)
{
    MutableStorage storage;
    evmone::test::TestState pre;
    pre[0x03_address];  // 完全空账户:默认构造 nonce=0/balance=0/code 空/storage 空。

    Storage2Ledger<MutableStorage> bridge(storage);
    bcos::evm::ledger::seedFromTestState(bridge, pre);

    auto result = bridge.get_account(0x03_address);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->nonce, 0U);
    EXPECT_EQ(result->balance, intx::uint256{0});
    EXPECT_FALSE(result->has_storage);
    EXPECT_FALSE(bridge.poisoned());
}

// (n) strict tripwire:deleted 项底层不存在 → throw。
TEST(Storage2Ledger, ApplyDiffDeletedGhostThrows)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmone::state::StateDiff diff;
    diff.deleted_accounts.push_back(0x01_address);

    EXPECT_THROW(bridge.applyDiff(diff), std::runtime_error);
}
