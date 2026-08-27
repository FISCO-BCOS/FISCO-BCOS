#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
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
#include <cstring>
#include <limits>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
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

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // 纯转账无 calldata：gas_used = intrinsic = 21000（7623 floor 空 calldata 亦为 21000）。
    const auto gasUsedInt = static_cast<uint64_t>(txR->gasUsed());
    BOOST_REQUIRE_EQUAL(gasUsedInt, 21000u);
    const auto gasUsed = intx::uint256{gasUsedInt};
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

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);

    const auto& meta = txR->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_fee, bcosU256FromIntx(props.l1_cost));
    BOOST_REQUIRE(meta->l1_gas_price.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_gas_price, bcosU256FromIntx(fee.l1_base_fee));
    // l1_gas_used：Isthmus 的 has_ecotone_l1_formula=false（Fjord+ 语义）→ 走
    // estimatedDaSizeScaled(flz) * 16 / 1e6 公式（op-geth rollup_cost.go:623-624）。
    // 公式本体由 RollupCostTest 的任意精度字面量锚定；此处断言钉的是接线（l1_gas_used
    // 必须来自 props.flz_len 的 Fjord 路径而非其他来源）。
    BOOST_REQUIRE(meta->l1_gas_used.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_gas_used,
        static_cast<uint64_t>(estimatedDaSizeScaled(props.flz_len) * 16 / 1'000'000));
    BOOST_REQUIRE(meta->operator_fee.has_value());
    // Isthmus operator = gasUsed×scalar(1e6)/1e6 + 0 = gasUsed（纯转账 21000）。
    BOOST_CHECK_EQUAL(*meta->operator_fee, bcos::u256(static_cast<uint64_t>(txR->gasUsed())));
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(txR->gasUsed()), 21000u);
    // effectiveGasPrice = base_fee(7) + priority(10) = 0x11（op-geth hexutil.Big 最小小写）；
    // 转账型 tx 无 contractAddress。
    BOOST_CHECK_EQUAL(txR->effectiveGasPrice(), "0x11");
    BOOST_CHECK(txR->contractAddress().empty());
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

    evmone::state::StateDiff diff;
    const auto txR =
        opTransition(ts, block, hashes, tx, cfg, vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);

    const auto gasUsedInt = static_cast<uint64_t>(txR->gasUsed());
    const auto expectedOp = computeOperatorCost(fee, gasUsedInt, cfg);
    const auto& meta = txR->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->operator_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->operator_fee, bcosU256FromIntx(expectedOp));
    BOOST_CHECK_EQUAL(expectedOp,
        intx::uint256{gasUsedInt} * intx::uint256{1} * intx::uint256{100} + intx::uint256{500});

    BOOST_REQUIRE(meta->da_footprint_gas_scalar.has_value());
    BOOST_CHECK_EQUAL(*meta->da_footprint_gas_scalar, 2u);
    BOOST_REQUIRE(meta->da_footprint.has_value());
    BOOST_CHECK_EQUAL(*meta->da_footprint, estimatedDaSize({env.data(), env.size()}) * 2u);

    // l1_gas_used（Fjord+ 分支）：ecotone_calldata_gas_used 为 nullopt → 走
    // estimatedDaSizeScaled(flz) * 16 / 1e6（op-geth rollup_cost.go:623-624）；公式本体由
    // RollupCostTest 锚定，此处钉接线（同 Isthmus 用例）。
    BOOST_REQUIRE(meta->l1_gas_used.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_gas_used,
        static_cast<uint64_t>(estimatedDaSizeScaled(props.flz_len) * 16 / 1'000'000));

    bcos::evm::applyStateDiffStrict(ts, diff);
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
    evmone::state::StateDiff diff;
    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm,
        std::get<OpTxProperties>(v), 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(txR->gasUsed()), 25405u);
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
    evmone::state::StateDiff diff;
    const auto txR = opTransition(ts, block, hashes, tx, isthmusConfig(), vm,
        std::get<OpTxProperties>(v), 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    // 纯转账 21000 + accessList(2400 地址 + 1900 槽) = 25300
    BOOST_CHECK_EQUAL(static_cast<uint64_t>(txR->gasUsed()), 25300u);

    // 幽灵删除必须已被 sanitizeStateDiff 剥离
    BOOST_CHECK_MESSAGE(
        (std::count(diff.deleted_accounts.begin(), diff.deleted_accounts.end(), kP256)) == (0),
        "override-table precompile must not enter deleted_accounts as a ghost");
    bcos::evm::applyStateDiffStrict(ts, diff);
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
    evmone::state::StateDiff diff;
    const auto txR =
        opTransition(ts, block, hashes, tx, cfg, vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // 总供应量守恒：修复前发送方未被扣费却仍获退款 + 金库入账 → 增发
    BOOST_CHECK_MESSAGE(
        (totalSupply(ts)) == (before), "operator fee must conserve across cfg/props disagreement");
}

// 回执元数据必须描述交易「实际按哪个分叉定价/收费」，而不是 opTransition 手上那份 cfg。
//
// 这条必须在 opTransition 这一层驱动，而不是直接调 deriveOpReceiptMeta：缺陷位于**调用点**
// （传 cfg 还是传 props），直接给 deriveOpReceiptMeta 喂字面布尔值的用例只能证明该函数尊重
// 自己的参数，无法证明 opTransition 传对了参数——把调用点改回 cfg 时那种用例照样全绿。
BOOST_AUTO_TEST_CASE(ReceiptMetaFollowsSnapshotNotTransitionCfg)
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
        .operator_fee_constant = 500,
        .da_footprint_gas_scalar = 3};
    std::vector<uint8_t> env{0x02, 0x11};

    // validate：Isthmus —— operator fee 生效、DA footprint 未生效，快照如实记录。
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    BOOST_REQUIRE(props.has_operator_fee);
    BOOST_REQUIRE(!props.has_da_footprint);

    // transition：cfg 两位都与 props 相反（operator fee 关、DA footprint 开）。
    OpForkConfig cfg = isthmusConfig();
    cfg.has_operator_fee = false;
    cfg.has_da_footprint = true;
    BOOST_REQUIRE(cfg.has_operator_fee != props.has_operator_fee);
    BOOST_REQUIRE(cfg.has_da_footprint != props.has_da_footprint);

    evmone::state::StateDiff diff;
    const auto txR =
        opTransition(ts, block, hashes, tx, cfg, vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);

    // 发送方被收了 operator fee（props 说了算），回执就必须报出来——读 cfg 会整个漏掉。
    const auto& meta = txR->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_CHECK_MESSAGE(meta->operator_fee.has_value(),
        "receipt must report the operator fee the sender was actually charged");
    // 交易并非按 DA footprint 定价，回执不得凭 transition 期 cfg 凭空添上该字段。
    BOOST_CHECK_MESSAGE(!meta->da_footprint_gas_scalar.has_value(),
        "receipt must not report a DA footprint the transaction was not priced under");
    BOOST_CHECK_MESSAGE(!meta->da_footprint.has_value(),
        "receipt must not report a DA footprint the transaction was not priced under");
}

