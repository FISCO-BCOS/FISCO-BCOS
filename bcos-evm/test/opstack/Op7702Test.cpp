// Op7702Test.cpp — EIP-7702 真实 ecrecover 端到端测试矩阵
//
// 金值来源: Python eth-account/eth-utils 权威实现按 keccak256(0x05 || rlp([chain_id,
// address, nonce])) 签出(原 scripts/gen_7702_vectors.py,已删;金值勿手改)。
// 私钥: 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d
// 对应地址 (authority): 0x70997970C51812dc3A010C7d01b50e0d17dc79C8
// 签名哈希公式: keccak256(0x05 || rlp([chain_id, address, nonce]))

#include "OpPredeploysSeed.h"
#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/eth/Eip7702Recover.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <evmone/delegation.hpp>
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
constexpr auto kSender7702 = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;

// === 金值:eth-account 签出, 私钥 0x59c6995e...86dae88c7a8412f4603b6b78690d ===
// authority = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8
constexpr auto kAuthority = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8_address;

// chain_id=1, addr=0xcc..., nonce=0 → 成功用例
const auto kR_ok = 0x8bd0c047683d78ac6855fd9997e17dd64c4941334308c2708930682e1831c42a_bytes32;
const auto kS_ok = 0x7399ba8d6bdec8bacec1cfb93d1f1bd00bedbade84959bda53464acaaa32f330_bytes32;
constexpr int kV_ok = 0;

// chain_id=1, addr=0xcc..., nonce=5 → nonce 不匹配用例（state nonce=0）
const auto kR_nonce5 = 0xba35713640851563a334d3887bdc0de9f609a846b065e7dae5de7d303fb054fb_bytes32;
const auto kS_nonce5 = 0x6223049799a988790d88847a6c3f767fec69cb72544e11a47c37552bcc6717ad_bytes32;
constexpr int kV_nonce5 = 0;

// 构造带一条 auth 的 set_code tx，执行 opTransition，返回 receipt + state diff（方案 A 阶段 2:
// opTransition 直接产 protocol::TransactionReceipt，state diff 经 out-param 返回）。
struct RunWithAuthResult
{
    bcos::protocol::TransactionReceipt::Ptr receipt;
    evmone::state::StateDiff diff;
};

RunWithAuthResult runWithAuth(
    test::TestState& ts, evmc::VM& vm, const state::Authorization& auth, uint64_t chainId = 1)
{
    test::TestBlockHashes hashes;
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::set_code;  // EIP-7702 set-code tx
    tx.sender = kSender7702;
    tx.to = kSender7702;  // 自调用；重点在 auth 处理
    tx.gas_limit = 200000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;
    tx.authorization_list = {auth};

    OpFeeParams fee{.l1_base_fee = 0_u256,
        .base_fee_scalar = 0,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env{0x04, 0x11};  // 非空 envelope 满足验证
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_CHECK(std::holds_alternative<OpTxProperties>(v));
    if (!std::holds_alternative<OpTxProperties>(v))
    {
        // 返回失败 receipt 以免崩溃，由调用方 BOOST_CHECK 捕获（与 base 的静态空 receipt
        // 守卫同形态）：status=1 使调用方的 BOOST_REQUIRE_EQUAL(r.receipt->status(), 0)
        // 干净失败而不是解引用 nullptr。
        auto failed = kOpTestReceiptFactory->createReceipt2(0, "", {}, 1, {}, 0);
        return RunWithAuthResult{std::move(failed), {}};
    }
    const auto& props = std::get<OpTxProperties>(v);
    evmone::state::StateDiff diff;
    auto receipt = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, chainId, kOpTestReceiptFactory, diff);
    return RunWithAuthResult{std::move(receipt), std::move(diff)};
}

[[nodiscard]] bool isDelegationDesignator(const evmc::bytes& code) noexcept
{
    return code.size() == 23 && code[0] == 0xef && code[1] == 0x01 && code[2] == 0x00;
}

void expectNoDelegationDesignatorAnywhere(const test::TestState& ts)
{
    for (const auto& [addr, acc] : ts)
    {
        BOOST_CHECK_MESSAGE(!(isDelegationDesignator(acc.code)),
            "account must NOT have delegation designator (0xef0100||addr)");
        (void)addr;
    }
}

