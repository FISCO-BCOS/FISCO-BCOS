// Op7702Test.cpp — EIP-7702 真实 ecrecover 端到端测试矩阵
//
// 金值来源: bcos-evm-ref/test/opstack/scripts/gen_7702_vectors.py
// 私钥: 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d
// 对应地址 (authority): 0x70997970C51812dc3A010C7d01b50e0d17dc79C8
// 签名哈希公式: keccak256(0x05 || rlp([chain_id, address, nonce]))

#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/OpValidate.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <evmone/delegation.hpp>
#include <vector>

using namespace bcos::evmref::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;

// === 金值：gen_7702_vectors.py, 私钥 0x59c6995e...86dae88c7a8412f4603b6b78690d ===
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

// 构造带一条 auth 的 set_code tx，执行 opTransition，返回 receipt 并把 diff 落回 ts。
OpTxReceipt runWithAuth(
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
    tx.sender = kSender;
    tx.to = kSender;  // 自调用；重点在 auth 处理
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
    EXPECT_TRUE(std::holds_alternative<OpTxProperties>(v));
    if (!std::holds_alternative<OpTxProperties>(v))
    {
        // 返回失败 receipt 以免崩溃，由调用方 EXPECT_TRUE 捕获
        static const evmone::state::TransactionReceipt kEmpty{};
        return OpTxReceipt{kEmpty, {}};
    }
    const auto& props = std::get<OpTxProperties>(v);
    return opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, chainId, {env.data(), env.size()});
}

[[nodiscard]] bool isDelegationDesignator(const evmc::bytes& code) noexcept
{
    return code.size() == 23 && code[0] == 0xef && code[1] == 0x01 && code[2] == 0x00;
}

void expectNoDelegationDesignatorAnywhere(const test::TestState& ts)
{
    for (const auto& [addr, acc] : ts)
    {
        EXPECT_FALSE(isDelegationDesignator(acc.code))
            << "account must NOT have delegation designator (0xef0100||addr)";
        (void)addr;
    }
}
}  // namespace

// ─── 用例 1: 真实 ecrecover 成功 → 写 0xef0100||kDelegate，nonce 从 0→1 ───
// signer = nullopt，强制走 ecrecover
TEST(Op7702, RecoversAuthorityAndWritesDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
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
    ASSERT_EQ(r.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiffStrict(ts, r.receipt.state_diff);

    // authority 被写 0xef0100||kDelegate，nonce 从 0 → 1
    ASSERT_NE(ts.find(kAuthority), ts.end()) << "authority account must exist after delegation";
    const auto& acc = ts.at(kAuthority);
    ASSERT_EQ(acc.code.size(), 23u);  // 3(magic) + 20(addr)
    EXPECT_EQ(acc.code[0], 0xef);
    EXPECT_EQ(acc.code[1], 0x01);
    EXPECT_EQ(acc.code[2], 0x00);
    // 后 20 字节应为 kDelegate
    const evmc::bytes expected_addr(kDelegate.bytes, kDelegate.bytes + 20);
    EXPECT_EQ(evmc::bytes(acc.code.begin() + 3, acc.code.end()), expected_addr);
    EXPECT_EQ(acc.nonce, 1u);
}

// ─── 用例 2: 无效 r → ecrecover 失败 → 不写 delegation ───
// signer = nullopt，强制走 ecrecover；r=0 使恢复失败 → skip
TEST(Op7702, BadSignatureRecoverFailsNoDelegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
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
    ASSERT_EQ(r.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiffStrict(ts, r.receipt.state_diff);

    // 真实 kAuthority 必须没有被委托
    auto it = ts.find(kAuthority);
    if (it != ts.end())
        EXPECT_TRUE(it->second.code.empty()) << "kAuthority must NOT have delegation after bad sig";

    // ecrecover 可能恢复出其他地址；任何账户都不应被写入 delegation designator
    expectNoDelegationDesignatorAnywhere(ts);
}

// ─── 用例 3: nonce 不匹配 → 恢复出的正确 authority，但 auth.nonce=5 ≠ state_nonce=0 → skip ───
// signer = nullopt，强制走 ecrecover
TEST(Op7702, NonceMismatchSkips)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
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
    bcos::evmref::applyStateDiffStrict(ts, r.receipt.state_diff);

    // kAuthority 不应有委托代码，nonce 不应被 bump
    auto it = ts.find(kAuthority);
    if (it != ts.end())
    {
        EXPECT_TRUE(it->second.code.empty()) << "no delegation on nonce mismatch";
        EXPECT_EQ(it->second.nonce, 0u) << "nonce must not be bumped on skip";
    }
}

// ─── 用例 4: chain_id 不匹配 → 步骤1 直接 skip，不进 ecrecover ───
// signer = nullopt；chain_id 检查在最前，不依赖签名有效性
TEST(Op7702, ChainIdMismatchSkips)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
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
    bcos::evmref::applyStateDiffStrict(ts, r.receipt.state_diff);

    auto it = ts.find(kAuthority);
    if (it != ts.end())
        EXPECT_TRUE(it->second.code.empty()) << "no delegation on chain_id mismatch";
}

// ─── 用例 5: 授权后委托调用 → kAuthority 预设委托代码，call to kAuthority 走 kDelegate 逻辑 ───
// 使用预置 signer（此用例测委托调用路径，不测 ecrecover）
TEST(Op7702, DelegatedCallAfterAuthorization)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = 340282366920938463463374607431768211456_u256};
    // kDelegate: PUSH1 42 PUSH1 0 SSTORE — 在委托上下文中写入 kAuthority 的 slot0
    ts[kDelegate] = {.code = evmc::bytes{0x60, 0x2a, 0x60, 0x00, 0x55}};
    // kAuthority 已有委托代码指向 kDelegate（nonce=1 已 bump）
    {
        auto delegation_code = evmone::state::bytes(evmone::DELEGATION_MAGIC) +
                               evmone::state::bytes(kDelegate.bytes, kDelegate.bytes + 20);
        ts[kAuthority] = {.nonce = 1, .code = delegation_code};
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
    tx.sender = kSender;
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
    ASSERT_TRUE(std::holds_alternative<OpTxProperties>(v));
    const auto& props = std::get<OpTxProperties>(v);
    const auto txR = opTransition(
        ts, block, hashes, tx, isthmusConfig(), vm, props, 1, {env.data(), env.size()});

    // 委托调用应成功执行 kDelegate 代码；SSTORE 在 authority 上下文中落槽
    EXPECT_EQ(txR.receipt.status, EVMC_SUCCESS);
    bcos::evmref::applyStateDiffStrict(ts, txR.receipt.state_diff);

    constexpr auto kSlot0 = evmc::bytes32{};
    const auto expectedSlot0 = intx::be::store<evmc::bytes32>(intx::uint256{42});
    ASSERT_NE(ts.find(kAuthority), ts.end());
    EXPECT_EQ(ts.at(kAuthority).storage.at(kSlot0), expectedSlot0)
        << "delegated call must execute kDelegate SSTORE under kAuthority context";
}
