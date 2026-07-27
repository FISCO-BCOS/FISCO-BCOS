// FINDING-1 修复判别单测（Task 2）：opTransition 出口 diff 的产地消毒行为。
// 三用例相位（接线前 → 接线后）：① 红 → 绿；② 恒绿（wiring 级对照，防 §3.3
// 错误接线/谓词反转）；③ 红 → 绿。构造照 OpTransitionTest/OpValidateTest 既有形制。
// ④（defer Task 1 审查修复，控制器授权范围更正）：② 的块级对应——真删除穿过
// processOpBlock 出口，堵回放 postState 空≡不存在规约造成的 KEEP 盲区。
#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/opstack/OpBlock.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include "OpPredeploysSeed.h"
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <optional>
#include <bcos-evm/eth/state/host.hpp>  // compute_create_address
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kDest = 0x00000000000000000000000000000000000000bb_address;
// FINDING-1 语料同款：access list 挂名、从未被调用/写入/预存在的地址。
constexpr auto kGhostAddr = 0xdddddddddddddddddddddddddddddddddddddddd_address;
// 预存在空账户（EIP-161 touch-delete 真删除对照）。
constexpr auto kEmptyAddr = 0x00000000000000000000000000000000c0ffee00_address;

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = OP_SEQUENCER_FEE_VAULT;
    return b;
}

state::Transaction baseTx()
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.gas_limit = 200000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    return tx;
}

// opValidate + opTransition（isthmus，OpFeeParams{} → l1_cost=0）；校验失败报告并返回 nullopt。
std::optional<OpTxReceipt> runTx(const test::TestState& ts, const state::Transaction& tx)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto block = blk();
    const std::vector<uint8_t> env{0x02, 0x11};
    const auto v = opValidate(
        ts, block, tx, {env.data(), env.size()}, isthmusConfig(), OpFeeParams{}, 30000000);
    if (const auto* err = std::get_if<std::error_code>(&v))
    {
        ADD_FAILURE() << "opValidate: " << err->message();
        return std::nullopt;
    }
    return opTransition(ts, block, hashes, tx, isthmusConfig(), vm, std::get<OpTxProperties>(v),
        1234, {env.data(), env.size()});
}
}  // namespace

// ① 复现级（修复前红）：access list 挂名不存在地址 → 消毒后 deleted_accounts 为空。
// 机理（DIVERGENCES.md FINDING-1）：预热走 get_or_insert(erase_if_empty) 捏造空账户，
// build_diff 谎报删除；op-geth 侧 2930 预热只进 access-list journal、不 touch 状态。
TEST(OpStateDiffSanitize, AccessListGhostDoesNotReachReceiptDiff)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[kDest] = {.nonce = 1, .balance = intx::uint256{0}};  // 非空既有账户（防 EIP-161 混入）
    seedOpPredeploys(ts);

    auto tx = baseTx();
    tx.to = kDest;
    tx.access_list = {{kGhostAddr, {}}};

    const auto r = runTx(ts, tx);
    ASSERT_TRUE(r.has_value());
    const auto& receipt = *r;
    ASSERT_EQ(receipt.receipt.status, EVMC_SUCCESS);
    EXPECT_TRUE(receipt.receipt.state_diff.deleted_accounts.empty());
}

// ② 真删除存活对照（修复前后恒绿——wiring 级，防 §3.3 错误接线/谓词反转）：
//    pre 置预存在空账户（balance/nonce 0、无 code），零值 CALL touch 之 →
//    真 EIP-161 touch-delete 必须存活消毒（view 中存在的账户之删除不得剔除）。
TEST(OpStateDiffSanitize, RealEmptyAccountDeleteSurvivesSanitize)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[kEmptyAddr] = {};  // 空账户：nonce=0, balance=0, 无 code
    seedOpPredeploys(ts);
    ASSERT_EQ(ts.count(kEmptyAddr), 1u);  // 前置断言：防空转假绿

    auto tx = baseTx();
    tx.to = kEmptyAddr;  // value = 0：零值 CALL touch

    const auto r = runTx(ts, tx);
    ASSERT_TRUE(r.has_value());
    const auto& receipt = *r;
    ASSERT_EQ(receipt.receipt.status, EVMC_SUCCESS);
    const auto& del = receipt.receipt.state_diff.deleted_accounts;
    EXPECT_NE(std::find(del.begin(), del.end(), kEmptyAddr), del.end());
}

