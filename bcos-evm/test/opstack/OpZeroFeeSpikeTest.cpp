// Task 4 gate 1: 零费用 spike 可行性。在「无 L1Block predeploy → loadOpFeeParams 读零槽」的前提下，
// 验证真实 opValidate/opTransition 路径对 evmone test::TestState 可用。
// 四测全绿 → spike 成立（Task 5 进入条件）；任一失败 → spike 不成立（Phase 1 收敛为纯基线）。
//
// 注：本文件是独立 TU，blk()/baseTx() 从 OpValidateTest.cpp 拷入并按 OpTransitionTest.cpp 模式适配
// （blk() 增 coinbase = OP_SEQUENCER_FEE_VAULT，令 priority tip 落到可断言的具名金库）。
#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x1000000000000000000000000000000000000001_address;
constexpr auto kRecipient = 0x2000000000000000000000000000000000000002_address;
constexpr auto kSenderBalance = 1000000000000000000_u256;  // 1 ETH（显式 wei；_ether 字面量不存在）

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase =
        OP_SEQUENCER_FEE_VAULT;  // 适配：priority tip 计入 sequencer 金库（OpTransitionTest 同款）
    return b;
}

state::Transaction baseTx()
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpZeroFeeSpikeTests)

// 1) 零槽 → 零费用：TestState 不插入 OP_L1_BLOCK → loadOpFeeParams 全零
BOOST_AUTO_TEST_CASE(loadOpFeeParams_reads_zero_without_L1Block)
{
    evmone::test::TestState ts;
    auto fee = loadOpFeeParams(ts);
    BOOST_CHECK_EQUAL(fee.l1_base_fee, 0);
    BOOST_CHECK_EQUAL(fee.base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(fee.blob_base_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(fee.blob_base_fee, 0);
    BOOST_CHECK_EQUAL(fee.operator_fee_scalar, 0u);
    BOOST_CHECK_EQUAL(fee.operator_fee_constant, 0ull);
    BOOST_CHECK_EQUAL(fee.da_footprint_gas_scalar, 0u);
}

// 2) 零费用参数 → 零 cost：flzLen>0 路径
BOOST_AUTO_TEST_CASE(zero_fee_params_produce_zero_costs)
{
    evmone::test::TestState ts;
    auto fee = loadOpFeeParams(ts);
    auto isthmus = isthmusConfig();
    BOOST_CHECK_EQUAL(computeL1CostFromFlz(fee, /*flzLen=*/1, isthmus), 0);
    BOOST_CHECK_EQUAL(computeOperatorCost(fee, /*gas=*/21000, isthmus), 0);
}

// 3) opValidate 零费用余额检查通过：普通转账（sender 1 ETH），dummy 非空 envelope
BOOST_AUTO_TEST_CASE(opValidate_zero_fee_passes_balance_check)
{
    evmone::test::TestState ts;
    ts[kSender] = {.balance = kSenderBalance};  // 必须插入 sender，否则 get_account 缺失 balance=0
                                                // 必拒
    auto isthmus = isthmusConfig();
    auto block = blk();
    auto tx = baseTx();
    evmc::bytes envelope{0x02};  // dummy 非空即可，无需真实签名
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(props));
}

// 4) 端到端：opTransition 执行 + .apply(diff) 写回 + post 断言——gate 才真正验证 spike 前提
BOOST_AUTO_TEST_CASE(opTransition_zero_fee_writes_back_state)
{
    evmone::test::TestState ts;
    ts[kSender] = {.balance = kSenderBalance};
    ts[kRecipient] = {};
    auto isthmus = isthmusConfig();
    auto vm = evmc::VM{evmc_create_evmone()};  // opTransition 签名取 evmc::VM&（evmone::VM 是 C
                                               // 结构体派生类，不适用）
    auto block = blk();
    test::TestBlockHashes hashes;
    auto tx = baseTx();
    tx.to = kRecipient;  // 使「recipient 余额 +value」断言有意义（baseTx 无 to=CREATE）
    tx.value = intx::uint256{12345};
    evmc::bytes envelope{0x02};
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(props));
    auto const& p = std::get<OpTxProperties>(props);

    // 零费用前提（validate 侧）：无 L1Block → l1_cost / operator_cost 均为 0。
    BOOST_CHECK_EQUAL(p.l1_cost, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.operator_cost_at_gas_limit, intx::uint256{0});

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmus, vm, p, /*chainId=*/1, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // ---- 断言值推导（自 blk()/baseTx()/OpTransition.cpp:237-323）----
    //   blk().base_fee = 7；tx.max_gas_price = 1000，tx.max_priority_gas_price = 10
    //   priority = min(max_priority, max_gas - base_fee) = min(10, 993) = 10
    //   effective_gas_price = base_fee + priority = 17
    //   纯 EOA 转账（空 calldata）→ gas_used = intrinsic = 21000（EIP-7623 floor 亦 21000）
    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR->gasUsed())};
    BOOST_CHECK_EQUAL(gasUsed, intx::uint256{21000});
    const auto priority = std::min(intx::uint256{tx.max_priority_gas_price},
        intx::uint256{tx.max_gas_price} - intx::uint256{block.base_fee});
    const auto effective = intx::uint256{block.base_fee} + priority;
    BOOST_CHECK_EQUAL(priority, intx::uint256{10});
    BOOST_CHECK_EQUAL(effective, intx::uint256{17});

    // sender 净扣款 = gas_used * effective + value（L1/operator 均为 0；value 在 host.call
    // 内转移）。
    BOOST_CHECK_EQUAL(
        ts.at(kSender).balance, kSenderBalance - gasUsed * effective - intx::uint256{tx.value});
    // recipient 收到转移的 value。
    BOOST_CHECK_EQUAL(ts.at(kRecipient).balance, intx::uint256{tx.value});
    // base_fee 部分燃烧入 OP_BASE_FEE_VAULT；tip 入 coinbase（sequencer 金库）。
    BOOST_CHECK_EQUAL(ts.at(OP_BASE_FEE_VAULT).balance, gasUsed * intx::uint256{block.base_fee});
    BOOST_CHECK_EQUAL(ts.at(OP_SEQUENCER_FEE_VAULT).balance, gasUsed * priority);
    // 零 L1/operator 费：两金库被 touch 但 credit 0 → build_diff 视作空账户删除、被
    // sanitizeStateDiff 剥离（view 中无此账户）→ 写回后不存在。这正是「零费用」的状态可见形态。
    BOOST_CHECK(ts.find(OP_L1_FEE_VAULT) == ts.end());
    BOOST_CHECK(ts.find(OP_OPERATOR_FEE_VAULT) == ts.end());
}

BOOST_AUTO_TEST_SUITE_END()
