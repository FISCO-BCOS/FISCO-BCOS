#include <bcos-evm-ref/adapter/StateDiffWriteback.h>
#include <bcos-evm-ref/eth/EthTransition.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>

using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
// NOTE: 原始数字分隔符写法 1'000'...'_u256 在本仓库 vcpkg 锁定的 intx 0.15.0 下编译失败：
// intx::from_string 的 from_dec_digit 不识别 '\'' 分隔符（consteval 求值直接抛错，
// 见 intx.hpp from_dec_digit/from_string）。数值不变，仅去除分隔符使其可编译。
// 参照 StateDiffWritebackTest.cpp 的 kFunding 写法。
constexpr auto kFunding = 1000000000000000000_u256;  // 1 ETH in wei
constexpr auto kWithdrawalWei = 5000000000_u256;     // 5 gwei = 5e9 wei
}  // namespace

TEST(EthTransition, SimpleTransfer21000)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    const auto receiver = 0x0000000000000000000000000000000000001234_address;

    test::TestState state;
    state[sender] = {.nonce = 0, .balance = kFunding};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.coinbase = 0x00000000000000000000000000000000c014ba5e_address;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = receiver;
    tx.value = 1000;
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432);

    ASSERT_TRUE(std::holds_alternative<state::TransactionReceipt>(res))
        << std::get<std::error_code>(res).message();
    const auto& receipt = std::get<state::TransactionReceipt>(res);
    EXPECT_EQ(receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(receipt.gas_used, 21000);

    // 包装不写回：state 尚未变化
    EXPECT_EQ(state.count(receiver), 0u);
    bcos::evmref::applyStateDiff(state, receipt.state_diff);
    EXPECT_EQ(state.at(receiver).balance, 1000);
}

TEST(EthTransition, InvalidTxRejectedWithoutSideEffect)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    test::TestState state;
    state[sender] = {.nonce = 0, .balance = 1_u256};  // 付不起 gas

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = sender;
    tx.to = sender;
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432);

    ASSERT_TRUE(std::holds_alternative<std::error_code>(res));
    EXPECT_EQ(state.at(sender).balance, 1);  // 状态不变
}

// 钉死 blockGasLeft/blobGasLeft 两个相邻 int64_t 形参不被换序：
// blobGasLeft=0 时 type-3 blob tx 必须被拒（换序后 786432 会放行它）。
// 同时是 spec §4.3 OP 路径 "blobGasLeft 内部传 0 拒 blob" 的前置契约。
TEST(EthTransition, BlobTxRejectedWhenNoBlobGasLeft)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    test::TestState state;
    state[sender] = {.nonce = 0, .balance = kFunding};

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30'000'000;
    block.base_fee = 7;
    block.blob_base_fee = 1;

    state::Transaction tx;
    tx.type = state::Transaction::Type::blob;
    tx.sender = sender;
    tx.to = 0x0000000000000000000000000000000000001234_address;  // blob tx 必须有 to
    tx.gas_limit = 100'000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.max_blob_gas_price = 1;
    tx.blob_hashes = {0x0100000000000000000000000000000000000000000000000000000000000001_bytes32};
    tx.nonce = 0;

    evmc::VM vm{evmc_create_evmone()};
    test::TestBlockHashes hashes;
    const auto res = bcos::evmref::eth::runTransaction(
        state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, /*blobGasLeft=*/0);

    ASSERT_TRUE(std::holds_alternative<std::error_code>(res));
}

// runBlockFinalize 的 withdrawals 路径（EEST state 约定不触达，M2 内唯一覆盖点）：
// 金额按 gwei 计，落账须 ×1e9 换算为 wei。
TEST(EthTransition, FinalizeAppliesWithdrawalGweiToWei)
{
    const auto payee = 0x0000000000000000000000000000000000005e11_address;
    test::TestState state;

    const state::Withdrawal w{
        .index = 0, .validator_index = 0, .recipient = payee, .amount_in_gwei = 5};
    const auto diff = bcos::evmref::eth::runBlockFinalize(state, EVMC_CANCUN,
        0x00000000000000000000000000000000c014ba5e_address, std::nullopt, {}, std::span{&w, 1});

    bcos::evmref::applyStateDiff(state, diff);
    EXPECT_EQ(state.at(payee).balance, kWithdrawalWei);
}
