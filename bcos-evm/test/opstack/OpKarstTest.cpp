// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Execution-level Karst vectors: what actually changes when the block timestamp crosses
// karst_time. Karst's EVM base is Osaka, so every assertion below is a Karst/Jovian
// A/B on one of the four Osaka rules the OP lane reaches — EIP-7825 (per-tx gas cap, deposits
// exempt), EIP-7939 (CLZ), EIP-7883 (MODEXP repricing) and EIP-7951 (P256VERIFY at 6900).
//
// Kept in its own TU rather than appended to OpValidateTest / OpHostTest / OpDepositTest so the
// fork A/B reads as one story and so it does not collide with the in-flight rewrite of those
// files.
#include "OpTestReceiptFactory.h"
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpHost.h>
#include <bcos-evm/opstack/OpPrecompiles.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/errors.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <span>
#include <test/utils/test_state.hpp>
#include <vector>

using namespace bcos::evm::opstack;
using namespace bcos::evm::opstack::testutil;  // kOpTestReceiptFactory
using namespace evmone;
using namespace evmc::literals;
using intx::operator""_u256;

namespace
{
constexpr auto kKarstSender = 0x00000000000000000000000000000000000000ab_address;
constexpr auto kKarstCallee = 0x0000000000000000000000000000000000001234_address;
constexpr int64_t kKarstBlockGas = 30'000'000;
/// EIP-7825 MAX_TX_GAS_LIMIT (state.cpp:385 against transaction.hpp:17).
constexpr int64_t kMaxTxGasLimit = 0x1000000;  // 2**24 = 16'777'216

// Both fork arms come from ONE genesis schedule and differ only in the block timestamp, which
// is how a real chain crosses the fork: Jovian from second 1000, Karst from second 2000.
constexpr uint64_t kJovianTime = 1000;
constexpr uint64_t kKarstTime = 2000;
const bcos::ledger::OpForkSchedule kSchedule{.m_jovianTime = kJovianTime,
    .m_karstTime = kKarstTime};

/// The config a block one second BEFORE karst_time runs under.
const OpForkConfig& underJovian()
{
    return configAt(kSchedule, kKarstTime - 1);
}
/// The config a block AT karst_time runs under.
const OpForkConfig& underKarst()
{
    return configAt(kSchedule, kKarstTime);
}

state::BlockInfo karstBlock()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = kKarstBlockGas;
    b.base_fee = 7;
    b.coinbase = 0x4200000000000000000000000000000000000011_address;
    return b;
}

state::Transaction karstTx(int64_t gasLimit)
{
    state::Transaction tx;
    tx.type = state::Transaction::Type::eip1559;
    tx.sender = kKarstSender;
    tx.to = kKarstCallee;
    tx.gas_limit = gasLimit;
    tx.max_gas_price = 7;
    tx.max_priority_gas_price = 7;
    tx.nonce = 0;
    return tx;
}

/// A funded sender plus the one-byte envelope opValidate requires (it only rejects the empty
/// one; the byte itself is never decoded here).
test::TestState fundedState()
{
    test::TestState ts;
    ts[kKarstSender] = {
        .nonce = 0, .balance = 1000000000000000000000_u256, .storage = {}, .code = {}};
    return ts;
}

bool validateAccepts(const OpForkConfig& cfg, int64_t gasLimit)
{
    auto ts = fundedState();
    const std::vector<uint8_t> env{0x02};
    const auto r = opValidate(ts, karstBlock(), karstTx(gasLimit), {env.data(), env.size()}, cfg,
        OpFeeParams{}, kKarstBlockGas);
    return std::holds_alternative<OpTxProperties>(r);
}

DepositTx karstDeposit(int64_t gasLimit)
{
    return DepositTx{.source_hash = 0x01_bytes32,
        .from = kKarstSender,
        .to = kKarstSender,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = gasLimit,
        .is_system_tx = false,
        .data = {}};
}

/// Runs one deposit and returns the FISCO receipt status (0 == success).
int32_t runKarstDeposit(const OpForkConfig& cfg, int64_t gasLimit)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kKarstSender] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;
    evmone::state::StateDiff diff;
    const auto r = runDeposit(ts, karstBlock(), hashes, karstDeposit(gasLimit), cfg, vm, 1234,
        kKarstBlockGas, kOpTestReceiptFactory, diff);
    return r->status();
}

/// Calls kKarstCallee (whose code the caller seeded into `st`) through OpHost under `cfg`.
evmc::Result callCode(evmc::VM& vm, state::State& st, const OpForkConfig& cfg)
{
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kKarstSender;
    const auto block = karstBlock();
    OpHost host{cfg.rev, vm, st, block, hashes, tx, 1234, cfg.precompiles};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = kKarstCallee;
    msg.code_address = kKarstCallee;
    msg.sender = kKarstSender;
    msg.gas = 100000;
    return host.call(msg);
}