/// 是否有任一账户被写入委托描述符。用于「该授权应当生效」的正向断言：改动 auth 的被签字段
/// （chain_id / nonce / addr）会让 ecrecover 恢复出另一个地址，因此只能按「某处生效」来断言，
/// 不能按 kAuthority 断言 —— 后者会让断言体永不执行而变成空转。
[[nodiscard]] bool anyDelegationDesignator(const test::TestState& ts)
{
    for (const auto& [addr, acc] : ts)
    {
        (void)addr;
        if (isDelegationDesignator(acc.code))
            return true;
    }
    return false;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Op7702Suite)

// ─── 用例 1: 真实 ecrecover 成功 → 写 0xef0100||kDelegate，nonce 从 0→1 ───
// signer = nullopt，强制走 ecrecover
BOOST_AUTO_TEST_CASE(RecoversAuthorityAndWritesDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    seedOpPredeploys(ts);

    // signer = nullopt → 必须走 ecrecover
    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    // authority 被写 0xef0100||kDelegate，nonce 从 0 → 1
    BOOST_REQUIRE_MESSAGE(
        (ts.find(kAuthority)) != (ts.end()), "authority account must exist after delegation");
    const auto& acc = ts.at(kAuthority);
    BOOST_REQUIRE_EQUAL(acc.code.size(), 23u);  // 3(magic) + 20(addr)
    BOOST_CHECK_EQUAL(acc.code[0], 0xef);
    BOOST_CHECK_EQUAL(acc.code[1], 0x01);
    BOOST_CHECK_EQUAL(acc.code[2], 0x00);
    // 后 20 字节应为 kDelegate
    const evmc::bytes expected_addr(kDelegate.bytes, kDelegate.bytes + 20);
    BOOST_CHECK_EQUAL(evmc::bytes(acc.code.begin() + 3, acc.code.end()), expected_addr);
    BOOST_CHECK_EQUAL(acc.nonce, 1u);
}

// ─── 用例 2: 无效 r → ecrecover 失败 → 不写 delegation ───
// signer = nullopt，强制走 ecrecover；r=0 使恢复失败 → skip
BOOST_AUTO_TEST_CASE(BadSignatureRecoverFailsNoDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    // r=0 → ecrecover 失败，不写 delegation
    const auto badR = evmc::bytes32{};

    // signer = nullopt → 必须走 ecrecover
    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(badR),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    // 坏签名只 skip 该条 authorization，tx 本身必须成功——否则"无委托"断言会把
    // "交易整体失败"误判为"正确跳过坏签名"。
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    // 真实 kAuthority 必须没有被委托
    auto it = ts.find(kAuthority);
    if (it != ts.end())
    {
        BOOST_CHECK_MESSAGE(
            it->second.code.empty(), "kAuthority must NOT have delegation after bad sig");
    }

    // ecrecover 可能恢复出其他地址；任何账户都不应被写入 delegation designator
    expectNoDelegationDesignatorAnywhere(ts);
}

// ─── 用例 3: nonce 不匹配 → 恢复出的正确 authority，但 auth.nonce=5 ≠ state_nonce=0 → skip ───
// signer = nullopt，强制走 ecrecover
BOOST_AUTO_TEST_CASE(NonceMismatchSkips)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    // kAuthority 不存在（state nonce=0），但 auth.nonce=5 → 不匹配
    seedOpPredeploys(ts);

    // 使用 nonce=5 签名的金值，signer = nullopt → 走 ecrecover
    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 5,  // 与 state nonce=0 不匹配
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_nonce5),
        .s = intx::be::load<intx::uint256>(kS_nonce5),
        .v = intx::uint256{kV_nonce5}};
    const auto r = runWithAuth(ts, vm, auth);
    // 交易本身必须成功（nonce 不匹配只 skip 该条 authorization）——否则"无委托"断言会把
    // opValidate 拒绝交易（status=1 空 diff 兜底 receipt）误判为"正确跳过"。
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    // kAuthority 不应有委托代码，nonce 不应被 bump
    auto it = ts.find(kAuthority);
    if (it != ts.end())
    {
        BOOST_CHECK_MESSAGE(it->second.code.empty(), "no delegation on nonce mismatch");
        BOOST_CHECK_MESSAGE((it->second.nonce) == (0u), "nonce must not be bumped on skip");
    }
}

