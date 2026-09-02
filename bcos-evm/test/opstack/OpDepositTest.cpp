#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/state/host.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kFrom = 0x00000000000000000000000000000000000000cc_address;

state::BlockInfo blkDeposit()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = 30000000;
    b.base_fee = 7;
    return b;
}

/// bcos::protocol::LogEntry (what the FISCO receipt stores) → evmone Log, for recomputing the
/// expected logsBloom in the bloom round-trip assertion.
inline evmone::state::Log toEvmoneLog(const bcos::protocol::LogEntry& e)
{
    evmone::state::Log log;
    std::memcpy(log.addr.bytes, e.address().data(), sizeof(log.addr.bytes));
    for (const auto& t : e.topics())
    {
        evmc::bytes32 topic{};
        std::memcpy(topic.bytes, t.data(), sizeof(topic.bytes));
        log.topics.push_back(topic);
    }
    log.data.assign(e.data().begin(), e.data().end());
    return log;
}

/// Narrow the FISCO receipt's gasUsed (u256) to the int64 the assertions compare against.
inline int64_t receiptGasUsed(const bcos::protocol::TransactionReceipt& r)
{
    return static_cast<int64_t>(static_cast<uint64_t>(r.gasUsed()));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpDepositSuite)

BOOST_AUTO_TEST_CASE(SuccessMintsAndAdvancesNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);

    BOOST_CHECK_EQUAL(r->status(), 0);
    const auto& meta = r->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_nonce, 5u);
    BOOST_CHECK_EQUAL(*meta->deposit_receipt_version, 1u);
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 6u);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{100});
    BOOST_CHECK_EQUAL(ts.count(OP_L1_FEE_VAULT), 0u);
}

BOOST_AUTO_TEST_CASE(EvmRevertKeepsMintAndChargesActualGas)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kRevert = 0x00000000000000000000000000000000000000dd_address;
    ts[kRevert] = {.nonce = 0,
        .balance = intx::uint256{0},
        .storage = {},
        .code = evmc::from_hex("60006000fd").value()};
    test::TestBlockHashes hashes;

    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kRevert,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);

    BOOST_CHECK_NE(r->status(), 0);
    const auto gasUsed = receiptGasUsed(*r);
    BOOST_CHECK_LT(gasUsed, 100000);
    BOOST_CHECK_GE(gasUsed, 21000);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{100});
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 1u);
}

BOOST_AUTO_TEST_CASE(EntryFailureChargesFullGasLimitButKeepsMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{50},
        .value = intx::uint256{0},
        .gas_limit = 20999,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);

    BOOST_CHECK_EQUAL(r->status(), 1);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 20999);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{50});
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 1u);
}

BOOST_AUTO_TEST_CASE(ContractCreationDerivesAddressFromPreExecutionNonce)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);

    // 地址须由「执行前」nonce（5）派生，而非 host.call 内部已 bump 过的 6。
    const auto expectedAddr = evmone::state::compute_create_address(kFrom, 5);

    BOOST_CHECK_EQUAL(r->status(), 0);
    const auto& meta = r->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_nonce, 5u);
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 6u);
    BOOST_REQUIRE_EQUAL(ts.count(expectedAddr), 1u);
    BOOST_CHECK_EQUAL(ts.at(expectedAddr).nonce, 1u);
    // 回执侧投影：contractAddress 用 preNonce(5) 派生、deposit 的 effectiveGasPrice 为 0x0。
    BOOST_CHECK_EQUAL(r->contractAddress(), evmc::hex(expectedAddr));
    BOOST_CHECK_EQUAL(r->effectiveGasPrice(), "0x0");
}

BOOST_AUTO_TEST_CASE(SystemTxIsBlockError)
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
    evmone::state::StateDiff diff;  // never written: is_system_tx throws before the out-param is
                                    // touched
    BOOST_CHECK_THROW(runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
                          kOpTestReceiptFactory, diff),
        std::runtime_error);
}

// D-03：SSTORE 清零 refund 从 deposit gasUsed 扣除（op-geth Regolith+ 无条件 calcRefund）
// intrinsic 21000 + PUSH1(3)+PUSH1(3)+SSTORE(2100冷+2900重置=5000) = 26006；
// refund = min(4800, 26006/5=5201) = 4800 → 21206；floor 21000 不抬。
BOOST_AUTO_TEST_CASE(RefundLowersDepositGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21206);
}

// D-03 反作弊（红队 F-4）：refund 受 EIP-3529 /5 上限约束。
// 4 槽清零：pre-refund = 21000 + 4*(3+3+5000) = 41024；cap = 41024/5 = 8204 → 32820。
// /2 或无上限作弊 → 21824，当场暴露。/5 结构性入断言。
BOOST_AUTO_TEST_CASE(RefundIsCappedAtOneFifthOfGasUsed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    constexpr int64_t kPreRefund = 41024;
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), kPreRefund - kPreRefund / 5);
}

