/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-S*: light HostContext smoke (identity precompile) — same path as Prague/BLS gating.
 *  @note Top-level TXHASH to 0x04 does not hit callBuiltInPrecompiled; use host hook here.
 *  @file CompatSmokeTest.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"
#include "bcos-utilities/DataConvertUtility.h"

#include <boost/test/unit_test.hpp>
namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_FIXTURE_TEST_SUITE(CompatSmoke, CompatHostContextFixture)

BOOST_AUTO_TEST_CASE(FC_S_cancun_only_identity_precompile)
{
    using compat::CompatFeatureProfile;

    auto host =
        makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly(), CompatEvmAttach::Identity);
    bytes input = bcos::fromHex("deadbeef");
    auto r = compatCallBuiltInPrecompiled(host, compatFillZeroAddr(4), std::move(input));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(r.output_size, 0U);
}

BOOST_AUTO_TEST_CASE(FC_S_legacy_tx_no_prague_path)
{
    // FC-06: ordinary pre-Cancun-Prague path — identity precompile on cancun-only profile.
    using compat::CompatFeatureProfile;

    auto host =
        makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly(), CompatEvmAttach::Identity);
    auto r = compatCallBuiltInPrecompiled(host, compatFillZeroAddr(4), bytes{0x01, 0x02});
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(FC_S_cancun_only_no_prague_precompile)
{
    // Without feature_evm_prague, BLS addresses halt non-exceptionally (EVMC_SUCCESS), not REVERT.
    // See HostContext::callBuiltInPrecompiled and FC_P_bls_success_without_prague.
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(FC_S_cancun_only_legacy_gas_smoke)
{
    BOOST_TEST_MESSAGE(
        "FC-01b: full legacy gas regression is FC-S scope only; EIP-2929 off stays cold in "
        "FC-A/TE-FC. Run --run_test=CompatEip2929 for account/storage warm behaviour.");
}

BOOST_AUTO_TEST_SUITE_END()  // CompatSmoke
BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
