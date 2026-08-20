// Storage2State 桥接语义最小测试（PR2 内补 PR3 PoisonTest 未覆盖的 invariants）：
//  - 零值槽写入 → 行删除 + existsOne 复查（contract ②）
//  - EIP-161 空账户创建 guard → applyDiff throw（非 seeding）
//  - fetchAllStorage 未知短键行 → throw（isKnownAccountField 白名单——mainline MPT
//    BcosExtension 行跳过）
// 装配模式与 opstack-executor/tests（PR3）一致：MemoryStorage + StateKey + accountTableName。
#include <opstack-executor/Storage2State.h>
#include <opstack-executor/Storage2StateHelpers.h>

#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <string>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateKeyView;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

using evmc::literals::operator""_address;

constexpr evmc::address kAddr = 0x00000000000000000000000000000000deadbeef_address;

/// 写入一条账户表行（32 字节槽值）。
void seedSlot(MutableStorage& storage, evmc::address const& addr, std::string const& slotKey,
    std::string const& value)
{
    const std::string table = bcos::evm::evmstate::accountTableName(addr);
    bcos::storage::Entry e;
    e.set(value);
    bcos::task::syncWait(bcos::storage2::writeOne(storage, StateKey{table, slotKey}, std::move(e)));
}

/// 写入一条账户表字段行（任意字段名，非 32 字节槽键）。
void seedField(MutableStorage& storage, evmc::address const& addr, std::string_view field,
    std::string const& value)
{
    const std::string table = bcos::evm::evmstate::accountTableName(addr);
    bcos::storage::Entry e;
    e.set(value);
    bcos::task::syncWait(
        bcos::storage2::writeOne(storage, StateKey{table, std::string{field}}, std::move(e)));
}

/// 写入 SYS_CODE_BINARY 表的一条记录（key=codeHash hex, value=bytecode）。
void seedCodeBinary(
    MutableStorage& storage, std::string const& codeHashHex, std::string const& code)
{
    bcos::storage::Entry e;
    e.set(code);
    bcos::task::syncWait(bcos::storage2::writeOne(
        storage, StateKey{bcos::ledger::SYS_CODE_BINARY, codeHashHex}, std::move(e)));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Storage2StateBridgeTests)

/// 零值槽写入 → applyDiff 走 removeOne 删除该行，existsOne 复查确认不存在（contract ②：
/// 槽位值 0 ≡ 槽不存在）。
BOOST_AUTO_TEST_CASE(zero_slot_write_deletes_row)
{
    MutableStorage storage;
    const std::string table = bcos::evm::evmstate::accountTableName(kAddr);
    // 先 applyDiff(seeding=true) 建账户（写 SYS_TABLES 标记 + nonce=1，避免 EIP-161 guard），
    // 再写一个非零槽，最后 applyDiff 写零值槽验证删除。
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{0},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);
    const std::string slotKey(32, '\x01');
    seedSlot(storage, kAddr, slotKey, std::string(32, '\x2a'));

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    // applyDiff：零值槽写入（modified_storage 值为 0 = 删除）。
    evmc::bytes32 slotKey32{};
    std::memcpy(slotKey32.bytes, slotKey.data(), sizeof(slotKey32.bytes));
    evmone::state::StateDiff diff;
    // nonce=1：非 EIP-161 空账户（零值槽删除测试不能触发空账户 guard）。
    evmone::state::StateDiff::Entry entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{0},
        .code = std::nullopt,
        .modified_storage = {}};
    entry.modified_storage.emplace_back(slotKey32, evmc::bytes32{});
    diff.modified_accounts.push_back(entry);
    bridge.applyDiff(diff);
    // 行已删除：existsOne 复查为 false。
    auto exists =
        bcos::task::syncWait(bcos::storage2::existsOne(storage, StateKey{table, slotKey}));
    BOOST_CHECK(!exists);
}

