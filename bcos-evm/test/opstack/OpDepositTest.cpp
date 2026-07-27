#include <bcos-evm/adapter/StateDiffWriteback.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <evmone/evmone.h>
#include <gtest/gtest.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/host.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kFrom = 0x00000000000000000000000000000000000000cc_address;

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    return b;
}
}  // namespace

TEST(OpDeposit, SuccessMintsAndAdvancesNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.type, kDepositTxType);
    EXPECT_EQ(r.deposit_nonce, 5u);
    EXPECT_EQ(r.deposit_receipt_version, 1u);
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{100});
    EXPECT_EQ(ts.count(OP_L1_FEE_VAULT), 0u);
}

TEST(OpDeposit, EvmRevertKeepsMintAndChargesActualGas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kRevert = 0x00000000000000000000000000000000000000dd_address;
    ts[kRevert] = {
        .nonce = 0, .balance = intx::uint256{0}, .code = evmc::from_hex("60006000fd").value()};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kRevert,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_REVERT);
    EXPECT_LT(r.receipt.gas_used, 100000);
    EXPECT_GE(r.receipt.gas_used, 21000);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{100});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

TEST(OpDeposit, EntryFailureChargesFullGasLimitButKeepsMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{50},
        .value = intx::uint256{0},
        .gas_limit = 20999,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);

    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 20999);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{50});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

TEST(OpDeposit, ContractCreationDerivesAddressFromPreExecutionNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;

    // PUSH1 0x00 PUSH1 0x00 RETURN：部署空 runtime code，仅用于验证 CREATE 地址派生的 nonce。
    const auto initCode = evmc::from_hex("60006000f3").value();
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = std::nullopt,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = initCode};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);

    // 地址须由「执行前」nonce（5）派生，而非 host.call 内部已 bump 过的 6。
    const auto expectedAddr = evmone::state::compute_create_address(kFrom, 5);

    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.deposit_nonce, 5u);
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
    ASSERT_EQ(ts.count(expectedAddr), 1u);
    EXPECT_EQ(ts.at(expectedAddr).nonce, 1u);
}

TEST(OpDeposit, SystemTxIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = true,
        .data = {}};
    EXPECT_THROW(runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000),
        std::runtime_error);
}

// D-03：SSTORE 清零 refund 从 deposit gasUsed 扣除（op-geth Regolith+ 无条件 calcRefund）
// intrinsic 21000 + PUSH1(3)+PUSH1(3)+SSTORE(2100冷+2900重置=5000) = 26006；
// refund = min(4800, 26006/5=5201) = 4800 → 21206；floor 21000 不抬。
TEST(OpDeposit, RefundLowersDepositGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kClear = 0x00000000000000000000000000000000000000ee_address;
    ts[kClear] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x00_bytes32, 0x01_bytes32}},
        .code = evmc::from_hex("600060005500").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kClear,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21206);
}

// D-03 反作弊（红队 F-4）：refund 受 EIP-3529 /5 上限约束。
// 4 槽清零：pre-refund = 21000 + 4*(3+3+5000) = 41024；cap = 41024/5 = 8204 → 32820。
// /2 或无上限作弊 → 21824，当场暴露。/5 结构性入断言。
TEST(OpDeposit, RefundIsCappedAtOneFifthOfGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kClear4 = 0x00000000000000000000000000000000000000e4_address;
    ts[kClear4] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {{0x00_bytes32, 0x01_bytes32}, {0x01_bytes32, 0x01_bytes32},
            {0x02_bytes32, 0x01_bytes32}, {0x03_bytes32, 0x01_bytes32}},
        .code = evmc::from_hex("600060005560006001556000600255600060035500").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kClear4,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    constexpr int64_t kPreRefund = 41024;
    EXPECT_EQ(r.receipt.gas_used, kPreRefund - kPreRefund / 5);
}

