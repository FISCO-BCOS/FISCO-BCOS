// Karst deposit gas semantics: deposits are exempt from the EIP-7825 per-tx cap
// (2^24 = 16,777,216) that Karst's EVMC_OSAKA revision enforces for normal
// transactions. Deposit gas is metered on L1 (the OptimismPortal caps deposits at
// 20M gas total per L1 block); the L2 EL performs no deposit gas check of its own,
// bounded only by blockGasLeft. Every vector here sits above 2^24, pinning the
// exemption — without the validate-revision clamp in runDeposit they would come
// back as MAX_GAS_LIMIT_EXCEEDED failure receipts.
#include "StateDiffWriteback.h"
#include "TestPrinters.h"
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <evmone/evmone.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/state.hpp>
#include <test/utils/test_state.hpp>

using namespace bcos::evm::opstack;
using namespace evmone;
using namespace evmc::literals;

namespace
{
constexpr auto kFrom = 0x00000000000000000000000000000000000000cc_address;
constexpr int64_t kBlockGasLeft = 30000000;

state::BlockInfo blk()
{
    state::BlockInfo b;
    b.number = 1;
    b.gas_limit = kBlockGasLeft;
    b.base_fee = 7;
    return b;
}

DepositTx depositWithGasLimit(int64_t gasLimit)
{
    return DepositTx{.source_hash = 0x01_bytes32,
        .from = kFrom,
        .to = kFrom,
        .mint = intx::uint256{100},
        .value = intx::uint256{0},
        .gas_limit = gasLimit,
        .is_system_tx = false,
        .data = {}};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpDepositGasCapSuite)

BOOST_AUTO_TEST_CASE(Above7825CapExecutes)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;

    const auto r = runDeposit(
        ts, blk(), hashes, depositWithGasLimit(19'999'999), karstConfig(), vm, 1234, kBlockGasLeft);
    BOOST_CHECK_EQUAL(r.receipt.status, EVMC_SUCCESS);
    BOOST_CHECK_LT(r.receipt.gas_used, 19'999'999);
}

BOOST_AUTO_TEST_CASE(TwentyMillionGasExecutes)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;

    const auto r = runDeposit(
        ts, blk(), hashes, depositWithGasLimit(20'000'000), karstConfig(), vm, 1234, kBlockGasLeft);
    bcos::evm::applyStateDiffStrict(ts, r.receipt.state_diff);
    BOOST_CHECK_EQUAL(r.receipt.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(ts.at(kFrom).nonce, 6u);
    BOOST_CHECK_EQUAL(ts.at(kFrom).balance, intx::uint256{100});
}

// The EL enforces no 20M cutoff of its own (rejecting a deposit the L1 accepted could
// strand the minted ETH): 20,000,001 still executes under Karst, bounded only by
// blockGasLeft.
BOOST_AUTO_TEST_CASE(AboveTwentyMillionStillExecutesUnderKarst)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    test::TestState ts;
    ts[kFrom] = {.nonce = 5, .balance = intx::uint256{0}, .storage = {}, .code = {}};
    test::TestBlockHashes hashes;

    const auto r = runDeposit(
        ts, blk(), hashes, depositWithGasLimit(20'000'001), karstConfig(), vm, 1234, kBlockGasLeft);
    BOOST_CHECK_EQUAL(r.receipt.status, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_SUITE_END()
