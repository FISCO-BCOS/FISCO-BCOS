// Task 4 gate 1: zero-fee spike feasibility. Under the premise "no L1Block predeploy ->
// loadOpFeeParams reads zero slots", verifies the real opValidate/opTransition path works
// on evmone test::TestState.
// All six tests green -> spike holds (Task 5 entry condition); any failure -> spike does
// not hold (Phase 1 converges to a pure baseline).
//
// Note: this file is an independent TU; spikeBlk()/spikeBaseTx() are copied from OpValidateTest.cpp
// and adapted to the OpTransitionTest.cpp pattern (spikeBlk() adds coinbase =
// OP_SEQUENCER_FEE_VAULT so the priority tip lands in an assertable named vault).
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
constexpr auto kSpikeSender = 0x1000000000000000000000000000000000000001_address;
constexpr auto kRecipient = 0x2000000000000000000000000000000000000002_address;
constexpr auto kSenderBalance =
    1000000000000000000_u256;  // 1 ETH (explicit wei; no _ether literal)

state::BlockInfo spikeBlk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = OP_SEQUENCER_FEE_VAULT;  // adaptation: priority tip lands in the sequencer vault
                                          // (same as OpTransitionTest)
    return b;
}

state::Transaction spikeBaseTx()
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSpikeSender;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpZeroFeeSpikeTests)

// 1) zero slots -> zero fees: TestState does not insert OP_L1_BLOCK -> loadOpFeeParams is all zero
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

// 2) zero fee params -> zero costs: flzLen>0 path + flzLen==0 early-return branch
BOOST_AUTO_TEST_CASE(zero_fee_params_produce_zero_costs)
{
    evmone::test::TestState ts;
    auto fee = loadOpFeeParams(ts);
    auto isthmus = isthmusConfig();
    BOOST_CHECK_EQUAL(computeL1CostFromFlz(fee, /*flzLen=*/1, isthmus), 0);
    BOOST_CHECK_EQUAL(computeL1CostFromFlz(fee, /*flzLen=*/0, isthmus), 0);  // 空 calldata 早退分支
    BOOST_CHECK_EQUAL(computeOperatorCost(fee, /*gas=*/21000, isthmus), 0);
    BOOST_CHECK_EQUAL(computeOperatorCost(fee, /*gas=*/0, isthmus), 0);
}

// 3) opValidate zero-fee balance check passes: plain transfer (sender 1 ETH), dummy non-empty
// envelope
BOOST_AUTO_TEST_CASE(opValidate_zero_fee_passes_balance_check)
{
    evmone::test::TestState ts;
    ts[kSpikeSender] = {.balance = kSenderBalance, .storage = {}, .code = {}};
    // sender must be inserted; otherwise get_account returns balance=0 and the balance check
    // rejects
    auto isthmus = isthmusConfig();
    auto block = spikeBlk();
    auto tx = spikeBaseTx();
    evmc::bytes envelope{0x02};  // dummy non-empty suffices; no real signature needed
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(props));
}

// 3b) Negative: zero-balance sender (not inserted) is rejected by the same balance check —
// pins that the check actually exists (a regression that deletes it would turn 3b red
// while 3 stays green).
BOOST_AUTO_TEST_CASE(opValidate_zero_balance_sender_rejected)
{
    evmone::test::TestState ts;  // no sender inserted → get_account balance=0
    auto isthmus = isthmusConfig();
    auto block = spikeBlk();
    auto tx = spikeBaseTx();
    evmc::bytes envelope{0x02};
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(props));
}

