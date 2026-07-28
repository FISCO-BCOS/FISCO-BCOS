#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <gtest/gtest.h>
#include <limits>
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

// 余额上限求和不得在 2^256 处回绕（对齐 evmone validate_transaction 的 512 位口径）。
// 构造：balance = 2^256-1，value = balance - gasLimit*maxGasPrice，使 evmone 的 512 位
// gasCost+value 检查恰好通过；再叠加任何非零 l1Cost，总额越过 2^256——
// 256 位求和会回绕成一个极小值而放行（回绕后的差额在 opTransition 侧变成余额下溢增发），
// 512 位求和则正确判定资金不足。
TEST(OpValidate, BalanceCapDoesNotWrapAt2Pow256)
{
    test::TestState ts;
    const auto balance = std::numeric_limits<intx::uint256>::max();
    ts[kSender] = {.nonce = 0, .balance = balance, .storage = {}, .code = {}};

    auto tx = baseTx();
    tx.value = balance - intx::uint256{static_cast<uint64_t>(tx.gas_limit)} * tx.max_gas_price;

    // 非零 L1 费用参数：l1Cost > 0 即足以触发回绕
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);

    const auto r =
        opValidate(ts, blk(), tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    ASSERT_TRUE(std::holds_alternative<std::error_code>(r))
        << "a total past 2^256 must be rejected, not wrapped into a tiny passing cap";
    EXPECT_EQ(std::get<std::error_code>(r), std::errc::result_out_of_range);
}
