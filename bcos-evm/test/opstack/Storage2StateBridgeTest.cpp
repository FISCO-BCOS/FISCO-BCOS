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
    seedDiff.modified_accounts.push_back(
        evmone::state::StateDiff::Entry{kAddr, 1, intx::uint256{0}, std::nullopt, {}});
    seeder.applyDiff(seedDiff, /*seeding=*/true);
    const std::string slotKey(32, '\x01');
    seedSlot(storage, kAddr, slotKey, std::string(32, '\x2a'));

    bcos::evm::evmstate::Storage2State<MutableStorage> bridge(storage);
    // applyDiff：零值槽写入（modified_storage 值为 0 = 删除）。
    evmc::bytes32 slotKey32{};
    std::memcpy(slotKey32.bytes, slotKey.data(), sizeof(slotKey32.bytes));
    evmone::state::StateDiff diff;
    // nonce=1：非 EIP-161 空账户（零值槽删除测试不能触发空账户 guard）。
    evmone::state::StateDiff::Entry entry{kAddr, 1, intx::uint256{0}, std::nullopt, {}};
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
        .modified_accounts = {{kAddr, 0, intx::uint256{0}, std::nullopt, {}}}};
    BOOST_CHECK_THROW(bridge.applyDiff(diff), std::runtime_error);
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
    seedDiff.modified_accounts.push_back(
        evmone::state::StateDiff::Entry{kAddr, 1, intx::uint256{0}, std::nullopt, {}});
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

BOOST_AUTO_TEST_SUITE_END()
