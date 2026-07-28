#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <gtest/gtest.h>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
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

TEST(OpValidate, RejectsBlobTx)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    auto tx = baseTx();
    tx.type = state::Transaction::Type::blob;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.blob_hashes = {0x0100000000000000000000000000000000000000000000000000000000000001_bytes32};
    tx.max_blob_gas_price = 1;
    const auto r = opValidate(ts, blk(), tx, {}, isthmusConfig(), OpFeeParams{}, 30000000);
    ASSERT_TRUE(std::holds_alternative<std::error_code>(r));
    EXPECT_EQ(std::get<std::error_code>(r), std::errc::not_supported);
}

TEST(OpValidate, InsufficientForL1CostFails)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 100000000_u256, .storage = {}, .code = {}};
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);
    const auto r =
        opValidate(ts, blk(), baseTx(), {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<std::error_code>(r));
}

TEST(OpValidate, EmptyEnvelopeFails)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    const auto r = opValidate(ts, blk(), baseTx(), {}, isthmusConfig(), OpFeeParams{}, 30000000);
    ASSERT_TRUE(std::holds_alternative<std::error_code>(r));
    EXPECT_EQ(std::get<std::error_code>(r), std::errc::invalid_argument);
}

TEST(OpValidate, SufficientBalancePasses)
{
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    const std::vector<uint8_t> env{0x02};
    const auto r = opValidate(
        ts, blk(), baseTx(), {env.data(), env.size()}, isthmusConfig(), OpFeeParams{}, 30000000);
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(r));
    EXPECT_EQ(std::get<OpTxProperties>(r).l1_cost, intx::uint256{0});
}
