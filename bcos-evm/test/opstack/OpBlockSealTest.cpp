#include "OpL1AttributesTestHelpers.h"
#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpReceiptEncode.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/utils/mpt.hpp>
#include <bcos-evm/eth/utils/mpt_hash.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

using namespace bcos::evmref::opstack;
using namespace bcos::evmref::opstack::testhelpers;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
state::TransactionReceipt normalReceipt(
    state::Transaction::Type type, int64_t cumGas, const std::vector<state::Log>& logs)
{
    state::TransactionReceipt r{};
    r.type = type;
    r.status = EVMC_SUCCESS;
    r.cumulative_gas_used = cumGas;
    r.logs = logs;
    r.logs_bloom_filter = state::compute_bloom_filter(r.logs);
    return r;
}

OpDepositReceipt depositReceipt(int64_t cumGas, uint64_t nonce)
{
    OpDepositReceipt d{};
    d.receipt.type = kDepositTxType;
    d.receipt.status = EVMC_SUCCESS;
    d.receipt.cumulative_gas_used = cumGas;
    d.deposit_nonce = nonce;
    d.deposit_receipt_version = 1;
    return d;
}

OpBlockResult resultOf(std::vector<std::variant<OpDepositReceipt, OpTxReceipt>> rs)
{
    OpBlockResult res;
    res.receipts = std::move(rs);
    return res;
}

const std::map<evmc::bytes32, evmc::bytes32> kEmptySnapshot{};
}  // namespace

// 差分锚：纯普通 receipt 序列的 receipts-root 必须 == evmone 自身 mpt_hash 路径
// （我方叶编码对普通 tx 是委托 rlp_encode，root 相等即证 MPT 组装（键/值/树）与上游一致）
TEST(OpBlockSeal, NormalOnlyReceiptsRootMatchesEvmoneMptHash)
{
    state::Log log{.addr = 0x00000000000000000000000000000000000000aa_address,
        .data = {},
        .topics = {0x01_bytes32}};
    std::vector<state::TransactionReceipt> plain{
        normalReceipt(state::Transaction::Type::legacy, 21000, {}),
        normalReceipt(state::Transaction::Type::eip1559, 63000, {log})};
    const auto res = resultOf({OpTxReceipt{plain[0], {}}, OpTxReceipt{plain[1], {}}});
    const auto seal = sealOpBlock(res, fjordConfig(), kEmptySnapshot);
    EXPECT_EQ(seal.receiptsRoot, state::mpt_hash(plain));
}

// deposit 的 nonce/version 确实进 root（变异敏感性）且重复计算稳定
TEST(OpBlockSeal, DepositFieldsEnterReceiptsRoot)
{
    const auto base = resultOf({depositReceipt(21000, 5)});
    const auto root0 = sealOpBlock(base, isthmusConfig(), kEmptySnapshot).receiptsRoot;
    EXPECT_EQ(sealOpBlock(base, isthmusConfig(), kEmptySnapshot).receiptsRoot, root0);

    auto mutNonce = resultOf({depositReceipt(21000, 6)});
    EXPECT_NE(sealOpBlock(mutNonce, isthmusConfig(), kEmptySnapshot).receiptsRoot, root0);

    auto mutVer = resultOf({depositReceipt(21000, 5)});
    std::get<OpDepositReceipt>(mutVer.receipts[0]).deposit_receipt_version = 2;
    EXPECT_NE(sealOpBlock(mutVer, isthmusConfig(), kEmptySnapshot).receiptsRoot, root0);
}

// 混排序列键分配正锚（rev.2 红队 M2）：3 receipt [deposit, normal, deposit]，
// 期望树用**显式全局索引键**手工组装（叶值复用 Task 1 已锚编码器，键分配属性非自指）。
// 挡"按类别各自计数"的键错配变体：其键 {0x80,0x80,0x01} 在 Release evmone（NDEBUG，
// MPT 重复键仅 debug-assert）静默 last-wins，本用例确定性翻红；也挡键移位/逆序分配。
TEST(OpBlockSeal, MixedSequenceReceiptsRootUsesGlobalIndexKeys)
{
    state::Log log{.addr = 0x00000000000000000000000000000000000000aa_address,
        .data = {},
        .topics = {0x01_bytes32}};
    const std::vector<std::variant<OpDepositReceipt, OpTxReceipt>> rs{depositReceipt(21000, 5),
        OpTxReceipt{normalReceipt(state::Transaction::Type::eip1559, 42000, {log}), {}},
        depositReceipt(63000, 6)};

    state::MPT expected;
    expected.insert(rlp::encode(size_t{0}), encodeReceiptForRoot(rs[0]));
    expected.insert(rlp::encode(size_t{1}), encodeReceiptForRoot(rs[1]));
    expected.insert(rlp::encode(size_t{2}), encodeReceiptForRoot(rs[2]));

    const auto res = resultOf({rs[0], rs[1], rs[2]});
    EXPECT_EQ(sealOpBlock(res, isthmusConfig(), kEmptySnapshot).receiptsRoot, expected.hash());
}

