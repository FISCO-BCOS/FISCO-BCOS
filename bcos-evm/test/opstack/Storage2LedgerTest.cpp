// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Storage2Ledger 单元测试(design §7 组 (a)-(g)):存在性 / 空账户归一化 / 往返读 /
// has_storage / 毒旗(异常注入)/ nonce 溢出毒旗 / 负缓存命中计数。fixture 用单层
// MemoryStorage<StateKey, StateValue, ORDERED>(仿
// transaction-executor/tests/TestMemoryStorage.h:8-9),经 EVMAccount 写入种子数据,
// EVMAccount(storage, addr, /*binaryAddress=*/false)——E-b 前置 feature_raw_address=off。

#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/MemoryLedger.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/FixedBytes.h>
#include <gtest/gtest.h>
#include <boost/algorithm/hex.hpp>
#include <array>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/CountingStorage.h"
#include "support/LeakyDeleteStorage.h"
#include "support/ThrowingStorage.h"

using namespace bcos::evm::ledger;
using namespace evmc::literals;

namespace
{
using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, bcos::storage2::memory_storage::ORDERED>;
// (p2)/(p3) 值变体判别测试专用:与生产 MultiLayerStorage 同语义的逻辑删除存储——
// removeOne（不带 BYPASS_LOGICAL_DELETE_TYPE 标签）在这类存储上保留 DELETED_TYPE 墓碑行,
// 而非物理擦除,借此直接构造 range() 会看到的原始变体(而不仅是行为层面的"删了就不见"）。
using LogicalDeleteStorage = bcos::storage2::memory_storage::MemoryStorage<
    bcos::executor_v1::StateKey, bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
using Account = bcos::ledger::account::EVMAccount<MutableStorage>;
using LogicalDeleteAccount = bcos::ledger::account::EVMAccount<LogicalDeleteStorage>;

// ── (z8) 用的 MultiLayerStorage fixture(终审批 9 F-2)─────────────────────────────
// 为什么单层的 LogicalDeleteStorage 不够,见 (z8) 的用例注释:生产 Storage2Ledger 的 Storage
// 是 MultiLayerStorage::ViewType,它**没有** existsOne 成员,`storage2::existsOne` 走的是
// readOne 回退 → View::readOne → getValue<Value> → DELETED_TYPE → nullopt;而 (z7) 钉的是
// MemoryStorage::existsOne → toOptional 那一条,**完全不同的一段代码**。
// CheckpointStorage stub 是本文件局部副本,理由同 EbT8nReplayTest.cpp:45-49 /
// OpSchedulerImplTest.cpp (不跨模块 include 别人的 test-private 头)。
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // 本 fixture 不需要历史 checkpoint。
    }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using BackendMemStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(
        bcos::storage2::memory_storage::ORDERED | bcos::storage2::memory_storage::CONCURRENT),
    std::hash<bcos::executor_v1::StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue, BackendMemStorage>;
// 可变层刻意用 LogicalDeleteStorage(ORDERED | LOGICAL_DELETION)——与 OpSchedulerImplTest.cpp /
// EbT8nReplayTest.cpp 的生产同构 fixture 一致。
using MLS = bcos::storage2::MultiLayerStorage<LogicalDeleteStorage, void, CheckpointBackend>;
using MlsViewType = typename MLS::ViewType;
using MlsAccount = bcos::ledger::account::EVMAccount<MlsViewType>;

/// 持有整条存储栈(backend + MLS + 已 fork 的可变 view),地址稳定,供 (z8) 直接拿 view 用。
struct MlsFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;
    MlsViewType view;

    MlsFixture()
      : checkpointBackend(backendStorage),
        multiLayerStorage(checkpointBackend),
        view(multiLayerStorage.fork())
    {
        view.newMutable();
    }
};

evmc_bytes32 slotKey(uint8_t last)
{
    evmc_bytes32 key{};
    key.bytes[31] = last;
    return key;
}

// accountTableName 的独立复现(该方法在 Storage2Ledger 内是 private static)——"/apps/" +
// hex_lower(addr),与 EVMAccount(storage, addr, /*binaryAddress=*/false) 落地的键空间一致。
std::string tableNameOf(const evmc::address& addr)
{
    std::array<char, sizeof(addr.bytes) * 2> hex{};
    boost::algorithm::hex_lower(
        std::string_view(reinterpret_cast<const char*>(addr.bytes), sizeof(addr.bytes)),
        hex.data());
    return std::string(bcos::ledger::SYS_DIRECTORY::USER_APPS) +
           std::string(hex.data(), hex.size());
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

// (i2) has_storage 墓碑变体(终审 I-1 回归锚点):此前 probeHasStorage 是纯 range 扫描,不判
//     值变体——LOGICAL_DELETION 存储上"删至最后一个槽"后,墓碑行(DELETED_TYPE)仍会被
//     range() 扫到,has_storage 翻不回 false。与 fetchAllStorage/visitAccountsImpl 同一
//     LOGICAL_DELETION fixture(见 (p2)/(p3)),setStorage 一槽 → storage2::removeOne(不带
//     BYPASS_LOGICAL_DELETE_TYPE,保留墓碑而非物理擦除)→ 断言 has_storage==false。
TEST(Storage2Ledger, HasStorageFalseAfterLogicalDeleteOfOnlySlot)
{
    LogicalDeleteStorage storage;
    LogicalDeleteAccount acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    const auto key = slotKey(0x01);
    bcos::task::syncWait(acc.setStorage(key, slotKey(0x2a)));

    const auto tableName = tableNameOf(0x01_address);
    std::string_view keyView(reinterpret_cast<const char*>(key.bytes), sizeof(key.bytes));
    bcos::task::syncWait(
        bcos::storage2::removeOne(storage, bcos::executor_v1::StateKeyView{tableName, keyView}));

    Storage2Ledger<LogicalDeleteStorage> bridge(storage);
    auto result = bridge.get_account(0x01_address);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->has_storage);
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
    // 终审批 9 F-1:写回 tripwire 必须**同时置毒旗**,否则 OpSchedulerImpl 的分类
    // (poisoned() → OpStorageError/-32603,否则 OpConsensusError/INVALID)会把这条
    // "本节点账本与 diff 不一致"的本地故障答成 INVALID,即投票反对一个合法块。
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_NE(bridge.firstError().find("ghost "), std::string_view::npos) << bridge.firstError();
}

