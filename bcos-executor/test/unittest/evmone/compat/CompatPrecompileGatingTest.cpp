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

// https://eips.ethereum.org/assets/eip-2537/msm_G1_bls.json — bls_g1msm_(g1+g1=2*g1)
bytes blsG1MsmOfficialInputG1PlusG1Times2()
{
    return bcos::fromHex(
        "0000000000000000000000000000000017f1d3a73197d7942695638c4fa9ac0fc3688c4f9774b905a14e3a3f17"
        "1bac586c55e83ff97a1aeffb3af00adb22c6bb"
        "0000000000000000000000000000000008b3f481e3aaa0f1a09e30ed741d8ae4fcf5e095d5d00af600db18cb2c"
        "04b3edd03cc744a2888ae40caa232946c5e7e1"
        "0000000000000000000000000000000000000000000000000000000000000002");
}

bytes blsG1MsmOfficialExpectedG1PlusG1Times2()
{
    return bcos::fromHex(
        "000000000000000000000000000000000572cbea904d67468808c8eb50a9450c9721db309128012543902d0ac"
        "358a62ae28f75bb8f1c7c42c39a8c5529bf0f4e"
        "00000000000000000000000000000000166a9d8cabc673a322fda673779d8e3822ba3ecb8670e461f73bb902"
        "1d5fd76a4c56d9d4cd16bd1bba86881979749d28");
}

// https://eips.ethereum.org/assets/eip-2537/map_fp_to_G1_bls.json — bls_g1map_
bytes blsMapFpToG1OfficialInput()
{
    return bcos::fromHex(
        "00000000000000000000000000000000156c8a6a2c184569d69a76be144b5cdc5141d2d2ca4fe341f011e25e39"
        "6"
        "9c55ad9e9b9ce2eb833c81a908e5fa4ac5f03");
}

bytes blsMapFpToG1OfficialExpected()
{
    return bcos::fromHex(
        "00000000000000000000000000000000184bb665c37ff561a89ec2122dd343f20e0f4cbcaec84e3c3052ea81d1"
        "83"
        "4e192c426074b02ed3dca4e7676ce4ce48ba"
        "0000000000000000000000000000000004407b8d35af4dacc809927071fc0405218f1401a6d15af775810e4e46"
        "0"
        "064bcc9468beeba82fdc751be70476c888bf3");
}

// https://eips.ethereum.org/assets/eip-2537/add_G2_bls.json — bls_g2add_g2+p2
bytes blsG2AddOfficialInputG2PlusP2()
{
    return bcos::fromHex(
        "00000000000000000000000000000000024aa2b2f08f0a91260805272dc51051c6e47ad4fa403b02"
        "b4510b647ae3d1770bac0326a805bbefd48056c8c121bdb800000000000000000000000000000000"
        "13e02b6052719f607dacd3a088274f65596bd0d09920b61ab5da61bbdc7f5049334cf11213945d57"
        "e5ac7d055d042b7e000000000000000000000000000000000ce5d527727d6e118cc9cdc6da2e351a"
        "adfd9baa8cbdd3a76d429a695160d12c923ac9cc3baca289e193548608b828010000000000000000"
        "00000000000000000606c4a02ea734cc32acd2b02bc28b99cb3e287e85a763af267492ab572e99ab"
        "3f370d275cec1da1aaa9075ff05f79be00000000000000000000000000000000103121a2ceaae586"
        "d240843a398967325f8eb5a93e8fea99b62b9f88d8556c80dd726a4b30e84a36eeabaf3592937f27"
        "00000000000000000000000000000000086b990f3da2aeac0a36143b7d7c824428215140db1bb859"
        "338764cb58458f081d92664f9053b50b3fbd2e4723121b6800000000000000000000000000000000"
        "0f9e7ba9a86a8f7624aa2b42dcc8772e1af4ae115685e60abc2c9b90242167acef3d0be4050bf935"
        "eed7c3b6fc7ba77e000000000000000000000000000000000d22c3652d0dc6f0fc9316e14268477c"
        "2049ef772e852108d269d9c38dba1d4802e8dae479818184c08f9a569d878451");
}

bytes blsG2AddOfficialExpectedG2PlusP2()
{
    return bcos::fromHex(
        "000000000000000000000000000000000b54a8a7b08bd6827ed9a797de216b8c9057b3a9ca93e2f8"
        "8e7f04f19accc42da90d883632b9ca4dc38d013f71ede4db00000000000000000000000000000000"
        "077eba4eecf0bd764dce8ed5f45040dd8f3b3427cb35230509482c14651713282946306247866dfe"
        "39a8e33016fcbe520000000000000000000000000000000014e60a76a29ef85cbd69f251b9f29147"
        "b67cfe3ed2823d3f9776b3a0efd2731941d47436dc6d2b58d9e65f8438bad0730000000000000000"
        "00000000000000001586c3c910d95754fef7a732df78e279c3d37431c6a2b77e67a00c7c130a8fcd"
        "4d19f159cbeb997a178108fffffcbd20");
}

bytes pointEvaluationValidProofInput()
{
    return bcos::fromHex(
        "014edfed8547661f6cb416eba53061a2f6dce872c0497e6dd485a876fe2567f1564c0a11a0f704f4fc3e8acfe0"
        "f8245f0ad1347b378fbf96e206da11a5d363066d928e13fe443e957d82e3e71d48cb65d51028eb4483e719bf8e"
        "fcdf12f7c321a421e229565952cfff4ef3517100a97da1d4fe57956fa50a442f92af03b1bf37adacc8ad4ed209"
        "b31287ea5bb94d9d06a444d6bb5aadc3ceb615b50d6606bd54bfe529f59247987cd1ab848d19de599a9052f183"
        "5fb0d0d44cf70183e19a68c9");
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

void assertBlsPrecompileVectorOk(
    CompatHostContextFixture& fixture, std::string_view addr, bytes input, bytes const& expected)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        fixture, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr), std::move(input));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, expected.size());
    BOOST_CHECK(std::memcmp(r.output_data, expected.data(), expected.size()) == 0);
}

