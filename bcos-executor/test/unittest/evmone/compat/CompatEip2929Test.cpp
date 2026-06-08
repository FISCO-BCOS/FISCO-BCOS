/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-A*: EIP-2929 cold/warm (executor HostContext access helpers).
 *  @file CompatEip2929Test.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"
#include <boost/test/unit_test.hpp>
#include <cstring>

namespace bcos::test
{

BOOST_AUTO_TEST_SUITE(Compat)
BOOST_FIXTURE_TEST_SUITE(CompatEip2929, CompatHostContextFixture)

BOOST_AUTO_TEST_CASE(FC_A_eip2929_off_always_cold)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    evmc_address addr{};
    std::memset(addr.bytes, 0xab, sizeof(addr.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_A_eip2929_on_second_access_warm)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    evmc_address addr{};
    std::memset(addr.bytes, 0xcd, sizeof(addr.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_eip2929_on_warm_storage)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    evmc_address addr{};
    std::memset(addr.bytes, 0x33, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x44, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_revision_london_forces_cold)
{
    using compat::CompatFeatureProfile;

    // Without feature_evm_eip2929, access stays cold regardless of evmc revision argument.
    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    evmc_address addr{};
    std::memset(addr.bytes, 0x55, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x66, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_LONDON), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_CANCUN), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_A_known_limit_single_host_depth)
{
    BOOST_TEST_MESSAGE(
        "I1: warm sets are per-HostContext depth; cross-internal-call warm not guaranteed");
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEip2929
BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
