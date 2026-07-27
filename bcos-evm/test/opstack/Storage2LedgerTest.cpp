// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Storage2Ledger 单元测试(design §7 组 (a)-(g)):存在性 / 空账户归一化 / 往返读 /
// has_storage / 毒旗(异常注入)/ nonce 溢出毒旗 / 负缓存命中计数。fixture 用单层
// MemoryStorage<StateKey, StateValue, ORDERED>(仿
// transaction-executor/tests/TestMemoryStorage.h:8-9),经 EVMAccount 写入种子数据,
// EVMAccount(storage, addr, /*binaryAddress=*/false)——E-b 前置 feature_raw_address=off。

#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/FixedBytes.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
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