// deposit 叶编码 ≠ "把内层 receipt 按 evmone typed 编码"（无 nonce/version 尾）——
// 挡"deposit 也走 rlp_encode 委托"的回退实现
TEST(OpBlockSeal, DepositRootDiffersFromUntaggedEvmoneEncoding)
{
    const auto dep = depositReceipt(21000, 5);
    const auto res = resultOf({dep});
    const auto ours = sealOpBlock(res, isthmusConfig(), kEmptySnapshot).receiptsRoot;
    // evmone rlp_encode 对 type=0x7e 会产同前缀但 4 字段列表（无 nonce/version）
    const std::vector<state::TransactionReceipt> inner{dep.receipt};
    EXPECT_NE(ours, state::mpt_hash(inner));
}

// 块级 bloom = 逐 receipt bloom 按位 OR；差分锚 = evmone compute_bloom_filter(span) 重载
TEST(OpBlockSeal, BlockBloomIsOrOfReceiptBlooms)
{
    state::Log la{.addr = 0x00000000000000000000000000000000000000aa_address,
        .data = {},
        .topics = {0x01_bytes32}};
    state::Log lb{.addr = 0x00000000000000000000000000000000000000bb_address,
        .data = {},
        .topics = {0x02_bytes32}};
    auto dep = depositReceipt(21000, 5);
    dep.receipt.logs = {la};
    dep.receipt.logs_bloom_filter = state::compute_bloom_filter(dep.receipt.logs);
    const auto tx = normalReceipt(state::Transaction::Type::eip1559, 42000, {lb});

    const auto res = resultOf({dep, OpTxReceipt{tx, {}}});
    const auto seal = sealOpBlock(res, isthmusConfig(), kEmptySnapshot);

    const std::vector<state::TransactionReceipt> assembled{dep.receipt, tx};
    const auto expected = state::compute_bloom_filter(assembled);
    EXPECT_EQ(evmc::bytes_view(seal.logsBloom), evmc::bytes_view(expected));
    EXPECT_NE(evmc::bytes_view(seal.logsBloom), evmc::bytes_view(dep.receipt.logs_bloom_filter));
}

// pre-Isthmus：withdrawalsRoot = 空列表根（快照即使非空也忽略）；requestsHash 无值
TEST(OpBlockSeal, PreIsthmusIgnoresSnapshotAndHasNoRequestsHash)
{
    std::map<evmc::bytes32, evmc::bytes32> snap{{0x01_bytes32, 0x02_bytes32}};
    const auto res = resultOf({depositReceipt(21000, 5)});
    const auto seal = sealOpBlock(res, fjordConfig(), snap);
    EXPECT_EQ(seal.withdrawalsRoot, state::EMPTY_MPT_HASH);
    EXPECT_FALSE(seal.requestsHash.has_value());
}

// Isthmus+：withdrawalsRoot = 快照 storage root。差分锚 = 账户叶嵌入：把同一快照装进
// 单账户 TestState，用 opStorageRoot 手工组装账户叶，账户树根必须 == evmone mpt_hash(TestState)
// （即证 opStorageRoot ≡ evmone 私有 storage-root helper）。requestsHash = 定值常量
// （0xe3b0…b855 直接钉在断言，锚 = op-geth hashes.go:43-44）。空快照 → EMPTY_MPT_HASH。
TEST(OpBlockSeal, IsthmusWithdrawalsRootMatchesEvmoneStorageTrie)
{
    std::map<evmc::bytes32, evmc::bytes32> snap{
        {0x01_bytes32, 0x0000000000000000000000000000000000000000000000000000000000000abc_bytes32},
        {0x02_bytes32, 0x05_bytes32}};

    const auto res = resultOf({depositReceipt(21000, 5)});
    const auto seal = sealOpBlock(res, isthmusConfig(), snap);
    EXPECT_EQ(seal.withdrawalsRoot, opStorageRoot(snap));

    // 账户叶嵌入差分锚
    test::TestState ts;
    const auto addr = OP_L2_TO_L1_MESSAGE_PASSER;
    ts[addr] = {.nonce = 3, .balance = 7_u256, .storage = snap, .code = evmc::bytes{0x00}};
    state::MPT accountTrie;
    accountTrie.insert(keccak256(evmc::bytes_view(addr)),
        rlp::encode_tuple(
            uint64_t{3}, 7_u256, opStorageRoot(snap), keccak256(evmc::bytes_view(ts[addr].code))));
    EXPECT_EQ(accountTrie.hash(), state::mpt_hash(ts));

    ASSERT_TRUE(seal.requestsHash.has_value());  // rev.2：先判有值再解引用，红相确定性
    EXPECT_EQ(*seal.requestsHash,
        0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32);
    EXPECT_EQ(
        sealOpBlock(res, isthmusConfig(), kEmptySnapshot).withdrawalsRoot, state::EMPTY_MPT_HASH);
    EXPECT_EQ(sealOpBlock(res, jovianConfig(), kEmptySnapshot).requestsHash,
        std::optional{OP_EMPTY_REQUESTS_HASH});
    EXPECT_EQ(sealOpBlock(res, karstConfig(), kEmptySnapshot).requestsHash,
        std::optional{OP_EMPTY_REQUESTS_HASH});  // rev.2：Karst 同门覆盖
}

