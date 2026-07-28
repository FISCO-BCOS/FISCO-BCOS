#include <bcos-evm/eth/EthTransition.h>
#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include "OpPredeploysSeed.h"
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include "TestPrinters.h"
#include <boost/test/unit_test.hpp>
#include <test/utils/test_state.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <vector>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kSender = 0x00000000000000000000000000000000000000aa_address;
constexpr auto kDest = 0x00000000000000000000000000000000000000bb_address;
constexpr auto kFunding = 340282366920938463463374607431768211456_u256;

[[nodiscard]] bool isOpFeeVaultExceptCoinbase(const evmc::address& addr) noexcept
{
    // SequencerFeeVault 常作 coinbase：tip 入账两边都应一致，须参与非 vault 等价比较。
    return addr == OP_BASE_FEE_VAULT || addr == OP_L1_FEE_VAULT || addr == OP_OPERATOR_FEE_VAULT;
}

[[nodiscard]] std::vector<state::StateDiff::Entry> nonVaultEntries(const state::StateDiff& diff)
{
    std::vector<state::StateDiff::Entry> out;
    out.reserve(diff.modified_accounts.size());
    for (const auto& e : diff.modified_accounts)
    {
        if (!isOpFeeVaultExceptCoinbase(e.addr))
            out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return std::memcmp(a.addr.bytes, b.addr.bytes, 20) < 0;
    });
    return out;
}

[[nodiscard]] std::vector<evmc::address> nonVaultDeleted(const state::StateDiff& diff)
{
    std::vector<evmc::address> out;
    for (const auto& addr : diff.deleted_accounts)
    {
        if (!isOpFeeVaultExceptCoinbase(addr))
            out.push_back(addr);
    }
    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b) { return std::memcmp(a.bytes, b.bytes, 20) < 0; });
    return out;
}

[[nodiscard]] std::optional<intx::uint256> balanceOf(
    const state::StateDiff& diff, const evmc::address& addr)
{
    for (const auto& e : diff.modified_accounts)
    {
        if (e.addr == addr)
            return e.balance;
    }
    return std::nullopt;
}

[[nodiscard]] bool entryEq(const state::StateDiff::Entry& a, const state::StateDiff::Entry& b)
{
    return a.addr == b.addr && a.nonce == b.nonce && a.balance == b.balance && a.code == b.code &&
           a.modified_storage == b.modified_storage;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpZeroDiffSuite)

