#define BOOST_TEST_MODULE BcosEvmEthTests
#include <bcos-evm/adapter/StateDiffSanitize.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <bcos-evm/eth/state/hash_utils.hpp>  // keccak256（Stub 的 code_hash 计算）
#include <bcos-evm/eth/state/host.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
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

// === EIP-7702 金值：与 opstack/Op7702Test.cpp 同源，由 eth-account 按
// keccak256(0x05 || rlp([chain_id, address, nonce])) 签出。
// 私钥 0x59c6995e998f97a5a0044966f0945389dc9e86dae88c7a8412f4603b6b78690d
// 签名覆盖 chain_id = 1、addr = kDelegate、nonce = 0。金值勿手改。===
constexpr auto kAuthority = 0x70997970C51812dc3A010C7d01b50e0d17dc79C8_address;
constexpr auto kDelegate = 0x00000000000000000000000000000000000000cc_address;
const auto kAuthR = 0x8bd0c047683d78ac6855fd9997e17dd64c4941334308c2708930682e1831c42a_bytes32;
const auto kAuthS = 0x7399ba8d6bdec8bacec1cfb93d1f1bd00bedbade84959bda53464acaaa32f330_bytes32;

// StateDiff::Entry::code 仅在代码变更时才有值，所以「无值」本身就是「没写委托」的证据。
[[nodiscard]] bool isDelegationDesignator(const std::optional<evmc::bytes>& code) noexcept
{
    return code.has_value() && code->size() == 23 && (*code)[0] == 0xef && (*code)[1] == 0x01 &&
           (*code)[2] == 0x00;
}

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