// ─── 用例 4: chain_id 不匹配 → 步骤1 直接 skip，不进 ecrecover ───
// signer = nullopt；chain_id 检查在最前，不依赖签名有效性
BOOST_AUTO_TEST_CASE(ChainIdMismatchSkips)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    seedOpPredeploys(ts);

    // auth.chain_id=999, tx chainId=1 → step1 不匹配 → skip
    // signer = nullopt，复用 kR_ok/kS_ok（chain_id 检查在 ecrecover 之前，不执行签名验证）
    state::Authorization auth{.chain_id = 999,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth, /*chainId=*/1);
    // 交易本身必须成功（chain_id 不匹配只 skip 该条 authorization）——否则"无委托"断言
    // 会把 opValidate 拒绝交易误判为"正确跳过"。
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    // 必须全量扫描，不能只看 kAuthority：chain_id=999 时签名恢复出的是另一个（垃圾）地址，
    // kAuthority 根本不会进入 state，于是 `if (it != ts.end())` 形式的断言体永不执行——把
    // 链 id 判断改成 if(false) 也能全绿。用例 2 已经在用这个全量辅助。
    expectNoDelegationDesignatorAnywhere(ts);
}

// ─── 用例 5: 授权后委托调用 → kAuthority 预设委托代码，call to kAuthority 走 kDelegate 逻辑 ───
// 使用预置 signer（此用例测委托调用路径，不测 ecrecover）
BOOST_AUTO_TEST_CASE(DelegatedCallAfterAuthorization)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    // kDelegate: PUSH1 42 PUSH1 0 SSTORE — 在委托上下文中写入 kAuthority 的 slot0
    ts[kDelegate] = {
        .nonce = 0, .balance = 0, .storage = {}, .code = evmc::bytes{0x60, 0x2a, 0x60, 0x00, 0x55}};
    // kAuthority 已有委托代码指向 kDelegate（nonce=1 已 bump）
    {
        auto delegation_code = evmone::state::bytes(evmone::DELEGATION_MAGIC) +
                               evmone::state::bytes(kDelegate.bytes, kDelegate.bytes + 20);
        ts[kAuthority] = {.nonce = 1, .balance = 0, .storage = {}, .code = delegation_code};
    }
    seedOpPredeploys(ts);

    // 发送普通 call 到 kAuthority（此时已有委托代码）
    test::TestBlockHashes hashes;
    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender7702;
    tx.to = kAuthority;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{0};
    tx.nonce = 0;

    OpFeeParams fee{.l1_base_fee = 0_u256,
        .base_fee_scalar = 0,
        .blob_base_fee_scalar = 0,
        .blob_base_fee = 0_u256,
        .operator_fee_scalar = 0,
        .operator_fee_constant = 0};
    std::vector<uint8_t> env{0x04, 0x11};
    const auto v =
        opValidate(ts, block, tx, {env.data(), env.size()}, isthmusConfig(), fee, 30000000);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    evmone::state::StateDiff diff;
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1, kOpTestReceiptFactory, diff);

    // 委托调用应成功执行 kDelegate 代码；SSTORE 在 authority 上下文中落槽
    BOOST_CHECK_EQUAL(txR->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, diff);

    constexpr auto kSlot0 = evmc::bytes32{};
    const auto expectedSlot0 = intx::be::store<evmc::bytes32>(intx::uint256{42});
    BOOST_REQUIRE((ts.find(kAuthority)) != (ts.end()));
    BOOST_CHECK_MESSAGE((ts.at(kAuthority).storage.at(kSlot0)) == (expectedSlot0),
        "delegated call must execute kDelegate SSTORE under kAuthority context");
}

// ─── 以下五条覆盖此前无任何测试的 EIP-7702 校验规则 ───
// 变异审计表明：删掉 chain_id==0 的 universal 分支、EIP-2 malleability 守卫、y-parity 守卫、
// nonce 上限守卫，或步骤 5 的「代码须为空或已委托」检查，整套 93 个用例都照样全绿。
//
// 注意断言方向：改动 auth 中被签名覆盖的字段（chain_id / nonce）会让 ecrecover 恢复出另一个
// 地址，因此否定断言必须全量扫描（expectNoDelegationDesignatorAnywhere），正向断言也只能断
// 「某处生效」（anyDelegationDesignator）——按 kAuthority 断言会变成永不执行的空转。

// chain_id == 0 表示「对任何链有效」。丢掉 `auth.chain_id != 0 &&` 这半个条件，会把这类
// universal 授权在所有链上一律误拒。
BOOST_AUTO_TEST_CASE(ChainIdZeroIsUniversal)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 0,  // universal
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    // 节点在链 999：universal 授权仍须被处理。
    const auto r = runWithAuth(ts, vm, auth, /*chainId=*/999);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    BOOST_CHECK_MESSAGE(anyDelegationDesignator(ts),
        "an authorization with chain_id == 0 must be applied on any chain");
}