// 守恒必须在 l1_cost 非零时验证。既有的 OperatorFeeConservesWhenCfgDisagreesWithProps 把
// l1_base_fee 设为 0，于是 props.l1_cost == 0，扣款那一侧根本没被覆盖：删掉
// `sender_acc.balance -= props.l1_cost`（OpTransition.cpp:130）后整套 93 个用例仍然全绿，
// 而那正是一笔每交易增发 l1_cost wei 的 mint。
//
// 这条同时钉住两侧：发送方净扣款额，以及总供应量不变。
BOOST_AUTO_TEST_CASE(L1CostIsDebitedFromSenderAndConserves)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    constexpr auto kFunding = 340282366920938463463374607431768211456_u256;
    ts[sender] = {.nonce = 0, .balance = kFunding, .storage = {}, .code = {}};
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

    // l1_base_fee 非零，operator fee 关闭：把 l1 这一项单独隔离出来。
    OpFeeParams fee{.l1_base_fee = 1000000000_u256,
        .base_fee_scalar = 1100,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env(120, 0x11);

    OpForkConfig cfg = isthmusConfig();
    cfg.has_operator_fee = false;

    const auto v = opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    BOOST_REQUIRE_MESSAGE(
        props.l1_cost > intx::uint256{0}, "test is vacuous unless l1_cost is non-zero");
    BOOST_REQUIRE_EQUAL(props.operator_cost_at_gas_limit, intx::uint256{0});

    const auto before = totalSupply(ts);
    evmone::state::StateDiff diff;
    const auto txR =
        opTransition(ts, block, hashes, tx, cfg, vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // 发送方净扣款 = gas_used*(base+tip) + l1_cost。少扣 l1_cost 即为增发。
    const auto gasUsed = intx::uint256{static_cast<uint64_t>(txR->gasUsed())};
    const auto tip = std::min(intx::uint256{tx.max_priority_gas_price},
        intx::uint256{tx.max_gas_price} - intx::uint256{block.base_fee});
    const auto expectedDebit = gasUsed * (intx::uint256{block.base_fee} + tip) + props.l1_cost;
    BOOST_CHECK_MESSAGE((kFunding - ts.at(sender).balance) == expectedDebit,
        "sender must be debited gas_used*(base+tip) + l1_cost");

    // L1 金库入账等额，且总量守恒。
    BOOST_CHECK_MESSAGE(
        (ts.at(OP_L1_FEE_VAULT).balance) == (props.l1_cost), "L1 vault must receive l1_cost");
    BOOST_CHECK_MESSAGE((totalSupply(ts)) == (before), "fees only move value between accounts");
}

// Round-11 F2: the CallSimulationView mask is visible to the EVM — a contract reading
// msg.sender.balance during a simulation observes the fabricated 2^256-1, so the behaviour is a
// decision on record rather than something found from a bug report. The contract returns
// BALANCE(CALLER); the receipt output carries the fabricated value.
BOOST_AUTO_TEST_CASE(CallSimulationMaskVisibleToBalanceOpcode)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[sender] = {.nonce = 0, .balance = 0, .storage = {}, .code = {}};
    // returns msg.sender.balance: CALLER; BALANCE; PUSH0; MSTORE; PUSH1 0x20; PUSH0; RETURN
    ts[dest] = {.nonce = 1,
        .balance = 0,
        .storage = {},
        .code = evmc::from_hex("33315f5260205ff3").value()};
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

    OpFeeParams fee{};
    std::vector<uint8_t> env{0x02, 0x11};
    CallSimulationView masked{ts, sender};
    const auto v =
        opValidate(masked, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        masked, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    // The contract returned msg.sender.balance. BALANCE runs after the simulation's fee
    // pre-charge (gas_limit * effective_gas_price) is deducted from the masked balance, so the
    // observable value is exactly 2^256-1 minus that pre-charge — the fabricated balance, not
    // the sender's real 0. This pins that the mask is visible to the EVM (a decision on record,
    // not an accident).
    const auto out = txR->output();
    BOOST_REQUIRE_EQUAL(out.size(), 32u);
    const auto effective = intx::uint256{block.base_fee} +
                           std::min(intx::uint256{tx.max_priority_gas_price},
                               intx::uint256{tx.max_gas_price} - intx::uint256{block.base_fee});
    const auto expectedBalance = std::numeric_limits<intx::uint256>::max() -
                                 intx::uint256{static_cast<uint64_t>(tx.gas_limit)} * effective;
    const auto expectedBe = intx::be::store<evmc::uint256be>(expectedBalance);
    BOOST_CHECK_EQUAL(std::memcmp(expectedBe.bytes, out.data(), sizeof(expectedBe.bytes)), 0);
}

BOOST_AUTO_TEST_SUITE_END()