// D-07：有日志的 deposit receipt 携带非零 bloom
BOOST_AUTO_TEST_CASE(DepositReceiptCarriesLogsBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kLogger = 0x00000000000000000000000000000000000000ef_address;
    ts[kLogger] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
        .code = evmc::from_hex("60006000a000").value()};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kLogger,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_REQUIRE_EQUAL(r->logEntries().size(), 1u);
    // round-trip: recompute the bloom from the FISCO LogEntry back to evmone Logs and compare
    // with the bloom the receipt carries.
    std::vector<evmone::state::Log> logs;
    logs.reserve(r->logEntries().size());
    for (const auto& e : r->logEntries())
        logs.push_back(toEvmoneLog(e));
    const auto expected = evmone::state::compute_bloom_filter(logs);
    const auto bloom = r->logsBloom();
    BOOST_CHECK((evmc::bytes_view{bloom.data(), bloom.size()} == evmc::bytes_view{expected}));
    BOOST_CHECK((!(evmc::bytes_view{bloom.data(), bloom.size()} ==
                   evmc::bytes_view{evmone::state::BloomFilter{}})));
}

// D-07 反向（红队 F-5）：LOG 后 REVERT——logs 必须空、bloom 必须全零
BOOST_AUTO_TEST_CASE(RevertedDepositHasEmptyLogsAndZeroBloom)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kLogRevert = 0x00000000000000000000000000000000000000e5_address;
    ts[kLogRevert] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_NE(r->status(), 0);
    // 钉住"代码确实执行到 LOG 后再 REVERT"而非入口级失败：入口失败收满 gasLimit(100000)，
    // LOG 后 REVERT 的实际消耗远低于此（对照 EvmRevertKeepsMintAndChargesActualGas）。
    BOOST_CHECK_LT(receiptGasUsed(*r), 100000);
    BOOST_CHECK(r->logEntries().empty());
    const auto bloom = r->logsBloom();
    BOOST_CHECK((evmc::bytes_view{bloom.data(), bloom.size()} ==
                 evmc::bytes_view{evmone::state::BloomFilter{}}));
}

// D-08：deposit 调用 7702 委托 EOA 执行委托目标代码（storage 落在 EOA 上下文）
BOOST_AUTO_TEST_CASE(DepositResolvesEip7702Delegation)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kImpl = 0x00000000000000000000000000000000000000aa_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ab_address;
    ts[kImpl] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
        .code = evmc::from_hex("600160005500").value()};
    ts[kEoa] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(ts.at(kEoa).storage.at(0x00_bytes32), 0x01_bytes32);
}

// D-08 反作弊（红队 F-7）：委托指向 0x100——必须带 EVMC_DELEGATED 走空码回退，gas=21000；
// 未设旗的作弊实现派发 P256 override → 24450。
BOOST_AUTO_TEST_CASE(DelegationToPrecompileFallsBackToEmptyCode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto k100 = 0x0000000000000000000000000000000000000100_address;
    constexpr auto kEoa = 0x00000000000000000000000000000000000000ac_address;
    ts[kEoa] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21000);
}

// D-09：sender 预热——BALANCE(ORIGIN) 收 warm 100（修复前 cold 2600 → 23604）
BOOST_AUTO_TEST_CASE(DepositWarmsSenderPerEip2929)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000ba_address;
    ts[kProbe] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21104);
}

// D-09 补强（红队 F-2）：EIP-3651 coinbase 预热——BALANCE(COINBASE) 同价 21104
BOOST_AUTO_TEST_CASE(DepositWarmsCoinbasePerEip3651)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    constexpr auto kProbe = 0x00000000000000000000000000000000000000bc_address;
    ts[kProbe] = {.nonce = 1,
        .balance = intx::uint256{0},
        .storage = {},
        .code = evmc::from_hex("41315000").value()};  // COINBASE BALANCE POP STOP
    test::TestBlockHashes hashes;
    auto b = blkDeposit();
    b.coinbase = 0x00000000000000000000000000000000000000c1_address;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kProbe,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(
        ts, b, hashes, dep, isthmusConfig(), vm, 1234, 30000000, kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21104);
}

