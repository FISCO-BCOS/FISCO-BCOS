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

    BOOST_CHECK(eip7623Active(features, revision));
    BOOST_CHECK(!eip7623Active(features, EVMC_CANCUN));
    BOOST_CHECK(eip7623TxpoolValidationEnabled(features, revision, true));
    BOOST_CHECK(!eip7623TxpoolValidationEnabled(features, revision, false));
    BOOST_CHECK(!eip7623TxpoolValidationEnabled(features, EVMC_CANCUN, true));

    BOOST_CHECK(ethGasSettlementEnabled(features, revision, 0, true));
    BOOST_CHECK(!ethGasSettlementEnabled(features, revision, 0, false));
    BOOST_CHECK(!ethGasSettlementEnabled(features, revision, 1, true));
    BOOST_CHECK(!ethGasSettlementEnabled(features, EVMC_CANCUN, 0, true));

    ledger::Features noPrague;
    noPrague.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
    noPrague.set(ledger::Features::Flag::feature_evm_cancun);
    BOOST_CHECK(!eip7623Active(noPrague, revision));
    BOOST_CHECK(!ethGasSettlementEnabled(noPrague, revision, 0, true));
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_emptyCalldata)
{
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
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
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
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
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 2);
    BOOST_CHECK_EQUAL(
        intrinsic.accessListCost, ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST);
    BOOST_CHECK_EQUAL(intrinsic.preExecutionDebit(), TX_BASE_GAS + ACCESS_LIST_ADDRESS_COST + 3800);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_gasLimitMinimum_gethAligned_accessList)
{
    // geth: max(21000+floor, 21000+access+normal). Single non-zero byte "x": floor=40, normal=16.
    executor::Eip2930AccessList list;
    list.emplace_back(
        Address("0x00000000000000000000000000000000000000aa"), std::vector<h256>{h256(1), h256(2)});
    bcos::bytes const input = bcos::asBytes("x");
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = input.data();
    msg.input_size = input.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 1);

    constexpr int64_t accessListCost = ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST;
    constexpr int64_t floorTotal = TX_BASE_GAS + 40;
    constexpr int64_t intrinsicTotal = TX_BASE_GAS + accessListCost + 16;
    BOOST_CHECK_EQUAL(intrinsicTotal, 27216);
    BOOST_CHECK_EQUAL(floorTotal, 21040);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), intrinsicTotal);
    BOOST_CHECK_NE(intrinsic.gasLimitMinimum(), TX_BASE_GAS + accessListCost + 40);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_gasLimitMinimum_gethAligned_dataHeavyWithAccessList)
{
    // 100-byte mixed calldata: normal=1000, floor=2500; access list adds 6200 to intrinsic only.
    auto const mixed = mixedCalldata100();
    executor::Eip2930AccessList list;
    list.emplace_back(
        Address("0x00000000000000000000000000000000000000aa"), std::vector<h256>{h256(1), h256(2)});
    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, std::addressof(list), 1);

    constexpr int64_t accessListCost = ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST;
    constexpr int64_t floorTotal = TX_BASE_GAS + 2500;
    constexpr int64_t intrinsicTotal = TX_BASE_GAS + accessListCost + 1000;
    BOOST_CHECK_EQUAL(intrinsicTotal, 28200);
    BOOST_CHECK_EQUAL(floorTotal, 23500);
    BOOST_CHECK_EQUAL(intrinsic.gasLimitMinimum(), intrinsicTotal);
    BOOST_CHECK_NE(intrinsic.gasLimitMinimum(), TX_BASE_GAS + accessListCost + 2500);
}

BOOST_AUTO_TEST_CASE(ComputeTxIntrinsicGas_createIntrinsic_words)
{
    bytes initcode(33);
    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.input_data = initcode.data();
    msg.input_size = initcode.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 0);
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

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_highIntrinsicRefundSubjectTo3529Cap)
{
    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = TX_BASE_GAS + ACCESS_LIST_ADDRESS_COST + 2 * ACCESS_LIST_STORAGE_KEY_COST;
    ctx.calldata.normalCost = 0;
    ctx.calldata.floorCost = 0;
    ctx.gasBeforeEvm = 400'000;
    ctx.evmGasLeft = 399'000;
    ctx.evmGasRefund = 50'000;

    int64_t const executionBurn = ctx.gasBeforeEvm - ctx.evmGasLeft;
    int64_t const createExtra =
        (ctx.createTerm > 0 && executionBurn < ctx.createTerm) ? ctx.createTerm - executionBurn : 0;
    int64_t const gasUsedBeforeRefund =
        ctx.fixedIntrinsic + ctx.calldata.normalCost + executionBurn + createExtra;
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

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_create_noDoubleCount)
{
    bytes initcode(10, 0x42);
    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    msg.input_data = initcode.data();
    msg.input_size = initcode.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 2);

    TxGasSettlementContext ctx;
    ctx.fixedIntrinsic = intrinsic.fixedCost();
    ctx.calldata.normalCost = intrinsic.normalCalldata;
    ctx.calldata.floorCost = intrinsic.floorReserve;
    ctx.createTerm = intrinsic.createIntrinsic;
    ctx.gasBeforeEvm = 50'000;
    // evmone debited full CREATE intrinsic from the execution gas pool.
    ctx.evmGasLeft = ctx.gasBeforeEvm - intrinsic.createIntrinsic;
    ctx.evmGasRefund = 0;

    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), intrinsic.gasLimitMinimum());

    // When executionBurn already includes createTerm, adding createTerm again must not inflate.
    ctx.gasBeforeEvm =
        intrinsic.gasLimitMinimum() - intrinsic.fixedCost() - intrinsic.normalCalldata;
    constexpr int64_t extraOpcodeGas = 38;
    ctx.evmGasLeft = ctx.gasBeforeEvm - intrinsic.createIntrinsic - extraOpcodeGas;
    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), intrinsic.gasLimitMinimum() + extraOpcodeGas);
}

BOOST_AUTO_TEST_CASE(FinalizeEthereumGasUsed_postEvmOOG_chargesFullGasLimit)
{
    auto const mixed = mixedCalldata100();
    auto const components = calcEip7623Components(ref(mixed));

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.input_data = mixed.data();
    msg.input_size = mixed.size();
    auto const intrinsic = computeTxIntrinsicGas(msg, nullptr, 2);
    auto const gasLimit = intrinsic.gasLimitMinimum();

    TxGasSettlementContext ctx;
    ctx.gasLimit = gasLimit;
    ctx.fixedIntrinsic = intrinsic.fixedCost();
    ctx.calldata = components;
    ctx.gasBeforeEvm = gasLimit - intrinsic.fixedCost() - intrinsic.normalCalldata;
    ctx.evmGasLeft = 0;
    ctx.evmGasRefund = 0;

    BOOST_CHECK_EQUAL(finalizeEthereumGasUsed(ctx), gasLimit);
}

BOOST_AUTO_TEST_SUITE_END()
