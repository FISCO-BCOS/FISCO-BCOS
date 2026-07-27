#include <bcos-evm/opstack/OpTransition.h>
#include <gtest/gtest.h>

using namespace bcos::evm::opstack;
using namespace evmc::literals;

TEST(OpDepositTx, ContractCreationHasNullTo)
{
    DepositTx tx{
        .source_hash = 0x01_bytes32,
        .from = 0x000000000000000000000000000000000000dead_address,
        .to = std::nullopt,  // nullopt = 合约创建
        .mint = intx::uint256{5},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    EXPECT_FALSE(tx.to.has_value());
    ASSERT_TRUE(tx.mint.has_value());
    EXPECT_EQ(*tx.mint, intx::uint256{5});
    EXPECT_FALSE(tx.is_system_tx);
}

TEST(OpDepositTx, AbsentMintDiffersFromZero)
{
    DepositTx tx{};                       // 值初始化：mint 默认 nullopt
    EXPECT_FALSE(tx.mint.has_value());    // absent ≠ 0：执行层据此跳过加余额
}

TEST(OpDepositTx, MintAndValueAreIndependent)
{
    DepositTx tx{};
    tx.mint = intx::uint256{7};           // mint 有值 → 无条件加 from 余额
    tx.value = intx::uint256{3};          // value → call 中转账（两字段独立）
    ASSERT_TRUE(tx.mint.has_value());
    EXPECT_NE(*tx.mint, tx.value);
}