// D-07：有日志的 deposit receipt 携带非零 bloom
TEST(OpDeposit, DepositReceiptCarriesLogsBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kLogger = 0x00000000000000000000000000000000000000ef_address;
    ts[kLogger] = {
        .nonce = 1, .balance = intx::uint256{0}, .code = evmc::from_hex("60006000a000").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kLogger,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    ASSERT_EQ(r.receipt.logs.size(), 1u);
    const auto expected = evmone::state::compute_bloom_filter(r.receipt.logs);
    EXPECT_TRUE(evmc::bytes_view{r.receipt.logs_bloom_filter} == evmc::bytes_view{expected});
    EXPECT_FALSE(evmc::bytes_view{r.receipt.logs_bloom_filter} ==
                 evmc::bytes_view{evmone::state::BloomFilter{}});
}

// D-07 反向（红队 F-5）：LOG 后 REVERT——logs 必须空、bloom 必须全零
TEST(OpDeposit, RevertedDepositHasEmptyLogsAndZeroBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kLogRevert = 0x00000000000000000000000000000000000000e5_address;
    ts[kLogRevert] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("60006000a060006000fd").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kLogRevert,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_REVERT);
    EXPECT_TRUE(r.receipt.logs.empty());
    EXPECT_TRUE(evmc::bytes_view{r.receipt.logs_bloom_filter} ==
                evmc::bytes_view{evmone::state::BloomFilter{}});
}

// D-08：deposit 调用 7702 委托 EOA 执行委托目标代码（storage 落在 EOA 上下文）
TEST(OpDeposit, DepositResolvesEip7702Delegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kImpl = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ab_address;
    ts[kImpl] = {
        .nonce = 1, .balance = intx::uint256{0}, .code = evmc::from_hex("600160005500").value()};
    ts[kEoa] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmone::state::bytes{kImpl.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kEoa,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kEoa).storage.at(0x00_bytes32), 0x01_bytes32);
}

// D-08 反作弊（红队 F-7）：委托指向 0x100——必须带 EVMC_DELEGATED 走空码回退，gas=21000；
// 未设旗的作弊实现派发 P256 override → 24450。
TEST(OpDeposit, DelegationToPrecompileFallsBackToEmptyCode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ac_address;
    ts[kEoa] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("ef0100").value() + evmone::state::bytes{k100.bytes, 20}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kEoa,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21000);
}

// D-09：sender 预热——BALANCE(ORIGIN) 收 warm 100（修复前 cold 2600 → 23604）
TEST(OpDeposit, DepositWarmsSenderPerEip2929)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000ba_address;
    ts[kProbe] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("32315000").value()};  // ORIGIN BALANCE POP STOP
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kProbe,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21104);
}

// D-09 补强（红队 F-2）：EIP-3651 coinbase 预热——BALANCE(COINBASE) 同价 21104
TEST(OpDeposit, DepositWarmsCoinbasePerEip3651)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000bc_address;
    ts[kProbe] = {.nonce = 1,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("41315000").value()};  // COINBASE BALANCE POP STOP
    test::TestBlockHashes hashes;
    auto b = blk();
    b.coinbase = 0x00000000000000000000000000000000000000c1_address;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kProbe,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, b, hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21104);
}

// 差分锚定（红队 F-2，「断言数值纪律」的锚）：同形探针（PUSH20 目标 BALANCE POP STOP），
// 仅目标不同：sender（必暖）vs 表外冷地址。Δ = 2600-100 = 2500（EIP-2929 常数）。
// sender 未预热 → Δ=0；全体乱暖 → Δ=0；均被抓。
TEST(OpDeposit, WarmColdDifferentialIs2500)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    constexpr auto kCold = 0x00000000000000000000000000000000000000fe_address;
    const auto probeCode = [](const evmc::address& target) {
        return evmc::from_hex("73").value() + evmone::state::bytes{target.bytes, 20} +
               evmc::from_hex("315000").value();
    };
    const auto run = [&](const evmc::address& target) {
        test::TestState ts;
        ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
        constexpr auto kProbe = 0x00000000000000000000000000000000000000be_address;
        ts[kProbe] = {.nonce = 1, .balance = intx::uint256{0}, .code = probeCode(target)};
        test::TestBlockHashes hashes;
        DepositTx dep{.source_hash = 0x01_bytes32,
            .from = kFrom,
            .to = kProbe,
            .mint = std::nullopt,
            .value = intx::uint256{0},
            .gas_limit = 100000,
            .is_system_tx = false,
            .data = {}};
        const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
        EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
        return r.receipt.gas_used;
    };
    EXPECT_EQ(run(kCold) - run(kFrom), 2500);
}

