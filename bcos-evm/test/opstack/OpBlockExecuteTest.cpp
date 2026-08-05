#include "OpL1AttributesTestHelpers.h"
#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testhelpers;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 7;
    b.timestamp = 1'700'000'000;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    b.coinbase = OP_SEQUENCER_FEE_VAULT;
    b.parent_beacon_block_root = 0xbe_bytes32;  // 4788 输入
    return b;
}

/// L1 attributes deposit（最小体：to=L1Block、from=DEPOSITOR；data 为 setter 调用，
/// 本测试用 harness 同款直写槽的 stub code——见 seedL1BlockStub）。
DepositTx attributesTx()
{
    return DepositTx{.source_hash = 0x01_bytes32,
        .from = OP_DEPOSITOR,
        .to = OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 1000000,
        .is_system_tx = false,
        .data = {}};
}

/// L1Block stub：空跑成功即可（槽值由测试直接预置，与 OpBlockHarnessTest 现行做法一致）。
void seedL1BlockStub(test::TestState& ts)
{
    ts[OP_L1_BLOCK] = {
        .nonce = 1, .balance = intx::uint256{0}, .code = evmc::from_hex("00").value()};  // STOP
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};
}

OpBlockTx wrap(DepositTx d)
{
    return {.tx = std::move(d), .signedEnvelope = {}};
}

/// 普通 tx 打包成 OpBlockTx（附签名 envelope）。
OpBlockTx wrap(state::Transaction tx, evmc::bytes_view env)
{
    return {.tx = std::move(tx), .signedEnvelope = evmc::bytes{env.begin(), env.end()}};
}

// 打包 helper（wordWith/slotKey/packUint256Low8/packSlot3/packSlot8/writeWord/
// packL1AttributesData/toBytes）已提取至共享头 OpL1AttributesTestHelpers.h（M-B2 Task 3
// Step 1，原与 OpBlockHarnessTest.cpp 逐字节重复）。

/// L1Block 真 setter（同 harness :106-107）+ seedOpPredeploys（同 :104）+ OP_DEPOSITOR（同 :108）。
/// 供需要 fee 槽真实写入效果的用例使用（区别于 seedL1BlockStub 的空跑 STOP 桩）。
void seedL1BlockSetter(test::TestState& ts)
{
    seedOpPredeploys(ts);
    ts[OP_L1_BLOCK].code =
        evmc::from_hex("60003560015560203560035560403560075560603560085500").value();
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};
}

/// attributes deposit，data 为 harness :123-125 同值打包（真实 setter 调用，非空跑）。
DepositTx realAttributesTx()
{
    auto attr = attributesTx();
    attr.data = toBytes(packL1AttributesData(
        /*l1BaseFee=*/1000000000, /*baseScalar=*/2, /*blobScalar=*/3,
        /*blobBaseFee=*/10000000, /*opScalar=*/1000000, /*opConst=*/0));
    return attr;
}

constexpr auto kNormalTxUser = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kNormalTxDest = 0x00000000000000000000000000000000000000bb_address;
constexpr auto kDepFrom = 0x00000000000000000000000000000000000000cc_address;

/// sender/dest 同 harness :101（余额同值）。
void seedNormalSender(test::TestState& ts)
{
    ts[kNormalTxUser] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[kNormalTxDest] = {};
}

/// 普通 tx 构造照抄 harness :172-181（eip1559/gas_limit=100000/max_gas_price=1000/priority=10）。
state::Transaction normalTx(uint64_t nonce)
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kNormalTxUser;
    tx.to = kNormalTxDest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = nonce;
    return tx;
}
}  // namespace