/// M6 零值差分护栏（rev.8 D12）：
/// OpFeeParams=0、operator off 时，opTransition 与 eth::runTransaction 在「非 vault 账户」上
/// state_diff 逐位等价；OP 相对 ETH 的唯一可解释差异是 BaseFeeVault += gasUsed×baseFee
/// （ETH 隐式销毁 base fee，OP 显式入账）。fee=0 时 OP 仍可能 touch 空 L1/Operator vault
/// 并被 EIP-161 删掉——属 OP 结算面，不纳入非 vault 等价。不验证 OP fee 逻辑本身。
BOOST_AUTO_TEST_CASE(SimpleTransferMatchesEthExceptBaseFeeVault)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kSender] = {.nonce = 0, .balance = kFunding, .storage = {}, .code = {}};
    ts[kDest] = {};
    seedOpPredeploys(ts);
    test::TestBlockHashes hashes;

    state::BlockInfo block;
    block.number = 1;
    block.gas_limit = 30000000;
    block.base_fee = 7;
    block.coinbase = OP_SEQUENCER_FEE_VAULT;

    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kSender;
    tx.to = kDest;
    tx.gas_limit = 100000;
    tx.max_gas_price = 1000;
    tx.max_priority_gas_price = 10;
    tx.value = intx::uint256{1000};
    tx.nonce = 0;
    tx.chain_id = 1;

    OpForkConfig cfg = isthmusConfig();
    cfg.has_operator_fee = false;
    cfg.precompiles = nullptr;  // 避开 override 表，贴近母本 Host 派发

    const OpFeeParams fee{};
    const std::vector<uint8_t> env{0x02};
    const auto validated =
        opValidate(ts, block, tx, {env.data(), env.size()}, cfg, fee, block.gas_limit);
    BOOST_REQUIRE(std::holds_alternative<OpTxProperties>(validated));
    const auto& props = std::get<OpTxProperties>(validated);
    BOOST_CHECK_EQUAL(props.l1_cost, intx::uint256{0});
    BOOST_CHECK_EQUAL(props.operator_cost_at_gas_limit, intx::uint256{0});

    const auto opTxR = opTransition(
        ts, block, hashes, tx, cfg, vm, props, /*chainId=*/1);
    BOOST_REQUIRE_EQUAL(opTxR.receipt.status, EVMC_SUCCESS);
    const auto& opReceipt = opTxR.receipt;

    const auto ethRes = bcos::evm::eth::runTransaction(
        ts, block, hashes, tx, cfg.rev, vm, block.gas_limit, /*blobGasLeft=*/0);
    BOOST_REQUIRE(std::holds_alternative<state::TransactionReceipt>(ethRes));
    const auto& ethReceipt = std::get<state::TransactionReceipt>(ethRes);
    BOOST_REQUIRE_EQUAL(ethReceipt.status, EVMC_SUCCESS);

    BOOST_CHECK_EQUAL(opReceipt.gas_used, ethReceipt.gas_used);
    BOOST_CHECK_EQUAL(opReceipt.status, ethReceipt.status);

    const auto opNonVault = nonVaultEntries(opReceipt.state_diff);
    const auto ethNonVault = nonVaultEntries(ethReceipt.state_diff);
    BOOST_REQUIRE_EQUAL(opNonVault.size(), ethNonVault.size());
    for (size_t i = 0; i < opNonVault.size(); ++i)
        BOOST_CHECK_MESSAGE(entryEq(opNonVault[i], ethNonVault[i]), "mismatch at non-vault index " << i);

    BOOST_CHECK((nonVaultDeleted(opReceipt.state_diff)) == (nonVaultDeleted(ethReceipt.state_diff)));

    // fee=0 下四个 vault 因已有 stub code 不再被判为空账户删除
    for (const auto& v :
        {OP_BASE_FEE_VAULT, OP_L1_FEE_VAULT, OP_OPERATOR_FEE_VAULT, OP_SEQUENCER_FEE_VAULT})
    {
        BOOST_CHECK_MESSAGE((std::count(opReceipt.state_diff.deleted_accounts.begin(),
                      opReceipt.state_diff.deleted_accounts.end(), v)) == (0), "vault should not be deleted");
    }

    const auto baseVaultBal = balanceOf(opReceipt.state_diff, OP_BASE_FEE_VAULT);
    BOOST_REQUIRE(baseVaultBal.has_value());
    BOOST_CHECK_EQUAL(*baseVaultBal, intx::uint256{static_cast<uint64_t>(opReceipt.gas_used)} * intx::uint256{block.base_fee});

    // L1 / operator 费用为 0：不应出现在 eth diff；OP 侧若 touch 余额须仍为 0。
    if (const auto l1 = balanceOf(opReceipt.state_diff, OP_L1_FEE_VAULT))
    {
        BOOST_CHECK_EQUAL(*l1, intx::uint256{0});
    }
    if (const auto opv = balanceOf(opReceipt.state_diff, OP_OPERATOR_FEE_VAULT))
    {
        BOOST_CHECK_EQUAL(*opv, intx::uint256{0});
    }
    BOOST_CHECK(!(balanceOf(ethReceipt.state_diff, OP_BASE_FEE_VAULT).has_value()));
    BOOST_CHECK(!(balanceOf(ethReceipt.state_diff, OP_L1_FEE_VAULT).has_value()));
    BOOST_CHECK(!(balanceOf(ethReceipt.state_diff, OP_OPERATOR_FEE_VAULT).has_value()));
}

BOOST_AUTO_TEST_SUITE_END()