/// EIP-161：新建全空账户（nonce=0, balance=0, 无 code）的 diff entry → applyDiff throw
/// （非 seeding；§6.4 D-6 协议违例）。
BOOST_AUTO_TEST_CASE(eip161_empty_account_creation_throws)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    evmone::state::StateDiff diff{
        .modified_accounts = {{kAddr, 0, intx::uint256{0}, std::nullopt, {}}},
        .deleted_accounts = {}};
    BOOST_CHECK_THROW(bridge.applyDiff(diff), std::runtime_error);
    // 错误分类契约的另一半：写回失败必须同时置 poison（OpSchedulerSeam 依此把故障分类为
    // OpStorageError/-32603 而非 INVALID）——只断言 throw 无法发现"rethrow 前 poison 被回归掉"。
    BOOST_CHECK(bridge.poisoned());
    BOOST_CHECK_MESSAGE(bridge.firstError().find("EIP-161-empty account") != std::string::npos,
        "poison message must name the EIP-161 guard: " + bridge.firstError());
    // seeding=true 豁免 guard（SeedPreState 快照路径）。
    bcos::evm::evmstate::Storage2State<MutableStorage> seedBridge(storage);
    BOOST_CHECK_NO_THROW(seedBridge.applyDiff(diff, /*seeding=*/true));
}

/// fetchAllStorage 对未知短键行（非 8 字段名、非 32 字节槽键）抛错——BCOS 扩展字段
/// （status/last_update/last_status）必须被 isKnownAccountField 接受（mainline MPT 跳过）。
BOOST_AUTO_TEST_CASE(unknown_short_key_row_throws)
{
    MutableStorage storage;
    // 先建账户（applyDiff seeding=true 写 SYS_TABLES 标记 + nonce=1 避免 EIP-161 guard）。
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{0},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);
    // 再写 status 扩展字段行（BCOS 扩展字段——应被 isKnownAccountField 白名单接受）。
    seedSlot(storage, kAddr, "status", "0");

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    // visitAccounts 触发 fetchAllStorage：status 行被跳过，不抛。
    bool visited = false;
    bridge.visitAccounts([&](const auto&) {
        visited = true;
        return true;
    });
    BOOST_CHECK(visited);
    BOOST_CHECK(!bridge.poisoned());

    // 对照组：完全未知的短键行（非白名单）→ throw + poison。
    MutableStorage storage2;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder2(storage2);
    seeder2.applyDiff(seedDiff, /*seeding=*/true);
    // 验证 SYS_TABLES 标记已写（visitAccounts 依赖它）。
    const std::string table = bcos::evm::evmstate::accountTableName(kAddr);
    const auto markerExists = bcos::task::syncWait(
        bcos::storage2::existsOne(storage2, StateKey{bcos::ledger::SYS_TABLES, table}));
    BOOST_CHECK_MESSAGE(markerExists, "SYS_TABLES marker must exist after seeding");
    // 诊断：手动 range 扫描 s_tables 的 /apps/ 前缀，确认标记可见。
    size_t markerCount = 0;
    {
        auto it = bcos::task::syncWait(bcos::storage2::range(storage2, bcos::storage2::RANGE_SEEK,
            StateKeyView{bcos::ledger::SYS_TABLES, bcos::ledger::SYS_DIRECTORY::USER_APPS}));
        while (auto item = bcos::task::syncWait(it.next()))
        {
            const auto& [k, v] = *item;
            StateKeyView kv(k);
            auto [t, tk] = kv.get();
            if (t != bcos::ledger::SYS_TABLES ||
                !tk.starts_with(bcos::ledger::SYS_DIRECTORY::USER_APPS))
                break;
            ++markerCount;
        }
    }
    BOOST_CHECK_MESSAGE(markerCount >= 1, "s_tables /apps/ range must yield the marker");
    seedSlot(storage2, kAddr, "not_a_field", "0");
    bcos::evm::evmstate::Storage2State<MutableStorage> bridge2(storage2);
    // 诊断：直接 range 扫描账户表，确认 not_a_field 行存在且可见。
    {
        auto it = bcos::task::syncWait(bcos::storage2::range(
            storage2, bcos::storage2::RANGE_SEEK, StateKeyView{table, std::string_view{}}));
        bool sawNotAField = false;
        while (auto item = bcos::task::syncWait(it.next()))
        {
            StateKeyView kv(std::get<0>(*item));
            auto [t, tk] = kv.get();
            if (t != table)
                break;
            if (tk == "not_a_field")
                sawNotAField = true;
        }
        BOOST_CHECK_MESSAGE(sawNotAField, "not_a_field row must be present in the account table");
    }
    bool visited2 = false;
    // 诊断：直接 get_storage 触发读路径（fetchAccount 应成功，不 poison）。
    evmc::bytes32 probeKey{};
    std::memset(probeKey.bytes, 0x01, sizeof(probeKey.bytes));
    (void)bridge2.get_storage(kAddr, probeKey);
    BOOST_CHECK_MESSAGE(
        !bridge2.poisoned(), "get_storage must not poison: " + std::string(bridge2.firstError()));
    // visitAccounts 是 noexcept——fetchAllStorage 对 not_a_field 行抛错被吞并 poison，
    // 返回 false；visitor 在 fetchAllStorage 之后，故不会被调用（visited2=false 是预期的）。
    const bool ok = bridge2.visitAccounts([&](const auto&) {
        visited2 = true;
        return true;
    });
    BOOST_CHECK(!ok);
    BOOST_CHECK(!visited2);
    BOOST_CHECK(bridge2.poisoned());
    BOOST_CHECK_MESSAGE(!bridge2.firstError().empty(), "poison must carry the error message");
    BOOST_CHECK_MESSAGE(
        bridge2.firstError().find("unknown key in account table") != std::string_view::npos,
        "poison message must name the unknown key: " + std::string(bridge2.firstError()));
}

