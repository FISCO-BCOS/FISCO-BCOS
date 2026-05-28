/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-P*: BLS / p256verify precompile gating via HostContext::callBuiltInPrecompiled.
 *  @note Top-level TX to 0x0b does not enter this path; integration must call the host hook.
 *  @file CompatPrecompileGatingTest.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"
#include "bcos-executor/src/vm/EvmPrecompiledAddress.h"
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

bytes scalarOne32()
{
    bytes s(32, 0);
    s.back() = 1;
    return s;
}

bytes scalarZero32()
{
    return bytes(32, 0);
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
BOOST_AUTO_TEST_SUITE(CompatEvmPrecompiledAddress)

BOOST_AUTO_TEST_CASE(bls_range_byte_parse_no_stoul)
{
    using bcos::executor::isBLSPrecompileAddress;
    BOOST_CHECK(isBLSPrecompileAddress("000000000000000000000000000000000000000b"));
    BOOST_CHECK(isBLSPrecompileAddress("0000000000000000000000000000000000000011"));
    BOOST_CHECK(isBLSPrecompileAddress("0x000000000000000000000000000000000000000f"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000a"));
    BOOST_CHECK(!isBLSPrecompileAddress("0000000000000000000000000000000000000012"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000B"));
    BOOST_CHECK(!isBLSPrecompileAddress("000000000000000000000000000000000000000g"));
    BOOST_CHECK(!isBLSPrecompileAddress("00000000000000000000000000000000000000"));    // too short
    BOOST_CHECK(!isBLSPrecompileAddress("100000000000000000000000000000000000000b"));  // non-zero
                                                                                       // high bytes
}

BOOST_AUTO_TEST_CASE(p256verify_accepts_optional_0x_prefix)
{
    using bcos::executor::isP256verifyPrecompileAddress;
    BOOST_CHECK(isP256verifyPrecompileAddress("0000000000000000000000000000000000000100"));
    BOOST_CHECK(isP256verifyPrecompileAddress("0x0000000000000000000000000000000000000100"));
    BOOST_CHECK(!isP256verifyPrecompileAddress("0000000000000000000000000000000000000101"));
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEvmPrecompiledAddress

BOOST_FIXTURE_TEST_SUITE(CompatPrecompileGating, CompatHostContextFixture)

BOOST_AUTO_TEST_CASE(FC_P_bls_success_without_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0));
    BOOST_CHECK_NE(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.gas_left, 10'000'000);
    BOOST_CHECK_EQUAL(r.output_size, 0);
}

BOOST_AUTO_TEST_CASE(FC_P_p256verify_success_without_osaka)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::P256VERIFY), bytes{});
    BOOST_CHECK_NE(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.gas_left, 10'000'000);
    BOOST_CHECK_EQUAL(r.output_size, 0);
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

BOOST_AUTO_TEST_CASE(FC_P_bls_g1add_commutative_vector_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto in = blsG1AddValidInputG1PlusP1();
    BOOST_REQUIRE_EQUAL(in.size(), 256u);
    auto const expected = blsG1AddExpectedG1PlusP1();
    BOOST_REQUIRE_EQUAL(expected.size(), 128u);

    // Swap the two G1 points: g1add(P, Q) == g1add(Q, P).
    bytes swapped(256, 0);
    std::memcpy(swapped.data(), in.data() + 128, 128);
    std::memcpy(swapped.data() + 128, in.data(), 128);

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsG1Add);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), std::move(swapped));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, expected.size());
    BOOST_CHECK(std::memcmp(r.output_data, expected.data(), expected.size()) == 0);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_invalid_point_rejected_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    // 256 bytes of 0xff are not a valid pair of BLS12-381 G1 points.
    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsG1Add);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0xff));
    BOOST_CHECK(r.status_code == EVMC_INTERNAL_ERROR || r.status_code == EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.output_size, 0);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g1msm_semantics_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto const g1addIn = blsG1AddValidInputG1PlusP1();
    BOOST_REQUIRE_EQUAL(g1addIn.size(), 256u);
    bytes point(g1addIn.begin(), g1addIn.begin() + 128);

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);

    // g1msm([P, 1]) = P
    bytes oneInput = point;
    auto one = scalarOne32();
    oneInput.insert(oneInput.end(), one.begin(), one.end());
    auto r1 = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1MSM), std::move(oneInput));
    BOOST_CHECK_EQUAL(r1.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r1.output_size, point.size());
    BOOST_CHECK(std::memcmp(r1.output_data, point.data(), point.size()) == 0);

    // g1msm([P, 0]) = infinity (all zero encoding)
    bytes zeroInput = point;
    auto zero = scalarZero32();
    zeroInput.insert(zeroInput.end(), zero.begin(), zero.end());
    auto r0 =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1MSM), std::move(zeroInput));
    BOOST_CHECK_EQUAL(r0.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r0.output_size, 128u);
    for (size_t i = 0; i < r0.output_size; ++i)
    {
        BOOST_CHECK_EQUAL(r0.output_data[i], 0);
    }
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g1msm_multi_scalar_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto const g1addIn = blsG1AddValidInputG1PlusP1();
    BOOST_REQUIRE_EQUAL(g1addIn.size(), 256u);
    bytes point(g1addIn.begin(), g1addIn.begin() + 128);

    // g1msm([P, P, 1, 0]) = 1*P + 0*P = P  (2 pairs, 320 bytes)
    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);
    auto one = scalarOne32();
    auto zero = scalarZero32();
    bytes input = point;                                    // P
    input.insert(input.end(), one.begin(), one.end());      // 1
    input.insert(input.end(), point.begin(), point.end());  // P
    input.insert(input.end(), zero.begin(), zero.end());    // 0
    BOOST_REQUIRE_EQUAL(input.size(), 320u);

    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1MSM), std::move(input));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, point.size());
    BOOST_CHECK(std::memcmp(r.output_data, point.data(), point.size()) == 0);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g2add_and_g2msm_semantics_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);

    // Derive a valid G2 point via map_fp2_to_g2(0).
    bytes fp2Zero(128, 0);
    auto mapped = compatCallBuiltInPrecompiled(
        host, std::string(addr::BLS_MAP_FP2_TO_G2), std::move(fp2Zero));
    BOOST_CHECK_EQUAL(mapped.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapped.output_size, 256u);
    bytes g2(mapped.output_data, mapped.output_data + mapped.output_size);

    // g2add(P, inf) = P
    bytes addInput = g2;
    addInput.insert(addInput.end(), 256, 0);
    auto addR =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G2ADD), std::move(addInput));
    BOOST_CHECK_EQUAL(addR.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(addR.output_size, g2.size());
    BOOST_CHECK(std::memcmp(addR.output_data, g2.data(), g2.size()) == 0);

    // g2msm([P,1]) = P
    bytes oneInput = g2;
    auto one = scalarOne32();
    oneInput.insert(oneInput.end(), one.begin(), one.end());
    auto m1 = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G2MSM), std::move(oneInput));
    BOOST_CHECK_EQUAL(m1.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(m1.output_size, g2.size());
    BOOST_CHECK(std::memcmp(m1.output_data, g2.data(), g2.size()) == 0);

    // g2msm([P,0]) = infinity
    bytes zeroInput = g2;
    auto zero = scalarZero32();
    zeroInput.insert(zeroInput.end(), zero.begin(), zero.end());
    auto m0 =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G2MSM), std::move(zeroInput));
    BOOST_CHECK_EQUAL(m0.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(m0.output_size, 256u);
    for (size_t i = 0; i < m0.output_size; ++i)
    {
        BOOST_CHECK_EQUAL(m0.output_data[i], 0);
    }
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g2msm_multi_scalar_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);

    // Derive a valid G2 point via map_fp2_to_g2(0).
    bytes fp2Zero(128, 0);
    auto mapped = compatCallBuiltInPrecompiled(
        host, std::string(addr::BLS_MAP_FP2_TO_G2), std::move(fp2Zero));
    BOOST_REQUIRE_EQUAL(mapped.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapped.output_size, 256u);
    bytes g2(mapped.output_data, mapped.output_data + mapped.output_size);

    // g2msm([Q, Q, 1, 0]) = 1*Q + 0*Q = Q  (2 pairs, 576 bytes)
    auto one = scalarOne32();
    auto zero = scalarZero32();
    bytes input = g2;
    input.insert(input.end(), one.begin(), one.end());
    input.insert(input.end(), g2.begin(), g2.end());
    input.insert(input.end(), zero.begin(), zero.end());
    BOOST_REQUIRE_EQUAL(input.size(), 576u);

    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G2MSM), std::move(input));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, g2.size());
    BOOST_CHECK(std::memcmp(r.output_data, g2.data(), g2.size()) == 0);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_pairing_and_map_semantics_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);

    // map_fp_to_g1 and map_fp2_to_g2 should succeed for zero field elements.
    auto mapG1 =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_MAP_FP_TO_G1), bytes(64, 0));
    BOOST_CHECK_EQUAL(mapG1.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapG1.output_size, 128u);

    auto mapG2 =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_MAP_FP2_TO_G2), bytes(128, 0));
    BOOST_CHECK_EQUAL(mapG2.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(mapG2.output_size, 256u);

    // pairing_check with one (inf, inf) pair should be true (last byte = 1).
    auto pairTrue =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_PAIRING_CHECK), bytes(384, 0));
    BOOST_CHECK_EQUAL(pairTrue.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(pairTrue.output_size, 32u);
    for (size_t i = 0; i < 31; ++i)
    {
        BOOST_CHECK_EQUAL(pairTrue.output_data[i], 0);
    }
    BOOST_CHECK_EQUAL(pairTrue.output_data[31], 1);

    // pairing_check with one non-trivial mapped pair should be false (last byte = 0).
    bytes onePair;
    onePair.insert(onePair.end(), mapG1.output_data, mapG1.output_data + mapG1.output_size);
    onePair.insert(onePair.end(), mapG2.output_data, mapG2.output_data + mapG2.output_size);
    BOOST_REQUIRE_EQUAL(onePair.size(), 384u);

    auto pairFalse = compatCallBuiltInPrecompiled(
        host, std::string(addr::BLS_PAIRING_CHECK), std::move(onePair));
    BOOST_CHECK_EQUAL(pairFalse.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(pairFalse.output_size, 32u);
    for (size_t i = 0; i < 31; ++i)
    {
        BOOST_CHECK_EQUAL(pairFalse.output_data[i], 0);
    }
    BOOST_CHECK_EQUAL(pairFalse.output_data[31], 0);

    // Invalid input size should be rejected by precompile execution.
    auto pairInvalid =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_PAIRING_CHECK), bytes(1, 0));
    BOOST_CHECK(
        pairInvalid.status_code == EVMC_INTERNAL_ERROR || pairInvalid.status_code == EVMC_REVERT);
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

