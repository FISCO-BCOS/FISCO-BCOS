// OpFloorGasTest.cpp — EIP-7623 calldata floor gas 验收测试
//
// EIP-7623 公式（Isthmus/Prague 生效）：
//   tokens    = zeroBytes * 1 + nonZeroBytes * 4
//   floorGas  = 21000 + tokens * 10
//   gas_used  = max(executionGasUsed, floorGas)
//
// 本轮验证：3000 个零字节 calldata
//   tokens   = 3000 * 1 = 3000
//   floorGas = 21000 + 3000 * 10 = 51000
//
// 标准 intrinsic（无 floor 时）= 21000 + 3000 * 4 = 33000
// floor(51000) > intrinsic(33000)，故 gas_used 应被抬升至 51000。
//
// 验证点：
//   - UserTx：opTransition 后 receipt.gas_used == props.props.min_gas_cost（== 51000）
//   - Deposit：runDeposit 后 gas_used 亦走 7623 floor（无 Isthmus deposit 豁免）

#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
/// Narrow the FISCO receipt's gasUsed (u256) to the int64 the assertions compare against.
inline int64_t floorReceiptGasUsed(const bcos::protocol::TransactionReceipt& r)
{
    return static_cast<int64_t>(static_cast<uint64_t>(r.gasUsed()));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpFloorGasSuite)

BOOST_AUTO_TEST_CASE(UserTxGasUsedRaisedToFloor)
{
    constexpr auto sender = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto dest = 0x00000000000000000000000000000000000000bb_address;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    // 2^128 — ample sender balance for gas payment
    ts[sender] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[dest] = {};  // 空账户，纯转账无执行开销
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
    tx.gas_limit = 5000000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.data = state::bytes(3000, 0x00);  // 3000 个零字节 → floor 抬升 gas_used

    OpFeeParams fee{};  // 全 0，隔离 L1/operator，聚焦 floor
    std::vector<uint8_t> env{0x02, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);

    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1234, kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(txR->status(), 0);
    // 7623 floor 生效：gas_used 恰等于公式推导的 floor，且严格大于 intrinsic
    constexpr int64_t kExpectedFloor3000 = 21000 + 3000 * 10;  // = 51000
    constexpr int64_t kIntrinsic3000 = 21000 + 3000 * 4;       // = 33000
    BOOST_CHECK_EQUAL(floorReceiptGasUsed(*txR), kExpectedFloor3000);
    BOOST_CHECK_EQUAL(floorReceiptGasUsed(*txR), props.props.min_gas_cost);
    BOOST_CHECK_GT(props.props.min_gas_cost, kIntrinsic3000);  // floor 51000 > intrinsic 33000
}

BOOST_AUTO_TEST_CASE(DepositGasUsedRaisedToFloor)
{
    constexpr auto depositor = OP_DEPOSITOR;
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[depositor] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = {}};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = depositor,
        .to = depositor,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 5000000,
        .is_system_tx = false,
        .data = state::bytes(3000, 0x00)};

    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, block, hashes, dep, isthmusConfig(), vm, 1234, block.gas_limit,
        kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(r->status(), 0);
    // deposit 同样吃 7623 floor（op-geth Isthmus 无豁免）：gas_used == floor
    constexpr int64_t kExpectedFloor3000 = 21000 + 3000 * 10;  // = 51000
    constexpr int64_t kExpectedFloorEmpty = 21000;             // empty calldata
    BOOST_CHECK_EQUAL(floorReceiptGasUsed(*r), kExpectedFloor3000);

    // 对照：空 calldata deposit 在独立 state 上运行，避免大 deposit 污染
    DepositTx small = dep;
    small.data = state::bytes{};
    test::TestState ts2;
    ts2[depositor] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = {}};
    seedOpPredeploys(ts2);
    evmone::state::StateDiff diff2;
    const auto rs = runDeposit(ts2, block, hashes, small, isthmusConfig(), vm, 1234,
        block.gas_limit, kOpTestReceiptFactory, diff2);
    BOOST_REQUIRE_EQUAL(rs->status(), 0);
    BOOST_CHECK_EQUAL(floorReceiptGasUsed(*rs), kExpectedFloorEmpty);
    BOOST_CHECK_GT(floorReceiptGasUsed(*r), floorReceiptGasUsed(*rs));
}

BOOST_AUTO_TEST_SUITE_END()
