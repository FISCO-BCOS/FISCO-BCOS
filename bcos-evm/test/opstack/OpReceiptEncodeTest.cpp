#include <bcos-evm/opstack/OpReceiptEncode.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/rlp_encode.hpp>

using namespace bcos::evm::opstack;
using namespace evmc::literals;

namespace
{
OpDepositReceipt minimalDeposit()
{
    OpDepositReceipt dep{};
    dep.receipt.type = kDepositTxType;
    dep.receipt.status = EVMC_SUCCESS;
    dep.receipt.cumulative_gas_used = 21000;
    // logs 空、bloom 全零（默认）
    dep.deposit_nonce = 5;
    dep.deposit_receipt_version = 1;
    return dep;
}
}  // namespace

// 手工逐字节推导的 golden fixture（断言数值纪律：推导即锚，最终字节权威归 M-B3 差分）。
// RLP 列表项：status 成功 → 0x01（1B）；cumGas 21000=0x5208 → 0x82 52 08（3B）；
// bloom 256 零字节 → 0xb9 0x0100 + 00×256（259B）；logs [] → 0xc0（1B）；
// nonce 5 → 0x05（1B）；version 1 → 0x01（1B）。载荷 = 1+3+259+1+1+1 = 266 = 0x010a
// → 列表头 0xf9 01 0a（3B）。前缀 0x7e。总长 1+3+266 = 270。
TEST(OpReceiptEncode, DepositGoldenBytes)
{
    const auto enc = encodeReceiptForRoot(minimalDeposit());
    evmc::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x01, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected += evmc::bytes(256, 0x00);
    expected += evmc::bytes{0xc0, 0x05, 0x01};
    ASSERT_EQ(enc.size(), 270u);
    EXPECT_EQ(enc, expected);
}

// 失败 deposit：status 项 = 空串 0x80（op-geth statusEncoding 失败分支）。
// rev.2 勘正：0x80 与成功的 0x01 同为 1 字节（RLP 单字节优化对空串产 0x80 无内容），
// 载荷不变仍 266 = 0x010a、总长仍 270——仅第 5 字节 0x01→0x80。
TEST(OpReceiptEncode, FailedDepositStatusIsEmptyString)
{
    auto dep = minimalDeposit();
    dep.receipt.status = EVMC_REVERT;
    const auto enc = encodeReceiptForRoot(dep);
    evmc::bytes expected{0x7e, 0xf9, 0x01, 0x0a, 0x80, 0x82, 0x52, 0x08, 0xb9, 0x01, 0x00};
    expected += evmc::bytes(256, 0x00);
    expected += evmc::bytes{0xc0, 0x05, 0x01};
    ASSERT_EQ(enc.size(), 270u);
    EXPECT_EQ(enc, expected);
}

// 带 log 的 deposit：logs 段（含 vector 列表 wrapper）以 evmone 独立编码为锚整段比对；
// 尾 2 字节 = {nonce, version}。总长关系锚（rev.2 红队补强，堵 wrapper 丢失盲区）：
// 空 logs 版总长 270，其中 logs 项占 1 字节（0xc0）→ 总长 = 269 + |rlp(logs)|
// （bloom 编码长度与内容无关恒 259；nonce 7 与 5 同为 1 字节）。
TEST(OpReceiptEncode, DepositWithLogEmbedsEncodedLogsAndNonceTail)
{
    auto dep = minimalDeposit();
    evmone::state::Log log{.addr = 0x00000000000000000000000000000000000000aa_address,
        .data = evmc::bytes{0x68, 0x69},
        .topics = {0x01_bytes32}};
    dep.receipt.logs.push_back(log);
    dep.receipt.logs_bloom_filter = evmone::state::compute_bloom_filter(dep.receipt.logs);
    dep.deposit_nonce = 7;
    const auto enc = encodeReceiptForRoot(dep);
    EXPECT_EQ(enc[0], 0x7e);
    // evmone 的 vector<Log> 列表编码（独立路径）须整段出现——覆盖列表 wrapper 字节
    const auto logsBytes = evmone::rlp::encode(dep.receipt.logs);
    EXPECT_NE(enc.find(logsBytes), evmc::bytes::npos);
    ASSERT_EQ(enc.size(), 269u + logsBytes.size());
    EXPECT_EQ(enc[enc.size() - 2], 0x07);  // nonce
    EXPECT_EQ(enc[enc.size() - 1], 0x01);  // version
}

// 普通 tx：逐字节委托 evmone rlp_encode（含 typed 前缀）——钉委托而非重实现
TEST(OpReceiptEncode, NormalReceiptDelegatesToEvmone)
{
    OpTxReceipt r{};
    r.receipt.type = evmone::state::Transaction::Type::eip1559;
    r.receipt.status = EVMC_SUCCESS;
    r.receipt.cumulative_gas_used = 42000;
    const auto enc = encodeReceiptForRoot(r);
    EXPECT_EQ(enc[0], 0x02);  // eip1559 typed 前缀
    EXPECT_EQ(enc, evmone::state::rlp_encode(r.receipt));
}

// variant 重载正确分派两臂
TEST(OpReceiptEncode, VariantDispatchesToBothArms)
{
    const std::variant<OpDepositReceipt, OpTxReceipt> d = minimalDeposit();
    EXPECT_EQ(encodeReceiptForRoot(d), encodeReceiptForRoot(minimalDeposit()));
    OpTxReceipt tx{};
    tx.receipt.status = EVMC_SUCCESS;
    const std::variant<OpDepositReceipt, OpTxReceipt> t = tx;
    EXPECT_EQ(encodeReceiptForRoot(t), encodeReceiptForRoot(tx));
    EXPECT_NE(encodeReceiptForRoot(d), encodeReceiptForRoot(t));
}
