#include "OpPredeploysSeed.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/RollupCost.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
/// 全账户余额之和，用于守恒断言（费用只在账户间搬运，总量不变）。
[[nodiscard]] intx::uint256 totalSupply(const test::TestState& ts)
{
    intx::uint256 sum{0};
    for (const auto& [addr, acc] : ts)
    {
        sum += acc.balance;
        (void)addr;
    }
    return sum;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpTransitionSuite)

BOOST_AUTO_TEST_CASE(RoutesFeesToFourVaults)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
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
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm, props, 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);
    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);

    // 纯转账无 calldata：gas_used = intrinsic = 21000（7623 floor 空 calldata 亦为 21000）。
    BOOST_REQUIRE_EQUAL(txR.receipt.gas_used, 21000);
    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)};
    // BaseFeeVault = gasUsed×baseFee(7)；Sequencer(coinbase) = gasUsed×priority(10)；
    // Isthmus operator = gasUsed×scalar(1e6)/1e6 + 0 = gasUsed。
    BOOST_CHECK_EQUAL(ts.at(OP_BASE_FEE_VAULT).balance, gasUsed * intx::uint256{7});
    BOOST_CHECK_EQUAL(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
    BOOST_CHECK_EQUAL(ts.at(OP_SEQUENCER_FEE_VAULT).balance, gasUsed * intx::uint256{10});
    BOOST_CHECK_EQUAL(ts.at(OP_OPERATOR_FEE_VAULT).balance, gasUsed);
}

BOOST_AUTO_TEST_CASE(ReceiptCarriesL1AndOperatorMeta)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
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
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm, props, 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);

    BOOST_REQUIRE(txR.meta.l1_fee.has_value());
    BOOST_CHECK_EQUAL(*txR.meta.l1_fee, props.l1_cost);
    BOOST_REQUIRE(txR.meta.l1_gas_price.has_value());
    BOOST_CHECK_EQUAL(*txR.meta.l1_gas_price, fee.l1_base_fee);
    BOOST_REQUIRE(txR.meta.operator_fee.has_value());
    // Isthmus operator = gasUsed×scalar(1e6)/1e6 + 0 = gasUsed（纯转账 21000）。
    BOOST_CHECK_EQUAL(
        *txR.meta.operator_fee, intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)});
    BOOST_CHECK_EQUAL(txR.receipt.gas_used, 21000);
}

BOOST_AUTO_TEST_CASE(JovianReceiptMetaAndOperatorFormula)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
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
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    const auto txR = opTransition(ts, block, hashes, tx, cfg, vm, props, 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);

    const auto expectedOp =
        computeOperatorCost(fee, static_cast<uint64_t>(txR.receipt.gas_used), cfg);
    BOOST_REQUIRE(txR.meta.operator_fee.has_value());
    BOOST_CHECK_EQUAL(*txR.meta.operator_fee, expectedOp);
    BOOST_CHECK_EQUAL(expectedOp, intx::uint256{static_cast<uint64_t>(txR.receipt.gas_used)} *
                                          intx::uint256{1} * intx::uint256{100} +
                                      intx::uint256{500});

    BOOST_REQUIRE(txR.meta.da_footprint_gas_scalar.has_value());
    BOOST_CHECK_EQUAL(*txR.meta.da_footprint_gas_scalar, 2u);
    BOOST_REQUIRE(txR.meta.da_footprint.has_value());
    BOOST_CHECK_EQUAL(*txR.meta.da_footprint, estimatedDaSize({env.data(), env.size()}) * 2u);

    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);
    BOOST_CHECK_EQUAL(ts.at(OP_OPERATOR_FEE_VAULT).balance, expectedOp);
    BOOST_CHECK_EQUAL(ts.at(OP_L1_FEE_VAULT).balance, props.l1_cost);
}

// 重构护栏：共享执行核不得丢 EIP-2930 access_list 预热。
// gas = 21000 + accessList(2400+1900) + PUSH1(3)+SLOAD(warm 100)+POP(2) = 25405；
// 预热被丢时 SLOAD 冷 2100 → 27405。
BOOST_AUTO_TEST_CASE(AccessListKeepsStorageWarm)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[dest] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
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
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto txR =
        opTransition(ts, block, hashes, tx, isthmusConfig(), vm, std::get<OpTxProperties>(v), 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(txR.receipt.gas_used, 25405);
}

// 回归：access list 列入覆写表内的预编译地址 + 存储槽 key。
// OpHost::access_account 对表内地址提前返回且不插入账户，而 State::get_storage 内部
// get() 断言账户非空——修复前此处 debug 断言中止 / release 空指针解引用。
// 同时确认 sanitize 仍生效：0x100 不得作为幽灵账户进入 deleted_accounts。
BOOST_AUTO_TEST_CASE(AccessListWithOverridePrecompileStorageKey)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    constexpr auto kP256 = 0x0000000000000000000000000000000000000100_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
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
    // 覆写表内地址（Isthmus 的 0x100 P256Verify）带存储槽 key
    tx.access_list = {{kP256, {0x00_bytes32}}};

    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto txR =
        opTransition(ts, block, hashes, tx, isthmusConfig(), vm, std::get<OpTxProperties>(v), 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);
    // 纯转账 21000 + accessList(2400 地址 + 1900 槽) = 25300
    BOOST_CHECK_EQUAL(txR.receipt.gas_used, 25300);

    // 幽灵删除必须已被 sanitizeStateDiff 剥离
    BOOST_CHECK_MESSAGE((std::count(txR.receipt.state_diff.deleted_accounts.begin(),
                            txR.receipt.state_diff.deleted_accounts.end(), kP256)) == (0),
        "override-table precompile must not enter deleted_accounts as a ghost");
    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);
}

// 回归：cfg 与 props 的 operator-fee 标志在分叉边界上不一致时，扣费与退款/入账
// 必须同源（均取 props 快照），否则凭空增发或销毁 operator_cost_at_gas_limit。
// 以 isthmus（has_operator_fee=true）做 validate，再用关掉该标志的 cfg 副本做 transition。
BOOST_AUTO_TEST_CASE(OperatorFeeConservesWhenCfgDisagreesWithProps)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
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

    OpFeeParams fee{.l1_base_fee = 0_u256,
        .base_fee_scalar = 0,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 1000000,
        .operator_fee_constant = 500};
    std::vector<uint8_t> env{0x02, 0x11};

    // validate 侧：operator fee 生效，props 记录快照与金额
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    BOOST_REQUIRE(props.has_operator_fee);
    BOOST_REQUIRE_GT(props.operator_cost_at_gas_limit, intx::uint256{0});

    // transition 侧：cfg 关掉 operator fee，与 props 不一致
    OpForkConfig cfg = isthmusConfig();
    cfg.has_operator_fee = false;

    const auto before = totalSupply(ts);
    const auto txR = opTransition(ts, block, hashes, tx, cfg, vm, props, 1234);
    BOOST_REQUIRE_EQUAL(txR.receipt.status, EVMC_SUCCESS);
    bcos::evm::applyStateDiffStrict(ts, txR.receipt.state_diff);

    // 总供应量守恒：修复前发送方未被扣费却仍获退款 + 金库入账 → 增发
    BOOST_CHECK_MESSAGE(
        (totalSupply(ts)) == (before), "operator fee must conserve across cfg/props disagreement");
}

BOOST_AUTO_TEST_SUITE_END()