// 自加严：空块 / 首笔非 attributes（普通 deposit、错误 from、错误 to）→ 块级错误
TEST(OpBlockExecute, RejectsEmptyOrBadFirstTx)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    EXPECT_THROW(processOpBlock(ts, blk(), hashes, {}, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);

    auto badFrom = attributesTx();
    badFrom.from = 0x00000000000000000000000000000000000000cc_address;
    std::vector<OpBlockTx> v1{wrap(badFrom)};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, v1, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);

    auto badTo = attributesTx();
    badTo.to = 0x00000000000000000000000000000000000000cc_address;
    std::vector<OpBlockTx> v2{wrap(badTo)};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, v2, isthmusConfig(), vm, 1234, apply),
        std::runtime_error);

    // 红队 F7：首笔是普通 tx（variant 另一臂）→ 同为块级错误。挡住"首笔若是 deposit
    // 才查 attributes、否则落普通分支"的实现变体。
    state::Transaction firstNormal;
    firstNormal.type = state::Transaction::Type::eip1559;
    firstNormal.sender = 0x00000000000000000000000000000000000000aa_address;
    firstNormal.to = 0x00000000000000000000000000000000000000bb_address;
    firstNormal.gas_limit = 100000;
    firstNormal.max_gas_price = 1000;
    std::vector<uint8_t> env(50, 0x11);
    std::vector<OpBlockTx> v3{
        OpBlockTx{.tx = firstNormal, .signedEnvelope = evmc::bytes{env.begin(), env.end()}}};
    try
    {
        processOpBlock(ts, blk(), hashes, v3, isthmusConfig(), vm, 1234, apply);
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("first tx is not the L1 attributes deposit"),
            std::string::npos);
    }
}

// 红队 F2：系统调用**顺序**探针（先写后抄）。BEACON_ROOTS 替身按 CALLER 分支：
// SYSTEM_ADDRESS(0xff..fe) 调用时 SSTORE(0,1)；其他调用者把 SLOAD(0) 现值抄进 slot1。
// attributes 替身 CALL BEACON_ROOTS。系统调用先于 attributes（且其 diff 已写回）
// → slot1==1；实现若把系统调用挪到 tx 之后或不写回其 diff → slot1==0 → 翻红。
TEST(OpBlockExecute, SystemCallRunsBeforeAttributesTx)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    ts[state::BEACON_ROOTS_ADDRESS] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("3373fffffffffffffffffffffffffffffffffffffffe"
                               "14602157600054600155005b600160005500")
                    .value()};
    // attributes 替身：CALL(gas=剩余, BEACON_ROOTS, 0,0,0,0,0) POP STOP
    ts[OP_L1_BLOCK].code =
        evmc::from_hex("6000600060006000600073000f3df6d732807ef1319fb7b8bb8522d0beac025af15000")
            .value();
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
    ASSERT_EQ(std::get<OpDepositReceipt>(r.receipts[0]).receipt.status, EVMC_SUCCESS);

    evmc::bytes32 slot1{};
    slot1.bytes[31] = 1;
    evmc::bytes32 one{};
    one.bytes[31] = 1;
    EXPECT_EQ(ts.at(state::BEACON_ROOTS_ADDRESS).storage.at(slot1), one);
}

// 红队 F3：finalize **被调**的证明——借 finalizeOpBlock 的护栏异常
// （disable_prague_requests=false → std::runtime_error，OpBlockFinalize.cpp）。
// "不调 finalize"的作弊实现无从抛出 → 翻红。类型从 invalid_argument 改为 runtime_error
// （batch D-4）：调度器分类层把 logic_error 系映射到 -32603（本地故障）、runtime_error 映射到
// INVALID（块级拒绝），而"OP 链出现 Prague requests 块"属于后者；本测试喂的是一笔合法
// attributes 块，结构校验全过，唯一能抛 runtime_error 的只有 finalize 的护栏，故"被调"
// 的证明不受类型族改变的影响。
// 局限（D-10 回填措辞按此降级）：证明"被调且 tx 全跑完后可达"，不证明其 diff 被
// applyDiff、也不证明发生在末笔之后——OP 下 finalize diff 恒空，原理上不可观测。
TEST(OpBlockExecute, FinalizeIsActuallyWired)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    OpForkConfig cfg = isthmusConfig();
    cfg.disable_prague_requests = false;
    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    EXPECT_THROW(processOpBlock(ts, blk(), hashes, txs, cfg, vm, 1234, apply), std::runtime_error);
}

// deposit-only 块（sequencer 空块）：attributes 一笔即完整块；receipts=1、gasUsed=其 gasUsed、
// finalize diff 空（干净 state 上 finalizeOpBlock 无副作用——rev.2 Task 7 已钉）
TEST(OpBlockExecute, DepositOnlyBlockSucceeds)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 1u);
    const auto& dep = std::get<OpDepositReceipt>(r.receipts[0]);
    EXPECT_EQ(dep.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(dep.receipt.cumulative_gas_used, dep.receipt.gas_used);
    EXPECT_EQ(r.gasUsed, dep.receipt.gas_used);
    EXPECT_TRUE(r.finalizeDiff.modified_accounts.empty());
    EXPECT_EQ(ts.at(OP_DEPOSITOR).nonce, 1u);  // 写回生效
}