// 零值槽必须不入树（evmone 侧是 assert 前置；我方防御性过滤，语义 = 剔除后调用）
TEST(OpBlockSeal, ZeroValueSlotsDoNotAffectStorageRoot)
{
    std::map<evmc::bytes32, evmc::bytes32> withZero{
        {0x01_bytes32, 0x05_bytes32}, {0x02_bytes32, {}}};
    std::map<evmc::bytes32, evmc::bytes32> withoutZero{{0x01_bytes32, 0x05_bytes32}};
    EXPECT_EQ(opStorageRoot(withZero), opStorageRoot(withoutZero));
    EXPECT_EQ(opStorageRoot({}), state::EMPTY_MPT_HASH);
}

// 端到端：processOpBlock 产出直接喂 sealOpBlock（Isthmus）。本用例钉**接线兼容性 +
// 快照新鲜度**（rev.2 红队 M1 升级）：message passer 带计数器 SSTORE code
// （60005460010160005500，M-B1 LaterTxSeesEarlierTxWrites 同款），块内普通 tx 调它——
// 块执行**改写**其 storage，故"读块前快照"的错误接线可判别。receipts-root 对内容敏感
// （字节正确性由 Task 1/2 锚 + M-B3 差分负责，此处不重复声称）。
// 注：finalize-vs-执行后的时点差 OP 下恒不可观测（finalizeDiff 空）仍归 M-B3；
// 本用例判别的是"块前 vs 块后"。构造照 OpBlockExecuteTest 共享 helper。
TEST(OpBlockSeal, EndToEndSealsProcessOpBlockResult)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    constexpr auto user = 0x00000000000000000000000000000000000000aa_address;

    seedOpPredeploys(ts);
    // L1Block setter：SSTORE slot1<-cd[0], slot3<-cd[32], slot7<-cd[64], slot8<-cd[96]
    // （同 OpBlockHarnessTest.cpp:117-118）。
    ts[OP_L1_BLOCK].code =
        evmc::from_hex("60003560015560203560035560403560075560603560085500").value();
    ts[OP_DEPOSITOR] = {.nonce = 0, .balance = intx::uint256{0}};
    ts[user] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    // message passer：nonce=1、非零预置槽 0x01→0x2a、计数器 code（SLOAD(0)+1→SSTORE(0)，
    // M-B1 LaterTxSeesEarlierTxWrites 同款）——块内普通 tx 调它会改写 slot0。
    ts[OP_L2_TO_L1_MESSAGE_PASSER] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x01_bytes32, 0x2a_bytes32}},
        .code = evmc::from_hex("60005460010160005500").value()};

    state::BlockInfo blk;
    blk.number = 1;
    blk.gas_limit = 30000000;
    blk.base_fee = 7;
    blk.coinbase = OP_SEQUENCER_FEE_VAULT;

    // L1 attributes deposit 首笔：写 L1Block 槽 1/3/7/8（同 OpBlockHarnessTest.cpp:131-134）。
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

    // 普通 tx（同 OpBlockExecuteTest.cpp normalTx 构造事实：eip1559/gas_limit=100000/
    // max_gas_price=1000/priority=10/50 字节 0x11 envelope），唯一偏离 = to = message passer。
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = user;
    tx.to = OP_L2_TO_L1_MESSAGE_PASSER;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    std::vector<uint8_t> env(50, 0x11);

    std::vector<OpBlockTx> txs{OpBlockTx{.tx = attr, .signedEnvelope = {}},
        OpBlockTx{.tx = tx, .signedEnvelope = evmc::bytes{env.begin(), env.end()}}};

    const auto preStorage = ts.at(OP_L2_TO_L1_MESSAGE_PASSER).storage;  // 块前快照留证
    test::TestBlockHashes hashes;
    const auto apply = [&](const state::StateDiff& d) {
        bcos::evmref::applyStateDiffStrict(ts, d);
    };
    const auto r = processOpBlock(ts, blk, hashes, txs, isthmusConfig(), vm, 1234, apply);

    const auto& postStorage = ts.at(OP_L2_TO_L1_MESSAGE_PASSER).storage;
    const auto seal = sealOpBlock(r, isthmusConfig(), postStorage);

    ASSERT_EQ(r.receipts.size(), 2u);
    ASSERT_EQ(std::get<OpTxReceipt>(r.receipts[1]).receipt.status, EVMC_SUCCESS);
    // 快照新鲜度：块执行写了 slot0 → 块后 root ≠ 块前 root，seal 用的必须是块后
    EXPECT_EQ(seal.withdrawalsRoot, opStorageRoot(postStorage));
    EXPECT_NE(seal.withdrawalsRoot, opStorageRoot(preStorage));
    EXPECT_NE(seal.withdrawalsRoot, state::EMPTY_MPT_HASH);
    EXPECT_TRUE(seal.requestsHash.has_value());

    // root 对内容敏感：换掉一个 receipt 的 cumulative → root 变
    auto mutated = r;
    std::visit([](auto& x) { x.receipt.cumulative_gas_used += 1; }, mutated.receipts[1]);
    EXPECT_NE(sealOpBlock(mutated, isthmusConfig(), postStorage).receiptsRoot, seal.receiptsRoot);
}

