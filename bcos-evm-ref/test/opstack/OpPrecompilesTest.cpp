#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPrecompiles.h>
#include <gtest/gtest.h>

using namespace bcos::evmref::opstack;

TEST(OpPrecompiles, P256VerifyGasOverrideAndAddressExtension)
{
    const evmc::address addr{0x100};
    const auto& overrides = isthmusPrecompileOverrides();
    EXPECT_TRUE(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->gas_cost_override, 3450);
    EXPECT_EQ(entry->max_input_size, 0U);
}

TEST(OpPrecompiles, Bn256PairingInputLimitNoGasOverride)
{
    const evmc::address addr{0x08};
    const auto& overrides = isthmusPrecompileOverrides();
    EXPECT_TRUE(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->max_input_size, 112687U);
    EXPECT_LT(entry->gas_cost_override, 0);
}

TEST(OpPrecompiles, BlsMsmPairingInputLimits)
{
    const auto& overrides = isthmusPrecompileOverrides();

    const evmc::address g1msm{0x0c};
    EXPECT_TRUE(overrides.contains(g1msm));
    const auto* g1Entry = overrides.find(g1msm);
    ASSERT_NE(g1Entry, nullptr);
    EXPECT_EQ(g1Entry->max_input_size, 513760U);
    EXPECT_LT(g1Entry->gas_cost_override, 0);

    const evmc::address g2msm{0x0e};
    EXPECT_TRUE(overrides.contains(g2msm));
    const auto* g2Entry = overrides.find(g2msm);
    ASSERT_NE(g2Entry, nullptr);
    EXPECT_EQ(g2Entry->max_input_size, 488448U);
    EXPECT_LT(g2Entry->gas_cost_override, 0);

    const evmc::address pairing{0x0f};
    EXPECT_TRUE(overrides.contains(pairing));
    const auto* pairingEntry = overrides.find(pairing);
    ASSERT_NE(pairingEntry, nullptr);
    EXPECT_EQ(pairingEntry->max_input_size, 235008U);
    EXPECT_LT(pairingEntry->gas_cost_override, 0);
}

TEST(OpPrecompiles, ForkConfigWiresPrecompiles)
{
    EXPECT_EQ(isthmusConfig().precompiles, &isthmusPrecompileOverrides());
}

TEST(OpPrecompiles, JovianLimitsStricterThanIsthmus)
{
    const auto* j08 = jovianPrecompileOverrides().find(evmc::address{0x08});
    const auto* i08 = isthmusPrecompileOverrides().find(evmc::address{0x08});
    ASSERT_NE(j08, nullptr);
    ASSERT_NE(i08, nullptr);
    EXPECT_EQ(j08->max_input_size, 81984u);
    EXPECT_LT(j08->max_input_size, i08->max_input_size);

    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0c})->max_input_size, 288960u);
    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0e})->max_input_size, 278784u);
    EXPECT_EQ(jovianPrecompileOverrides().find(evmc::address{0x0f})->max_input_size, 156672u);
    EXPECT_EQ(jovianConfig().precompiles, &jovianPrecompileOverrides());
    EXPECT_EQ(karstConfig().precompiles, &jovianPrecompileOverrides());
}
