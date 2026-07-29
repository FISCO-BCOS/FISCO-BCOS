#define BOOST_TEST_MODULE BcosEvmEthTests
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>  // keccak256（Stub 的 code_hash 计算）
#include <bcos-evm/eth/state/state.hpp>
#include <map>
#include <system_error>
#include <variant>

using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
using EthResult = std::variant<state::TransactionReceipt, std::error_code>;

/// validate -> transition -> sanitize, inlined here after EthTransition.{h,cpp} was removed.
/// chainId is the NODE's chain id: state::transition compares each EIP-7702 authorization
/// against it, and validate_transaction never checks tx.chain_id, so it must not be read off
/// the transaction.
EthResult runTx(const state::StateView& view, const state::BlockInfo& block,
    const state::BlockHashes& hashes, const state::Transaction& tx, evmc_revision rev, evmc::VM& vm,
    int64_t blockGasLeft, int64_t blobGasLeft, uint64_t chainId)
{
    const auto validated =
        state::validate_transaction(view, block, tx, rev, blockGasLeft, blobGasLeft);
    if (const auto* err = std::get_if<std::error_code>(&validated))
        return *err;
    auto receipt = state::transition(view, block, hashes, tx, rev, vm,
        std::get<state::TransactionProperties>(validated), chainId);
    receipt.state_diff = bcos::evm::sanitizeStateDiff(view, std::move(receipt.state_diff));
    return receipt;
}
// NOTE: 原始数字分隔符写法 1'000'...'_u256 在本仓库 vcpkg 锁定的 intx 0.15.0 下编译失败：
// intx::from_string 的 from_dec_digit 不识别 '\'' 分隔符（consteval 求值直接抛错，
// 见 intx.hpp from_dec_digit/from_string）。数值不变，仅去除分隔符使其可编译。
constexpr auto kFunding = 1000000000000000000_u256;  // 1 ETH in wei
constexpr auto kWithdrawalWei = 5000000000_u256;     // 5 gwei = 5e9 wei

// 最小内存 StateView 桩：三个只读方法与真实实现语义对齐（code_hash = keccak256(code)，
// has_storage = 存储非空）。内存后端与 StateDiff 回写缝属后续 PR；本文件的断言直接
// 检查 receipt.state_diff / finalize 返回的 diff，不做写回。
struct StubAccount
{
    uint64_t nonce = 0;
    intx::uint256 balance;
    std::map<evmc::bytes32, evmc::bytes32> storage;
    evmc::bytes code;
};

class StubState : public state::StateView
{
public:
    std::map<evmc::address, StubAccount> accounts;

    std::optional<Account> get_account(const evmc::address& addr) const noexcept override
    {
        const auto it = accounts.find(addr);
        if (it == accounts.end())
            return std::nullopt;
        const auto& acc = it->second;
        return Account{acc.nonce, acc.balance, keccak256(acc.code), !acc.storage.empty()};
    }

    evmc::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        const auto it = accounts.find(addr);
        return it != accounts.end() ? it->second.code : evmc::bytes{};
    }

    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        const auto it = accounts.find(addr);
        if (it == accounts.end())
            return {};
        const auto sit = it->second.storage.find(key);
        return sit != it->second.storage.end() ? sit->second : evmc::bytes32{};
    }
};

class StubBlockHashes : public state::BlockHashes
{
public:
    evmc::bytes32 get_block_hash(int64_t /*block_number*/) const noexcept override { return {}; }
};

// 在 diff 的 modified_accounts 中查找账户条目；找不到返回 nullptr。
const state::StateDiff::Entry* findModified(const state::StateDiff& diff, const evmc::address& addr)
{
    for (const auto& m : diff.modified_accounts)
        if (m.addr == addr)
            return &m;
    return nullptr;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EthTransitionTest)

BOOST_AUTO_TEST_CASE(SimpleTransfer21000)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    const auto receiver = 0x0000000000000000000000000000000000001234_address;

    StubState state;
    state.accounts[sender].balance = kFunding;  // nonce 缺省 0

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
    StubBlockHashes hashes;
    const auto res =
        runTx(state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432, /*chainId=*/1);

    if (const auto* err = std::get_if<std::error_code>(&res))
        BOOST_FAIL("runTransaction: " + err->message());
    const auto& receipt = std::get<state::TransactionReceipt>(res);
    BOOST_CHECK(receipt.status == EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(receipt.gas_used, 21000);

    // 包装不写回：结果只体现在 receipt.state_diff 中
    const auto* entry = findModified(receipt.state_diff, receiver);
    BOOST_REQUIRE(entry != nullptr);
    BOOST_CHECK(entry->balance == 1000);
}

BOOST_AUTO_TEST_CASE(InvalidTxRejectedWithoutSideEffect)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    StubState state;
    state.accounts[sender].balance = 1_u256;  // 付不起 gas；nonce 缺省 0

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
    StubBlockHashes hashes;
    const auto res =
        runTx(state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, 786432, /*chainId=*/1);

    BOOST_REQUIRE(std::holds_alternative<std::error_code>(res));
    BOOST_CHECK(std::get<std::error_code>(res) ==
                state::make_error_code(state::INSUFFICIENT_FUNDS));  // 钉住拒因，防其他校验失败假绿
}

// 钉死 blockGasLeft/blobGasLeft 两个相邻 int64_t 形参不被换序：
// blobGasLeft=0 时 type-3 blob tx 必须被拒（换序后 786432 会放行它）。
BOOST_AUTO_TEST_CASE(BlobTxRejectedWhenNoBlobGasLeft)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;
    StubState state;
    state.accounts[sender].balance = kFunding;  // nonce 缺省 0

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
    StubBlockHashes hashes;
    const auto res =
        runTx(state, block, hashes, tx, EVMC_CANCUN, vm, block.gas_limit, /*blobGasLeft=*/0,
            /*chainId=*/1);

    BOOST_REQUIRE(std::holds_alternative<std::error_code>(res));
    BOOST_CHECK(std::get<std::error_code>(res) ==
                state::make_error_code(state::BLOB_GAS_LIMIT_EXCEEDED));  // 拒因必须是 blob 预算
}

// runBlockFinalize 的 withdrawals 路径：金额按 gwei 计，diff 中须为 ×1e9 换算后的 wei。
BOOST_AUTO_TEST_CASE(FinalizeAppliesWithdrawalGweiToWei)
{
    const auto payee = 0x0000000000000000000000000000000000005e11_address;
    StubState state;

    const state::Withdrawal w{
        .index = 0, .validator_index = 0, .recipient = payee, .amount_in_gwei = 5};
    const auto diff = bcos::evm::sanitizeStateDiff(state,
        state::finalize(state, EVMC_CANCUN, 0x00000000000000000000000000000000c014ba5e_address,
            std::nullopt, {}, std::span{&w, 1}));

    const auto* entry = findModified(diff, payee);
    BOOST_REQUIRE(entry != nullptr);
    BOOST_CHECK(entry->balance == kWithdrawalWei);
}

BOOST_AUTO_TEST_SUITE_END()