// (o) 审查修复:modified entry 含系统地址(c_systemTxsAddress 成员,如
//     SYS_CONFIG_ADDRESS = 0x...001000)→ EVMAccount 会静默路由进 /sys/,与读桥
//     accountTableName 判定不一致;写路径同样不得猜测路由 → applyDiff 必须 throw(与
//     applyDeletedEntry 的 ghost-delete tripwire 同一 strict 纪律,design §4.4)。
TEST(Storage2Ledger, ApplyDiffModifiedSystemAddressThrows)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    constexpr auto sysAddr =
        0x0000000000000000000000000000000000001000_address;  // SYS_CONFIG_ADDRESS
    evmone::state::StateDiff diff;
    diff.modified_accounts.push_back(
        {.addr = sysAddr, .nonce = 0, .balance = {}, .code = std::nullopt, .modified_storage = {}});

    EXPECT_THROW(bridge.applyDiff(diff), std::runtime_error);
    // 终审批 9 F-1:同上——桥"不猜 /sys/ 路由"是本桥的能力边界(本地故障),不是对 payload 的
    // 判决。读路的同一守卫(get_account 的 accountTableName 分支)本来就置毒旗,写路现在对称。
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_NE(bridge.firstError().find("routes to /sys/"), std::string_view::npos)
        << bridge.firstError();
}

// ── 真账本桥 Task 5:visitAccounts 遍历(design §6)──────────────────────────────
// brief 编号 (o)-(s) 中的 (o) 已被上面的 ApplyDiffModifiedSystemAddressThrows(Task 4
// 审查修复)占用,以下测试沿用 brief 的语义分组顺延为 (p)-(t)(映射见任务报告)。

// (p) 墓碑跳过(design §6):三个变体——行为级(块内 applyDiff 删其一,建根与单账户
//     MemoryLedger 相等)+ 两个 Critical 字面变体判别(直接在 LOGICAL_DELETION 存储上
//     构造 DELETED_TYPE 墓碑行,而非仅靠"删了就物理不见"的行为等价性)。
TEST(Storage2Ledger, VisitAccountsSkipsDeletedAccountAfterApplyDiff)
{
    MutableStorage storage;
    Storage2Ledger<MutableStorage> bridge(storage);

    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(
        {0x01_address, 3, 10, evmc::bytes{0x60, 0x00}, {{slotKey(1), slotKey(7)}}});
    seedDiff.modified_accounts.push_back({0x02_address, 5, 20, std::nullopt, {}});
    bridge.applyDiff(seedDiff);

    evmone::state::StateDiff deleteDiff;
    deleteDiff.deleted_accounts.push_back(0x02_address);
    bridge.applyDiff(deleteDiff);

    std::vector<evmc::address> visited;
    const bool ok = bridge.visitAccounts([&](const auto& av) {
        visited.push_back(av.addr);
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], 0x01_address);

    // 建根 == 单账户 MemoryLedger 建根(同一状态,泛型 stateRootOf<Ledger>)。
    bcos::evm::ledger::MemoryLedger reference;
    evmone::state::StateDiff refDiff;
    refDiff.modified_accounts.push_back(
        {0x01_address, 3, 10, evmc::bytes{0x60, 0x00}, {{slotKey(1), slotKey(7)}}});
    reference.applyDiff(refDiff);

    EXPECT_EQ(bcos::evm::stateRootOf(bridge), bcos::evm::stateRootOf(reference));
}

