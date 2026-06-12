/*
 * Task 0 (EIP-7623 TE gas settlement): freeze evmone baseline gas_left / gas_refund semantics.
 * Uses evmc::MockedHost + evmone::baseline::execute (same path as VMInstance.cpp).
 */
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <evmc/evmc.hpp>
#include <evmc/mocked_host.hpp>
#include <evmone/baseline.hpp>
#include <evmone/vm.hpp>

namespace
{
using evmc::address;
using evmc::bytes32;
using evmc::MockedHost;

evmc_result runBytecode(MockedHost& host, evmc_revision rev, int64_t gas, address const& contract,
    std::initializer_list<uint8_t> code)
{
    auto& account = host.accounts[contract];
    account.code.assign(code);

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = gas;
    msg.recipient = contract;
    msg.code_address = contract;
    msg.sender = address{0x42};

    const auto container = evmc::bytes{code};
    const auto analysis = evmone::baseline::analyze(container);
    evmc::VM vm{evmc_create_evmone()};
    auto* evmoneVm = static_cast<evmone::VM*>(vm.get_raw_pointer());
    return evmone::baseline::execute(
        *evmoneVm, host.get_interface(), host.to_context(), rev, msg, analysis);
}

}  // namespace

BOOST_AUTO_TEST_SUITE(Task0EvmoneRefundSemantics)

/// STOP only: gas_left retains unspent gas; gas_refund stays zero (Branch A).
BOOST_AUTO_TEST_CASE(StopContract_noRefundInGasLeft)
{
    constexpr int64_t kGasBefore = 1'000'000;
    MockedHost host;
    address const contract{0x01};
    auto const result = runBytecode(host, EVMC_PRAGUE, kGasBefore, contract, {0x00});

    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.gas_left, kGasBefore);
    BOOST_CHECK_EQUAL(result.gas_refund, 0);

    const int64_t executionBurn = kGasBefore - result.gas_left;
    BOOST_CHECK_EQUAL(executionBurn, 0);
    BOOST_TEST_MESSAGE("STOP: gas_before=" << kGasBefore << " gas_left=" << result.gas_left
                                           << " gas_refund=" << result.gas_refund
                                           << " executionBurn=" << executionBurn);
}

/// SSTORE slot 1 -> 0: refund accrues in gas_refund, not added back into gas_left (Branch A).
BOOST_AUTO_TEST_CASE(SstoreClear_refundSeparateFromGasLeft)
{
    constexpr int64_t kGasBefore = 1'000'000;
    MockedHost host;
    address const contract{0x02};

    bytes32 const slot{};
    bytes32 one{};
    one.bytes[31] = 1;
    // original=non-zero, current=non-zero -> clearing to zero yields storage refund (EIP-3529).
    host.accounts[contract].storage[slot] = evmc::StorageValue{one, one, EVMC_ACCESS_WARM};

    // PUSH1 0 PUSH1 0 SSTORE STOP
    auto const result =
        runBytecode(host, EVMC_PRAGUE, kGasBefore, contract, {0x60, 0x00, 0x60, 0x00, 0x55, 0x00});

    BOOST_REQUIRE_EQUAL(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(result.gas_refund, 0);
    BOOST_CHECK_LT(result.gas_left, kGasBefore);

    const int64_t executionBurn = kGasBefore - result.gas_left;
    BOOST_CHECK_GT(executionBurn, 0);
    BOOST_CHECK_EQUAL(executionBurn, kGasBefore - result.gas_left);
    // Branch A: refund counter is not fully folded into gas_left (surplus = gas_refund -
    // executionBurn).
    BOOST_CHECK_GT(result.gas_refund, executionBurn);

    BOOST_TEST_MESSAGE("SSTORE clear: gas_before=" << kGasBefore << " gas_left=" << result.gas_left
                                                   << " gas_refund=" << result.gas_refund
                                                   << " executionBurn=" << executionBurn);
}

BOOST_AUTO_TEST_SUITE_END()