// 系统调用接线①：预部署有 code（测试替身：SSTORE(timestamp, calldata)）→ 槽被写。
// 替身语义仅测接线；真实 4788 合约行为由 M-B3 差分兜底（spec §4.3）。
// 替身 code: CALLDATALOAD(0) TIMESTAMP SSTORE STOP = 600035425500
TEST(OpBlockExecute, BlockStartSystemCallWritesBeaconSlot)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    ts[state::BEACON_ROOTS_ADDRESS] = {
        .nonce = 1, .balance = intx::uint256{0}, .code = evmc::from_hex("600035425500").value()};
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    const auto b = blk();
    processOpBlock(ts, b, hashes, txs, isthmusConfig(), vm, 1234, apply);

    // 替身把 calldata(=parent_beacon_block_root) 存到 key=timestamp 的槽
    const auto key = intx::be::store<evmc::bytes32>(intx::uint256{b.timestamp});
    EXPECT_EQ(ts.at(state::BEACON_ROOTS_ADDRESS).storage.at(key), 0xbe_bytes32);
}

// 系统调用接线②：预部署无 code → 静默跳过（EIP-4788 规范语义），块照常成功
TEST(OpBlockExecute, MissingSystemContractIsSilentlySkipped)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);  // 不种 BEACON_ROOTS
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    std::vector<OpBlockTx> txs{wrap(attributesTx())};
    EXPECT_NO_THROW(processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply));
    EXPECT_EQ(ts.count(state::BEACON_ROOTS_ADDRESS), 0u);
}

// 系统调用接线③（fork 门控负向断言）：Fjord（CANCUN）下 2935 不发生——
// HISTORY_STORAGE 种同款替身，Fjord 跑完其 storage 仍空；Isthmus（PRAGUE）下被写。
TEST(OpBlockExecute, HistoryStorageOnlyWrittenFromIsthmus)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto runWith = [&](const OpForkConfig& cfg) {
        test::TestState ts;
        seedL1BlockStub(ts);
        ts[state::HISTORY_STORAGE_ADDRESS] = {.nonce = 1,
            .balance = intx::uint256{0},
            .code = evmc::from_hex("600035425500").value()};
        const auto apply = [&](const state::StateDiff& d) {
            bcos::evm::applyStateDiffStrict(ts, d);
        };
        std::vector<OpBlockTx> txs{wrap(attributesTx())};
        processOpBlock(ts, blk(), hashes, txs, cfg, vm, 1234, apply);
        return ts.at(state::HISTORY_STORAGE_ADDRESS).storage.empty();
    };
    EXPECT_TRUE(runWith(fjordConfig()));     // CANCUN：2935 未激活
    EXPECT_FALSE(runWith(isthmusConfig()));  // PRAGUE：被系统调用写入
}

// 混排 cumulative + receipts 顺序（红队 F6 强化）：attributes + 普通 deposit + 普通 tx ×2
TEST(OpBlockExecute, CumulativeGasAccumulatesAcrossMixedTxs)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedNormalSender(ts);
    ts[kDepFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    seedL1BlockSetter(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };

    // 普通 deposit：mint=1000，to=自身（同 harness :155-162）。
    DepositTx dep{.source_hash = 0x02_bytes32,
        .from = kDepFrom,
        .to = kDepFrom,
        .mint = intx::uint256{1000},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};

    std::vector<uint8_t> env(50, 0x11);
    auto tx1 = normalTx(0);
    auto tx2 = normalTx(1);  // 同 sender，nonce=1（必要偏离，harness 只有一笔普通 tx，如实记录）
    tx2.data = evmc::bytes(100, 0x01);  // 100 字节非零 calldata：红队 F6 gas 不对称钉顺序
    std::vector<OpBlockTx> txs{wrap(realAttributesTx()), wrap(dep),
        wrap(tx1, {env.data(), env.size()}), wrap(tx2, {env.data(), env.size()})};

    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 4u);
    const auto& r0 = std::get<OpDepositReceipt>(r.receipts[0]).receipt;
    const auto& r1 = std::get<OpDepositReceipt>(r.receipts[1]).receipt;
    const auto& r2 = std::get<OpTxReceipt>(r.receipts[2]).receipt;
    const auto& r3 = std::get<OpTxReceipt>(r.receipts[3]).receipt;
    EXPECT_EQ(r0.cumulative_gas_used, r0.gas_used);
    EXPECT_EQ(r1.cumulative_gas_used, r0.gas_used + r1.gas_used);
    EXPECT_EQ(r2.cumulative_gas_used, r1.cumulative_gas_used + r2.gas_used);
    EXPECT_EQ(r3.cumulative_gas_used, r2.cumulative_gas_used + r3.gas_used);
    EXPECT_EQ(r.gasUsed, r3.cumulative_gas_used);
    EXPECT_LT(r2.gas_used, r3.gas_used);  // 非对称 gas 钉 receipts[i]↔txs[i] 映射（防自洽换序）
}