// EIP-2：s 必须 <= secp256k1n/2。高 s 是同一签名的可延展变体，去掉该守卫后 ecrecover 仍会
// 成功恢复出某个地址并写入委托。
BOOST_AUTO_TEST_CASE(HighSValueIsRejected)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = bcos::evm::eth::SECP256K1N_OVER_2 + 1,  // 恰好越过 EIP-2 上界
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    expectNoDelegationDesignatorAnywhere(ts);
}

// y-parity 只能是 0 或 1。去掉该守卫后 v=2 会被 `auth.v != 0` 当成 parity 1 送进 ecrecover。
BOOST_AUTO_TEST_CASE(InvalidYParityIsRejected)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{2}};  // 非法 parity
    const auto r = runWithAuth(ts, vm, auth);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    expectNoDelegationDesignatorAnywhere(ts);
}

// 步骤 2：nonce == 2^64-1 必须跳过——否则步骤 9 的 ++authority.nonce 会回绕到 0。
//
// 这条要能证伪，必须让步骤 6（auth.nonce == authority.nonce）也放行，否则去掉步骤 2 的守卫后
// 交易仍会被步骤 6 拦下，用例就成了「两条守卫都能让它通过」的假覆盖——最初写成那样时，去掉
// 守卫的变异确实没有被抓到。
//
// 把 auth.nonce 改成 2^64-1 会改变签名哈希，ecrecover 因而恢复出另一个确定地址；该地址由
// recoverAuthority 对下方金值直接算出（确定性函数，重复运行一致）。把它的 state nonce 预置成
// 2^64-1，步骤 6 即放行，此时唯一还能拦住这条授权的就是步骤 2。
constexpr auto kNonceMaxRecovered = 0x4e2cad1f006ebe3e4b701d4e77bc145167fb8ede_address;

BOOST_AUTO_TEST_CASE(NonceMaxIsRejected)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    constexpr auto kNonceMax = std::numeric_limits<uint64_t>::max();
    ts[kNonceMaxRecovered] = {.nonce = kNonceMax, .balance = 0_u256, .storage = {}, .code = {}};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = kNonceMax,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    // 前置条件：签名确实恢复到我们预置的那个地址，否则本用例退化为空转。
    BOOST_REQUIRE_MESSAGE((ts.find(kNonceMaxRecovered)) != (ts.end()),
        "recovered authority must be present; golden vector may have drifted");
    // 不得写入委托，且 nonce 不得从 2^64-1 回绕到 0。
    expectNoDelegationDesignatorAnywhere(ts);
    BOOST_CHECK_MESSAGE((ts.at(kNonceMaxRecovered).nonce) == (kNonceMax),
        "nonce at 2^64-1 must not be incremented (would wrap to 0)");
}

// 步骤 5：authority 已有非委托代码（普通合约）时必须跳过，否则会把一个已部署合约的代码
// 覆盖成委托描述符。这里签名与状态 nonce 都匹配，唯一该拦住它的就是这条代码检查。
BOOST_AUTO_TEST_CASE(AuthorityWithNonDelegatedCodeIsSkipped)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender7702] = {.nonce = 0,
        .balance = 340282366920938463463374607431768211456_u256,
        .storage = {},
        .code = {}};
    ts[kDelegate] = {};
    // authority 预置为一个普通合约（非委托代码），nonce 仍为 0 以便匹配 auth.nonce。
    const evmc::bytes existingCode{0x60, 0x00, 0x60, 0x00, 0xf3};
    ts[kAuthority] = {.nonce = 0, .balance = 0_u256, .storage = {}, .code = existingCode};
    seedOpPredeploys(ts);

    state::Authorization auth{.chain_id = 1,
        .addr = kDelegate,
        .nonce = 0,
        .signer = std::nullopt,
        .r = intx::be::load<intx::uint256>(kR_ok),
        .s = intx::be::load<intx::uint256>(kS_ok),
        .v = intx::uint256{kV_ok}};
    const auto r = runWithAuth(ts, vm, auth);
    BOOST_REQUIRE_EQUAL(r.receipt->status(), 0);
    bcos::evm::applyStateDiffStrict(ts, r.diff);

    BOOST_REQUIRE_MESSAGE((ts.find(kAuthority)) != (ts.end()), "authority must still exist");
    BOOST_CHECK_MESSAGE((ts.at(kAuthority).code) == (existingCode),
        "existing non-delegated code must not be overwritten by a delegation designator");
    BOOST_CHECK_MESSAGE(
        (ts.at(kAuthority).nonce) == (0u), "a skipped authorization must not advance the nonce");
}

BOOST_AUTO_TEST_SUITE_END()