/// TC-1: get_account / get_account_code / get_storage 读路径返回正确值。
/// seed 含 nonce/balance/code/storage 的 account → 逐一断言返回值。
BOOST_AUTO_TEST_CASE(read_paths_return_correct_values)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    // nonce=42, balance=0x1234
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 42,
        .balance = intx::uint256{0x1234},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);

    // 手动写入 storage slot：key=0x01*32, value=0xaa*32
    const std::string slotKey(32, '\x01');
    seedSlot(storage, kAddr, slotKey, std::string(32, '\xaa'));

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);

    // get_account: nonce, balance, has_storage
    auto acc = bridge.get_account(kAddr);
    BOOST_REQUIRE(acc.has_value());
    BOOST_CHECK_EQUAL(acc->nonce, 42u);
    BOOST_CHECK_EQUAL(intx::to_string(acc->balance), "4660");  // 0x1234 = 4660
    BOOST_CHECK(acc->has_storage);

    // get_account_code: 无 code → 空字节
    auto code = bridge.get_account_code(kAddr);
    BOOST_CHECK(code.empty());

    // get_storage: 已 seed 的 slot → 正确值
    evmc::bytes32 sKey{};
    std::memcpy(sKey.bytes, slotKey.data(), sizeof(sKey.bytes));
    auto val = bridge.get_storage(kAddr, sKey);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(val.bytes[0]), 0xaa);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(val.bytes[31]), 0xaa);

    // get_storage: 未设置的 slot → 全零
    evmc::bytes32 unsetKey{};
    unsetKey.bytes[0] = 0xff;
    auto unsetVal = bridge.get_storage(kAddr, unsetKey);
    BOOST_CHECK(evmc::is_zero(unsetVal));

    BOOST_CHECK(!bridge.poisoned());
}

