/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief FC-S*: light HostContext smoke (identity precompile).
 *  @note Top-level TXHASH to 0x04 does not hit callBuiltInPrecompiled; use host hook here.
 *  @file CompatSmokeTest.cpp
 */

#include "CompatHostContextHarness.h"
#include "CompatTestFixture.h"

#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-framework/executor/ExecuteError.h>
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
    // FC-06: identity precompile on the cancun-only profile (not a pre-Cancun path).
    using compat::CompatFeatureProfile;

    auto host =
        makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly(), CompatEvmAttach::Identity);
    auto r = compatCallBuiltInPrecompiled(host, compatFillZeroAddr(4), bytes{0x01, 0x02});
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(FC_S_cancun_only_no_prague_precompile)
{
    namespace addr = bcos::test::compat::compat_addr;
    using compat::CompatFeatureProfile;

    auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
    auto r = compatCallBuiltInPrecompiled(host, std::string(addr::BLS_G1ADD), bytes(256, 0));
    BOOST_CHECK_EQUAL(r.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(FC_S_access_always_cold)
{
    auto host = makeCompatHostContext(*this, compat::CompatFeatureProfile::cancunOnly());
    auto* context = reinterpret_cast<evmc_host_context*>(&host);
    evmc_address address{};
    evmc_bytes32 key{};

    BOOST_REQUIRE(host.hostInterface != nullptr);
    BOOST_CHECK_EQUAL(host.hostInterface->access_account(context, &address), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(host.hostInterface->access_account(context, &address), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(
        host.hostInterface->access_storage(context, &address, &key), EVMC_ACCESS_COLD);
    BOOST_CHECK_EQUAL(
        host.hostInterface->access_storage(context, &address, &key), EVMC_ACCESS_COLD);
}

// recordHostFault("", halting) must still fail the frame (review F5): the record's
// presence is a separate flag, not the message's emptiness — an Error built with only
// a code (empty diagnostic) is a real node fault. Both directions pinned: non-empty
// control throws, empty message throws too. (The pre-fix code silently recorded
// nothing and the frame committed.)
BOOST_AUTO_TEST_CASE(recordHostFaultEmptyMessageStillFailsFrame)
{
    using compat::CompatFeatureProfile;
    {
        auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
        host.recordHostFault("storage I/O fault", /*halting=*/true);
        BOOST_CHECK_THROW(host.throwIfHostFault(), std::runtime_error);
    }
    {
        auto host = makeCompatHostContext(*this, CompatFeatureProfile::cancunOnly());
        host.recordHostFault("", /*halting=*/true);
        BOOST_CHECK_THROW(host.throwIfHostFault(), std::runtime_error);
    }
}


BOOST_AUTO_TEST_CASE(hostCallbacksStopMutatingAfterRecordedFault)
{
    using compat::CompatFeatureProfile;
    // The healthy frame is a negative control: each callback must still do its work.
    for (bool faulted : {false, true})
    {
        CompatHostContextFixture fixture;
        auto host = makeCompatHostContext(fixture, CompatFeatureProfile::cancunOnly());
        auto* context = reinterpret_cast<evmc_host_context*>(&host);
        evmc_address address{};
        address.bytes[19] = 2;  // makeCompatHostContext's storage owner
        evmc_bytes32 key{};
        evmc_bytes32 value{};
        value.bytes[31] = 1;
        const uint8_t data = 0xab;
        if (faulted)
        {
            host.recordHostFault("injected storage fault", /*halting=*/false);
        }

        BOOST_CHECK_EQUAL(host.hostInterface->account_exists(context, &address), !faulted);
        host.hostInterface->set_transient_storage(context, &address, &key, &value);
        auto stored = host.getTransientStorage(&key);
        BOOST_CHECK_EQUAL(stored.bytes[31], faulted ? 0 : 1);

        host.hostInterface->emit_log(context, &address, &data, 1, &key, 1);
        host.hostInterface->selfdestruct(context, &address, &address);
        BOOST_CHECK_EQUAL(host.getTransactionExecutive()->hasContractTableChanged(), !faulted);
        auto params = host.takeCallParameters();
        BOOST_CHECK_EQUAL(params->logEntries.size(), faulted ? 0U : 1U);
        BOOST_CHECK_EQUAL(host.hasHostFault(), faulted);
        if (faulted)
        {
            BOOST_CHECK_EQUAL(host.hostFault(), "injected storage fault");
            // R3 MEDIUM: non-halting path must preserve ExecuteError::DEAD_LOCK
            // (not BCOS_ERROR(-1)), so TransactionExecutive's DEBUG branch matches.
            BOOST_CHECK_EXCEPTION(
                host.throwIfHostFault(), bcos::Error, [](bcos::Error const& error) {
                    return error.errorCode() == bcos::executor::ExecuteError::DEAD_LOCK &&
                           error.errorMessage() == "injected storage fault";
                });
        }
    }
}


BOOST_AUTO_TEST_SUITE_END()  // CompatSmoke

BOOST_AUTO_TEST_SUITE_END()  // Compat (shared with other compat/*.cpp)

}  // namespace bcos::test
