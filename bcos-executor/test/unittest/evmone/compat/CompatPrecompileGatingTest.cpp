/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-P*: BLS / p256verify precompile gating via HostContext::callBuiltInPrecompiled.
 *  @note Top-level TX to 0x0b does not enter this path; integration must call the host hook.
 *  @file CompatPrecompileGatingTest.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::test
{
namespace
{
using compat::compatMakeModexpInput;

// First valid vector from https://eips.ethereum.org/assets/eip-2537/add_G1_bls.json
// ("bls_g1add_g1+p1").
bytes blsG1AddValidInputG1PlusP1()
{
    return bcos::fromHex(
        "0000000000000000000000000000000017f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f17"
        "1bac586c55e83ff97a1aeffb3af00adb22c6bb"
        "0000000000000000000000000000000008b3f481e3aaa0f1a09e30ed741d8ae4fcf5e095d5d00af600db18cb2c"
        "04b3edd03cc744a2888ae40caa232946c5e7e1"
        "00000000000000000000000000000000112b98340eee2777cc3c14163dea3ec97977ac3dc5c70da32e6e87578f"
        "44912e902ccef9efe28d4a78b8999dfbca9426"
        "00000000000000000000000000000000186b28d92356c4dfec4b5201ad099dbdede3781f8998ddf929b4cd7756"
        "192185ca7b8f4ef7088f813270ac3d48868a21");
}

bytes blsG1AddExpectedG1PlusP1()
{
    return bcos::fromHex(
        "000000000000000000000000000000000a40300ce2dec9888b60690e9a41d3004fda4886854573974fab73b046"
        "d3147ba5b7a5bde85279ffede1b45b3918d82d"
        "0000000000000000000000000000000006d3d887e9f53b9ec4eb6cedf5607226754b07c01ace7834f57f3e7315"
        "faefb739e59018e22c492006190fba4a870025");
}

bytes p256verifyValidSignatureInput()
{
    bytes input;
    input += bcos::fromHex("bb5a52f42f9c9261ed4361f59422a1e30036e7c32b270c8807a419feca605023");
    input += bcos::fromHex("2ba3a8be6b94d5ec80a6d9d1190a436effe50d85a1eee859b8cc6af9bd5c2e18");
    input += bcos::fromHex("4cd60b855d442f5b3c7b11eb6c4e0ae7525fe710fab9aa7c77a67f79e6fadd76");
    input += bcos::fromHex("2927b10512bae3eddcfe467828128bad2903269919f7086069c8c4df6c732838");
    input += bcos::fromHex("c7787964eaac00e5921fb1498a60f4606766b3d9685001558d1a974e7341513e");
    return input;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_FIXTURE_TEST_SUITE(CompatPrecompileGating, CompatHostContextFixture)

BOOST_AUTO_TEST_CASE(FC_P_bls_revert_without_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(FC_P_p256verify_revert_without_osaka)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::P256VERIFY), bytes{});
    BOOST_CHECK_EQUAL(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.gas_left, 0);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_reaches_precompile_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsG1Add);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0));
    // Past Prague gate: not the early EVMC_REVERT path. Invalid input may succeed or fail inside.
    BOOST_CHECK_NE(r.status_code, EVMC_REVERT);
    BOOST_CHECK(r.status_code == EVMC_SUCCESS || r.status_code == EVMC_INTERNAL_ERROR);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_ok_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto in = blsG1AddValidInputG1PlusP1();
    BOOST_REQUIRE_EQUAL(in.size(), 256u);
    auto const expected = blsG1AddExpectedG1PlusP1();
    BOOST_REQUIRE_EQUAL(expected.size(), 128u);

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsG1Add);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), std::move(in));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, expected.size());
    BOOST_CHECK(std::memcmp(r.output_data, expected.data(), expected.size()) == 0);
}

BOOST_AUTO_TEST_CASE(FC_P_p256verify_ok_with_osaka)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto input = p256verifyValidSignatureInput();
    BOOST_REQUIRE_EQUAL(input.size(), 160u);

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::osakaEnabled(), CompatEvmAttach::P256Verify);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::P256VERIFY), std::move(input));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, 32u);
    for (size_t i = 0; i < 31; ++i)
    {
        BOOST_CHECK_EQUAL(r.output_data[i], 0);
    }
    BOOST_CHECK_EQUAL(r.output_data[31], 1);
}

BOOST_AUTO_TEST_CASE(FC_P_legacy_modexp_still_works)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    // London / no Cancun flags: EIP-2537 BLS / Osaka p256 are irrelevant; modexp (0x05) must still
    // run.
    auto host =
        makeCompatHostContext(*this, CompatFeatureProfile::legacyLondon(), CompatEvmAttach::Modexp);
    auto data = compatMakeModexpInput({0x07}, {0x00}, {0x0b});  // 7^0 mod 11 = 1
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::MODEXP), std::move(data));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, 1u);
    BOOST_CHECK_EQUAL(r.output_data[0], 1);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatPrecompileGating
BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