// gas pool 恰等边界（两段式实测取锚，断言差分不写魔数）
TEST(OpBlockExecute, BlockGasPoolExactBoundary)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    std::vector<uint8_t> env(50, 0x11);
    // 偏离（构造必要修正，如实记录）：attributesTx() 默认声明 gas_limit=1'000'000（Task 1
    // 助手，为宽限块设计）。本用例构造紧凑块（总预算 ≈ attrGasUsed+tx1GasUsed+tx2.gas_limit，
    // 量级 ~14 万），若沿用 1'000'000，attributes 自身入场检查（声明 gas_limit vs 满额
    // blockGasLeft）即先于本用例意图的 tx2 边界之前抛错（同 OpDepositTest.
    // GasLimitOverBlockBudgetIsBlockError 语义，已用运行验证）。收窄至 100000——仍远大于
    // 其实测 gasUsed（~2 万级），不影响测试语义。
    auto attr = attributesTx();
    attr.gas_limit = 100000;

    // 第一段：宽限（30M，blk() 默认）跑 attributes+tx1 取实测 gasUsed。
    test::TestState tsMeasure;
    seedL1BlockStub(tsMeasure);
    seedNormalSender(tsMeasure);
    test::TestBlockHashes hashesMeasure;
    const auto applyMeasure = [&](const state::StateDiff& d) {
        bcos::evm::applyStateDiffStrict(tsMeasure, d);
    };
    std::vector<OpBlockTx> txsMeasure{wrap(attr), wrap(normalTx(0), {env.data(), env.size()})};
    const auto rMeasure = processOpBlock(
        tsMeasure, blk(), hashesMeasure, txsMeasure, isthmusConfig(), vm, 1234, applyMeasure);
    ASSERT_EQ(rMeasure.receipts.size(), 2u);
    const auto attrGasUsed = std::get<OpDepositReceipt>(rMeasure.receipts[0]).receipt.gas_used;
    const auto tx1GasUsed = std::get<OpTxReceipt>(rMeasure.receipts[1]).receipt.gas_used;

    // 第二段：b.gas_limit = attr.gasUsed + tx1.gasUsed + tx2.gas_limit 恰等
    test::TestState ts;
    seedL1BlockStub(ts);
    seedNormalSender(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    const auto tx2 = normalTx(1);
    std::vector<OpBlockTx> txs{wrap(attr), wrap(normalTx(0), {env.data(), env.size()}),
        wrap(tx2, {env.data(), env.size()})};
    auto bExact = blk();
    bExact.gas_limit = attrGasUsed + tx1GasUsed + tx2.gas_limit;
    const auto rExact = processOpBlock(ts, bExact, hashes, txs, isthmusConfig(), vm, 1234, apply);
    EXPECT_EQ(rExact.receipts.size(), 3u);  // 恰等 → 接受

    // 第三段：b.gas_limit 减 1
    test::TestState ts3;
    seedL1BlockStub(ts3);
    seedNormalSender(ts3);
    const auto apply3 = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts3, d); };
    auto bMinus1 = blk();
    bMinus1.gas_limit = bExact.gas_limit - 1;
    EXPECT_THROW(processOpBlock(ts3, bMinus1, hashes, txs, isthmusConfig(), vm, 1234, apply3),
        std::runtime_error);  // 超 1 gas → 块级错误
}

