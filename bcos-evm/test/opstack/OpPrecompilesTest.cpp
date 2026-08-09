#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::evm::opstack;

BOOST_AUTO_TEST_SUITE(OpPrecompilesSuite)

BOOST_AUTO_TEST_CASE(P256VerifyGasOverrideAndAddressExtension)
{
    const evmc::address addr{0x100};
    const auto& overrides = isthmusPrecompileOverrides();
    BOOST_CHECK(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    BOOST_REQUIRE((entry) != nullptr);
    BOOST_CHECK_EQUAL(entry->gas_cost_override, 3450);
    BOOST_CHECK_EQUAL(entry->max_input_size, 0U);
}

BOOST_AUTO_TEST_CASE(Bn256PairingInputLimitNoGasOverride)
{
    const evmc::address addr{0x08};
    const auto& overrides = isthmusPrecompileOverrides();
    BOOST_CHECK(overrides.contains(addr));
    const auto* entry = overrides.find(addr);
    BOOST_REQUIRE((entry) != nullptr);
    BOOST_CHECK_EQUAL(entry->max_input_size, 112687U);
    BOOST_CHECK_LT(entry->gas_cost_override, 0);
}

BOOST_AUTO_TEST_CASE(BlsMsmPairingInputLimits)
{
    const auto& overrides = isthmusPrecompileOverrides();

    const evmc::address g1msm{0x0c};
    BOOST_CHECK(overrides.contains(g1msm));
    const auto* g1Entry = overrides.find(g1msm);
    BOOST_REQUIRE((g1Entry) != nullptr);
    BOOST_CHECK_EQUAL(g1Entry->max_input_size, 513760U);
    BOOST_CHECK_LT(g1Entry->gas_cost_override, 0);

    const evmc::address g2msm{0x0e};
    BOOST_CHECK(overrides.contains(g2msm));
    const auto* g2Entry = overrides.find(g2msm);
    BOOST_REQUIRE((g2Entry) != nullptr);
    BOOST_CHECK_EQUAL(g2Entry->max_input_size, 488448U);
    BOOST_CHECK_LT(g2Entry->gas_cost_override, 0);

    const evmc::address pairing{0x0f};
    BOOST_CHECK(overrides.contains(pairing));
    const auto* pairingEntry = overrides.find(pairing);
    BOOST_REQUIRE((pairingEntry) != nullptr);
    BOOST_CHECK_EQUAL(pairingEntry->max_input_size, 235008U);
    BOOST_CHECK_LT(pairingEntry->gas_cost_override, 0);
}

BOOST_AUTO_TEST_CASE(ForkConfigWiresPrecompiles)
{
    BOOST_CHECK_EQUAL(isthmusConfig().precompiles, &isthmusPrecompileOverrides());
}

BOOST_AUTO_TEST_CASE(JovianLimitsStricterThanIsthmus)
{
    const auto* j08 = jovianPrecompileOverrides().find(evmc::address{0x08});
    const auto* i08 = isthmusPrecompileOverrides().find(evmc::address{0x08});
    BOOST_REQUIRE((j08) != nullptr);
    BOOST_REQUIRE((i08) != nullptr);
    BOOST_CHECK_EQUAL(j08->max_input_size, 81984u);
    BOOST_CHECK_LT(j08->max_input_size, i08->max_input_size);

    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0c})->max_input_size, 288960u);
    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0e})->max_input_size, 278784u);
    BOOST_CHECK_EQUAL(
        jovianPrecompileOverrides().find(evmc::address{0x0f})->max_input_size, 156672u);
    BOOST_CHECK_EQUAL(jovianConfig().precompiles, &jovianPrecompileOverrides());
    BOOST_CHECK_EQUAL(karstConfig().precompiles, &jovianPrecompileOverrides());
}

BOOST_AUTO_TEST_SUITE_END()