TEST(Storage2Ledger, VisitAccountsSkipsLogicallyDeletedStorageSlot)
{
    LogicalDeleteStorage storage;
    LogicalDeleteAccount acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    const auto slot = slotKey(1);
    bcos::task::syncWait(acc.setStorage(slot, slotKey(9)));

    // 底层直接逻辑删除该槽行:removeOne 不带 BYPASS_LOGICAL_DELETE_TYPE 标签 → 在
    // LOGICAL_DELETION 存储上保留 DELETED_TYPE 墓碑,而非物理擦除(与生产
    // MultiLayerStorage 的逻辑删除路径同语义)。
    const auto tableName = tableNameOf(0x01_address);
    std::string_view slotKeyView(reinterpret_cast<const char*>(slot.bytes), sizeof(slot.bytes));
    bcos::task::syncWait(bcos::storage2::removeOne(
        storage, bcos::executor_v1::StateKeyView{tableName, slotKeyView}));

    Storage2Ledger<LogicalDeleteStorage> bridge(storage);
    bool sawAccount = false;
    std::map<evmc::bytes32, evmc::bytes32> seenStorage;
    const bool ok = bridge.visitAccounts([&](const auto& av) {
        sawAccount = true;
        seenStorage = av.storage;
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    EXPECT_TRUE(sawAccount);
    EXPECT_TRUE(seenStorage.empty());  // 墓碑跳过:不当零值毒旗,也不当槽出现
}

TEST(Storage2Ledger, VisitAccountsSkipsLogicallyDeletedAccountMarker)
{
    LogicalDeleteStorage storage;
    LogicalDeleteAccount acc1(storage, 0x01_address, false);
    LogicalDeleteAccount acc2(storage, 0x02_address, false);
    bcos::task::syncWait(acc1.create());
    bcos::task::syncWait(acc2.create());

    // SYS_TABLES 标记行本身也逻辑删除(同上,不带 BYPASS 标签)——验证账户枚举扫描
    // (visitAccountsImpl 的 SYS_TABLES /apps/ 前缀扫描)同样正确跳过墓碑,而不仅是
    // fetchAllStorage 的账户内槽扫描。
    const auto tableName1 = tableNameOf(0x01_address);
    bcos::task::syncWait(bcos::storage2::removeOne(
        storage, bcos::executor_v1::StateKeyView{bcos::ledger::SYS_TABLES, tableName1}));

    Storage2Ledger<LogicalDeleteStorage> bridge(storage);
    std::vector<evmc::address> visited;
    const bool ok = bridge.visitAccounts([&](const auto& av) {
        visited.push_back(av.addr);
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    ASSERT_EQ(visited.size(), 1U);
    EXPECT_EQ(visited[0], 0x02_address);
}

// (q) 键分类(design §6):ALIVE/FROZEN/SHARD/ABI 手工注入不误判为槽、不误报毒旗;
//     17 字节未知键 → poisoned()。
TEST(Storage2Ledger, VisitAccountsIgnoresKnownNonSlotFields)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    bcos::task::syncWait(acc.setStorage(slotKey(1), slotKey(2)));

    const auto tableName = tableNameOf(0x01_address);
    using Fields = bcos::ledger::ACCOUNT_TABLE_FIELDS;
    for (const auto field : {Fields::ALIVE, Fields::FROZEN, Fields::SHARD, Fields::ABI})
    {
        bcos::task::syncWait(
            bcos::storage2::writeOne(storage, bcos::executor_v1::StateKeyView{tableName, field},
                bcos::storage::Entry(std::string("1"))));
    }

    Storage2Ledger<MutableStorage> bridge(storage);
    std::map<evmc::bytes32, evmc::bytes32> seenStorage;
    bool sawAccount = false;
    const bool ok = bridge.visitAccounts([&](const auto& av) {
        sawAccount = true;
        seenStorage = av.storage;
        return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_FALSE(bridge.poisoned());
    ASSERT_TRUE(sawAccount);
    // 唯一的真槽是 slotKey(1)→slotKey(2);ALIVE/FROZEN/SHARD/ABI 四行必须不出现在槽集里。
    ASSERT_EQ(seenStorage.size(), 1U);
    EXPECT_EQ(seenStorage.at(slotKey(1)), slotKey(2));
}

TEST(Storage2Ledger, VisitAccountsUnknownKeyPoisons)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());

    const auto tableName = tableNameOf(0x01_address);
    const std::string unknownKey(17, 'x');  // 既非已知字段名,也非 32 字节槽键
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, bcos::executor_v1::StateKeyView{tableName, unknownKey},
            bcos::storage::Entry(std::string("v"))));

    Storage2Ledger<MutableStorage> bridge(storage);
    const bool ok = bridge.visitAccounts([](const auto&) { return true; });
    EXPECT_FALSE(ok);
    EXPECT_TRUE(bridge.poisoned());
}

// ══════════ 终审批 4:B4-4 visitAccounts 的 2× 无谓开销 ══════════
//
// visitAccountsImpl 此前对每个账户 (a) 直接调私有 fetchAccount、绕过写穿的 m_accountCache,
// (b) 在 fetchAccount 里跑 probeHasStorage——而 has_storage 既不在 AccountView 里、也不被
// stateRootOf 路径读取。两项都是纯浪费,且都乘在 O(层数) 上(每次 range 要为每一层各建一个
// 迭代器)。
//
// 断言用**精确计数**而非"更少即可":精确值才有判别力——任一优化回退、或读模式发生未经审阅
// 的变化,计数都会变。若将来因合法原因变动,更新数值的同时必须在报告说明变动来源
// (brief 明文允许该做法)。数值来自本轮实测,并已用"注释掉修复→翻红"自验反向确认:
// 恢复旧行为后两个断言各自变大且测试翻红。
TEST(Storage2Ledger, VisitAccountsUsesCacheAndSkipsHasStorageProbe)
{
    // 一个带存储槽的账户:有槽才谈得上 probeHasStorage 真的扫了东西。
    MutableStorage rawStorage;
    {
        Storage2Ledger<MutableStorage> seeder(rawStorage);
        evmone::state::StateDiff seedDiff;
        seedDiff.modified_accounts.push_back(
            {0x01_address, 3, 10, evmc::bytes{0x60, 0x00}, {{slotKey(1), slotKey(7)}}});
        seeder.applyDiff(seedDiff);
        ASSERT_FALSE(seeder.poisoned());
    }

    // 冷缓存:visitAccounts 自己发起的全部读。
    std::size_t coldVisitReads = 0;
    {
        bcos::evm::test::CountingStorage<MutableStorage> counting(rawStorage);
        Storage2Ledger<decltype(counting)> bridge(counting);
        const auto before = counting.readCount;
        const bool ok = bridge.visitAccounts([](const auto&) { return true; });
        ASSERT_TRUE(ok);
        ASSERT_FALSE(bridge.poisoned());
        coldVisitReads = counting.readCount - before;
    }

    // 热缓存:先 get_account 把该账户读进写穿缓存(执行期本来就会发生),再 visitAccounts。
    std::size_t warmVisitReads = 0;
    {
        bcos::evm::test::CountingStorage<MutableStorage> counting(rawStorage);
        Storage2Ledger<decltype(counting)> bridge(counting);
        ASSERT_TRUE(bridge.get_account(0x01_address).has_value());
        const auto before = counting.readCount;
        const bool ok = bridge.visitAccounts([](const auto&) { return true; });
        ASSERT_TRUE(ok);
        ASSERT_FALSE(bridge.poisoned());
        warmVisitReads = counting.readCount - before;
    }

    // (1) 冷路:fetchAccount 的 existsOne+字段读 + fetchAllStorage 的 range + SYS_TABLES 扫描,
    //     但**不含** probeHasStorage 的那次 range。
    EXPECT_EQ(coldVisitReads, 6U) << "cold visitAccounts read count drifted";
    // (2) 热路:账户字段整段来自缓存,只剩 SYS_TABLES 扫描与 fetchAllStorage。
    EXPECT_EQ(warmVisitReads, 2U) << "warm visitAccounts read count drifted";
    // (3) 关系断言:即使将来两个精确值都合法地变了,"命中缓存必须更省"这条关系也必须成立——
    //     它是本条修复的语义,而具体数值只是它今天的取值。
    EXPECT_LT(warmVisitReads, coldVisitReads);
}

// (B4-4 复审 I-1)缓存不得被"字段没算完"的 Account 污染。
//
// `fetchAccountForVisit` 在缓存未命中时以 `computeHasStorage=false` 读取,并**刻意不把结果写
// 进 m_accountCache**。上一轮我把这个风险点写进了注释,却没配断言——审查者的反证实验证明这
// 是真空档:把 miss 结果 emplace 回缓存后,全量 237/237 照绿。本例即为那条缺失的断言(§11:
// 注释写明的风险点必须有会翻红的断言)。
//
// 后果链(非理论):`Account::has_storage` 默认 false(eth/state/state_view.hpp),
// `eth/state/state.cpp` 的 `has_initial_storage` 直接取它,进而影响 EIP-161/7610 空账户清除
// 与 CREATE 碰撞判定。今天的窗口窄(executeOpBlock 在 visitAccounts 之后不再 get_account),
// 但窄不等于不存在,而且窄窗口正是最容易在后续重排中被悄悄拉宽的那种。
TEST(Storage2Ledger, VisitAccountsMissDoesNotPoisonAccountCache)
{
    MutableStorage storage;
    {
        Storage2Ledger<MutableStorage> seeder(storage);
        evmone::state::StateDiff seedDiff;
        // 带槽账户:has_storage 的正确值是 true,默认值是 false —— 两者可区分,断言才有意义。
        seedDiff.modified_accounts.push_back(
            {0x01_address, 3, 10, evmc::bytes{0x60, 0x00}, {{slotKey(1), slotKey(7)}}});
        seeder.applyDiff(seedDiff);
        ASSERT_FALSE(seeder.poisoned());
    }

    Storage2Ledger<MutableStorage> bridge(storage);

    // visitAccounts 先跑:缓存全空,该账户走的正是 miss 分支(computeHasStorage=false)。
    const bool visited = bridge.visitAccounts([](const auto&) { return true; });
    ASSERT_TRUE(visited);
    ASSERT_FALSE(bridge.poisoned());

    // 随后的 get_account 必须看到**真实算过**的 has_storage,而不是 miss 分支的默认 false。
    auto account = bridge.get_account(0x01_address);
    ASSERT_TRUE(account.has_value());
    EXPECT_TRUE(account->has_storage)
        << "visitAccounts 的 miss 结果污染了 m_accountCache:get_account 拿到了一个 "
           "has_storage 未计算的 Account";
    EXPECT_FALSE(bridge.poisoned());
}

// ══════════ 终审批 9:零值槽语义统一("槽值为 0 ≡ 该槽不存在") ══════════
//
// 修复前两处判据方向相反,以太坊正解是两处都不对:
//   * fetchAllStorage 见到零值槽行 → throw → visitAccounts 毒旗 → stateRootOf 产物全废。
//     stateRootOf 正走 visitAccounts → visitAccountsImpl → fetchAllStorage,而生产账本
//     必然含零值槽行(transaction-executor HostContext::set 与 bcos-ledger 创世 alloc 导入
//     对零值照写不删),后果是节点每个 OP 块都 -32603。
//   * probeHasStorage 只看键长与墓碑、不看值 → 零值槽行让 has_storage 误真 →
//     eth/state/state.cpp `has_initial_storage` → host.cpp `is_create_collision` →
//     同一个 CREATE2 在 op-geth 成功而在本桥 INVALID(EIP-7610 误判)。
// 正解两处都是"跳过":零值槽既不 throw,也不算有内容。零值槽本就不进 trie
// (adapter/StateRootCompute.cpp::accountStorageRoot 与 opstack/OpBlockSeal.cpp::opStorageRoot
// 都是 is_zero → continue),所以"跳过"与"该槽不存在"建出的根字节相同——(z1) 钉的就是这条。
//
// fixture 说明:本组用 EVMAccount::setStorage(key, evmc_bytes32{}) 直接落一行 32 字节全零的槽
// 行——这是**绕过 applyDiff** 的写入(生产写路径正是这样),也是构造该布局的唯一手段;
// applyDiff 自己永远走不到这个形状(契约②:零值 = removeOne),那条不变量现在由
// applyModifiedEntry 的删槽后置回读守护,见 (z6)。

// (z6) 用的装饰器是 support/LeakyDeleteStorage.h(removeOne 空操作、其余算子直通)。它此前是
// 本文件内的一个匿名命名空间局部类,终审批 9 F-1 抽成共享头——因为**同一注入现在要在两个层级
// 各出一次场**:本文件断言桥层 throw + 置毒旗,OpSchedulerImplTest 断言同一注入在
// executeOpBlock 里被分类成 OpStorageError(-32603)而不是 OpConsensusError(INVALID)。
using bcos::evm::test::LeakyDeleteStorage;

// (z1) 本批最核心的一条:stateRootOf 在含零值槽的账户表上不再毒旗,且算出的根与"该槽根本
//      不存在"时逐字节相等 —— 同时钉死"不 throw"(旧行为在此翻红)和"不进 trie"。
TEST(Storage2Ledger, StateRootIgnoresZeroValuedSlotRow)
{
    // 甲:一个非零槽 + 一行直接落库的零值槽行。
    MutableStorage withZeroRow;
    {
        Account acc(withZeroRow, 0x01_address, false);
        bcos::task::syncWait(acc.create());
        bcos::task::syncWait(acc.setStorage(slotKey(1), slotKey(7)));
        bcos::task::syncWait(acc.setStorage(slotKey(2), evmc_bytes32{}));  // 零值槽行
    }
    // 乙:同一状态,但那个零值槽压根不存在。
    MutableStorage withoutZeroRow;
    {
        Account acc(withoutZeroRow, 0x01_address, false);
        bcos::task::syncWait(acc.create());
        bcos::task::syncWait(acc.setStorage(slotKey(1), slotKey(7)));
    }

    Storage2Ledger<MutableStorage> bridgeWithZeroRow(withZeroRow);
    Storage2Ledger<MutableStorage> bridgeWithoutZeroRow(withoutZeroRow);

    // 遍历本身必须成功、不毒旗(旧行为:fetchAllStorage throw → visitAccounts 返回 false 且毒旗)。
    std::map<evmc::bytes32, evmc::bytes32> seenStorage;
    const bool ok = bridgeWithZeroRow.visitAccounts([&](const auto& av) {
        seenStorage = av.storage;
        return true;
    });
    ASSERT_TRUE(ok);
    ASSERT_FALSE(bridgeWithZeroRow.poisoned()) << bridgeWithZeroRow.firstError();
    ASSERT_EQ(seenStorage.size(), 1U);  // 零值槽不进建根 map
    EXPECT_EQ(seenStorage.at(evmc::bytes32(slotKey(1))), evmc::bytes32(slotKey(7)));

    const auto rootWithZeroRow = bcos::evm::stateRootOf(bridgeWithZeroRow);
    const auto rootWithoutZeroRow = bcos::evm::stateRootOf(bridgeWithoutZeroRow);
    EXPECT_FALSE(bridgeWithZeroRow.poisoned());
    EXPECT_FALSE(bridgeWithoutZeroRow.poisoned());
    EXPECT_EQ(rootWithZeroRow, rootWithoutZeroRow);
    // 反向哨兵:根不是空状态的根,否则上面的相等可能是"两边都全废/都空"的空真。
    MutableStorage emptyStorage;
    Storage2Ledger<MutableStorage> emptyBridge(emptyStorage);
    EXPECT_NE(rootWithZeroRow, bcos::evm::stateRootOf(emptyBridge));
}

// (z2) 账户只有一个零值槽 → has_storage 必须为 false(EIP-7610 误判修复)。
TEST(Storage2Ledger, HasStorageFalseWhenOnlyZeroValuedSlot)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    bcos::task::syncWait(acc.setStorage(slotKey(1), evmc_bytes32{}));

    Storage2Ledger<MutableStorage> bridge(storage);
    auto account = bridge.get_account(0x01_address);
    // KEEP 契约不得被改坏:账户依然**存在**(不是 nullopt),只是没有存储。
    ASSERT_TRUE(account.has_value());
    EXPECT_FALSE(account->has_storage);
    EXPECT_FALSE(bridge.poisoned());
    // 单槽读路早就是"零值 ≡ 不存在"的口径,这里顺手钉死三处判据同口径。
    EXPECT_EQ(bridge.get_storage(0x01_address, evmc::bytes32(slotKey(1))), evmc::bytes32{});
    EXPECT_EQ(bridge.get_storage(0x01_address, evmc::bytes32(slotKey(9))), evmc::bytes32{});
}