// EIP-7702 步骤 1 必须用「节点的」chain id 判定授权，而不是交易自带的 tx.chain_id。
// validate_transaction 从不校验 tx.chain_id，所以若把它当作节点 chain id 传入，比较就成了
// 「用户输入 == 用户输入」，恒真：为链 X 签的授权只要把 tx.chain_id 也填成 X，就能在链 Y 上
// 写入委托。本用例专门钉这一点——授权金值签的是 chain_id = 1，攻击者把 tx.chain_id 也设为 1，
// 而节点在链 999，委托必须不被写入。旧实现（传 tx.chain_id）会写入，故本断言可证伪。
//
// 该路径此前是死代码：base 的步骤 3 是 `if (!auth.signer.has_value()) continue;`，而 signer
// 只有 evmone 的 t8n fixture loader 会填，真实字节解出来恒为 nullopt，于是每条授权都被跳过。
// 本分支换成真 ecrecover 后这条链 id 校验才变成活的。
BOOST_AUTO_TEST_CASE(AuthorizationForAnotherChainIsSkipped)
{
    const auto sender = 0xa94f5374fce5edbc8e2a8697c15331677e6ebf0b_address;

    const auto runOnChain = [&](uint64_t nodeChainId) {
        StubState state;
        state.accounts[sender].balance = kFunding;
        state.accounts[kDelegate];  // 委托目标存在

        state::BlockInfo block;
        block.number = 1;
        block.gas_limit = 30'000'000;
        block.base_fee = 7;

        state::Transaction tx;
        tx.type = state::Transaction::Type::set_code;  // EIP-7702
        tx.sender = sender;
        tx.to = sender;  // set_code tx 必须有 to；重点在 auth 处理
        tx.gas_limit = 200'000;
        tx.max_gas_price = 1000;
        tx.max_priority_gas_price = 10;
        tx.nonce = 0;
        tx.chain_id = 1;  // 攻击者填成授权所属链，恰是旧实现会误信的那个值
        tx.authorization_list = {state::Authorization{.chain_id = 1,
            .addr = kDelegate,
            .nonce = 0,
            .signer = std::nullopt,  // 强制走真 ecrecover
            .r = intx::be::load<intx::uint256>(kAuthR),
            .s = intx::be::load<intx::uint256>(kAuthS),
            .v = intx::uint256{0}}};

        evmc::VM vm{evmc_create_evmone()};
        StubBlockHashes hashes;
        return runTx(state, block, hashes, tx, EVMC_PRAGUE, vm, block.gas_limit,
            /*blobGasLeft=*/0, nodeChainId);
    };

    // 正向对照：节点就在链 1 时授权成立，委托被写入。没有这一条，下面的否定断言可能
    // 因为签名恢复失败之类的无关原因而假绿。
    {
        const auto res = runOnChain(1);
        BOOST_REQUIRE(std::holds_alternative<state::TransactionReceipt>(res));
        const auto* entry =
            findModified(std::get<state::TransactionReceipt>(res).state_diff, kAuthority);
        BOOST_REQUIRE_MESSAGE(entry != nullptr, "authority must appear in the diff on chain 1");
        BOOST_REQUIRE_MESSAGE(isDelegationDesignator(entry->code),
            "chain 1: delegation designator (0xef0100||addr) must be written");
        BOOST_CHECK_MESSAGE(
            std::equal(entry->code->begin() + 3, entry->code->end(), std::begin(kDelegate.bytes)),
            "delegation designator must point at kDelegate");
        BOOST_CHECK_EQUAL(entry->nonce, 1u);  // 步骤 9：authority nonce 前进
    }

    // 判别断言：节点在链 999，授权只为链 1 签过 —— 必须跳过。
    {
        const auto res = runOnChain(999);
        BOOST_REQUIRE(std::holds_alternative<state::TransactionReceipt>(res));
        const auto& diff = std::get<state::TransactionReceipt>(res).state_diff;
        const auto* entry = findModified(diff, kAuthority);
        if (entry != nullptr)  // 未授权时 authority 可能因 warm 访问出现在 diff 中，但不得带委托
        {
            BOOST_CHECK_MESSAGE(!isDelegationDesignator(entry->code),
                "cross-chain authorization must NOT write a delegation designator");
            BOOST_CHECK_EQUAL(entry->nonce, 0u);  // nonce 不得前进
        }
        for (const auto& e : diff.modified_accounts)
        {
            BOOST_CHECK_MESSAGE(!isDelegationDesignator(e.code),
                "no account may receive a delegation designator on a foreign chain");
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()

// ==========================================================================
// EIP-4788 beacon roots（BEACON_ROOTS_ADDRESS）钉子格。
// 生产入口：OpBlockExecute.h 的 preBlockOpSteps → system_call_block_start；
// 该 entry 由 bcos-evm/eth/state/system_contracts.cpp 以 EVMC_CANCUN（Ecotone）档启用。
// kBeaconRootsCode 是主网真实部署的历史根合约字节码（97 字节），与语料
// isthmus/jovian_system_contracts_real.json preState 中 0x000f...beac02 的 code
// 逐字一致（语料只读，此处为拷贝常量）；golden 重放（OpNewPayloadRpcE2eTest
// ::runGoldenVector）覆盖 Ecotone+ 正常写路径（WI-24 格①，不在此重建）。
// ==========================================================================
const auto kBeaconRootsCode = evmc::from_hex(
    "3373fffffffffffffffffffffffffffffffffffffffe14604d57602036146024575f5ffd5b5f35"
    "801560495762001fff810690815414603c575f5ffd5b62001fff01545f5260205ff35b5f5ffd5b"
    "62001fff42064281555f359062001fff015500")
                                  .value();

BOOST_AUTO_TEST_SUITE(BeaconRootsSystemContractTest)

// 格② pre-Ecotone 负向：beacon roots entry 的 since = EVMC_CANCUN，Ecotone 之前
// （Regolith=EVMC_PARIS / Canyon=EVMC_SHANGHAI）不得执行该系统调用 —— 语义上等价于
// EIP-4788 的"if no code exists at [address], the call must fail silently"：
// 地址无合约 → 空码/空效果，绝不是 panic 或意外状态写入。本格故意把真实字节码
// 预置在 BEACON_ROOTS_ADDRESS 并给非零 parent_beacon_block_root：一旦档位 gate
// 被放开，写路径会把 SSTORE 写进 diff，断言立即失败（反向验证可 RED）。
// clang-format off
BOOST_AUTO_TEST_CASE(PreEcotoneDoesNotRunBeaconRootsSystemCall, * boost::unit_test::label("fork-regolith") * boost::unit_test::label("fork-canyon"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};

    // (a) 档位 gate：pre-Ecotone（Canyon = EVMC_SHANGHAI）即使地址有码也不执行。
    {
        StubState stub;
        auto& acc = stub.accounts[state::BEACON_ROOTS_ADDRESS];
        acc.code = kBeaconRootsCode;
        acc.nonce = 1;
        state::BlockInfo block;
        block.number = 1000;
        block.timestamp = 1000;
        block.gas_limit = 30'000'000;
        block.parent_beacon_block_root =
            0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20_bytes32;
        const StubBlockHashes hashes;

        const auto diff = state::system_call_block_start(stub, block, hashes, EVMC_SHANGHAI, vm);
        BOOST_CHECK_MESSAGE(diff.modified_accounts.empty(),
            "pre-Ecotone must not execute the beacon roots system call");
    }

    // (b) Ecotone 档但地址无码（账户不存在）：EIP-4788 要求静默跳过 —— 空返回、
    // 不 panic、diff 为空。
    {
        StubState stub;  // BEACON_ROOTS_ADDRESS 无账户、无码
        state::BlockInfo block;
        block.number = 1000;
        block.timestamp = 1000;
        block.gas_limit = 30'000'000;
        block.parent_beacon_block_root =
            0x0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20_bytes32;
        const StubBlockHashes hashes;

        const auto diff = state::system_call_block_start(stub, block, hashes, EVMC_CANCUN, vm);
        BOOST_CHECK_MESSAGE(diff.modified_accounts.empty(),
            "Cancun with no code at BEACON_ROOTS_ADDRESS must fail silently per EIP-4788");
    }
}

// 格③ 未记录时间戳 → 空返回：Ecotone 档下以普通调用者向 BEACON_ROOTS_ADDRESS 发起
// 32 字节 calldata 的查询（ring buffer 为空，时间戳必然未命中）。实现即主网真实
// 字节码：查询路径在 sload(ts & 0x1fff) != ts 时执行 revert(0,0)——即 EVMC_REVERT
// 且返回数据 0 长度。注意 EIP-4788 参考实现的未命中语义是"revert 空数据"而非
// "返回 32 字节全零"，本断言以代码实际行为为准。
// clang-format off
BOOST_AUTO_TEST_CASE(UnknownTimestampRevertsWithEmptyOutput, * boost::unit_test::label("fork-ecotone") * boost::unit_test::label("fork-fjord") * boost::unit_test::label("fork-granite") * boost::unit_test::label("fork-holocene") * boost::unit_test::label("fork-isthmus") * boost::unit_test::label("fork-jovian") * boost::unit_test::label("fork-karst"))
// clang-format on
{
    auto vm = evmc::VM{evmc_create_evmone()};
    StubState stub;
    auto& acc = stub.accounts[state::BEACON_ROOTS_ADDRESS];
    acc.code = kBeaconRootsCode;
    acc.nonce = 1;
    // ring buffer 完全为空：没有任何时间戳被记录过。

    state::BlockInfo block;
    block.number = 1000;
    block.timestamp = 1000;
    block.gas_limit = 30'000'000;
    const StubBlockHashes hashes;
    const state::Transaction emptyTx{};
    state::State st{stub};
    state::Host host{EVMC_CANCUN, vm, st, block, hashes, emptyTx};

    // 一个从未出块的未来时间戳（不在 ring 中，且非 0）。
    const auto ts = 0x000000000000000000000000000000000000000000000000000000007fffffff_bytes32;
    constexpr auto kCaller = 0x0000000000000000000000000000000000001234_address;
    const evmc_message msg{
        .kind = EVMC_CALL,
        .flags = 0,
        .depth = 0,
        .gas = 30'000'000,
        .recipient = state::BEACON_ROOTS_ADDRESS,
        .sender = kCaller,  // 非 SYSTEM_ADDRESS → 走查询路径而非写路径
        .input_data = ts.bytes,
        .input_size = sizeof(ts.bytes),
        .value = {},
        .create2_salt = {},
        .code_address = {},
        .code = nullptr,
        .code_size = 0,
    };

    const auto res =
        vm.execute(host, EVMC_CANCUN, msg, kBeaconRootsCode.data(), kBeaconRootsCode.size());
    BOOST_CHECK_MESSAGE(res.status_code == EVMC_REVERT,
        "unknown timestamp must revert per the deployed EIP-4788 bytecode, got status "
            << static_cast<int>(res.status_code));
    BOOST_CHECK_MESSAGE(res.output_size == 0,
        "miss path reverts with EMPTY revert data (revert(0,0)), not 32 zero bytes");
}

BOOST_AUTO_TEST_SUITE_END()