// 红队 F1 固化（风险 6 钉死结论 (a) 落码）：deposit 未用 gas **已释放**给后续 tx——
// attributes gas_limit=100'000 实用 ~21k；b.gas_limit = attr.实测gasUsed + tx.gas_limit
// （远小于 attr.gas_limit + tx.gas_limit）。gasUsed 口径 → 接受；错误的 gasLimit 口径
// 实现（重构回潮）→ tx 超"剩余" → throw → 翻红。
TEST(OpBlockExecute, DepositUnusedGasReleasedToPool)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    std::vector<uint8_t> env(50, 0x11);
    // 偏离（构造必要修正，如实记录；理由同 BlockGasPoolExactBoundary）：brief 述
    // attributes gas_limit=1'000'000，但紧凑块总预算（attrGasUsed + tx.gas_limit，量级
    // ~12 万）小于该声明值，attributes 自身入场检查先于本用例意图的"未用 gas 释放"场景
    // 抛错（同 OpDepositTest.GasLimitOverBlockBudgetIsBlockError 语义，已用运行验证）。
    // 收窄至 100000——仍远大于其实测 gasUsed（~2 万级），"声明超额 → 实用少 → 差额释放
    // 给后续 tx"的区分力不变（错误的 gasLimit 口径实现仍会因超额扣减而拒收 tx2）。
    auto attr = attributesTx();
    attr.gas_limit = 100000;

    // 第一段：宽限实测 attr.gasUsed。
    test::TestState tsMeasure;
    seedL1BlockStub(tsMeasure);
    test::TestBlockHashes hashesMeasure;
    const auto applyMeasure = [&](const state::StateDiff& d) {
        bcos::evm::applyStateDiffStrict(tsMeasure, d);
    };
    std::vector<OpBlockTx> txsMeasure{wrap(attr)};
    const auto rMeasure = processOpBlock(
        tsMeasure, blk(), hashesMeasure, txsMeasure, isthmusConfig(), vm, 1234, applyMeasure);
    ASSERT_EQ(rMeasure.receipts.size(), 1u);
    const auto attrGasUsed = std::get<OpDepositReceipt>(rMeasure.receipts[0]).receipt.gas_used;

    // 第二段：b.gas_limit = attr.gasUsed + 100000（普通 tx gas_limit）
    test::TestState ts;
    seedL1BlockStub(ts);
    seedNormalSender(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    const auto tx = normalTx(0);
    std::vector<OpBlockTx> txs{wrap(attr), wrap(tx, {env.data(), env.size()})};
    auto b = blk();
    b.gas_limit = attrGasUsed + tx.gas_limit;
    const auto r = processOpBlock(ts, b, hashes, txs, isthmusConfig(), vm, 1234, apply);

    EXPECT_EQ(r.receipts.size(), 2u);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[1]).receipt.status, EVMC_SUCCESS);
}

