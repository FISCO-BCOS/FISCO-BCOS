/*
 * Unit tests for EIP-7623 TE gas settlement helpers (spec §6.1).
 */
#include "bcos-transaction-executor/gas/EthTxGasSettlement.h"
#include "bcos-executor/src/CallParameters.h"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/vm/VMInstance.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include <boost/test/unit_test.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::executor_v1::gas;

namespace
{
ledger::Features pragueFeatures()
{
    ledger::Features features;
    features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    features.set(ledger::Features::Flag::feature_evm_cancun);
    features.set(ledger::Features::Flag::feature_evm_prague);
    return features;
}

bytes mixedCalldata100()
{
    bytes mixed(100);
    std::fill(mixed.begin(), mixed.begin() + 50, 0x00);
    std::fill(mixed.begin() + 50, mixed.end(), 0x42);
    return mixed;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(EthTxGasSettlementTest)

BOOST_AUTO_TEST_CASE(Eip7623Components_mixed_calldata)
{
    auto const mixed = mixedCalldata100();
    auto const c = calcEip7623Components(ref(mixed));
    BOOST_CHECK_EQUAL(c.normalCost, 1000);
    BOOST_CHECK_EQUAL(c.floorCost, 2500);
    BOOST_CHECK_EQUAL(c.tokenCount, 250);
    BOOST_CHECK_EQUAL(calcEip7623CalldataGas(ref(mixed)), std::max(c.normalCost, c.floorCost));
}

BOOST_AUTO_TEST_CASE(EthGasSettlementEnabled_gateMatrix)
{
    auto const features = pragueFeatures();
    auto const revision = bcos::executor::toRevision(
        features, static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));

    BOOST_CHECK(ethGasSettlementEnabled(features, revision, 0, true));
    BOOST_CHECK(!ethGasSettlementEnabled(features, revision, 0, false));
    BOOST_CHECK(!ethGasSettlementEnabled(features, revision, 1, true));
    BOOST_CHECK(!ethGasSettlementEnabled(features, EVMC_CANCUN, 0, true));

    ledger::Features noPrague;
    noPrague.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    noPrague.set(ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK(!ethGasSettlementEnabled(noPrague, revision, 0, true));
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_emptyCalldata)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0, nullptr);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 0);
    BOOST_CHECK_EQUAL(intrinsic.floorReserve, 0);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), TX_BASE_GAS);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_mixedCalldata_preExecutionUsesNormalNotFloor)
{
    auto const mixed = mixedCalldata100();
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0, nullptr);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 1000);
    BOOST_CHECK_EQUAL(intrinsic.floorReserve, 2500);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + 1000);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), TX_BASE_GAS + 2500);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_accessList_cost)
{
    executor::Eip2930AccessList list;
    list.emplace_back(
        "0x00000000000000000000000000000000000000aa", std::vector<h256>{h256(1), h256(2)});
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 2, nullptr);
    BOOST_CHECK_EQUAL(
        intrinsic.accessListCost, ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + ACCESS_LIST_ADDRESS_COST + 3800);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_eip7702_only_type4)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;

    executor::Eip7702AuthorizationList authList(2);
    auto const type4Intrinsic = computeTxIntrinsicGas(msg, nullptr, 4, std::addressof(authList));
    auto const type2Intrinsic = computeTxIntrinsicGas(msg, nullptr, 2, std::addressof(authList));
    BOOST_CHECK_EQUAL(type4Intrinsic.eip7702AuthCost, 50000);
    BOOST_CHECK_EQUAL(type2Intrinsic.eip7702AuthCost, 0);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_createIntrinsic_words)
{
    bytes initcode(33);
    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.input_data = initcode.data();
    msg.input_size = initcode.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0, nullptr);
    BOOST_CHECK_EQUAL(intrinsic.createIntrinsic, CREATE_BASE_GAS + INITCODE_WORD_GAS * 2);
    BOOST_CHECK_EQUAL(intrinsic.normalCalldata, 132);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + 132 + CREATE_BASE_GAS + 4);
}

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_cases)
{
    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = TX_BASE_GAS;
    ctx.calldata.normalCost = 800;
    ctx.calldata.floorCost = 1000;
    ctx.createTerm = 0;

    ctx.gasBeforeEvm = 100000;
    ctx.evmGasRefund = 0;

    ctx.evmGasLeft = 99500;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), 22300);

    ctx.evmGasLeft = 99800;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), 22000);

    ctx.evmGasLeft = 99950;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), 22000);
}

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_floorDominatesLowExecution)
{
    auto const mixed = mixedCalldata100();
    auto const components = calcEip7623Components(ref(mixed));

    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = TX_BASE_GAS;
    ctx.calldata = components;
    ctx.gasBeforeEvm = 100000;
    ctx.evmGasLeft = 99950;
    ctx.evmGasRefund = 0;

    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), TX_BASE_GAS + components.floorCost);
}

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_task0SstoreClear_vector)
{
    // Task0 SSTORE clear: gas_before=1M, gas_left=997094, gas_refund=4800, executionBurn=2906.
    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = TX_BASE_GAS;
    ctx.calldata = {};
    ctx.gasBeforeEvm = 1'000'000;
    ctx.evmGasLeft = 997'094;
    ctx.evmGasRefund = 4800;

    BOOST_CHECK_EQUAL(ctx.gasBeforeEvm - ctx.evmGasLeft, 2906);
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), TX_BASE_GAS);
}

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_7702RefundSubjectTo3529Cap)
{
    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = TX_BASE_GAS + 2 * executor_v1::EIP_7702_PER_EMPTY_ACCOUNT_COST;
    ctx.calldata.normalCost = 0;
    ctx.calldata.floorCost = 0;
    ctx.gasBeforeEvm = 400'000;
    ctx.evmGasLeft = 399'000;
    ctx.evmGasRefund = 50'000;

    int64_t const executionBurn = ctx.gasBeforeEvm - ctx.evmGasLeft;
    int64_t const gasUsedBeforeRefund =
        ctx.fixedIntrinsic + ctx.calldata.normalCost + executionBurn + ctx.createTerm;
    int64_t const cap = effectiveRefundEip3529(ctx.evmGasRefund, gasUsedBeforeRefund);
    BOOST_CHECK_EQUAL(cap, gasUsedBeforeRefund / 5);
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), ctx.fixedIntrinsic);
}

BOOST_AUTO_TEST_CASE(EffectiveRefundEip3529_cap)
{
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(10000, 1000), 200);
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(100, 1000), 100);
    BOOST_CHECK_EQUAL(effectiveRefundEip3529(100, 0), 0);
}

BOOST_AUTO_TEST_CASE(FinalizeWithoutEvmStart_intrinsicOOG_vector)
{
    TxGasSettlementContext ctx;
    ctx.gasLimit = 20'000;
    auto const debit = TX_BASE_GAS;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsedWithoutEvmStart(ctx, debit, 0, true), 20'000);
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsedWithoutEvmStart(ctx, debit, 0, false), 20'000);
    ctx.gasLimit = 500'000;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsedWithoutEvmStart(ctx, debit, 0, true), debit);
}

BOOST_AUTO_TEST_SUITE_END()