/// Calls precompile `addr` with `input` under `cfg` and returns the gas actually charged.
int64_t precompileGas(const OpForkConfig& cfg, const evmc::address& addr,
    std::span<const uint8_t> input, int64_t gas = 1'000'000)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    // Host::prepare_message(depth == 0) reads the sender account, so it must exist.
    ts[kKarstSender] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    state::State st{ts};
    test::TestBlockHashes hashes;
    state::Transaction tx;
    tx.sender = kKarstSender;
    const auto block = karstBlock();
    OpHost host{cfg.rev, vm, st, block, hashes, tx, 1234, cfg.precompiles};

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.recipient = addr;
    msg.code_address = addr;
    msg.sender = kKarstSender;
    msg.gas = gas;
    msg.input_data = input.data();
    msg.input_size = input.size();
    const auto r = host.call(msg);
    BOOST_REQUIRE_EQUAL(r.status_code, EVMC_SUCCESS);
    return gas - r.gas_left;
}

/// MODEXP input with base_len = exp_len = mod_len = 32 and exponent 2**255, so the adjusted
/// exponent length is 255 and max_len is 32 under both revisions. The only thing that moves is
/// the multiplication-complexity formula and the final divisor.
std::vector<uint8_t> modexpInput()
{
    std::vector<uint8_t> in(6 * 32, 0x00);
    in[31] = 32;          // base_len
    in[63] = 32;          // exp_len
    in[95] = 32;          // mod_len
    in[96 + 31] = 0x03;   // base = 3
    in[128] = 0x80;       // exp  = 2**255 (top byte set -> 256-bit exponent)
    in[160 + 31] = 0x05;  // mod  = 5
    return in;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpKarstSuite)

// EIP-7825 caps a NORMAL transaction at 2**24 gas from Osaka on. Jovian (Prague) has no such
// cap, so the same transaction is accepted there — this pair is the whole observable difference
// for ordinary traffic.
BOOST_AUTO_TEST_CASE(KarstEnforcesTxGasCapJovianDoesNot)
{
    BOOST_CHECK(validateAccepts(underKarst(), kMaxTxGasLimit));
    BOOST_CHECK(!validateAccepts(underKarst(), kMaxTxGasLimit + 1));

    BOOST_CHECK(validateAccepts(underJovian(), kMaxTxGasLimit));
    BOOST_CHECK(validateAccepts(underJovian(), kMaxTxGasLimit + 1));
}

// The rejection is the 7825 cap itself, not a knock-on funding/gas-pool failure: the same
// gas limit passes under Jovian with the identical state, block and envelope.
BOOST_AUTO_TEST_CASE(KarstTxGasCapErrorIsTheOsakaOne)
{
    auto ts = fundedState();
    const std::vector<uint8_t> env{0x02};
    const auto r = opValidate(ts, karstBlock(), karstTx(kMaxTxGasLimit + 1),
        {env.data(), env.size()}, underKarst(), OpFeeParams{}, kKarstBlockGas);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(r));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(r),
        evmone::state::make_error_code(evmone::state::MAX_GAS_LIMIT_EXCEEDED));
}

// Deposits are exempt from EIP-7825 (docs.optimism.io/notices/upgrade-19: deposits are already
// capped at 20M gas total per L1 block, and rejecting on L2 a deposit that L1 accepted would burn
// the minted ETH). Every vector here sits above 2**24 — the same gas limit the normal-tx case
// above rejects — and must still execute. Without runDeposit's validate-revision clamp they would
// come back as MAX_GAS_LIMIT_EXCEEDED failure receipts.
//
// There is deliberately NO EL-side 20,000,000 cutoff: 20,000,001 executes too, bounded only by
// blockGasLeft. The 20M figure is the L1 OptimismPortal's per-L1-block budget, not an L2
// execution rule.
BOOST_AUTO_TEST_CASE(KarstDepositsAreExemptFromTxGasCap)
{
    BOOST_CHECK_EQUAL(runKarstDeposit(underKarst(), kMaxTxGasLimit + 1), 0);
    BOOST_CHECK_EQUAL(runKarstDeposit(underKarst(), 20'000'000), 0);
    BOOST_CHECK_EQUAL(runKarstDeposit(underKarst(), 20'000'001), 0);
    // Unchanged under Jovian, where the cap never applied.
    BOOST_CHECK_EQUAL(runKarstDeposit(underJovian(), 20'000'000), 0);
}