// Jovian：blobGasUsed = Σ 非 deposit receipt 的 meta.da_footprint（deposit 结构性无 meta 排除）；
// 锚 = 手工 Σ + 值变异敏感
TEST(OpBlockSeal, JovianBlobGasUsedSumsNonDepositDaFootprint)
{
    auto tx1 = OpTxReceipt{normalReceipt(state::Transaction::Type::eip1559, 21000, {}), {}};
    tx1.meta.da_footprint = 40000;
    auto tx2 = OpTxReceipt{normalReceipt(state::Transaction::Type::eip1559, 42000, {}), {}};
    tx2.meta.da_footprint = 2500;
    const auto res = resultOf({depositReceipt(21000, 5), tx1, tx2});
    const auto seal = sealOpBlock(res, jovianConfig(), kEmptySnapshot);
    ASSERT_TRUE(seal.blobGasUsed.has_value());
    EXPECT_EQ(*seal.blobGasUsed, 42500u);
}

TEST(OpBlockSeal, BlobGasUsedAbsentWhenNoDaFootprintFork)
{
    const auto res = resultOf({depositReceipt(21000, 5)});
    EXPECT_FALSE(sealOpBlock(res, isthmusConfig(), kEmptySnapshot).blobGasUsed.has_value());
    EXPECT_FALSE(sealOpBlock(res, fjordConfig(), kEmptySnapshot).blobGasUsed.has_value());
}

// deposits-only：Σ 无项 → 有值的 0（≡ op-geth 第一 Jovian 块特判分支产 0）
TEST(OpBlockSeal, JovianDepositOnlyBlockHasZeroBlobGasUsed)
{
    const auto res = resultOf({depositReceipt(21000, 5)});
    const auto seal = sealOpBlock(res, jovianConfig(), kEmptySnapshot);
    ASSERT_TRUE(seal.blobGasUsed.has_value());
    EXPECT_EQ(*seal.blobGasUsed, 0u);
}

// meta.da_footprint 空 optional 防御性按 0 计
TEST(OpBlockSeal, MissingDaFootprintMetaCountsAsZero)
{
    auto tx1 = OpTxReceipt{normalReceipt(state::Transaction::Type::eip1559, 21000, {}), {}};
    tx1.meta.da_footprint = std::nullopt;
    auto tx2 = OpTxReceipt{normalReceipt(state::Transaction::Type::eip1559, 42000, {}), {}};
    tx2.meta.da_footprint = 7;
    const auto res = resultOf({depositReceipt(21000, 5), tx1, tx2});
    EXPECT_EQ(
        sealOpBlock(res, jovianConfig(), kEmptySnapshot).blobGasUsed, std::optional<uint64_t>{7});
}