// (z3) 防改过头:账户有非零槽 → has_storage 仍为 true。
TEST(Storage2Ledger, HasStorageTrueWithNonZeroSlotOnly)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    bcos::task::syncWait(acc.setStorage(slotKey(1), slotKey(7)));

    Storage2Ledger<MutableStorage> bridge(storage);
    auto account = bridge.get_account(0x01_address);
    ASSERT_TRUE(account.has_value());
    EXPECT_TRUE(account->has_storage);
    EXPECT_FALSE(bridge.poisoned());
}

// (z4) 零值槽 + 非零槽混合 → has_storage 为 true,且 stateRootOf 只计入非零槽。
//      零值行**排在非零行之前**(slotKey(1) 零、slotKey(2) 非零,ORDERED 存储按键序扫),
//      这样 probeHasStorage 必须真的往后继续扫,而不能在第一行就判负。
TEST(Storage2Ledger, MixedZeroAndNonZeroSlots)
{
    MutableStorage mixed;
    {
        Account acc(mixed, 0x01_address, false);
        bcos::task::syncWait(acc.create());
        bcos::task::syncWait(acc.setStorage(slotKey(1), evmc_bytes32{}));  // 先零
        bcos::task::syncWait(acc.setStorage(slotKey(2), slotKey(7)));      // 后非零
    }
    MutableStorage nonZeroOnly;
    {
        Account acc(nonZeroOnly, 0x01_address, false);
        bcos::task::syncWait(acc.create());
        bcos::task::syncWait(acc.setStorage(slotKey(2), slotKey(7)));
    }

    Storage2Ledger<MutableStorage> bridge(mixed);
    auto account = bridge.get_account(0x01_address);
    ASSERT_TRUE(account.has_value());
    EXPECT_TRUE(account->has_storage);

    std::map<evmc::bytes32, evmc::bytes32> seenStorage;
    ASSERT_TRUE(bridge.visitAccounts([&](const auto& av) {
        seenStorage = av.storage;
        return true;
    }));
    ASSERT_FALSE(bridge.poisoned()) << bridge.firstError();
    ASSERT_EQ(seenStorage.size(), 1U);
    EXPECT_EQ(seenStorage.begin()->first, evmc::bytes32(slotKey(2)));

    Storage2Ledger<MutableStorage> reference(nonZeroOnly);
    EXPECT_EQ(bcos::evm::stateRootOf(bridge), bcos::evm::stateRootOf(reference));
    EXPECT_FALSE(reference.poisoned());
}

