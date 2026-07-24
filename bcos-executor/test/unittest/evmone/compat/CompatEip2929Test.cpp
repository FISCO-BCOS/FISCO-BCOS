/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-A*: EIP-2929 cold/warm (executor HostContext access helpers).
 *  @file CompatEip2929Test.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"
#include "bcos-executor/src/Common.h"
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

BOOST_AUTO_TEST_CASE(FC_A_warmup_api_idempotent)
{
    bcos::executor::Eip2929AccessState accessState;
    evmc_address addr{};
    std::memset(addr.bytes, 0x51, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x61, sizeof(key.bytes));

    BOOST_CHECK(accessState.warmUpAddress(addr));
    BOOST_CHECK(!accessState.warmUpAddress(addr));
    BOOST_CHECK(accessState.containsAddress(addr));

    BOOST_CHECK(accessState.warmUpStorage(addr, key));
    BOOST_CHECK(!accessState.warmUpStorage(addr, key));
    BOOST_CHECK(accessState.containsStorage(addr, key));
}

BOOST_AUTO_TEST_CASE(FC_A_revision_gate_eip2929_on_prefork_evmc_rev_always_cold)
{
    using compat::CompatFeatureProfile;

    // EIP-2929 cold/warm is suppressed when evmc rev < Berlin
    // (London in evmc is still >= Berlin; use Istanbul/Petersburg for pre-Berlin).
    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    evmc_address addr{};
    std::memset(addr.bytes, 0xef, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0xfe, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_ISTANBUL), EVMC_ACCESS_COLD);

    // Prefork probes must not warm the EIP-2929 set used at Berlin+.
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_revision_pre_berlin_forces_cold)
{
    using compat::CompatFeatureProfile;

    // EIP-2929 is suppressed when evmc rev < Berlin.
    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    evmc_address addr{};
    std::memset(addr.bytes, 0x55, sizeof(addr.bytes));
    evmc_bytes32 key{};
    std::memset(key.bytes, 0x66, sizeof(key.bytes));

    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_ISTANBUL), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessStorage(addr, key, EVMC_BERLIN - 1), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_A_eip2929_warm_shared_across_call_depth)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    evmc_address addr{};
    std::memset(addr.bytes, 0x99, sizeof(addr.bytes));

    BOOST_CHECK_EQUAL(host.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_COLD);

    auto parentExe = host.getTransactionExecutive();
    auto childExe = std::dynamic_pointer_cast<CompatHostTestExecutive>(parentExe);
    BOOST_REQUIRE(childExe);
    auto childExecutive = childExe->buildCompatChild(1);
    auto childCallParams =
        std::make_unique<executor::CallParameters>(executor::CallParameters::MESSAGE);
    childCallParams->origin = "0000000000000000000000000000000000000001";
    childCallParams->senderAddress = childCallParams->origin;
    childCallParams->receiveAddress = "0000000000000000000000000000000000000002";
    executor::HostContext childHost(std::move(childCallParams), childExecutive, "");

    BOOST_CHECK_EQUAL(childHost.accessAccount(addr, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_origin)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address const origin = unhexAddress("0000000000000000000000000000000000000001");
    BOOST_CHECK_EQUAL(host.accessAccount(origin, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_to)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address const toAddr = unhexAddress("0000000000000000000000000000000000000002");
    BOOST_CHECK_EQUAL(host.accessAccount(toAddr, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_precompiles)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    for (int i = 1; i <= 9; ++i)
    {
        evmc_address pre{};
        pre.bytes[19] = static_cast<uint8_t>(i);
        BOOST_CHECK_EQUAL(host.accessAccount(pre, EVMC_CANCUN), EVMC_ACCESS_WARM);
    }
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_prague_includes_0x0a_and_bls)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    evmc_address bls0b{};
    bls0b.bytes[19] = 0x0b;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a, EVMC_PRAGUE), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(bls0b, EVMC_PRAGUE), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_cancun_includes_0x0a_excludes_bls)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunEip2929());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    evmc_address bls0b{};
    bls0b.bytes[19] = 0x0b;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a, EVMC_CANCUN), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(bls0b, EVMC_CANCUN), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_shanghai_excludes_0x0a)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::shanghaiEip2929());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address pre0a{};
    pre0a.bytes[19] = 0x0a;
    BOOST_CHECK_EQUAL(host.accessAccount(pre0a, EVMC_SHANGHAI), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_osaka_includes_p256verify)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::osakaEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address p256{};
    p256.bytes[18] = 0x01;
    p256.bytes[19] = 0x00;
    BOOST_CHECK_EQUAL(host.accessAccount(p256, EVMC_OSAKA), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_create_skips_to)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(
        *this, CompatFeatureProfile::pragueEnabled(), CompatEvmAttach::None, true);
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams, true);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address const origin = unhexAddress("0000000000000000000000000000000000000001");
    evmc_address wouldBeCallee{};
    std::memset(wouldBeCallee.bytes, 0xee, sizeof(wouldBeCallee.bytes));

    evmc_address pre1{};
    pre1.bytes[19] = 0x01;
    BOOST_CHECK_EQUAL(host.accessAccount(origin, EVMC_CANCUN), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(pre1, EVMC_CANCUN), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessAccount(wouldBeCallee, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(wouldBeCallee, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_A_initial_prewarm_flag_off)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    evmc_address const origin = unhexAddress("0000000000000000000000000000000000000001");
    evmc_address pre1{};
    pre1.bytes[19] = 0x01;
    BOOST_CHECK_EQUAL(host.accessAccount(origin, EVMC_CANCUN), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.accessAccount(pre1, EVMC_CANCUN), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_CASE(FC_EIP2930_access_list_warms_account_and_storage)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    h256 storageKey = h256(0x42424242);
    warmParams.web3TypedTxKind = 1;
    warmParams.eip2930AccessList = {
        {toAddress("00000000000000000000000000000000c0ffee01"), {storageKey}}};
    compatExecutorEip2930WarmAccessList(host, warmParams);

    evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee01");
    evmc_bytes32 key{};
    std::memcpy(key.bytes, storageKey.data(), h256::SIZE);
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr, EVMC_CANCUN), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr, key, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_EIP2930_eip1559_access_list_warms)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    h256 storageKey = h256(0x55);
    warmParams.web3TypedTxKind = 2;  // EIP-1559
    warmParams.eip2930AccessList = {
        {toAddress("00000000000000000000000000000000c0ffee02"), {storageKey}}};
    compatExecutorEip2930WarmAccessList(host, warmParams);

    evmc_address const listAddr = unhexAddress("00000000000000000000000000000000c0ffee02");
    evmc_bytes32 key{};
    std::memcpy(key.bytes, storageKey.data(), h256::SIZE);
    BOOST_CHECK_EQUAL(host.accessAccount(listAddr, EVMC_CANCUN), EVMC_ACCESS_WARM);
    BOOST_CHECK_EQUAL(host.accessStorage(listAddr, key, EVMC_CANCUN), EVMC_ACCESS_WARM);
}

BOOST_AUTO_TEST_CASE(FC_EIP2930_empty_access_list_no_extra_warm)
{
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::pragueEnabled());
    executor::CallParameters warmParams(executor::CallParameters::MESSAGE);
    compatFillDefaultCallParametersForWarm(warmParams);
    compatExecutorEip2929WarmInitial(host, warmParams);

    warmParams.web3TypedTxKind = 1;
    warmParams.eip2930AccessList.clear();
    compatExecutorEip2930WarmAccessList(host, warmParams);

    evmc_address const extra{};
    BOOST_CHECK_EQUAL(host.accessAccount(extra, EVMC_CANCUN), EVMC_ACCESS_COLD);
}

BOOST_AUTO_TEST_SUITE_END()  // CompatEip2929
BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