void assertBlsPrecompileRejected(
    CompatHostContextFixture& fixture, std::string_view addr, bytes input)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        fixture, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::BlsAll);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr), std::move(input));
    BOOST_CHECK(r.status_code == EVMC_INTERNAL_ERROR || r.status_code == EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.output_size, 0);
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
    BOOST_CHECK(isBLSPrecompileAddress("000000000000000000000000000000000000000B"));
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

BOOST_AUTO_TEST_CASE(point_evaluation_address_byte_parse)
{
    using bcos::executor::isPointEvaluationPrecompileAddress;
    BOOST_CHECK(isPointEvaluationPrecompileAddress("000000000000000000000000000000000000000a"));
    BOOST_CHECK(isPointEvaluationPrecompileAddress("0x000000000000000000000000000000000000000a"));
    BOOST_CHECK(!isPointEvaluationPrecompileAddress("000000000000000000000000000000000000000b"));
    BOOST_CHECK(!isPointEvaluationPrecompileAddress("0000000000000000000000000000000000000009"));
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEvmPrecompiledAddress

BOOST_FIXTURE_TEST_SUITE(CompatPrecompileGating, CompatHostContextFixture)

BOOST_AUTO_TEST_CASE(FC_P_point_evaluation_success_without_cancun)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::shanghaiEip2929(), CompatEvmAttach::PointEvaluation);
    auto r = compatCallBuiltInPrecompiled(
        host, std::string(addr::POINT_EVALUATION), pointEvaluationValidProofInput());
    BOOST_CHECK_NE(r.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(r.gas_left, 10'000'000);
    BOOST_CHECK_EQUAL(r.output_size, 0);
}

BOOST_AUTO_TEST_CASE(FC_P_point_evaluation_valid_proof_with_cancun)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto input = pointEvaluationValidProofInput();
    BOOST_REQUIRE_EQUAL(input.size(), 192u);

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::cancunOnly(), CompatEvmAttach::PointEvaluation);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::POINT_EVALUATION), input);
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(r.output_size, 64u);
    BOOST_CHECK_EQUAL(r.output_data[30], 0x10);
    BOOST_CHECK_EQUAL(r.output_data[31], 0x00);
    BOOST_CHECK_EQUAL(r.output_data[32], 0x73);
}

BOOST_AUTO_TEST_CASE(FC_P_point_evaluation_invalid_input_reverts_with_cancun)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::cancunOnly(), CompatEvmAttach::PointEvaluation);
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::POINT_EVALUATION), bytes(192, 0));
    BOOST_CHECK_NE(r.status_code, EVMC_SUCCESS);
}

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

BOOST_AUTO_TEST_CASE(FC_P_bls_g1msm_ok_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    auto in = blsG1MsmOfficialInputG1PlusG1Times2();
    auto const expected = blsG1MsmOfficialExpectedG1PlusG1Times2();
    BOOST_REQUIRE_EQUAL(in.size(), 160u);
    BOOST_REQUIRE_EQUAL(expected.size(), 128u);
    assertBlsPrecompileVectorOk(*this, addr::BLS_G1MSM, std::move(in), expected);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g1msm_invalid_length_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    assertBlsPrecompileRejected(*this, addr::BLS_G1MSM, bytes(128, 0));
}

BOOST_AUTO_TEST_CASE(FC_P_bls_map_fp_to_g1_ok_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    auto in = blsMapFpToG1OfficialInput();
    auto const expected = blsMapFpToG1OfficialExpected();
    BOOST_REQUIRE_EQUAL(in.size(), 64u);
    BOOST_REQUIRE_EQUAL(expected.size(), 128u);
    assertBlsPrecompileVectorOk(*this, addr::BLS_MAP_FP_TO_G1, std::move(in), expected);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_map_fp_to_g1_invalid_field_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    assertBlsPrecompileRejected(*this, addr::BLS_MAP_FP_TO_G1, bytes(64, 0xff));
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g2add_ok_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    auto in = blsG2AddOfficialInputG2PlusP2();
    auto const expected = blsG2AddOfficialExpectedG2PlusP2();
    BOOST_REQUIRE_EQUAL(in.size(), 512u);
    BOOST_REQUIRE_EQUAL(expected.size(), 256u);
    assertBlsPrecompileVectorOk(*this, addr::BLS_G2ADD, std::move(in), expected);
}

BOOST_AUTO_TEST_CASE(FC_P_bls_g2add_invalid_point_with_prague)
{
    namespace addr = bcos::test::compat::compat_addr;

    assertBlsPrecompileRejected(*this, addr::BLS_G2ADD, bytes(512, 0xff));
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

    // pairing_check bls_pairing_e(0,0) from pairing_check_bls.json (384 zero bytes → true).
    auto const pairE00Expected =
        bcos::fromHex("0000000000000000000000000000000000000000000000000000000000000001");
    auto pairTrue =
        compatCallBuiltInPrecompiled(host, std::string(addr::BLS_PAIRING_CHECK), bytes(384, 0));
    BOOST_CHECK_EQUAL(pairTrue.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_EQUAL(pairTrue.output_size, pairE00Expected.size());
    BOOST_CHECK(
        std::memcmp(pairTrue.output_data, pairE00Expected.data(), pairE00Expected.size()) == 0);

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