// 红队 F4（覆盖表虚标修复）：deposit 出现在普通 tx 之后 → 块级错误（自加严，用户裁定）
TEST(OpBlockExecute, DepositAfterNormalTxIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    seedNormalSender(ts);
    ts[kDepFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    std::vector<uint8_t> env(50, 0x11);
    DepositTx dep{.source_hash = 0x02_bytes32,
        .from = kDepFrom,
        .to = kDepFrom,
        .mint = intx::uint256{1000},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    std::vector<OpBlockTx> txs{
        wrap(attributesTx()), wrap(normalTx(0), {env.data(), env.size()}), wrap(dep)};

    try
    {
        processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("deposit after non-deposit tx"), std::string::npos);
    }
}

// 红队 F5：fee params 解包时序——pre-state slot1 预置陈旧值 A，attributes 经 setter 写 B；
// oracle 用 B 的字节独立构造（unpackOpFeeParams + computeL1Cost，均已被既有测试钉死），
// 不经被测路径。块首（attributes 前）取参的作弊实现按 A/零 scalar 计费 → 翻红。
TEST(OpBlockExecute, FeeParamsLoadedAfterAttributesExecution)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedNormalSender(ts);
    seedL1BlockSetter(ts);
    ts[OP_L1_BLOCK].storage[slotKey(1)] =
        intx::be::store<evmc::bytes32>(intx::uint256{5'000'000'000});  // 陈旧 A
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    std::vector<uint8_t> env(50, 0x11);
    std::vector<OpBlockTx> txs{
        wrap(realAttributesTx()), wrap(normalTx(0), {env.data(), env.size()})};

    processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);

    const auto feeB = loadOpFeeParams(ts);  // 块后重取 = B（setter 已执行）
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance,
        computeL1Cost(feeB, {env.data(), env.size()}, isthmusConfig()));
    EXPECT_NE(feeB.l1_base_fee, intx::uint256{5'000'000'000});  // 确认 A 已被 B 覆盖（防真空）
}

// 普通 tx validate 错误 = 块级（nonce=99）。断言异常**消息**（基线审查 Finding 6：
// Task 1 占位 throw 同为 runtime_error，只断类型在红相是假红）
TEST(OpBlockExecute, InvalidNormalTxIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    seedNormalSender(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    std::vector<uint8_t> env(50, 0x11);
    std::vector<OpBlockTx> txs{wrap(attributesTx()), wrap(normalTx(99), {env.data(), env.size()})};

    try
    {
        processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("invalid non-deposit tx"), std::string::npos);
    }
}

// ── 批 C（spec §6.4）：Jovian L1-attributes 块形状校验 validateJovianBlockShape ──
// 对照 op-geth core/types/rollup_cost.go CalcDAFootprint（:563-591）的**校验**半部：
//   C-3 选择器/长度（ExtractDAFootprintGasScalar :547-556）；
//   C-4 激活块只含 deposit（:568-576）。
// 直接测被导出的自由函数，无需 VM/state（拒收在执行前发生）。常量 176/178/0x3db6be2b。
namespace
{
/// 造一笔 attributes deposit，calldata 长度=len、前 4 字节=selector（其余 0）。
DepositTx attrWithData(size_t len, std::array<uint8_t, 4> selector)
{
    auto d = attributesTx();
    evmc::bytes data(len, 0x00);
    for (size_t i = 0; i < selector.size() && i < len; ++i)
        data[i] = selector[i];
    d.data = std::move(data);
    return d;
}
OpBlockTx userTx()  // 非 deposit（variant 另一臂），用于激活块红队与普通块正例
{
    return wrap(normalTx(0), {});
}
}  // namespace

// C-3 正例：普通 Jovian 块，attributes=178B + Jovian selector，含用户交易 → 放行
TEST(OpBlockExecute, JovianShapeAcceptsNormalBlock)
{
    std::vector<OpBlockTx> txs{
        wrap(attrWithData(JovianL1AttributesLen, JovianL1AttributesSelector)), userTx()};
    EXPECT_NO_THROW(validateJovianBlockShape(txs, jovianConfig()));
}

// C-3 红队①：错选择器（178B 但首 4 字节非 Jovian）→ 块级错误
TEST(OpBlockExecute, JovianShapeRejectsWrongSelector)
{
    std::vector<OpBlockTx> txs{
        wrap(attrWithData(JovianL1AttributesLen, {0x44, 0x0a, 0x5e, 0x20}))};  // Ecotone selector
    try
    {
        validateJovianBlockShape(txs, jovianConfig());
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("does not have Jovian selector"), std::string::npos);
    }
}

// C-3 红队②：长度不足（177B < 178，且 ≠176 激活长度）→ 块级错误（太短）
TEST(OpBlockExecute, JovianShapeRejectsTooShort)
{
    std::vector<OpBlockTx> txs{
        wrap(attrWithData(JovianL1AttributesLen - 1, JovianL1AttributesSelector))};
    try
    {
        validateJovianBlockShape(txs, jovianConfig());
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(
            std::string(e.what()).find("too short for DA footprint gas scalar"), std::string::npos);
    }
}

// C-4 正例：激活块（attributes=176B），只含 deposit → 放行
TEST(OpBlockExecute, JovianShapeAcceptsDepositOnlyActivationBlock)
{
    std::vector<OpBlockTx> txs{
        wrap(attrWithData(IsthmusL1AttributesLen, {0x09, 0x89, 0x99, 0xbe}))};  // Isthmus selector
    EXPECT_NO_THROW(validateJovianBlockShape(txs, jovianConfig()));
}

// C-4 红队：激活块（176B）尾随一笔用户交易 → 块级错误
TEST(OpBlockExecute, JovianShapeRejectsUserTxInActivationBlock)
{
    std::vector<OpBlockTx> txs{
        wrap(attrWithData(IsthmusL1AttributesLen, {0x09, 0x89, 0x99, 0xbe})), userTx()};
    try
    {
        validateJovianBlockShape(txs, jovianConfig());
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("non-deposit transactions in Jovian activation block"),
            std::string::npos);
    }
}

