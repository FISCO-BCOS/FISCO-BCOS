#include "OpPredeploysSeed.h"
#include "StateDiffWriteback.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

TEST(OpTransition, RoutesFeesToFourVaults)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 1000000,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm, props, 1234);
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);
    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);

    // 纯转账无 calldata：gas_used = intrinsic = 21000（7623 floor 空 calldata 亦为 21000）。
    ASSERT_EQ(txR.receipt.gas_used, 21000);
    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)};
    // BaseFeeVault = gasUsed×baseFee(7)；Sequencer(coinbase) = gasUsed×priority(10)；
    // Isthmus operator = gasUsed×scalar(1e6)/1e6 + 0 = gasUsed。
    EXPECT_EQ(ts.at(OP_BASE_FEE_VAULT).balance, gasUsed * intx::uint256{7});
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
    EXPECT_EQ(ts.at(OP_SEQUENCER_FEE_VAULT).balance, gasUsed * intx::uint256{10});
    EXPECT_EQ(ts.at(OP_OPERATOR_FEE_VAULT).balance, gasUsed);
}

TEST(OpTransition, ReceiptCarriesL1AndOperatorMeta)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 1000000,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm, props, 1234);
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);

    ASSERT_TRUE(txR.meta.l1_fee.has_value());
    EXPECT_EQ(*txR.meta.l1_fee, props.l1_cost);
    ASSERT_TRUE(txR.meta.l1_gas_price.has_value());
    EXPECT_EQ(*txR.meta.l1_gas_price, fee.l1_base_fee);
    ASSERT_TRUE(txR.meta.operator_fee.has_value());
    // Isthmus operator = gasUsed×scalar(1e6)/1e6 + 0 = gasUsed（纯转账 21000）。
    EXPECT_EQ(*txR.meta.operator_fee, intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)});
    EXPECT_EQ(txR.receipt.gas_used, 21000);
}

TEST(OpTransition, JovianReceiptMetaAndOperatorFormula)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    // Jovian: gas * scalar * 100 + constant — use small scalar so buyGas stays affordable.
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 1,
        .operator_fee_constant = 500,
        .da_footprint_gas_scalar = 2};
    std::vector<uint8_t> env(50, 0x11);
    const auto& cfg = jovianConfig();
    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, cfg, vm, props, 1234);
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);

    const auto expectedOp =
        computeOperatorCost(fee, static_cast<uint64_t>(txR.receipt.gas_used), cfg);
    ASSERT_TRUE(txR.meta.operator_fee.has_value());
    EXPECT_EQ(*txR.meta.operator_fee, expectedOp);
    EXPECT_EQ(expectedOp, intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)} *
                                  intx::uint256{1} * intx::uint256{100} +
                              intx::uint256{500});

    ASSERT_TRUE(txR.meta.da_footprint_gas_scalar.has_value());
    EXPECT_EQ(*txR.meta.da_footprint_gas_scalar, 2u);
    ASSERT_TRUE(txR.meta.da_footprint.has_value());
    EXPECT_EQ(*txR.meta.da_footprint, estimatedDaSize({env.data(), env.size()}) * 2u);

    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);
    EXPECT_EQ(ts.at(OP_OPERATOR_FEE_VAULT).balance, expectedOp);
    EXPECT_EQ(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
}

// 重构护栏：共享执行核不得丢 EIP-2930 access_list 预热。
// gas = 21000 + accessList(2400+1900) + PUSH1(3)+SLOAD(warm 100)+POP(2) = 25405；
// 预热被丢时 SLOAD 冷 2100 → 27405。
TEST(OpTransition, AccessListKeepsStorageWarm)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    ts[dest] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("6000545000").value()};  // PUSH1 0 SLOAD POP STOP
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = dest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.access_list = {{dest, {0x00_bytes32}}};

    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto txR =
        opTransition(ts, block, hashes, tx, isthmusConfig(), vm, std::get<OpTxProperties>(v), 1234);
    ASSERT_EQ(txR.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(txR.receipt.gas_used, 25405);
}
