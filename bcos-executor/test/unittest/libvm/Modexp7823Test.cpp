/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief EIP-7823 modexp input length bounds (Osaka+).
 *  @file Modexp7823Test.cpp
 */

#include "vm/ModexpGas.h"
#include "vm/Precompiled.h"
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/protocol/Protocol.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{

namespace
{
ledger::Features osakaFeatures()
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    features.set(ledger::Features::Flag::feature_evm_osaka);
    return features;
}

bytes modexpHeaderBaseLen1025()
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Modexp7823Test)

BOOST_AUTO_TEST_CASE(parse_lengths_small)
{
    bytes input(96, 0);
    input[31] = 32;
    input[63] = 1;
    input[95] = 32;
    auto const lens = executor::parseModexpLengths(ref(input));
    BOOST_CHECK(!lens.overflow);
    BOOST_CHECK_EQUAL(lens.baseLen, 32u);
    BOOST_CHECK_EQUAL(lens.expLen, 1u);
    BOOST_CHECK_EQUAL(lens.modLen, 32u);
}

BOOST_AUTO_TEST_CASE(parse_lengths_uint64_overflow)
{
    bytes input(96, 0);
    input[0] = 0xff;
    input[1] = 0xff;
    input[2] = 0xff;
    input[3] = 0xff;
    input[4] = 0xff;
    input[5] = 0xff;
    input[6] = 0xff;
    input[7] = 0xff;
    input[8] = 0x01;
    auto const lens = executor::parseModexpLengths(ref(input));
    BOOST_CHECK(lens.overflow);
}

BOOST_AUTO_TEST_CASE(validate_1024_ok_osaka)
{
    bytes input(96, 0);
    input[30] = 4;  // baseLen = 1024 (0x400 BE)
    BOOST_CHECK(executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_fail_osaka)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;  // baseLen = 1025
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_modlen0_fail_osaka)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    input[95] = 0;
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(validate_1025_ok_prague)
{
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    BOOST_CHECK(executor::validateModexpEip7823(ref(input), EVMC_PRAGUE));
}

BOOST_AUTO_TEST_CASE(modexpEip7823Enabled_gateMatrix)
{
    auto const features = osakaFeatures();
    BOOST_CHECK(executor::modexpEip7823Enabled(features, EVMC_OSAKA));
    BOOST_CHECK(!executor::modexpEip7823Enabled(features, EVMC_PRAGUE));

    ledger::Features noOsaka;
    noOsaka.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    noOsaka.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK(!executor::modexpEip7823Enabled(noOsaka, EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(shouldRejectModexpEip7823_boundaries)
{
    auto const features = osakaFeatures();
    auto const oversize = modexpHeaderBaseLen1025();
    bytes inputOk(96, 0);
    inputOk[30] = 4;

    evmc_address modexpAddr{};
    modexpAddr.bytes[19] = 0x05;
    evmc_address otherAddr{};
    otherAddr.bytes[19] = 0x01;

    BOOST_CHECK(
        executor::shouldRejectModexpEip7823(modexpAddr, ref(oversize), features, EVMC_OSAKA));
    BOOST_CHECK(
        !executor::shouldRejectModexpEip7823(modexpAddr, ref(inputOk), features, EVMC_OSAKA));
    BOOST_CHECK(
        !executor::shouldRejectModexpEip7823(otherAddr, ref(oversize), features, EVMC_OSAKA));
    BOOST_CHECK(
        !executor::shouldRejectModexpEip7823(modexpAddr, ref(oversize), features, EVMC_PRAGUE));

    ledger::Features noOsaka;
    noOsaka.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    noOsaka.set(ledger::Features::Flag::feature_evm_prague);
    BOOST_CHECK(
        !executor::shouldRejectModexpEip7823(modexpAddr, ref(oversize), noOsaka, EVMC_OSAKA));
}

BOOST_AUTO_TEST_CASE(modexp_executor_does_not_enforce_7823)
{
    // EIP-7823 is enforced at host call sites (legacy HostContext / TE callBuiltinPrecompiled),
    // not inside the modexp executor itself.
    bytes input(96, 0);
    input[30] = 4;
    input[31] = 1;
    BOOST_CHECK(!executor::validateModexpEip7823(ref(input), EVMC_OSAKA));
    auto const [ok, out] = executor::PrecompiledRegistrar::executor("modexp")(ref(input));
    BOOST_CHECK(ok);
    BOOST_CHECK(out.empty());
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace bcos::test