// EIP-7939: CLZ (0x1e, evmone instructions_opcodes.hpp:44, gated at EVMC_OSAKA in
// instructions_traits.hpp:258) is a live opcode under Karst and an undefined one under Jovian.
// The program is PUSH1 1, CLZ, PUSH1 0, MSTORE, PUSH1 32, PUSH1 0, RETURN — CLZ(1) is 255.
BOOST_AUTO_TEST_CASE(ClzExecutesUnderKarstAndIsUndefinedUnderJovian)
{
    const auto code = evmc::from_hex("60011e60005260206000f3").value();

    for (const auto* cfg : {&underKarst(), &underJovian()})
    {
        auto vm = evmc::VM{evmc_create_evmone()};
        test::TestState ts;
        ts[kKarstSender] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
        ts[kKarstCallee] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = code};
        state::State st{ts};
        const auto r = callCode(vm, st, *cfg);

        if (cfg->rev >= EVMC_OSAKA)
        {
            BOOST_REQUIRE_EQUAL(r.status_code, EVMC_SUCCESS);
            BOOST_REQUIRE_EQUAL(r.output_size, 32u);
            BOOST_CHECK_EQUAL(
                intx::be::unsafe::load<intx::uint256>(r.output_data), intx::uint256{255});
        }
        else
        {
            BOOST_CHECK_EQUAL(r.status_code, EVMC_UNDEFINED_INSTRUCTION);
        }
    }
}

// EIP-7883/7823 reprice MODEXP at Osaka. For the fixed input above (max_len 32, adjusted
// exponent length 255): Prague charges num_words**2 * 255 / 3 = 16 * 255 / 3 = 1360, Osaka
// charges mult_complexity_eip7883(32) * 255 / 1 = 16 * 255 = 4080.
BOOST_AUTO_TEST_CASE(ModexpGasDiffersBetweenKarstAndJovian)
{
    constexpr auto kModexp = 0x0000000000000000000000000000000000000005_address;
    const auto input = modexpInput();

    BOOST_CHECK_EQUAL(precompileGas(underKarst(), kModexp, input), 4080);
    BOOST_CHECK_EQUAL(precompileGas(underJovian(), kModexp, input), 1360);
}

// EIP-7951: Karst drops the RIP-7212 gas override for 0x100, so the call falls through to the
// vendored Osaka-gated p256verify at 6900 gas (precompiles.cpp:294/807). Jovian still charges
// op-geth's P256VerifyGasFjord 3450 from its override table.
BOOST_AUTO_TEST_CASE(P256VerifyCosts6900UnderKarstAnd3450UnderJovian)
{
    const std::vector<uint8_t> empty;
    BOOST_CHECK_EQUAL(precompileGas(underKarst(), kP256VerifyAddress, empty), 6900);
    BOOST_CHECK_EQUAL(precompileGas(underJovian(), kP256VerifyAddress, empty), 3450);
}

// bn256Pairing's input limit tightens to 81984 -> 57600 under Karst.
//
// Both sizes are whole multiples of the 192-byte pair: ecpairing_execute
// (bcos-evm/bcos-evm/eth/state/precompiles.cpp:546) rejects a ragged length outright, so a
// 57601-byte probe would fail under EVERY table and could not tell the Karst cap from Jovian's.
// 57792 = 301 * 192 is the smallest legal length above the Karst cap; the Jovian control below
// pins that the rejection comes from the cap and nothing else. All-zero pairs are points at
// infinity, so the pairing itself succeeds whenever the length is admitted.
// 57600 (= 300 * 192) being accepted pins OpHost::call's `>` against a `>=` mutation.
BOOST_AUTO_TEST_CASE(KarstBn256PairingInputLimitBoundary)
{
    constexpr auto kBn256 = 0x0000000000000000000000000000000000000008_address;
    // 301 pairs cost 45000 + 301 * 34000 = 10,279,000 (ecpairing_analyze, Istanbul pricing),
    // comfortably inside this budget, so an out-of-gas cannot masquerade as a cap rejection.
    constexpr int64_t kPairingGas = 15'000'000;

    auto callUnder = [&](const OpForkConfig& cfg, size_t size) {
        auto vm = evmc::VM{evmc_create_evmone()};
        test::TestState ts;
        ts[kKarstSender] = {.nonce = 0, .balance = intx::uint256{0}, .storage = {}, .code = {}};
        state::State st{ts};
        test::TestBlockHashes hashes;
        state::Transaction tx;
        tx.sender = kKarstSender;
        const auto block = karstBlock();
        OpHost host{cfg.rev, vm, st, block, hashes, tx, 1234, cfg.precompiles};

        std::vector<uint8_t> input(size, 0x00);
        evmc_message msg{};
        msg.kind = EVMC_CALL;
        msg.recipient = kBn256;
        msg.code_address = kBn256;
        msg.sender = kKarstSender;
        msg.gas = kPairingGas;
        msg.input_data = input.data();
        msg.input_size = input.size();
        return host.call(msg).status_code;
    };

    BOOST_CHECK_EQUAL(callUnder(underKarst(), 57600), EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(callUnder(underKarst(), 57792), EVMC_FAILURE);
    // Control: the identical input is admitted under Jovian (57792 <= 81984), so the Karst
    // rejection above is the tightened cap, not the length or the pairing.
    BOOST_CHECK_EQUAL(callUnder(underJovian(), 57792), EVMC_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()