// (z5) 零值槽 + 墓碑混合:新增的零值过滤是**叠加**在终审 I-1 的墓碑过滤之上的第二层,不是
//      替代——两层都在,has_storage 为 false,建根 map 为空,且不毒旗。
TEST(Storage2Ledger, ZeroValuedSlotAndTombstoneBothFiltered)
{
    LogicalDeleteStorage storage;
    LogicalDeleteAccount acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    const auto tombstoned = slotKey(1);
    bcos::task::syncWait(acc.setStorage(tombstoned, slotKey(9)));
    bcos::task::syncWait(acc.setStorage(slotKey(2), evmc_bytes32{}));  // 存活但零值

    // slotKey(1) 逻辑删除:保留 DELETED_TYPE 墓碑行,range() 仍会扫到它(不带
    // BYPASS_LOGICAL_DELETE_TYPE,与生产 MultiLayerStorage 同语义)。
    const auto tableName = tableNameOf(0x01_address);
    std::string_view tombstonedView(
        reinterpret_cast<const char*>(tombstoned.bytes), sizeof(tombstoned.bytes));
    bcos::task::syncWait(bcos::storage2::removeOne(
        storage, bcos::executor_v1::StateKeyView{tableName, tombstonedView}));

    Storage2Ledger<LogicalDeleteStorage> bridge(storage);
    auto account = bridge.get_account(0x01_address);
    ASSERT_TRUE(account.has_value());
    EXPECT_FALSE(account->has_storage);  // 墓碑层 + 零值层都得生效

    std::map<evmc::bytes32, evmc::bytes32> seenStorage;
    bool sawAccount = false;
    ASSERT_TRUE(bridge.visitAccounts([&](const auto& av) {
        sawAccount = true;
        seenStorage = av.storage;
        return true;
    }));
    EXPECT_FALSE(bridge.poisoned()) << bridge.firstError();
    EXPECT_TRUE(sawAccount);
    EXPECT_TRUE(seenStorage.empty());
}

