#include <bcos-evm/opstack/OpPredeploys.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace evmc::literals;

TEST(OpPredeploys, AddressesMatchOpStackNamespace)
{
    EXPECT_EQ(OP_L1_BLOCK, 0x4200000000000000000000000000000000000015_address);
    EXPECT_EQ(OP_GAS_PRICE_ORACLE, 0x420000000000000000000000000000000000000f_address);
    EXPECT_EQ(OP_SEQUENCER_FEE_VAULT, 0x4200000000000000000000000000000000000011_address);
    EXPECT_EQ(OP_BASE_FEE_VAULT, 0x4200000000000000000000000000000000000019_address);
    EXPECT_EQ(OP_L1_FEE_VAULT, 0x420000000000000000000000000000000000001a_address);
    EXPECT_EQ(OP_OPERATOR_FEE_VAULT, 0x420000000000000000000000000000000000001b_address);
    EXPECT_EQ(OP_DEPOSITOR, 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address);
}

TEST(OpPredeploys, SeedCreatesSixPredeployAccounts)
{
    evmone::test::TestState state;
    seedOpPredeploys(state);
    EXPECT_TRUE(state.contains(OP_L1_BLOCK));
    EXPECT_TRUE(state.contains(OP_GAS_PRICE_ORACLE));
    EXPECT_TRUE(state.contains(OP_SEQUENCER_FEE_VAULT));
    EXPECT_TRUE(state.contains(OP_BASE_FEE_VAULT));
    EXPECT_TRUE(state.contains(OP_L1_FEE_VAULT));
    EXPECT_TRUE(state.contains(OP_OPERATOR_FEE_VAULT));
    EXPECT_FALSE(state.contains(OP_DEPOSITOR));  // 合成 sender 不预填
}

TEST(OpPredeploys, VaultsHaveNonEmptyCode)
{
    evmone::test::TestState ts;
    seedOpPredeploys(ts);
    for (const auto& v :
        {OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT, OP_SEQUENCER_FEE_VAULT})
    {
        EXPECT_FALSE(ts[v].code.empty()) << "vault should have stub code";
    }
    // L1Block / oracle 的 code 不由本函数写入（harness setter 自管）。
    EXPECT_TRUE(ts[OP_L1_BLOCK].code.empty());
    EXPECT_TRUE(ts[OP_GAS_PRICE_ORACLE].code.empty());
}
