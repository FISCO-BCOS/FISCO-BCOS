/*
 * Unit tests for Eip2929Util, Eip2929PrecompileWarm, and Eip2929TransactionPrewarm.
 */
#include "bcos-executor/src/vm/Eip2929AccessState.h"
#include "bcos-executor/src/vm/Eip2929PrecompileWarm.h"
#include "bcos-executor/src/vm/Eip2929TransactionPrewarm.h"
#include "bcos-executor/src/vm/Eip2929Util.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstring>
#include <vector>

using bcos::executor::Eip2929AccessState;
using bcos::executor::eip2929Enabled;
using bcos::executor::eip2929TransactionEntryWarmEnabled;
using bcos::executor::Eip2929TxPrewarmInput;
using bcos::executor::forEachActivePrecompileAddress;
using bcos::executor::warmEip2929AtTransactionEntry;
using bcos::executor::warmEip2930AccessListOnly;

namespace
{
evmc_address addrByte(uint8_t b)
{
    evmc_address a{};
    a.bytes[19] = b;
    return a;
}

evmc_address precompileAtLastByte(uint8_t lastByte)
{
    evmc_address a{};
    a.bytes[19] = lastByte;
    return a;
}

bool containsPrecompileLastByte(std::vector<evmc_address> const& addrs, uint8_t lastByte)
{
    auto const target = precompileAtLastByte(lastByte);
    return std::any_of(addrs.begin(), addrs.end(), [&](evmc_address const& a) {
        return std::memcmp(a.bytes, target.bytes, sizeof(a.bytes)) == 0;
    });
}

bool containsOsakaPrecompile(std::vector<evmc_address> const& addrs)
{
    return std::any_of(addrs.begin(), addrs.end(),
        [](evmc_address const& a) { return a.bytes[18] == 0x01 && a.bytes[19] == 0x00; });
}

bcos::ledger::Features featuresWithEip2929On()
{
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_evm_eip2929);
    return features;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Eip2929Util)

BOOST_AUTO_TEST_CASE(enabled_requires_berlin_and_feature_flag)
{
    auto const enabled = featuresWithEip2929On();
    bcos::ledger::Features disabled;
    BOOST_CHECK(!eip2929Enabled(EVMC_ISTANBUL, enabled));
    BOOST_CHECK(!eip2929Enabled(EVMC_BERLIN, disabled));
    BOOST_CHECK(eip2929Enabled(EVMC_BERLIN, enabled));
    BOOST_CHECK(eip2929Enabled(EVMC_CANCUN, enabled));
}

BOOST_AUTO_TEST_CASE(enabled_ledger_config_overload)
{
    bcos::ledger::LedgerConfig config;
    config.setFeatures(featuresWithEip2929On());
    BOOST_CHECK(eip2929Enabled(EVMC_BERLIN, config));
}

BOOST_AUTO_TEST_CASE(transaction_entry_warm_gate)
{
    Eip2929AccessState state;
    auto const features = featuresWithEip2929On();
    BOOST_CHECK(eip2929TransactionEntryWarmEnabled(0, EVMC_BERLIN, features, &state));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(1, EVMC_BERLIN, features, &state));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(0, EVMC_BERLIN, features, nullptr));
    BOOST_CHECK(!eip2929TransactionEntryWarmEnabled(0, EVMC_ISTANBUL, features, &state));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Eip2929PrecompileWarm)

BOOST_AUTO_TEST_CASE(berlin_includes_precompiles_1_through_9)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_BERLIN, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 9U);
    for (uint8_t i = 1; i <= 9; ++i)
    {
        BOOST_CHECK(containsPrecompileLastByte(addrs, i));
    }
    BOOST_CHECK(!containsPrecompileLastByte(addrs, 0x0a));
}

BOOST_AUTO_TEST_CASE(cancun_adds_point_evaluation_precompile)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_CANCUN, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 10U);
    BOOST_CHECK(containsPrecompileLastByte(addrs, 0x0a));
}

BOOST_AUTO_TEST_CASE(prague_adds_bls_precompile_range)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_PRAGUE, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 17U);
    for (uint8_t i = 0x0b; i <= 0x11; ++i)
    {
        BOOST_CHECK(containsPrecompileLastByte(addrs, i));
    }
}

BOOST_AUTO_TEST_CASE(osaka_adds_p256verify_precompile)
{
    std::vector<evmc_address> addrs;
    forEachActivePrecompileAddress(EVMC_OSAKA, [&](evmc_address const& a) { addrs.push_back(a); });
    BOOST_CHECK_EQUAL(addrs.size(), 18U);
    BOOST_CHECK(containsOsakaPrecompile(addrs));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(Eip2929TransactionPrewarm)

BOOST_AUTO_TEST_CASE(warm_at_transaction_entry_covers_w1_create_coinbase_and_w2)
{
    Eip2929AccessState state;
    state.pushCheckpoint();

    evmc_address const listAddr = addrByte(0x55);
    evmc_bytes32 slot{};
    slot.bytes[31] = 0x09;
    bcos::executor::Eip2930AccessList accessList{
        {"ignored", {bcos::h256(slot.bytes, bcos::h256::SIZE)}}};

    Eip2929TxPrewarmInput input;
    input.revision = EVMC_CANCUN;
    input.origin = addrByte(0x01);
    input.callee = addrByte(0x02);
    input.createCodeAddress = addrByte(0x03);
    input.coinbase = addrByte(0x04);
    input.web3TypedTxKind = 2;
    input.accessList = &accessList;

    warmEip2929AtTransactionEntry(state, input, [&](std::string const&) { return listAddr; });

    state.rollbackCheckpoint();
    BOOST_CHECK(state.containsAddress(input.origin));
    BOOST_CHECK(state.containsAddress(*input.callee));
    BOOST_CHECK(state.containsAddress(*input.createCodeAddress));
    BOOST_CHECK(state.containsAddress(*input.coinbase));
    BOOST_CHECK(state.containsAddress(listAddr));
    BOOST_CHECK(state.containsStorage(listAddr, slot));
    BOOST_CHECK(state.containsAddress(precompileAtLastByte(1)));
    BOOST_CHECK(state.containsAddress(precompileAtLastByte(0x0a)));
}

BOOST_AUTO_TEST_CASE(create_entry_omits_callee_but_warms_code_address)
{
    Eip2929AccessState state;
    Eip2929TxPrewarmInput input;
    input.revision = EVMC_BERLIN;
    input.origin = addrByte(0x10);
    input.createCodeAddress = addrByte(0x20);

    warmEip2929AtTransactionEntry(state, input, [](std::string const&) {
        BOOST_FAIL("access list converter must not run without W2 input");
        return addrByte(0);
    });

    BOOST_CHECK(state.containsAddress(input.origin));
    BOOST_CHECK(state.containsAddress(*input.createCodeAddress));
}

BOOST_AUTO_TEST_CASE(warm_eip2930_access_list_only_skips_legacy_kind)
{
    Eip2929AccessState state;
    bcos::executor::Eip2930AccessList list{{"ignored", {}}};
    warmEip2930AccessListOnly(state, 0, list, [&](std::string const&) { return addrByte(0x77); });
    BOOST_CHECK(!state.containsAddress(addrByte(0x77)));

    warmEip2930AccessListOnly(state, 1, list, [&](std::string const&) { return addrByte(0x77); });
    BOOST_CHECK(state.containsAddress(addrByte(0x77)));
}

BOOST_AUTO_TEST_SUITE_END()