/// fetchCode 对"codeHash 存在但 SYS_CODE_BINARY 缺 blob"→ throw + poison（账本数据损坏
/// 不得静默当无码账户执行）；keccak(empty) 是唯一的"无码"标记，缺行是正常路径。
BOOST_AUTO_TEST_CASE(missing_code_blob_poisons)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{0},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);

    // 对照组 1：codeHash = keccak(empty)、无 blob → 空码，不 poison（正常无码账户）。
    const auto emptyCodeHash = evmone::keccak256(evmc::bytes_view{});
    seedField(storage, kAddr, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH,
        std::string(
            reinterpret_cast<const char*>(emptyCodeHash.bytes), sizeof(emptyCodeHash.bytes)));
    bcos::evm::evmstate::Storage2State<MutableStorage> emptyBridge(storage);
    BOOST_CHECK(emptyBridge.get_account_code(kAddr).empty());
    BOOST_CHECK(!emptyBridge.poisoned());

    // 对照组 2：非空 codeHash、无 blob → poison（损坏账本，不得静默当无码账户）。
    const std::string corruptHash(32, '\xcd');
    seedField(storage, kAddr, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, corruptHash);
    bcos::evm::evmstate::Storage2State<MutableStorage> corruptBridge(storage);
    BOOST_CHECK(corruptBridge.get_account_code(kAddr).empty());
    BOOST_CHECK(corruptBridge.poisoned());
    BOOST_CHECK_MESSAGE(corruptBridge.firstError().find("code blob missing") != std::string::npos,
        "poison message must name the missing blob: " + corruptBridge.firstError());
}

/// TC-1 补充：含代码的 account → get_account_code 返回正确 bytecode，codeHash 正确。
BOOST_AUTO_TEST_CASE(read_path_with_code)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{0},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);

    // 手动写入 codeHash 字段（raw 32 bytes）+ SYS_CODE_BINARY（key=同一 raw bytes）。
    // fetchCode 读 codeHash entry 的 value → 用作 SYS_CODE_BINARY 的 key。
    const std::string codeHashRaw(32, '\xab');
    seedField(storage, kAddr, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, codeHashRaw);
    seedCodeBinary(storage, codeHashRaw, "hello");

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    auto code = bridge.get_account_code(kAddr);
    BOOST_CHECK_EQUAL(code.size(), 5u);
    BOOST_CHECK_EQUAL(std::string(code.begin(), code.end()), "hello");

    // codeHash 在 get_account 中正确传递
    auto acc = bridge.get_account(kAddr);
    BOOST_REQUIRE(acc.has_value());
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(acc->code_hash.bytes[0]), 0xab);
    BOOST_CHECK_EQUAL(static_cast<uint8_t>(acc->code_hash.bytes[31]), 0xab);
    BOOST_CHECK(!bridge.poisoned());
}

/// TC-2: applyDiff 后 cache write-through — get_account/get_storage 返回更新值。
BOOST_AUTO_TEST_CASE(cache_write_through_after_applyDiff)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 1,
        .balance = intx::uint256{100},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);

    // seed 一个 storage slot
    const std::string slotKey(32, '\x01');
    seedSlot(storage, kAddr, slotKey, std::string(32, '\x11'));

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);

    // applyDiff：更新 nonce/balance，删除旧 slot，写入新 slot
    evmc::bytes32 sKey{};
    std::memcpy(sKey.bytes, slotKey.data(), sizeof(sKey.bytes));
    evmc::bytes32 newKey{};
    newKey.bytes[0] = 0x02;
    evmone::state::StateDiff diff;
    evmone::state::StateDiff::Entry entry{kAddr, 5, intx::uint256{999}, std::nullopt, {}};
    entry.modified_storage.emplace_back(sKey, evmc::bytes32{});        // 删除旧 slot
    entry.modified_storage.emplace_back(newKey, evmc::bytes32{0x42});  // 新 slot
    diff.modified_accounts.push_back(entry);
    bridge.applyDiff(diff);

    // 立即验证：cache 应已 write-through
    auto acc = bridge.get_account(kAddr);
    BOOST_REQUIRE(acc.has_value());
    BOOST_CHECK_EQUAL(acc->nonce, 5u);
    BOOST_CHECK_EQUAL(intx::to_string(acc->balance), "999");

    auto oldSlot = bridge.get_storage(kAddr, sKey);
    BOOST_CHECK(evmc::is_zero(oldSlot));  // 已删除

    auto newSlot = bridge.get_storage(kAddr, newKey);
    BOOST_CHECK_EQUAL(newSlot.bytes[31], 0x42);  // 新值

    BOOST_CHECK(!bridge.poisoned());
}