// ③ EIP-6780 剔除（修复前红——未消毒时 destructed 分支发射该地址）：
//    同 tx 创建+自毁：eip1559-create，initcode = PUSH20 <beneficiary> SELFDESTRUCT
//    （0x73 ‖ beneficiary20B ‖ 0xff）；创建地址 = compute_create_address(sender, nonce)。
//    value=1 照活文档：beneficiary 非空，避免其以 EIP-161 分支混入删除列表。
//    同 tx 生灭对账本零痕（op-geth 亦不持久化）→ 消毒后 deleted_accounts 为空。
TEST(OpStateDiffSanitize, SameTxCreateSelfdestructIsSanitized)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    seedOpPredeploys(ts);

    auto tx = baseTx();
    tx.to = {};    // 合约创建
    tx.value = 1;  // 使 beneficiary 非空
    // initcode: PUSH20 0x...beef ; SELFDESTRUCT
    tx.data = *evmc::from_hex("73"
                              "000000000000000000000000000000000000beef"
                              "ff");
    const auto createdAddr = state::compute_create_address(kSender, 0);
    ASSERT_EQ(ts.count(createdAddr), 0u);  // 创建地址不预存在（消毒剔除的正是它）

    const auto r = runTx(ts, tx);
    ASSERT_TRUE(r.has_value());
    const auto& receipt = *r;
    ASSERT_EQ(receipt.receipt.status, EVMC_SUCCESS);
    EXPECT_TRUE(receipt.receipt.state_diff.deleted_accounts.empty());
    // 对照活文档：StateDiffWritebackTest.DeletesSameTxSelfdestruct 验证未消毒原始行为
    //（raw applyStateDiff 语境下同 tx 自毁进 deleted_accounts）。
}

// ④ 块级判别补钉（defer Task 1 审查修复）：② 的同款真删除必须穿过 processOpBlock
//    的块级出口。最小块 = attributes deposit + 零值 CALL touch 预存在空账户
//    （预置/构造照 OpBlockExecuteTest 的 seedL1BlockStub/attributesTx 形制），断言
//    普通 tx 收据 state_diff.deleted_accounts 含该地址、Strict 写回后终态同步删。
//    动机：回放 postState 比较把空≡不存在归一，"漏删"在语料向量上不可见（审查
//    实测：从 diff 中丢弃删除仍全绿）——empty_account_cleanup 向量对该轴无判别力，
//    KEEP 方向的块级判别由本用例承担（cases.go 该案注释同步如实化）。
TEST(OpStateDiffSanitize, BlockLevelRealDeleteSurvivesSanitize)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[kEmptyAddr] = {};  // 空账户：nonce=0, balance=0, 无 code
    // 最小块预置照 OpBlockExecuteTest::seedL1BlockStub：L1Block STOP 桩 + DEPOSITOR。
    ts[OP_L1_BLOCK] = {
        .nonce = 1, .balance = intx::uint256{0}, .code = evmc::from_hex("00").value()};
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};
    ASSERT_EQ(ts.count(kEmptyAddr), 1u);  // 前置断言：防空转假绿

    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) {
        bcos::evm::applyStateDiffStrict(ts, d);
    };

    // attributes deposit 照 OpBlockExecuteTest::attributesTx；普通 tx 照本文件 baseTx。
    const DepositTx attr{.source_hash = 0x01_bytes32,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1000000,
        .is_system_tx = false,
        .data = {}};
    auto touch = baseTx();
    touch.to = kEmptyAddr;  // value = 0：零值 CALL touch
    const std::vector<uint8_t> env(50, 0x11);
    const std::vector<OpBlockTx> txs{OpBlockTx{.tx = attr, .signedEnvelope = {}},
        OpBlockTx{.tx = touch, .signedEnvelope = evmc::bytes{env.begin(), env.end()}}};

    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 2u);
    const auto& txReceipt = std::get<OpTxReceipt>(r.receipts[1]).receipt;
    ASSERT_EQ(txReceipt.status, EVMC_SUCCESS);
    const auto& del = txReceipt.state_diff.deleted_accounts;
    EXPECT_NE(std::find(del.begin(), del.end(), kEmptyAddr), del.end());
    EXPECT_EQ(ts.count(kEmptyAddr), 0u);  // Strict 写回后终态同步删
}