// fork 门控负向断言：pre-Jovian（isthmusConfig，has_da_footprint=false）→ 全放行，
// 连"激活块含用户交易"这种 Jovian 下会拒的形状也不校验（CalcDAFootprint 不得 pre-Jovian 调用）。
TEST(OpBlockExecute, JovianShapeIsNoOpBeforeJovian)
{
    std::vector<OpBlockTx> bad{
        wrap(attrWithData(IsthmusL1AttributesLen, {0x09, 0x89, 0x99, 0xbe})), userTx()};
    EXPECT_NO_THROW(validateJovianBlockShape(bad, isthmusConfig()));
    std::vector<OpBlockTx> wrongSel{
        wrap(attrWithData(JovianL1AttributesLen, {0x44, 0x0a, 0x5e, 0x20}))};
    EXPECT_NO_THROW(validateJovianBlockShape(wrongSel, isthmusConfig()));
}

// 退化用例交回 processOpBlock：空块 / 首笔非 deposit → 本函数放行（不越权判决）。
TEST(OpBlockExecute, JovianShapeDefersEmptyAndNonDepositFirst)
{
    std::vector<OpBlockTx> empty{};
    EXPECT_NO_THROW(validateJovianBlockShape(empty, jovianConfig()));
    std::vector<OpBlockTx> nonDepFirst{userTx()};
    EXPECT_NO_THROW(validateJovianBlockShape(nonDepFirst, jovianConfig()));
}

// 集成：经 processOpBlock 走通——错选择器的普通 Jovian 块在**执行前**被拒（块级错误）。
// 用 seedL1BlockStub（attributes 空跑 STOP）即可，拒收发生在 per-tx 循环之前。
TEST(OpBlockExecute, ProcessOpBlockRejectsJovianWrongSelector)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    std::vector<OpBlockTx> txs{wrap(attrWithData(JovianL1AttributesLen, {0x44, 0x0a, 0x5e, 0x20}))};
    try
    {
        processOpBlock(ts, blk(), hashes, txs, jovianConfig(), vm, 1234, apply);
        FAIL() << "expected block-level error";
    }
    catch (const std::runtime_error& e)
    {
        EXPECT_NE(std::string(e.what()).find("does not have Jovian selector"), std::string::npos);
    }
}

// D-1（spec §6.4）：Jovian DA scalar 必须取首笔 attributes calldata[176:178]，
// 覆盖 L1Block slot8 读出值 —— 对齐 op-geth ExtractDAFootprintGasScalar。
// 分叉场景：pre 里 slot8 预置"旧值"（≠ calldata），断言 footprint 用 calldata 值。
namespace
{
/// 构造 178 字节 Jovian attributes calldata：前 4 字节 selector，bytes[176:178]=scalar（BE）。
DepositTx jovianAttrWithScalar(uint16_t scalar)
{
    auto d = attributesTx();
    evmc::bytes data(JovianL1AttributesLen, 0x00);
    for (size_t i = 0; i < JovianL1AttributesSelector.size(); ++i)
        data[i] = JovianL1AttributesSelector[i];
    data[JovianL1AttributesLen - 2] = static_cast<uint8_t>(scalar >> 8);
    data[JovianL1AttributesLen - 1] = static_cast<uint8_t>(scalar & 0xff);
    d.data = std::move(data);
    return d;
}

/// 给 L1Block 预置 slot8（含 da_scalar 位 bytes[18:20]=slotScalar）与三个 fee 槽。
/// 返回 slot8 的旧值字节。
void presetL1BlockSlots(test::TestState& ts, uint16_t slotScalar)
{
    using namespace bcos::evm::opstack::testhelpers;
    auto& acc = ts[OP_L1_BLOCK];
    acc.storage[slotKey(1)] = packUint256Low8(1000000000);
    acc.storage[slotKey(3)] = packSlot3(2, 3);
    acc.storage[slotKey(7)] = packUint256Low8(10000000);
    // slot8 = da_scalar@[18:20) | op_scalar@[20:24) | op_const@[24:32)
    auto s8 = packSlot8(1000000, 0);
    s8.bytes[18] = static_cast<uint8_t>(slotScalar >> 8);
    s8.bytes[19] = static_cast<uint8_t>(slotScalar & 0xff);
    acc.storage[slotKey(8)] = s8;
}
}  // namespace