/// TC-3: sharedError 块级毒化传播 — 一个实例 poison → 共享同一 error slot 的所有实例
/// report poisoned()=true。
BOOST_AUTO_TEST_CASE(shared_error_propagates_poison)
{
    auto sharedErr = std::make_shared<bcos::evm::evmstate::SharedErrorSlot>();
    MutableStorage storage;

    // 写入 SYS_TABLES 标记（fetchAccount 需要它）+ 一个畸形 codeHash（长度 ≠ 32 字节）
    // → fetchAccount 抛 length_error → poison 传播到 shared slot。
    const std::string table = bcos::evm::evmstate::accountTableName(kAddr);
    {
        bcos::storage::Entry marker;
        marker.set("1");
        bcos::task::syncWait(bcos::storage2::writeOne(
            storage, StateKey{bcos::ledger::SYS_TABLES, table}, std::move(marker)));
    }
    seedField(storage, kAddr, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, "short");

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge1(storage, sharedErr);
    bcos::evm::evmstate::Storage2State<MutableStorage> bridge2(storage, sharedErr);

    // bridge1 读取畸形 codeHash → length_error → poison
    bridge1.get_account(kAddr);
    BOOST_CHECK(bridge1.poisoned());
    BOOST_CHECK(!sharedErr->message.empty());
    BOOST_CHECK(
        bridge1.firstError().find("codeHash field size mismatch") != std::string_view::npos);

    // bridge2 共享同一 error slot → 也 report poisoned
    BOOST_CHECK(bridge2.poisoned());
    BOOST_CHECK_EQUAL(bridge2.firstError(), bridge1.firstError());
}

/// TC-4: applyDiff deleted_accounts 路径 — 删除含 code+storage 的 account →
/// get_account=nullopt, get_account_code=empty, get_storage=zero, visitAccounts 跳过。
BOOST_AUTO_TEST_CASE(deleted_account_path)
{
    MutableStorage storage;
    bcos::evm::evmstate::Storage2State<MutableStorage> seeder(storage);

    // seed account（含 nonce/balance/code/storage）
    evmone::state::StateDiff seedDiff;
    seedDiff.modified_accounts.push_back(evmone::state::StateDiff::Entry{.addr = kAddr,
        .nonce = 10,
        .balance = intx::uint256{500},
        .code = std::nullopt,
        .modified_storage = {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);
    const std::string slotKey(32, '\xcc');
    seedSlot(storage, kAddr, slotKey, std::string(32, '\xdd'));
    const std::string codeHashRaw(32, '\xee');
    seedField(storage, kAddr, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH, codeHashRaw);
    seedCodeBinary(storage, codeHashRaw, "bytecode");

    // 删除 account
    evmone::state::StateDiff delDiff;
    delDiff.deleted_accounts.push_back(kAddr);
    seeder.applyDiff(delDiff);

    // 验证：get_account = nullopt
    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    auto acc = bridge.get_account(kAddr);
    BOOST_CHECK(!acc.has_value());

    // 验证：get_account_code = 空
    auto code = bridge.get_account_code(kAddr);
    BOOST_CHECK(code.empty());

    // 验证：get_storage = zero
    evmc::bytes32 sKey{};
    std::memcpy(sKey.bytes, slotKey.data(), sizeof(sKey.bytes));
    auto val = bridge.get_storage(kAddr, sKey);
    BOOST_CHECK(evmc::is_zero(val));

    // 验证：visitAccounts 跳过已删除 account（SYS_TABLES 标记已删除）
    bool visited = false;
    bridge.visitAccounts([&](const auto&) {
        visited = true;
        return true;
    });
    BOOST_CHECK(!visited);
    BOOST_CHECK(!bridge.poisoned());
}

BOOST_AUTO_TEST_SUITE_END()