// 4) End-to-end: opTransition executes + .apply(diff) write-back + post assertions — only here does
// the gate truly verify the spike premise
BOOST_AUTO_TEST_CASE(opTransition_zero_fee_writes_back_state)
{
    evmone::test::TestState ts;
    ts[kSpikeSender] = {.balance = kSenderBalance, .storage = {}, .code = {}};
    ts[kRecipient] = {};
    auto isthmus = isthmusConfig();
    auto vm = evmc::VM{evmc_create_evmone()};  // opTransition signature takes evmc::VM& (evmone::VM
                                               // is a C struct-derived class, not applicable)
    auto block = spikeBlk();
    test::TestBlockHashes hashes;
    auto tx = spikeBaseTx();
    tx.to = kRecipient;  // makes the "recipient balance +value" assertion meaningful (spikeBaseTx
                         // has no to=CREATE)
    tx.value = intx::uint256{12345};
    evmc::bytes envelope{0x02};
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(props));
    auto const& p = std::get<OpTxProperties>(props);

    // Zero-fee premise (validate side): no L1Block -> l1_cost / operator_cost are both 0.
    BOOST_CHECK_EQUAL(p.l1_cost, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.operator_cost_at_gas_limit, intx::uint256{0});

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmus, vm, p, /*chainId=*/1, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // ---- assertion-value derivation (from spikeBlk()/spikeBaseTx()/OpTransition.cpp:237-323) ----
    //   spikeBlk().base_fee = 7; tx.max_gas_price = 1000, tx.max_priority_gas_price = 10
    //   priority = min(max_priority, max_gas - base_fee) = min(10, 993) = 10
    //   effective_gas_price = base_fee + priority = 17
    //   plain EOA transfer (empty calldata) -> gas_used = intrinsic = 21000 (EIP-7623 floor also
    //   21000)
    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR->gasUsed())};
    BOOST_CHECK_EQUAL(gasUsed, intx::uint256{21000});
    const auto priority = std::min(intx::uint256{tx.max_priority_gas_price},
        intx::uint256{tx.max_gas_price} - intx::uint256{block.base_fee});
    const auto effective = intx::uint256{block.base_fee} + priority;
    BOOST_CHECK_EQUAL(priority, intx::uint256{10});
    BOOST_CHECK_EQUAL(effective, intx::uint256{17});

    // sender net debit = gas_used * effective + value (L1/operator both 0; value is
    // transferred inside host.call).
    BOOST_CHECK_EQUAL(ts.at(kSpikeSender).balance,
        kSenderBalance - gasUsed * effective - intx::uint256{tx.value});
    // recipient receives the transferred value.
    BOOST_CHECK_EQUAL(ts.at(kRecipient).balance, intx::uint256{tx.value});
    // the base_fee portion burns into OP_BASE_FEE_VAULT; the tip goes to coinbase (sequencer
    // vault).
    BOOST_CHECK_EQUAL(ts.at(OP_BASE_FEE_VAULT).balance, gasUsed * intx::uint256{block.base_fee});
    BOOST_CHECK_EQUAL(ts.at(OP_SEQUENCER_FEE_VAULT).balance, gasUsed * priority);
    // Zero L1/operator fee: both vaults are touched but credited 0 -> build_diff treats
    // them as empty-account deletions, stripped by sanitizeStateDiff (absent from the
    // view) -> they do not exist after write-back. This is the observable state shape of
    // "zero fees".
    BOOST_CHECK(ts.find(OP_L1_FEE_VAULT) == ts.end());
    BOOST_CHECK(ts.find(OP_OPERATOR_FEE_VAULT) == ts.end());
}

// 4b) base_fee=0 variant: effective gas price collapses to the priority tip (no base-fee burn),
// and the base-fee vault is never touched. Pins the effective=priority path (spikeBlk() fixed
// base_fee=7 elsewhere).
BOOST_AUTO_TEST_CASE(opTransition_zero_base_fee_no_burn)
{
    evmone::test::TestState ts;
    ts[kSpikeSender] = {.balance = kSenderBalance, .storage = {}, .code = {}};
    ts[kRecipient] = {};
    auto isthmus = isthmusConfig();
    auto vm = evmc::VM{evmc_create_evmone()};
    auto block = spikeBlk();
    block.base_fee = 0;
    test::TestBlockHashes hashes;
    auto tx = spikeBaseTx();
    tx.to = kRecipient;
    tx.value = intx::uint256{12345};
    evmc::bytes envelope{0x02};
    auto props = opValidateFromState(ts, block, tx, envelope, isthmus, /*blockGasLeft=*/30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(props));
    auto const& p = std::get<OpTxProperties>(props);
    BOOST_CHECK_EQUAL(p.l1_cost, intx::uint256{0});
    BOOST_CHECK_EQUAL(p.operator_cost_at_gas_limit, intx::uint256{0});

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmus, vm, p, /*chainId=*/1, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR->gasUsed())};
    BOOST_CHECK_EQUAL(gasUsed, intx::uint256{21000});
    // base_fee=0 -> priority = min(max_priority, max_gas - 0) = 10, effective = 0 + 10 = 10
    BOOST_CHECK_EQUAL(ts.at(kSpikeSender).balance,
        kSenderBalance - gasUsed * intx::uint256{10} - intx::uint256{tx.value});
    BOOST_CHECK_EQUAL(ts.at(kRecipient).balance, intx::uint256{tx.value});
    // no base-fee burn: the base-fee vault is never created/touched.
    BOOST_CHECK(ts.find(OP_BASE_FEE_VAULT) == ts.end());
    // the whole tip goes to the sequencer vault.
    BOOST_CHECK_EQUAL(ts.at(OP_SEQUENCER_FEE_VAULT).balance, gasUsed * intx::uint256{10});
}

BOOST_AUTO_TEST_SUITE_END()