// D-01：标准 L1→L2 桥接——from 余额 0，mint 供资再转给收款人，必须成功
TEST(OpDeposit, BridgeDepositSpendsMintedValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = intx::uint256{100},
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{40});
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

// D-01 反作弊（红队 F-1）：value 由既有余额供资（mint=nullopt）——
// 可支付性对象必须是铸币后余额，不是 mint 本身
TEST(OpDeposit, ValueFundedByPreexistingBalanceWithoutMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{100}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f2_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = std::nullopt,
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{40});
}

// D-01 反作弊（红队 F-1）：余额+mint 联合供资（value > mint 但 ≤ 铸币后余额）
TEST(OpDeposit, ValueFundedJointlyByBalanceAndMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{50}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f3_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = intx::uint256{20},
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(ts.at(kTo).balance, intx::uint256{60});
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{10});
}

// D-02：带（非委托）code 的 sender 发 deposit——op-geth 对 deposit 跳过 EOA 检查。
// 注意不得用 ef0100 委托码：evmone state.cpp:496 对委托码本来就豁免，测不到面具。
TEST(OpDeposit, SenderWithCodeIsAllowed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 3,
        .balance = intx::uint256{0},
        .code = evmc::from_hex("00").value()};  // 任意非委托字节码（STOP）
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{10},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
}

// D-01 边界（rev.2 更正）：value 超铸币后余额 = op-geth 共识层错误
// （state_transition.go:578 clause 6）→ 失败 receipt 收满 gasLimit（:498），非 intrinsic。
TEST(OpDeposit, ValueOverPostMintBalanceFailsWithFullGasLimit)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    constexpr auto kTo = 0x00000000000000000000000000000000000000f1_address;
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kTo,
        .mint = intx::uint256{5},
        .value = intx::uint256{60},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 100000);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{5});  // mint 保留，value 未动
    EXPECT_EQ(ts.at(kFrom).nonce, 1u);
}

// D-04：deposit gasLimit 超块剩余 gas = 块级错误（op-geth 豁免名单恰两个：
// ErrSystemTxNotSupported 与 ErrGasLimitReached，state_transition.go:486）
TEST(OpDeposit, GasLimitOverBlockBudgetIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 60000,
        .is_system_tx = false,
        .data = {}};
    EXPECT_THROW(
        runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, /*blockGasLeft=*/50000),
        std::runtime_error);
}

// D-04 边界（红队 F-6）：恰等于块剩余 gas 必须接受——">=" 作弊在此暴露
TEST(OpDeposit, GasLimitExactlyBlockBudgetIsAccepted)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 60000,
        .is_system_tx = false,
        .data = {}};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234,
        /*blockGasLeft=*/60000);
    EXPECT_EQ(r.receipt.status, EVMC_SUCCESS);
    EXPECT_EQ(r.receipt.gas_used, 21000);
}

// D-04×D-05 交界（红队 F-11）：create 型 deposit intrinsic 失败——
// nonce 仍 +1、mint 保留、不得部署任何合约
TEST(OpDeposit, FailedCreateDepositStillBumpsNonceAndDeploysNothing)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = std::nullopt,
        .mint = intx::uint256{7},
        .value = intx::uint256{0},
        .gas_limit = 21000,  // create intrinsic 53006（21000+32000+data4+initcode2）> 21000 →
                             // INTRINSIC_GAS_TOO_LOW
        .is_system_tx = false,
        .data = evmc::from_hex("00").value()};
    const auto r = runDeposit(ts, blk(), hashes, dep, isthmusConfig(), vm, 1234, 30000000);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    EXPECT_EQ(r.receipt.status, EVMC_FAILURE);
    EXPECT_EQ(r.receipt.gas_used, 21000);  // 处理级失败收 gasLimit（此处恰 21000）
    EXPECT_EQ(ts.at(kFrom).nonce, 6u);
    EXPECT_EQ(ts.at(kFrom).balance, intx::uint256{7});
    EXPECT_EQ(ts.count(evmone::state::compute_create_address(kFrom, 5)), 0u);
}
