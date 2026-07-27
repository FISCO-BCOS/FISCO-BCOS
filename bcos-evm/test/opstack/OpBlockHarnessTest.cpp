#include "OpL1AttributesTestHelpers.h"
#include "OpPredeploysSeed.h"
#include "StateDiffWriteback.h"
#include <bcos-evm/opstack/OpBlock.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testhelpers;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
OpBlockTx wrap(DepositTx d)
{
    return {.tx = std::move(d), .signedEnvelope = {}};
}

OpBlockTx wrap(state::Transaction tx, evmc::bytes_view env)
{
    return {.tx = std::move(tx), .signedEnvelope = evmc::bytes{env.begin(), env.end()}};
}
}  // namespace

TEST(OpBlockHarness, IsthmusBlockWithAttributesDeposit)
{
    constexpr auto user = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    constexpr auto depFrom = 0x00000000000000000000000000000000000000cc_address;
    auto vm = evmc::VM{evmc_create_evmone()};

    test::TestState ts;
    ts[user] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    ts[depFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    seedOpPredeploys(ts);
    // L1Block setter：SSTORE slot1<-cd[0], slot3<-cd[32], slot7<-cd[64], slot8<-cd[96]
    ts[OP_L1_BLOCK].code =
        evmc::from_hex("60003560015560203560035560403560075560603560085500").value();
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;
    test::TestBlockHashes hashes;

    // pre-block 系统调用（EIP-4788/2935）由 processOpBlock 内部执行（evmone
    // system_call_block_start，REF 已导出——旧注释"未导出"有误，2026-07-11 勘正）。

    // L1 attributes deposit 首笔：写 L1Block 槽 1/3/7/8。
    const auto attrData = packL1AttributesData(
        /*l1BaseFee=*/1000000000, /*baseScalar=*/2, /*blobScalar=*/3,
        /*blobBaseFee=*/10000000, /*opScalar=*/1000000, /*opConst=*/0);
    DepositTx attr{.source_hash = 0x01_bytes32,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 200000,
        .is_system_tx = false,
        .data = toBytes(attrData)};

    // user deposit
    DepositTx dep{.source_hash = 0x02_bytes32,
        .from = depFrom,
        .to = depFrom,
        .mint = intx::uint256{1000},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};

    // 普通转账
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = user;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    std::vector<uint8_t> env(50, 0x11);

    std::vector<OpBlockTx> txs{wrap(attr), wrap(dep), wrap(tx, {env.data(), env.size()})};
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    const auto r = processOpBlock(ts, block, hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 3u);
    const auto& attrR = std::get<OpDepositReceipt>(r.receipts[0]);
    const auto& depR = std::get<OpDepositReceipt>(r.receipts[1]);
    const auto& txR = std::get<OpTxReceipt>(r.receipts[2]);
    ASSERT_EQ(attrR.receipt.status, EVMC_SUCCESS);
    ASSERT_EQ(depR.receipt.status, EVMC_SUCCESS);
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(depFrom).balance, intx::uint256{1000});

    const auto fee = loadOpFeeParams(ts);
    // 对照：与手读四槽一致（保留一次断言）
    const auto manualFee = unpackOpFeeParams(ts.get_storage(OP_L1_BLOCK, slotKey(1)),
        ts.get_storage(OP_L1_BLOCK, slotKey(3)), ts.get_storage(OP_L1_BLOCK, slotKey(7)),
        ts.get_storage(OP_L1_BLOCK, slotKey(8)));
    EXPECT_EQ(fee.l1_base_fee, manualFee.l1_base_fee);
    EXPECT_EQ(fee.l1_base_fee, 1000000000_u256);
    EXPECT_EQ(fee.base_fee_scalar, 2u);
    EXPECT_EQ(fee.blob_base_fee_scalar, 3u);
    EXPECT_EQ(fee.blob_base_fee, 10000000_u256);
    EXPECT_EQ(fee.operator_fee_scalar, 1000000u);
    EXPECT_EQ(fee.operator_fee_constant, 0u);

    // 四 vault 守恒 + cumulative gas：数值不变，锚改为库产出
    // （props.l1_cost → OpTxReceipt.meta.l1_fee；手工 cumulative 累加 → r.gasUsed）。
    ASSERT_TRUE(txR.meta.l1_fee.has_value());
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance, txR.meta.l1_fee.value());
    EXPECT_GT(ts.at(OP_BASE_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_GT(ts.at(OP_SEQUENCER_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_GT(ts.at(OP_OPERATOR_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_EQ(r.gasUsed, attrR.receipt.gas_used + depR.receipt.gas_used + txR.receipt.gas_used);
    EXPECT_GT(block.gas_limit - r.gasUsed, 0);
    // finalize：processOpBlock 内部已接线调用（OP 块 withdrawals 恒空）；
    // receiptRoot/withdrawalsRoot 属块头层非 M-B1 目标。
}

// FromState≡ 钉扎（拆自库化前的手工顺序段，保留手工最小序列）：*FromState 变体自行从 view
// 读取块中间态（fee 参数），不接受注入参数——钉的是该 API 本身与注入式路径的等价性，
// 需要真实的块中间态（attributes 已执行写回），不能也不必调库化（processOpBlock 是块级
// 编排入口，不是逐笔 API 的替代品）。
TEST(OpBlockHarness, FromStateApiCrossCheck)
{
    constexpr auto user = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};

    test::TestState ts;
    ts[user] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    seedOpPredeploys(ts);
    ts[OP_L1_BLOCK].code =
        evmc::from_hex("60003560015560203560035560403560075560603560085500").value();
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;
    test::TestBlockHashes hashes;
    int64_t blockGasLeft = block.gas_limit;

    // 手工最小序列：attributes 真执行 runDeposit + 写回（*FromState 需要块中间态：
    // L1Block 槽已被 attributes 写入）。
    const auto attrData = packL1AttributesData(
        /*l1BaseFee=*/1000000000, /*baseScalar=*/2, /*blobScalar=*/3,
        /*blobBaseFee=*/10000000, /*opScalar=*/1000000, /*opConst=*/0);
    DepositTx attr{.source_hash = 0x01_bytes32,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 200000,
        .is_system_tx = false,
        .data = toBytes(attrData)};
    const auto attrR = runDeposit(ts, block, hashes, attr, isthmusConfig(), vm, 1234, blockGasLeft);
    ASSERT_EQ(attrR.receipt.status, EVMC_SUCCESS);
    bcos::evm::applyStateDiffStrict(ts, attrR.receipt.state_diff);
    blockGasLeft -= attrR.receipt.gas_used;

    const auto fee = loadOpFeeParams(ts);

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = user;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    std::vector<uint8_t> env(50, 0x11);

    // 注入式路径（对拍参照值）
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, blockGasLeft);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, {env.data(), env.size()});
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);

    // FromState 路径：自行从 view 读取 fee 参数，与注入式路径对拍。
    const auto vFS =
        opValidateFromState(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), blockGasLeft);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(vFS));
    const auto& propsFS = std::get<OpTxProperties>(vFS);
    const auto txRFS = opTransitionFromState(
        ts, block, hashes, tx, isthmusConfig(), vm, propsFS, 1234, {env.data(), env.size()});
    EXPECT_EQ(txRFS.receipt.gas_used, txR.receipt.gas_used);
    EXPECT_EQ(propsFS.l1_cost, props.l1_cost);
}
