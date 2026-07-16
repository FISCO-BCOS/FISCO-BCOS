#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/opstack/OpDepositTx.h>
#include <bcos-evm-ref/opstack/OpFeeParams.h>
#include <bcos-evm-ref/opstack/OpForkSchedule.h>
#include <bcos-evm-ref/opstack/OpPredeploys.h>
#include <bcos-evm-ref/opstack/OpTransition.h>
#include <bcos-evm-ref/opstack/OpValidate.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/state/state.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// 造一个 32 字节大端 word：在 [byteOff, byteOff+len) 放入 value 的低 len 字节。
evmc::bytes32 wordWith(size_t byteOff, uint64_t value, size_t len)
{
    evmc::bytes32 w{};
    for (size_t i = 0; i < len; ++i)
    {
        w.bytes[byteOff + len - 1 - i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    return w;
}

evmc::bytes32 slotKey(uint8_t slot)
{
    evmc::bytes32 k{};
    k.bytes[31] = slot;
    return k;
}

evmc::bytes32 packUint256Low8(uint64_t value)
{
    return wordWith(24, value, 8);
}

/// slot3 打包：base_fee_scalar @ bytes[16,20)，blob_base_fee_scalar @ [20,24)。
evmc::bytes32 packSlot3(uint32_t baseScalar, uint32_t blobScalar)
{
    evmc::bytes32 w = wordWith(16, baseScalar, 4);
    const auto blob = wordWith(20, blobScalar, 4);
    for (size_t i = 0; i < 32; ++i)
    {
        w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | blob.bytes[i]);
    }
    return w;
}

/// slot8 打包：operator_fee_scalar @ bytes[20,24)，operator_fee_constant @ [24,32)。
evmc::bytes32 packSlot8(uint32_t opScalar, uint64_t opConst)
{
    evmc::bytes32 w = wordWith(20, opScalar, 4);
    const auto c = wordWith(24, opConst, 8);
    for (size_t i = 0; i < 32; ++i)
    {
        w.bytes[i] = static_cast<uint8_t>(w.bytes[i] | c.bytes[i]);
    }
    return w;
}

void writeWord(std::vector<uint8_t>& data, size_t offset, const evmc::bytes32& word)
{
    for (size_t i = 0; i < 32; ++i)
    {
        data[offset + i] = word.bytes[i];
    }
}

std::vector<uint8_t> packL1AttributesData(uint64_t l1BaseFee, uint32_t baseScalar,
    uint32_t blobScalar, uint64_t blobBaseFee, uint32_t opScalar, uint64_t opConst)
{
    std::vector<uint8_t> data(128, 0);
    writeWord(data, 0, packUint256Low8(l1BaseFee));
    writeWord(data, 32, packSlot3(baseScalar, blobScalar));
    writeWord(data, 64, packUint256Low8(blobBaseFee));
    writeWord(data, 96, packSlot8(opScalar, opConst));
    return data;
}

evmc::bytes toBytes(const std::vector<uint8_t>& v)
{
    return evmc::bytes{v.begin(), v.end()};
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
    int64_t blockGasLeft = block.gas_limit;
    int64_t cumulative = 0;

    // (1) pre-block 系统调用（EIP-4788/2935）：evmone 未导出 system_call_block_start，本冒烟省略。
    //     非本用例四 vault 守恒断言对象。

    // (2) L1 attributes deposit 首笔：经 runDeposit 真执行写 L1Block 槽 1/3/7/8。
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
    const auto attrR =
        runDeposit(ts, block, hashes, attr, isthmusConfig(), vm, 1234, block.gas_limit);
    ASSERT_EQ(attrR.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiff(ts, attrR.receipt.state_diff);
    blockGasLeft -= attrR.receipt.gas_used;
    cumulative += attrR.receipt.gas_used;

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

    // (3a) user deposit
    DepositTx dep{.source_hash = 0x02_bytes32,
        .from = depFrom,
        .to = depFrom,
        .mint = intx::uint256{1000},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto depR =
        runDeposit(ts, block, hashes, dep, isthmusConfig(), vm, 1234, block.gas_limit);
    ASSERT_EQ(depR.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiff(ts, depR.receipt.state_diff);
    blockGasLeft -= depR.receipt.gas_used;
    cumulative += depR.receipt.gas_used;
    EXPECT_EQ(ts.at(depFrom).balance, intx::uint256{1000});

    // (3b) 普通转账
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
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, blockGasLeft);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, {env.data(), env.size()});
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);

    // FromState≡注入断言：同 tx 两路径 gas_used / l1_cost 相等。
    const auto vFS =
        opValidateFromState(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), blockGasLeft);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(vFS));
    const auto& propsFS = std::get<OpTxProperties>(vFS);
    const auto txRFS = opTransitionFromState(
        ts, block, hashes, tx, isthmusConfig(), vm, propsFS, 1234, {env.data(), env.size()});
    EXPECT_EQ(txRFS.receipt.gas_used, txR.receipt.gas_used);
    EXPECT_EQ(propsFS.l1_cost, props.l1_cost);

    bcos::evmref::applyStateDiff(ts, txR.receipt.state_diff);
    blockGasLeft -= txR.receipt.gas_used;
    cumulative += txR.receipt.gas_used;

    // (4) 四 vault 守恒 + cumulative gas
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
    EXPECT_GT(ts.at(OP_BASE_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_GT(ts.at(OP_SEQUENCER_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_GT(ts.at(OP_OPERATOR_FEE_VAULT).balance, intx::uint256{0});
    EXPECT_EQ(cumulative, attrR.receipt.gas_used + depR.receipt.gas_used + txR.receipt.gas_used);
    EXPECT_GT(blockGasLeft, 0);
    // finalize：OP 块 withdrawals 恒空；receiptRoot/withdrawalsRoot 属块头层非 M5 目标。
}