// (z6) 不变量守护迁移后的归属地测试(方案 A):契约②"桥的写回从不留下零值槽行"这条不变量,
//      现在在**写**的时候就断言 —— 注入一个 removeOne 为空操作的存储,applyDiff 的零值分支
//      删不掉行,后置回读当场 throw。
//      为什么不是 setStorage 前的 assert(!is_zero(value)):applyModifiedEntry 的 if/else 结构
//      令那种断言永不可达、也就永不可测(六步自验无从翻红);后置回读校验的是**结果**,可达可测。
TEST(Storage2Ledger, ApplyDiffZeroSlotWriteBackLeakThrows)
{
    MutableStorage backend;
    LeakyDeleteStorage<MutableStorage> leaky(backend);

    // 先用直通的 writeOne 落一行非零槽(不经 removeOne,所以不受注入影响)。
    Storage2Ledger<LeakyDeleteStorage<MutableStorage>> bridge(leaky);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back({0x01_address, 1, 10, std::nullopt,
        {{evmc::bytes32(slotKey(1)), evmc::bytes32(slotKey(7))}}});
    ASSERT_NO_THROW(bridge.applyDiff(seedDiff));

    // 再写零 = 删槽:removeOne 被注入成空操作 → 行仍然存活 → 写路径守护必须响。
    evmone::state::StateDiff zeroDiff;
    zeroDiff.modified_accounts.push_back(
        {0x01_address, 1, 10, std::nullopt, {{evmc::bytes32(slotKey(1)), evmc::bytes32{}}}});
    EXPECT_THROW(bridge.applyDiff(zeroDiff), std::runtime_error);
    // 终审批 9 F-1:本守护抛的是"桥自己写回有 bug"= 本地故障 → 必须置毒旗,才能在
    // OpSchedulerImpl 里被分类成 OpStorageError(-32603)而不是 OpConsensusError(INVALID)。
    // 正例标识:消息含本层的判据文字;反例标识见 OpSchedulerImplTest 的分类用例。
    EXPECT_TRUE(bridge.poisoned());
    EXPECT_NE(bridge.firstError().find("write-back leak"), std::string_view::npos)
        << bridge.firstError();

    // 对照:同样的两枚 diff 打在正常存储上,不得抛(守护不能对合法写回误报)。
    MutableStorage healthy;
    Storage2Ledger<MutableStorage> healthyBridge(healthy);
    EXPECT_NO_THROW(healthyBridge.applyDiff(seedDiff));
    EXPECT_NO_THROW(healthyBridge.applyDiff(zeroDiff));
    EXPECT_FALSE(healthyBridge.poisoned());
}

// (z7) 协调者裁定要求的实测:(z6) 的后置回读判据用的是 `storage2::existsOne`,而 `removeOne`
//      在 LOGICAL_DELETION 存储(与生产 MultiLayerStorage 同语义)上写的是 **DELETED_TYPE 墓碑**
//      而非物理擦除。若 `existsOne` 对墓碑返回 true,(z6) 那条守护就会在**每一次正常的零值写入**
//      上误报 —— 不是"会响的守护",而是"永远在响"。本例把这件事测掉,而不是靠推理:
//        第一段:直接测原语 —— removeOne 后 range() 仍能扫到该行(墓碑物理存在),而 existsOne
//                对同一个键返回 false(层间解析把 DELETED_TYPE 映射为"不存在")。
//        第二段:测集成 —— 在 LOGICAL_DELETION 存储上跑 applyDiff 的零值分支,必须不抛。
//      注:后置回读不能写成 "readOne + liveContent()":liveContent() 判别的是 range() 交出的
//      **原始变体** storage2::StorageValueType<Value>;readOne/existsOne 已经在层间解析里做完了
//      同一件事(墓碑 → nullopt),它们的返回值里没有变体可判。两者同源、不重复。
TEST(Storage2Ledger, ExistsOneTreatsTombstoneAsAbsentSoZeroWriteDoesNotFalseFire)
{
    LogicalDeleteStorage storage;
    LogicalDeleteAccount acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    const auto slot = slotKey(1);
    bcos::task::syncWait(acc.setStorage(slot, slotKey(9)));

    const auto tableName = tableNameOf(0x01_address);
    std::string_view slotView(reinterpret_cast<const char*>(slot.bytes), sizeof(slot.bytes));
    const bcos::executor_v1::StateKeyView slotStateKey{tableName, slotView};

    ASSERT_TRUE(bcos::task::syncWait(bcos::storage2::existsOne(storage, slotStateKey)));
    bcos::task::syncWait(bcos::storage2::removeOne(storage, slotStateKey));

    // ── 第一段:原语实测 ───────────────────────────────────────────────────────
    // 墓碑行物理存在:range() 一定还能扫到这个 32 字节键。
    bool rangeStillSeesTheRow = false;
    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        auto iterator = co_await bcos::storage2::range(storage, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            bcos::executor_v1::StateKeyView view(std::get<0>(*item));
            auto [table, fieldKey] = view.get();
            if (table != tableName)
                break;
            if (fieldKey == slotView)
                rangeStillSeesTheRow = true;
        }
    }());
    EXPECT_TRUE(rangeStillSeesTheRow)
        << "LOGICAL_DELETION 存储上 removeOne 应保留墓碑行,本例的前提不成立";
    // 而 existsOne 必须判"不存在" —— 这正是后置回读判据可用的依据。
    EXPECT_FALSE(bcos::task::syncWait(bcos::storage2::existsOne(storage, slotStateKey)))
        << "existsOne 把 DELETED_TYPE 墓碑当成存在:applyModifiedEntry 的删槽后置回读会在每一次"
           "正常的零值写入上误报,必须换判据";

    // ── 第二段:集成实测 —— 零值分支在墓碑语义存储上不得误报 ─────────────────────
    Storage2Ledger<LogicalDeleteStorage> bridge(storage);
    evmone::state::StateDiff nonZeroDiff;
    nonZeroDiff.modified_accounts.push_back(
        {0x01_address, 1, 10, std::nullopt, {{evmc::bytes32(slot), evmc::bytes32(slotKey(9))}}});
    ASSERT_NO_THROW(bridge.applyDiff(nonZeroDiff));

    evmone::state::StateDiff zeroDiff;
    zeroDiff.modified_accounts.push_back(
        {0x01_address, 1, 10, std::nullopt, {{evmc::bytes32(slot), evmc::bytes32{}}}});
    EXPECT_NO_THROW(bridge.applyDiff(zeroDiff));

    auto account = bridge.get_account(0x01_address);
    ASSERT_TRUE(account.has_value());
    EXPECT_FALSE(account->has_storage);
    EXPECT_FALSE(bridge.poisoned());
}