// 分叉正例：slot 旧值=9，calldata=400 → footprint 必须按 400 算（op-geth 语义）。
TEST(OpBlockExecute, D1CalldataScalarOverridesSlot)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    presetL1BlockSlots(ts, /*slotScalar=*/9);
    seedNormalSender(ts);

    // 普通 tx 必须带非空 signed envelope，否则 opValidate 以 invalid_argument 拒绝
    // （OpValidate.cpp:15-16），processOpBlock 直接抛块级错误——测试走不到回执断言。
    std::vector<uint8_t> env(50, 0x11);
    std::vector<OpBlockTx> txs{
        wrap(jovianAttrWithScalar(/*scalar=*/400)), wrap(normalTx(0), {env.data(), env.size()})};
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    const auto r =
        processOpBlock(ts, blk(), test::TestBlockHashes{}, txs, jovianConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 2u);
    // 普通 tx 的 DA footprint 按 calldata scalar=400 计算 → 非零。
    const auto* meta = std::get_if<OpTxReceipt>(&r.receipts[1]);
    ASSERT_NE(meta, nullptr);
    ASSERT_TRUE(meta->meta.da_footprint.has_value());
    EXPECT_NE(meta->meta.da_footprint.value(), 0u)
        << "DA footprint must use calldata scalar (400), not slot value (9)";
    EXPECT_EQ(meta->meta.da_footprint_gas_scalar, 400);
}

// D-1 激活块语义回归：176B attributes + 一笔普通 deposit（无用户 tx，fee 不加载）。
// slot8 预置旧值 9 —— 断言块正常执行、seal.blobGasUsed（DA footprint）恒为 0。
// （C-4 保证激活块无用户 tx，故 scalar 分支不可达；本测试锁定"slot 旧值绝不泄漏"。）
TEST(OpBlockExecute, D1ActivationBlockSlotStaleValueDoesNotLeak)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    presetL1BlockSlots(ts, /*slotScalar=*/9);

    auto attr = attributesTx();
    evmc::bytes data(IsthmusL1AttributesLen, 0x00);
    for (size_t i = 0; i < 4 && i < data.size(); ++i)
        data[i] = static_cast<uint8_t>(0x09 + i);  // Isthmus selector 占位
    attr.data = std::move(data);

    // 一笔普通 deposit（非用户 tx，指向无 code 账户 → 成功回执）。
    DepositTx dep{.source_hash = 0x02_bytes32,
        .from = OP_DEPOSITOR,
        .to = 0x00000000000000000000000000000000000000dd_address,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};

    std::vector<OpBlockTx> txs{wrap(std::move(attr)), wrap(std::move(dep))};
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    const auto r =
        processOpBlock(ts, blk(), test::TestBlockHashes{}, txs, jovianConfig(), vm, 1234, apply);

    ASSERT_EQ(r.receipts.size(), 2u);
    const auto seal = sealOpBlock(r, jovianConfig(), {});
    ASSERT_TRUE(seal.blobGasUsed.has_value());
    EXPECT_EQ(seal.blobGasUsed.value(), 0u)
        << "activation-block DA footprint must be 0 (no user txs, scalar branch unreachable); "
           "slot stale value (9) must not leak";
}

// 写回时序（计数器探针）：kSeq 合约 code = SLOAD(0)+1 → SSTORE(0)
// （hex 60005460010160005500）；tx1/tx2（同 sender，nonce 0/1）先后调它。
// 每笔 diff 及时写回 → tx2 读到 1 再 +1 → post-state slot0==2；
// 写回失效 → 两笔都从 0 起算 → slot0==1 → 翻红。
TEST(OpBlockExecute, LaterTxSeesEarlierTxWrites)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    seedL1BlockStub(ts);
    seedNormalSender(ts);
    constexpr auto kSeq = 0x00000000000000000000000000000000000000ee_address;
    ts[kSeq] = {.nonce = 0,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("60005460010160005500").value()};
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) { bcos::evm::applyStateDiffStrict(ts, d); };
    std::vector<uint8_t> env(50, 0x11);
    auto tx1 = normalTx(0);
    tx1.to = kSeq;
    auto tx2 = normalTx(1);
    tx2.to = kSeq;
    std::vector<OpBlockTx> txs{wrap(attributesTx()), wrap(tx1, {env.data(), env.size()}),
        wrap(tx2, {env.data(), env.size()})};

    const auto r = processOpBlock(ts, blk(), hashes, txs, isthmusConfig(), vm, 1234, apply);
    ASSERT_EQ(r.receipts.size(), 3u);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[1]).receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(std::get<OpTxReceipt>(r.receipts[2]).receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kSeq).storage.at(0x00_bytes32), 0x02_bytes32);
}
