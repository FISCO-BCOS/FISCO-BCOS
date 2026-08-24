#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSenderValidate = 0x00000000000000000000000000000000000000aa_address;

state::BlockInfo blkValidate()
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
    tx.sender = kSenderValidate;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.nonce = 0;
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpValidateSuite)

BOOST_AUTO_TEST_CASE(RejectsBlobTx)
{
    test::TestState ts;
    ts[kSenderValidate] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    auto tx = baseTx();
    tx.type = state::Transaction::Type::blob;
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.blob_hashes = {0x0100000000000000000000000000000000000000000000000000000000000001_bytes32};
    tx.max_blob_gas_price = 1;
    const auto r = opValidate(ts, blkValidate(), tx, {}, isthmusConfig(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::not_supported);
}

// 0x7E 必须走 runDeposit，不得被普通交易路径接受。这条不是「顺手拒绝一个不支持的类型」：
// kDepositTxType 是 static_cast 出来的、位于 Transaction::Type 枚举之外的值，而
// validate_transaction 的类型 switch（state.cpp:365-383）没有 default 标号，所以未加这道
// 判断时 0x7E 会静默穿过全部 revision 门控、被当作 legacy 通过校验。
//
// 后果一路错到共识字段：opTransition 会为它买 gas、收 L1 与 operator 费、强制 nonce（deposit
// 三者都不该有），并产出缺失 deposit_nonce / deposit_receipt_version 语义的回执——对一笔
// 标记为 0x7E 的交易而言就是错误的 receipts-root 叶子（deposit 必须走 runDeposit 专用路径）。
//
// 用非零 envelope + 充足余额，确保拒因只可能来自类型判断本身，而不是空 envelope 或余额不足。
BOOST_AUTO_TEST_CASE(RejectsDepositTxOnTheNonDepositPath)
{
    test::TestState ts;
    ts[kSenderValidate] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    auto tx = baseTx();
    tx.type = kDepositTxType;
    tx.to = 0x0000000000000000000000000000000000001234_address;

    const std::vector<uint8_t> env{0x7e, 0x11};
    const auto r = opValidate(
        ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::not_supported);
}

// 0x7E 只是这个洞里的一个值。Transaction::Type 的底层类型是 uint8_t，而 validate_transaction
// 的类型 switch 没有 default 标号，所以 set_code(0x04) 以上的每一个字节都会穿过全部 revision
// 门控、被当作 legacy 校验通过。rlp_encode 会把原始类型字节作为 typed 前缀写进回执，于是这类
// 交易会给 receipts root 贡献一个带该前缀、却按 legacy 规则定价的叶子。
//
// 因此拒绝必须是白名单。只黑名单 0x7E 的实现能通过上面那条用例，却通不过这一条。
BOOST_AUTO_TEST_CASE(RejectsEveryOutOfEnumTxType)
{
    test::TestState ts;
    ts[kSenderValidate] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    const std::vector<uint8_t> env{0x11, 0x22};

    for (const unsigned t : {0x05u, 0x40u, 0x7eu, 0x7fu, 0xffu})
    {
        auto tx = baseTx();
        tx.type = static_cast<state::Transaction::Type>(t);
        tx.to = 0x0000000000000000000000000000000000001234_address;
        const auto r = opValidate(ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(),
            OpFeeParams{}, 30000000);
        BOOST_REQUIRE_MESSAGE(std::holds_alternative<std::error_code>(r),
            "out-of-enum tx type 0x" << std::hex << t << " must not be accepted");
        BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::not_supported);
    }

    // 反向守卫：四种合法类型不得被白名单误伤（blob 另有其独立拒绝理由，已由 RejectsBlobTx 覆盖）。
    for (const auto t : {state::Transaction::Type::legacy, state::Transaction::Type::access_list,
             state::Transaction::Type::eip1559, state::Transaction::Type::set_code})
    {
        auto tx = baseTx();
        tx.type = t;
        tx.to = 0x0000000000000000000000000000000000001234_address;
        if (t == state::Transaction::Type::set_code)
            tx.authorization_list = {state::Authorization{.chain_id = 1,
                .addr = 0x00000000000000000000000000000000000000cc_address,
                .nonce = 0,
                .signer = std::nullopt,
                .r = 1_u256,
                .s = 1_u256,
                .v = intx::uint256{0}}};
        const auto r = opValidate(ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(),
            OpFeeParams{}, 30000000);
        BOOST_CHECK_MESSAGE(std::holds_alternative<OpTxProperties>(r),
            "valid tx type " << static_cast<unsigned>(t) << " must not be rejected");
    }
}

