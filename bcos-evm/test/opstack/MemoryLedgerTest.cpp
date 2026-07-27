#include <bcos-evm/ledger/MemoryLedger.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
using namespace bcos::evm::ledger;
using namespace evmc::literals;

TEST(MemoryLedger, KeepContractEmptyAccountIsNotNullopt)
{
    MemoryLedger l;
    l.accounts()[0x01_address];  // 存在但空
    auto acc = l.get_account(0x01_address);
    ASSERT_TRUE(acc.has_value());  // KEEP:不折叠
    EXPECT_EQ(acc->nonce, 0u);
    EXPECT_EQ(acc->balance, intx::uint256{0});
    EXPECT_FALSE(acc->has_storage);
    EXPECT_FALSE(l.get_account(0x02_address).has_value());  // 不存在=nullopt
}
TEST(MemoryLedger, HasStorageIsDynamic)
{
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back(
        {0x01_address, 1, 1, std::nullopt, {{0x01_bytes32, 0x02_bytes32}}});
    l.applyDiff(d);
    EXPECT_TRUE(l.get_account(0x01_address)->has_storage);
    evmone::state::StateDiff d2;  // 契约②:写零=删槽 → has_storage 翻 false
    d2.modified_accounts.push_back(
        {0x01_address, 1, 1, std::nullopt, {{0x01_bytes32, 0x00_bytes32}}});
    l.applyDiff(d2);
    EXPECT_FALSE(l.get_account(0x01_address)->has_storage);
}
TEST(MemoryLedger, ApplyDiffThreeContracts)
{
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back(
        {0x01_address, 5, 100, evmc::bytes{0x60, 0x00}, {{0x01_bytes32, 0x02_bytes32}}});
    l.applyDiff(d);
    EXPECT_EQ(l.get_account_code(0x01_address), (evmc::bytes{0x60, 0x00}));
    EXPECT_EQ(l.get_account(0x01_address)->code_hash, evmone::keccak256(evmc::bytes{0x60, 0x00}));
    evmone::state::StateDiff d3;  // 契约③:code 无值不覆写
    d3.modified_accounts.push_back({0x01_address, 6, 100, std::nullopt, {}});
    l.applyDiff(d3);
    EXPECT_EQ(l.get_account_code(0x01_address), (evmc::bytes{0x60, 0x00}));
    evmone::state::StateDiff d4;  // 契约①:删除
    d4.deleted_accounts.push_back(0x01_address);
    l.applyDiff(d4);
    EXPECT_FALSE(l.get_account(0x01_address).has_value());
}
TEST(MemoryLedger, StrictTripwireOnGhostDelete)
{
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.deleted_accounts.push_back(0xdd_address);  // view 中不存在
    EXPECT_THROW(l.applyDiff(d), std::runtime_error);
}
TEST(MemoryLedger, EnsureExistsUnconditional)
{  // spec §5:空 entry 也落账
    MemoryLedger l;
    evmone::state::StateDiff d;
    d.modified_accounts.push_back({0x01_address, 0, 0, std::nullopt, {}});
    l.applyDiff(d);
    EXPECT_TRUE(l.get_account(0x01_address).has_value());
}