// (z8) (z7) 的孪生用例,跑在 **MultiLayerStorage::View** 之上(终审批 9 F-2,复审跟进第 2 条)。
//
// 为什么必须有这一条:(z7) 钉的是 `MemoryStorage::existsOne → toOptional` 那半边,而**生产**
// 的 `Storage2Ledger<Storage>` 里 Storage 是 `MultiLayerStorage::ViewType`
// (`OpSchedulerImpl.h:818`)。`View` **没有** `existsOne` 成员,所以 `storage2::existsOne`
// 走的是回退路径 `View::readOne → getValue<Value> → DELETED_TYPE → nullopt → false` ——
// 与 (z7) 钉的**完全是另一段代码**。复审的注入实验(INJ-E)证明了这条缺口是真的:给 `View`
// 加一个"省掉 Value 构造"的 `existsOne` 成员
//     auto existsOne(const auto& key) -> task::Task<bool>
//     { co_return !std::holds_alternative<storage2::NOT_EXISTS_TYPE>(co_await readOneRaw(key)); }
// (一个非常可能真实发生的性能优化)——全套用例**零红**,而生产行为已经坏成"每一次合法的
// 零值写入都抛" = 每块 -32603。对照实验 INJ-E′(同一个成员改成无条件 `co_return true`)是
// 12 红,证明该成员确实被 `storage2::existsOne` 选中、确实走到桥上,INJ-E 的零红不是空实验。
// 本用例就是补上那个零:它在 INJ-E 下必须翻红。
//
// 判据与 (z7) 同构、两段:
//   第一段:原语 —— view 上 removeOne 后,range() 仍能扫到该行(墓碑物理存在于可变层),而
//           storage2::existsOne 对同一个键必须返回 false(层间解析把 DELETED_TYPE 判为不存在)。
//   第二段:集成 —— 在 view 上跑 applyDiff 的零值分支,必须不抛、不置毒旗。
// 第二段才是钉住"后果"的那一半(守护误报 = 生产每块失败),与 (z7) 段二同理。
//
// **不补"跨层墓碑"用例的理由(re-review R-3,结论是结构性不可达,不是覆盖缺口)**:
// 本 fixture 只有一层可变层 + backend,看似漏了"上层墓碑遮住下层实值"这一形状。它**不可能
// 发生**,三条独立成立:
//   (1) `removeOne` 永远写在**顶层可变层**,墓碑不可能出现在下层;
//   (2) `View::readOne` 自顶向下,命中非 NOT_EXISTS 即返回、**不再读下层**——顶层墓碑
//       (DELETED_TYPE)就是命中,下层实值根本读不到,遮不遮住无从谈起;
//   (3) `applyModifiedEntry` 里 `removeOne` 与后置回读 `existsOne` 是**相邻两条 co_await**,
//       中间没有 `pushMutable`/`fork` 的插层机会,顶层在这两步之间不会变。
// 所以补一条"跨层墓碑"用例只会得到一条**无判别力**的绿——它断言的形状构造不出来。
// 写在这里是为了拦住后人"顺手补全"的冲动。
TEST(Storage2Ledger, MlsViewExistsOneTreatsTombstoneAsAbsentSoZeroWriteDoesNotFalseFire)
{
    MlsFixture fixture;
    MlsAccount acc(fixture.view, 0x01_address, false);
    bcos::task::syncWait(acc.create());
    const auto slot = slotKey(1);
    bcos::task::syncWait(acc.setStorage(slot, slotKey(9)));

    const auto tableName = tableNameOf(0x01_address);
    std::string_view slotView(reinterpret_cast<const char*>(slot.bytes), sizeof(slot.bytes));
    const bcos::executor_v1::StateKeyView slotStateKey{tableName, slotView};

    ASSERT_TRUE(bcos::task::syncWait(bcos::storage2::existsOne(fixture.view, slotStateKey)));
    bcos::task::syncWait(bcos::storage2::removeOne(fixture.view, slotStateKey));

    // ── 第一段:原语实测(View 这一层)────────────────────────────────────────────
    bool rangeStillSeesTheRow = false;
    bcos::task::syncWait([&]() -> bcos::task::Task<void> {
        auto iterator = co_await bcos::storage2::range(fixture.view, bcos::storage2::RANGE_SEEK,
            bcos::executor_v1::StateKeyView{tableName, std::string_view{}});
        while (auto item = co_await iterator.next())
        {
            bcos::executor_v1::StateKeyView view(std::get<0>(*item));
            auto [table, fieldKey] = view.get();
            if (table != tableName)
                break;
            if (fieldKey == slotView)
                rangeStillSeesTheRow = true;
        }
    }());
    EXPECT_TRUE(rangeStillSeesTheRow)
        << "MLS 可变层(LOGICAL_DELETION)上 removeOne 应保留墓碑行,本例的前提不成立";
    EXPECT_FALSE(bcos::task::syncWait(bcos::storage2::existsOne(fixture.view, slotStateKey)))
        << "View 上的 storage2::existsOne 把 DELETED_TYPE 墓碑当成存在:applyModifiedEntry 的"
           "删槽后置回读会在**生产**路径上每一次合法零值写入时误报(= 每块 -32603)";

    // ── 第二段:集成实测 —— 生产存储形态下零值分支不得误报 ─────────────────────────
    Storage2Ledger<MlsViewType> bridge(fixture.view);
    evmone::state::StateDiff nonZeroDiff;
    nonZeroDiff.modified_accounts.push_back(
        {0x01_address, 1, 10, std::nullopt, {{evmc::bytes32(slot), evmc::bytes32(slotKey(9))}}});
    ASSERT_NO_THROW(bridge.applyDiff(nonZeroDiff));

    evmone::state::StateDiff zeroDiff;
    zeroDiff.modified_accounts.push_back(
        {0x01_address, 1, 10, std::nullopt, {{evmc::bytes32(slot), evmc::bytes32{}}}});
    EXPECT_NO_THROW(bridge.applyDiff(zeroDiff));

    auto account2 = bridge.get_account(0x01_address);
    ASSERT_TRUE(account2.has_value());
    EXPECT_FALSE(account2->has_storage);
    EXPECT_FALSE(bridge.poisoned()) << bridge.firstError();
}