// 差分锚定（红队 F-2，「断言数值纪律」的锚）：同形探针（PUSH20 目标 BALANCE POP STOP），
// 仅目标不同：sender（必暖）vs 表外冷地址。Δ = 2600-100 = 2500（EIP-2929 常数）。
// sender 未预热 → Δ=0；全体乱暖 → Δ=0；均被抓。
BOOST_AUTO_TEST_CASE(WarmColdDifferentialIs2500)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    constexpr auto kCold = 0x00000000000000000000000000000000000000fe_address;
    const auto probeCode = [](const evmc::address& target) {
        return evmc::from_hex("73").value() + evmone::state::bytes{target.bytes, 20} +
               evmc::from_hex("315000").value();
    };
    const auto run = [&](const evmc::address& target) {
        test::TestState ts;
        ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
        constexpr auto kProbe = 0x00000000000000000000000000000000000000be_address;
        ts[kProbe] = {
            .nonce = 1, .balance = intx::uint256{0}, .storage = {}, .code = probeCode(target)};
        test::TestBlockHashes hashes;
        DepositTx dep{.source_hash = 0x01_bytes32,
            .from = kFrom,
            .to = kProbe,
            .mint = std::nullopt,
            .value = intx::uint256{0},
            .gas_limit = 100000,
            .is_system_tx = false,
            .data = {}};
        evmone::state::StateDiff diff;
        const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234,
            30000000, kOpTestReceiptFactory, diff);
        BOOST_CHECK_EQUAL(r->status(), 0);
        return receiptGasUsed(*r);
    };
    BOOST_CHECK_EQUAL(run(kCold) - run(kFrom), 2500);
}

// D-01：标准 L1→L2 桥接——from 余额 0，mint 供资再转给收款人，必须成功
BOOST_AUTO_TEST_CASE(BridgeDepositSpendsMintedValue)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(ts.at(kTo).balance, intx::uint256{60});
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{40});
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 1u);
}

// D-01 反作弊（红队 F-1）：value 由既有余额供资（mint=nullopt）——
// 可支付性对象必须是铸币后余额，不是 mint 本身
BOOST_AUTO_TEST_CASE(ValueFundedByPreexistingBalanceWithoutMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{100}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(ts.at(kTo).balance, intx::uint256{60});
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{40});
}

// D-01 反作弊（红队 F-1）：余额+mint 联合供资（value > mint 但 ≤ 铸币后余额）
BOOST_AUTO_TEST_CASE(ValueFundedJointlyByBalanceAndMint)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{50}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(ts.at(kTo).balance, intx::uint256{60});
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{10});
}

// D-02：带（非委托）code 的 sender 发 deposit——op-geth 对 deposit 跳过 EOA 检查。
// 注意不得用 ef0100 委托码：evmone state.cpp:496 对委托码本来就豁免，测不到面具。
BOOST_AUTO_TEST_CASE(SenderWithCodeIsAllowed)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 3,
        .balance = intx::uint256{0},
        .storage = {},
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
}

// D-01 边界（rev.2 更正）：value 超铸币后余额 = op-geth 共识层错误
// （state_transition.go:578 clause 6）→ 失败 receipt 收满 gasLimit（:498），非 intrinsic。
BOOST_AUTO_TEST_CASE(ValueOverPostMintBalanceFailsWithFullGasLimit)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 1);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 100000);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{5});  // mint 保留，value 未动
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 1u);
}

// D-04：deposit gasLimit 超块剩余 gas = 块级错误（op-geth 豁免名单恰两个：
// ErrSystemTxNotSupported 与 ErrGasLimitReached，state_transition.go:486）
BOOST_AUTO_TEST_CASE(GasLimitOverBlockBudgetIsBlockError)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 60000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    BOOST_CHECK_THROW(runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234,
                          /*blockGasLeft=*/50000, kOpTestReceiptFactory, diff),
        std::runtime_error);
}

// D-04 边界（红队 F-6）：恰等于块剩余 gas 必须接受——">=" 作弊在此暴露
BOOST_AUTO_TEST_CASE(GasLimitExactlyBlockBudgetIsAccepted)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;
    DepositTx dep{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 60000,
        .is_system_tx = false,
        .data = {}};
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234,
        /*blockGasLeft=*/60000, kOpTestReceiptFactory, diff);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21000);
}

// D-04×D-05 交界（红队 F-11）：create 型 deposit intrinsic 失败——
// nonce 仍 +1、mint 保留、不得部署任何合约
BOOST_AUTO_TEST_CASE(FailedCreateDepositStillBumpsNonceAndDeploysNothing)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
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
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, blkDeposit(), hashes, dep, isthmusConfig(), vm, 1234, 30000000,
        kOpTestReceiptFactory, diff);
    bcos::evm::applyStateDiffStrict(ts, diff);
    BOOST_CHECK_EQUAL(r->status(), 1);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21000);  // 处理级失败收 gasLimit（此处恰 21000）
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 6u);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{7});
    BOOST_CHECK_EQUAL(ts.count(evmone::state::compute_create_address(kFrom, 5)), 0u);
}

BOOST_AUTO_TEST_SUITE_END()
