#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPrecompiles.h>
#include <gtest/gtest.h>

using namespace bcos::evmref::opstack;

TEST(OpForkSchedule, IsthmusMapsToPrague)
{
    const auto& cfg = isthmusConfig();
    EXPECT_EQ(cfg.fork, OpFork::Isthmus);
    EXPECT_EQ(cfg.rev, EVMC_PRAGUE);
    EXPECT_TRUE(cfg.disable_prague_requests);
    EXPECT_TRUE(cfg.has_operator_fee);
}

TEST(OpForkSchedule, JovianAndKarstConfigs)
{
    const auto& j = jovianConfig();
    EXPECT_EQ(j.fork, OpFork::Jovian);
    EXPECT_EQ(j.rev, EVMC_PRAGUE);
    EXPECT_TRUE(j.has_operator_fee);
    EXPECT_TRUE(j.has_jovian_operator_formula);
    EXPECT_TRUE(j.has_da_footprint);
    EXPECT_TRUE(j.disable_prague_requests);
    EXPECT_NE(j.precompiles, nullptr);

    const auto& k = karstConfig();
    EXPECT_EQ(k.fork, OpFork::Karst);
    EXPECT_EQ(k.rev, j.rev);
    EXPECT_EQ(k.has_operator_fee, j.has_operator_fee);
    EXPECT_EQ(k.has_jovian_operator_formula, j.has_jovian_operator_formula);
    EXPECT_EQ(k.has_da_footprint, j.has_da_footprint);
    EXPECT_EQ(k.precompiles, j.precompiles);
}

TEST(OpForkSchedule, IsthmusDisablesJovianFlags)
{
    const auto& i = isthmusConfig();
    EXPECT_FALSE(i.has_jovian_operator_formula);
    EXPECT_FALSE(i.has_da_footprint);
}

TEST(OpForkSchedule, PreIsthmusConfigsPinned)
{
    for (const auto* cfg : {&ecotoneConfig(), &fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        EXPECT_EQ(cfg->rev, EVMC_CANCUN);
        EXPECT_TRUE(cfg->disable_prague_requests);
        EXPECT_FALSE(cfg->has_operator_fee);
        EXPECT_FALSE(cfg->has_jovian_operator_formula);
        EXPECT_FALSE(cfg->has_da_footprint);
    }
    EXPECT_EQ(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于 Fjord/Granite/Holocene 表
    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        EXPECT_NE(cfg->precompiles, nullptr);  // 明细由 FjordOnward* 用例覆盖
    }
    EXPECT_EQ(ecotoneConfig().fork, OpFork::Ecotone);
    EXPECT_EQ(fjordConfig().fork, OpFork::Fjord);
    EXPECT_EQ(graniteConfig().fork, OpFork::Granite);
    EXPECT_EQ(holoceneConfig().fork, OpFork::Holocene);
    EXPECT_TRUE(ecotoneConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(fjordConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(graniteConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(holoceneConfig().has_ecotone_l1_formula);
}

TEST(OpForkSchedule, IsthmusPlusDisableEcotoneL1Formula)
{
    EXPECT_FALSE(isthmusConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(jovianConfig().has_ecotone_l1_formula);
    EXPECT_FALSE(karstConfig().has_ecotone_l1_formula);
}

// D-15：op-geth 自 Fjord 起 0x100 P256VERIFY 活跃（contracts.go:193，gas 3450 params:183）；
// D-11：bn256Pairing 112687 上限自 Granite 起（params:172，Holocene 沿用）
TEST(OpForkSchedule, FjordOnwardCarryP256VerifyAndGraniteCapsBn256)
{
    EXPECT_EQ(ecotoneConfig().precompiles, nullptr);  // Ecotone 早于两者

    for (const auto* cfg : {&fjordConfig(), &graniteConfig(), &holoceneConfig()})
    {
        ASSERT_NE(cfg->precompiles, nullptr);
        const auto* p256 = cfg->precompiles->find(evmc::address{0x100});
        ASSERT_NE(p256, nullptr);
        EXPECT_EQ(p256->gas_cost_override, 3450);
    }
    EXPECT_FALSE(fjordConfig().precompiles->contains(evmc::address{0x08}));  // cap 是 Granite 的
    for (const auto* cfg : {&graniteConfig(), &holoceneConfig()})
    {
        const auto* bn256 = cfg->precompiles->find(evmc::address{0x08});
        ASSERT_NE(bn256, nullptr);
        EXPECT_EQ(bn256->max_input_size, 112687u);
        EXPECT_EQ(bn256->gas_cost_override, -1);
        EXPECT_FALSE(cfg->precompiles->contains(evmc::address{0x0c}));  // BLS 是 PRAGUE 的
    }
}