BOOST_AUTO_TEST_CASE(InsufficientForL1CostFails)
{
    test::TestState ts;
    ts[kSenderValidate] = {.nonce = 0, .balance = 100000000_u256, .storage = {}, .code = {}};
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 2,
        .blob_base_fee_scalar = 3,
        .blob_base_fee = 10000000_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(50, 0x11);
    const auto r = opValidate(
        ts, blkValidate(), baseTx(), {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
}

BOOST_AUTO_TEST_CASE(EmptyEnvelopeFails)
{
    test::TestState ts;
    ts[kSenderValidate] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    const auto r =
        opValidate(ts, blkValidate(), baseTx(), {}, isthmusConfig(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SufficientBalancePasses)
{
    test::TestState ts;
    ts[kSenderValidate] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    const std::vector<uint8_t> env{0x02};
    const auto r = opValidate(ts, blkValidate(), baseTx(), {env.data(), env.size()},
        isthmusConfig(), OpFeeParams{}, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(r));
    BOOST_CHECK_EQUAL(std::get<OpTxProperties>(r).l1_cost, intx::uint256{0});
}

// 余额上限求和不得在 2^256 处回绕（对齐 evmone validate_transaction 的 512 位口径）。
// 构造：balance = 2^256-1，value = balance - gasLimit*maxGasPrice，使 evmone 的 512 位
// gasCost+value 检查恰好通过；再叠加任何非零 l1Cost，总额越过 2^256——
// 256 位求和会回绕成一个极小值而放行（回绕后的差额在 opTransition 侧变成余额下溢增发），
// 512 位求和则正确判定资金不足。
BOOST_AUTO_TEST_CASE(BalanceCapDoesNotWrapAt2Pow256)
{
    test::TestState ts;
    const auto balance = std::numeric_limits<intx::uint256>::max();
    ts[kSenderValidate] = {.nonce = 0, .balance = balance, .storage = {}, .code = {}};

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
        opValidate(ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE_MESSAGE(std::holds_alternative<std::error_code>(r),
        "a total past 2^256 must be rejected, not wrapped into a tiny passing cap");
    BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::result_out_of_range);
}

// balance cap 的四项必须逐项精确：既不能少算（放行付不起的交易 → opTransition 随后从不足额
// 余额里做无检查减法，NDEBUG 下 assert 已失效，即 mint），也不能多算（误拒付得起的交易）。
//
// 既有用例只断言「返回了某个 error_code」，因此以下变异都能全身而退：把 `maxCost += l1Cost`
// 重复计一次、或删掉 `maxCost += opCost`。本用例用「余额恰好等于四项之和」这个边界把它们钉死：
// 恰好足额必须通过，少一个 wei 必须以 result_out_of_range 拒绝。
BOOST_AUTO_TEST_CASE(BalanceCapCountsEveryTermExactlyOnce)
{
    const std::vector<uint8_t> env(120, 0x11);
    // l1 与 operator 两项都非零，否则漏算任一项都察觉不到。
    const OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 1100,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 2000000,
        .operator_fee_constant = 500};

    auto tx = baseTx();
    tx.to = 0x0000000000000000000000000000000000001234_address;
    tx.value = intx::uint256{12345};

    // 先用充裕余额取得四项的真实值。
    intx::uint256 exact{0};
    {
        test::TestState ts;
        ts[kSenderValidate] = {.nonce = 0,
            .balance = 340282366920938463463374607431768211456_u256,
            .storage = {},
            .code = {}};
        const auto r = opValidate(
            ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
        BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(r));
        const auto& p = std::get<OpTxProperties>(r);
        BOOST_REQUIRE_MESSAGE(p.l1_cost > intx::uint256{0}, "l1_cost must be non-zero");
        BOOST_REQUIRE_MESSAGE(
            p.operator_cost_at_gas_limit > intx::uint256{0}, "operator cost must be non-zero");
        exact =
            intx::uint256{static_cast<uint64_t>(tx.gas_limit)} * intx::uint256{tx.max_gas_price} +
            tx.value + p.l1_cost + p.operator_cost_at_gas_limit;
    }

    // 恰好足额 → 必须通过。多算任何一项（例如 l1Cost 计两次）都会在此误拒。
    {
        test::TestState ts;
        ts[kSenderValidate] = {.nonce = 0, .balance = exact, .storage = {}, .code = {}};
        const auto r = opValidate(
            ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
        BOOST_CHECK_MESSAGE(std::holds_alternative<OpTxProperties>(r),
            "a balance exactly covering gasLimit*maxGasPrice + value + l1Cost + opCost must pass");
    }

    // 少一个 wei → 必须拒绝。漏算任何一项（例如不加 opCost）都会在此放行。
    {
        test::TestState ts;
        ts[kSenderValidate] = {.nonce = 0, .balance = exact - 1, .storage = {}, .code = {}};
        const auto r = opValidate(
            ts, blkValidate(), tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
        BOOST_REQUIRE_MESSAGE(std::holds_alternative<std::error_code>(r),
            "one wei short of the cap must be rejected");
        BOOST_CHECK_EQUAL(std::get<std::error_code>(r), std::errc::result_out_of_range);
    }
}

BOOST_AUTO_TEST_SUITE_END()