// (z9) 终审批 9 F-3:非 32 字节槽**值**这条路此前**全无测试** —— 不只是复审说的"有意的不对称
//      没有用例",连**终审 M-1 自己修的那个 throw**(fetchStorage / fetchAllStorage 的值长度
//      校验:长度 != 32 不再静默降级成合法零值)也从来没有过红绿见证。唯一沾边的既有用例是
//      EngineNewPayloadGateTest 的 StorageLayoutFaultIsInternalErrorNotInvalid,但它注入的是
//      29 字节**键**(op_gate_injected_layout_fault),走的是 `fieldKey.size() != 32` 那条
//      **完全不同**的 throw —— 键那条一直有覆盖,值那条一条都没有。
//
//      本例把不对称的三条腿一次钉死。三个**独立的 bridge 实例**:毒旗是黏的(只记第一条,
//      后续不覆盖),共用一个实例会让第 (3) 腿读到前两腿留下的旗,断言失去意义。
//        (1) 单槽读 get_storage → fetchStorage 的 length_error → 毒旗(M-1 的红绿见证 A);
//        (2) 全量读 visitAccounts → fetchAllStorage 的 length_error → 毒旗(见证 B;
//            stateRootOf 走的正是这条,即"每块作废"的那条路);
//        (3) has_storage 侧**刻意不对称**:isZeroSlotValue 只对"恰 32 字节全零"判真,长度不对
//            的内容被保守地算作"有内容" → has_storage=true 且**不**毒旗。这是有意为之而非
//            遗漏(EIP-7610 方向上偏保守 = 拒绝在其上 CREATE),权威的长度校验留给读路。
//            钉住它,免得下一个人把这两侧"顺手统一掉"。
TEST(Storage2Ledger, NonThirtyTwoByteSlotValueThrowsOnReadButCountsAsContentForHasStorage)
{
    // fixture:一行 32 字节合法槽**键** + 31 字节的槽**值**(既不是 32 字节,也不全零)。
    // 只能绕过 EVMAccount::setStorage 直接 writeOne —— setStorage 的形参就是 bytes32,
    // 造不出长度违规的值(与 (z1) 组"零值槽行只能直接落库"同一个理由)。
    const auto slot = slotKey(3);
    const std::string_view slotView(reinterpret_cast<const char*>(slot.bytes), sizeof(slot.bytes));
    const auto tableName = tableNameOf(0x01_address);
    const std::string shortValue(31, '\x7f');

    const auto seed = [&](MutableStorage& storage) {
        Account acc(storage, 0x01_address, false);
        bcos::task::syncWait(acc.create());
        bcos::task::syncWait(
            bcos::storage2::writeOne(storage, bcos::executor_v1::StateKeyView{tableName, slotView},
                bcos::storage::Entry(shortValue)));
    };

    // (1) 单槽读:fetchStorage 的值长度 throw → get_storage 的 catch → 毒旗 + 安全值(全零)。
    {
        MutableStorage storage;
        seed(storage);
        Storage2Ledger<MutableStorage> bridge(storage);
        EXPECT_EQ(bridge.get_storage(0x01_address, slot), evmc::bytes32{});
        EXPECT_TRUE(bridge.poisoned())
            << "31 字节槽值被 get_storage 当成合法零值静默吞掉了(终审 M-1 修的正是这个)";
        EXPECT_NE(bridge.firstError().find("fetchStorage: storage slot value size mismatch"),
            std::string_view::npos)
            << bridge.firstError();
    }

    // (2) 全量读:fetchAllStorage 的值长度 throw → visitAccounts 的 catch → 毒旗 + 提前终止。
    //     这条就是 stateRootOf 走的路径,毒旗一置整块作废。
    {
        MutableStorage storage;
        seed(storage);
        Storage2Ledger<MutableStorage> bridge(storage);
        EXPECT_FALSE(bridge.visitAccounts([](const auto&) { return true; }));
        EXPECT_TRUE(bridge.poisoned());
        EXPECT_NE(
            bridge.firstError().find("storage slot value size mismatch"), std::string_view::npos)
            << bridge.firstError();
    }

    // (3) has_storage 侧的有意不对称:同一行**不**触发 throw,而是被算作"有内容"。
    {
        MutableStorage storage;
        seed(storage);
        Storage2Ledger<MutableStorage> bridge(storage);
        auto account = bridge.get_account(0x01_address);
        ASSERT_TRUE(account.has_value());
        EXPECT_TRUE(account->has_storage)
            << "isZeroSlotValue 把长度违规的内容判成了'零/不存在':has_storage 少报会让 EIP-7610"
               "方向上**放行** CREATE,与保守取向相反";
        EXPECT_FALSE(bridge.poisoned()) << bridge.firstError();
    }
}

// (z10) 终审批 9 fix2 F2-1:读路的**消息保真**断言 —— 与 (d2) 的
//       expectOpStorageErrorWithMessage 对称的那一半。
//
//       背景(re-review 的 INJ-R2 实测,已推翻上一轮的"直抛 vs 穿协程"归因):本仓 typed-catch
//       的判别式是**异常类型族**,与协程边界无关 —— `std::runtime_error` 子树的 typed catch
//       **不生效**(逃逸到 catch(...)),`std::logic_error` 子树正常生效。读路四个方法此前只有
//       两级(std::exception + ...),于是**每一个 runtime_error 今天就在丢消息**:
//       firstError() 退化成 "unknown exception",运维看到的 -32603 理由为空内容。而读路是
//       毒旗的**主要**来源(get_account/get_account_code/get_storage/visitAccounts)。
//
//       本例钉的不是"毒旗置没置位"(那个两级四级都会置),而是**置位时消息还在不在**。
//       触发用的是 fetchAllStorage 的 "unknown key in account table" —— 一条 runtime_error,
//       也正是 re-review 实测到丢消息的那一条。既有的 VisitAccountsUnknownKeyPoisons 只断言
//       poisoned()==true,对消息一字未提,所以它在两级阶梯下也是绿的、拦不住这个退化。
TEST(Storage2Ledger, ReadPathRuntimeErrorKeepsOriginalMessage)
{
    MutableStorage storage;
    Account acc(storage, 0x01_address, false);
    bcos::task::syncWait(acc.create());

    const auto tableName = tableNameOf(0x01_address);
    const std::string unknownKey(17, 'x');  // 既非已知字段名,也非 32 字节槽键
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, bcos::executor_v1::StateKeyView{tableName, unknownKey},
            bcos::storage::Entry(std::string("v"))));

    Storage2Ledger<MutableStorage> bridge(storage);
    EXPECT_FALSE(bridge.visitAccounts([](const auto&) { return true; }));
    ASSERT_TRUE(bridge.poisoned());

    // 正例标识:必须是 fetchAllStorage 自己 throw 里的原文(含出事的表名,运维据此定位)。
    EXPECT_NE(bridge.firstError().find("unknown key in account table"), std::string_view::npos)
        << "读路 runtime_error 的消息被吞了,firstError()=" << bridge.firstError();
    EXPECT_NE(bridge.firstError().find(tableName), std::string_view::npos) << bridge.firstError();
    // 反例标识:不得退化成 catch(...) 那句无内容的兜底串。
    EXPECT_EQ(bridge.firstError().find("unknown exception"), std::string_view::npos)
        << "firstError() 退化成 catch(...) 的兜底串,-32603 的理由为空内容:" << bridge.firstError();
}