BOOST_AUTO_TEST_CASE(FC_P_p256verify_invalid_signature)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    // Flip the message hash to make the signature invalid.
    auto input = p256verifyValidSignatureInput();
    BOOST_REQUIRE_EQUAL(input.size(), 160u);
    input[0] ^= 0xff;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::osakaEnabled(), CompatEvmAttach::P256Verify);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::P256VERIFY), std::move(input));
    // EIP-7212: invalid signature → success with empty output (not 0x00...00).
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.output_size, 0);
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

// FC-P: verify that 36-zero EVM_PRECOMPILED_PREFIX and 35-zero SYS_ADDRESS_PREFIX
// do not misroute addresses. The dual-check (prefix AND contains()) ensures:
// - BLS (0x0b) / p256verify (0x0100) → Ethereum precompile, never caught by FISCO static
// - SystemConfig (0x1000) → FISCO static precompile, never caught by Ethereum
BOOST_AUTO_TEST_CASE(FC_P_address_routing_prefix_overlap)
{
    namespace addr = bcos::test::compat::compat_addr;

    auto blockContext =
        std::make_shared<executor::BlockContext>(stateStorage, ledgerCache, hashImpl, 1, h256(), 0,
            static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION), false, false, backend);
    auto exec = std::make_shared<CompatHostTestExecutive>(blockContext, "", 100, 0, gasInjector);

    // Attach BLS + p256verify to m_evmPrecompiled (single combined map — each
    // compatAttach* helper replaces the whole map, so they can't be chained).
    compatAttachBlsAndP256VerifyEvmPrecompile(exec);

    // Register SystemConfig in m_staticPrecompiled
    exec->setStaticPrecompiled(std::make_shared<const std::set<std::string>>(
        std::initializer_list<std::string>{std::string(addr::SYSCONFIG)}));

    auto bls = std::string(addr::BLS_G1ADD);
    auto p256 = std::string(addr::P256VERIFY);
    auto sys = std::string(addr::SYSCONFIG);

    // BLS: 35-zero prefix matches but contains() → false → falls through to Ethereum
    BOOST_TEST_MESSAGE("BLS G1ADD: isStatic=" << exec->isStaticPrecompiled(bls)
                                              << " isEvm=" << exec->isEthereumPrecompiled(bls));
    BOOST_CHECK(!exec->isStaticPrecompiled(bls));
    BOOST_CHECK(exec->isEthereumPrecompiled(bls));

    // p256verify: same logic — prefix overlap but contains() gates correctly
    BOOST_TEST_MESSAGE("p256verify: isStatic=" << exec->isStaticPrecompiled(p256)
                                               << " isEvm=" << exec->isEthereumPrecompiled(p256));
    BOOST_CHECK(!exec->isStaticPrecompiled(p256));
    BOOST_CHECK(exec->isEthereumPrecompiled(p256));

    // SystemConfig: registered in static set → caught by isStaticPrecompiled FIRST
    BOOST_TEST_MESSAGE("SystemConfig: isStatic=" << exec->isStaticPrecompiled(sys)
                                                 << " isEvm=" << exec->isEthereumPrecompiled(sys));
    BOOST_CHECK(exec->isStaticPrecompiled(sys));
    BOOST_CHECK(!exec->isEthereumPrecompiled(sys));
}

BOOST_AUTO_TEST_SUITE_END()  // CompatPrecompileGating
BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
