// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthDepositGasTest — the externally anchored deposit gas-accounting cases of the
// retired bcos-evm OpDepositTest (deleted with the old layer in the OP rewrite),
// re-pinned on the new layer's opRunDeposit (opstack-executor/OpEthDeposit.h). The
// pinned numbers are derived from constants outside this repo: the EIP-3529 refund
// quotient (/5) and SSTORE-clearing refund (4800), the EIP-2929 warm/cold access
// costs (100/2600), the EIP-3651 coinbase warmth, and the standard intrinsic 21000
// — each case's comment carries the derivation, as the originals did. The op-geth
// state_transition.go failure-branch anchors (mint retention + nonce bump on EVM revert /
// entry failure / over-value, the full-gasLimit charge on entry failure and over-value, the
// uint256 mint-add wrap, and the ErrGasLimitReached block-budget boundary) are restored here
// too; the Bedrock/Regolith fork matrix is covered on this layer by OpEthForkMatrixTest and
// is not duplicated.

#include <opstack-executor/OpEthDeposit.h>
#include <opstack-executor/OpForkSpec.h>
#include <ethereum-executor/EthereumHost.h>      // EthCallParams
#include <ethereum-executor/EthereumTransition.h>  // validateTransaction
#include <bcos-framework/protocol/TxGasModel.h>    // MAX_TX_GAS_LIMIT
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>
#include <evmone/evmone.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
using MutableStorage = bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
    bcos::executor_v1::StateValue,
    bcos::storage2::memory_storage::Attribute(bcos::storage2::memory_storage::ORDERED |
                                              bcos::storage2::memory_storage::LOGICAL_DELETION)>;
namespace eth = bcos::executor_v1::eth;
namespace opeth = bcos::executor_v1::opstack;
using evmc::literals::operator""_address;

constexpr evmc::address c_from = 0x00000000000000000000000000000000000000cc_address;
constexpr uint64_t c_chainId = 1234;

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto suite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite);
}

void seedAccount(MutableStorage& storage, evmc::address const& addr, uint64_t nonce,
    bcos::u256 balance, bcos::bytes code,
    std::vector<std::pair<evmc::bytes32, evmc::bytes32>> slots = {})
{
    auto const hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    auto account = eth::ethViewAccount(storage, addr);
    bcos::task::syncWait(account.create());
    // Same existence pattern as OpEthForkMatrixTest's seedAccounts: a non-zero codeHash
    // marks the account as existing; empty code takes the empty-code hash.
    bcos::task::syncWait(account.setCode(
        code, {}, code.empty() ? hashImpl->emptyHash() : hashImpl->hash(bcos::ref(code))));
    bcos::task::syncWait(account.setNonce(std::to_string(nonce)));
    bcos::task::syncWait(account.setBalance(balance));
    for (auto const& [key, value] : slots)
        bcos::task::syncWait(account.setStorage(key, value));
}

eth::EthBlockInfo depositBlock()
{
    eth::EthBlockInfo b{};
    b.number = 1;
    b.gas_limit = 30'000'000;
    b.base_fee = 7;
    return b;
}

opeth::DepositTx plainDeposit(evmc::address to, int64_t gasLimit = 100'000)
{
    opeth::DepositTx dep{};
    dep.sourceHash.bytes[31] = 0x01;
    dep.from = c_from;
    dep.to = to;
    dep.gasLimit = gasLimit;
    return dep;
}

bcos::protocol::TransactionReceipt::Ptr runIsthmusDeposit(MutableStorage& storage,
    opeth::DepositTx const& dep, eth::EthBlockInfo const& block)
{
    eth::EthereumState<MutableStorage> state{storage};
    evmc::VM vm{evmc_create_evmone()};
    return bcos::task::syncWait(opeth::opRunDeposit(state, block, /*blockHashLookup=*/{}, dep,
        opeth::OP_ISTHMUS_SPEC, vm, c_chainId, block.gas_limit, *makeReceiptFactory(),
        block.number));
}

int64_t receiptGasUsed(bcos::protocol::TransactionReceipt const& r)
{
    return static_cast<int64_t>(static_cast<uint64_t>(r.gasUsed()));
}

bcos::u256 viewBalance(MutableStorage& storage, evmc::address const& addr)
{
    auto account = eth::ethViewAccount(storage, addr);
    return bcos::task::syncWait(account.balance());
}

std::string viewNonce(MutableStorage& storage, evmc::address const& addr)
{
    auto account = eth::ethViewAccount(storage, addr);
    return bcos::task::syncWait(account.nonce()).value_or("0");
}

evmc::bytes32 word(uint8_t v)
{
    evmc::bytes32 w{};
    w.bytes[31] = v;
    return w;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthDepositGasSuite)

// SSTORE-to-zero refund deducted from the deposit gasUsed (op-geth Regolith+ unconditional
// calcRefund): intrinsic 21000 + PUSH1(3)+PUSH1(3)+SSTORE(2100 cold + 2900 reset = 5000) =
// 26006; refund = min(4800, 26006/5=5201) = 4800 -> 21206; the EIP-7623 floor (21000) does
// not raise it.
BOOST_AUTO_TEST_CASE(RefundLowersDepositGasUsed)
{
    constexpr auto c_clear = 0x00000000000000000000000000000000000000ee_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_clear, 1, bcos::u256{0}, {0x60, 0x00, 0x60, 0x00, 0x55, 0x00},
        {{word(0), word(1)}});

    auto const r = runIsthmusDeposit(storage, plainDeposit(c_clear), depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21206);
}

// Anti-cheat twin (red-team F-4 in the original): the refund is capped at EIP-3529's /5 of
// the gas used. 4 slot clears: pre-refund = 21000 + 4*(3+3+5000) = 41024; cap = 41024/5 =
// 8204 -> 32820. A /2 or uncapped cheat yields 21824 and is caught; the /5 is structural in
// the assertion.
BOOST_AUTO_TEST_CASE(RefundIsCappedAtOneFifthOfGasUsed)
{
    constexpr auto c_clear4 = 0x00000000000000000000000000000000000000e4_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_clear4, 1, bcos::u256{0},
        {0x60, 0x00, 0x60, 0x00, 0x55, 0x60, 0x00, 0x60, 0x01, 0x55, 0x60, 0x00, 0x60, 0x02,
            0x55, 0x60, 0x00, 0x60, 0x03, 0x55, 0x00},
        {{word(0), word(1)}, {word(1), word(1)}, {word(2), word(1)}, {word(3), word(1)}});

    auto const r = runIsthmusDeposit(storage, plainDeposit(c_clear4), depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    constexpr int64_t c_preRefund = 41024;
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), c_preRefund - c_preRefund / 5);
}

// Sender pre-warming (EIP-2929): BALANCE(ORIGIN) charges the warm 100, not the cold 2600
// (a missing warm-up shows as 23604). Code: ORIGIN BALANCE POP STOP.
BOOST_AUTO_TEST_CASE(DepositWarmsSenderPerEip2929)
{
    constexpr auto c_probe = 0x00000000000000000000000000000000000000ba_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_probe, 1, bcos::u256{0}, {0x32, 0x31, 0x50, 0x00});

    auto const r = runIsthmusDeposit(storage, plainDeposit(c_probe), depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21104);
}

// Coinbase pre-warming (EIP-3651, Shanghai+): BALANCE(COINBASE) is likewise warm — 21104.
BOOST_AUTO_TEST_CASE(DepositWarmsCoinbasePerEip3651)
{
    constexpr auto c_probe = 0x00000000000000000000000000000000000000bc_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_probe, 1, bcos::u256{0}, {0x41, 0x31, 0x50, 0x00});
    auto block = depositBlock();
    block.coinbase = 0x00000000000000000000000000000000000000c1_address;

    auto const r = runIsthmusDeposit(storage, plainDeposit(c_probe), block);
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21104);
}

// Differential anchor (red-team F-2 in the original): same-shape probes (PUSH20 target,
// BALANCE POP STOP) differing only in the target — the always-warm sender vs an off-list
// cold address. Delta = 2600-100 = 2500 (the EIP-2929 constant); an unwarmed sender or an
// everything-warm cheat both collapse the delta to 0 and are caught.
BOOST_AUTO_TEST_CASE(WarmColdDifferentialIs2500)
{
    constexpr auto c_cold = 0x00000000000000000000000000000000000000fe_address;
    constexpr auto c_probe = 0x00000000000000000000000000000000000000be_address;
    auto const probeCode = [](evmc::address const& target) {
        bcos::bytes code{0x73};  // PUSH20
        code.insert(code.end(), target.bytes, target.bytes + sizeof(target.bytes));
        code.insert(code.end(), {0x31, 0x50, 0x00});  // BALANCE POP STOP
        return code;
    };
    auto const run = [&](evmc::address const& target) {
        MutableStorage storage{1};
        seedAccount(storage, c_from, 0, bcos::u256{0}, {});
        seedAccount(storage, c_probe, 1, bcos::u256{0}, probeCode(target));
        auto const r = runIsthmusDeposit(storage, plainDeposit(c_probe), depositBlock());
        BOOST_CHECK_EQUAL(r->status(), 0);
        return receiptGasUsed(*r);
    };
    BOOST_CHECK_EQUAL(run(c_cold) - run(c_from), 2500);
}

// Anti-cheat (red-team F-7 in the original): a 7702 delegation pointing at 0x100 must take
// the EVMC_DELEGATED empty-code fallback — gas 21000. A cheat that dispatches the P256
// override without the flag would report 24450.
BOOST_AUTO_TEST_CASE(DelegationToPrecompileFallsBackToEmptyCode)
{
    constexpr auto c_addr100 = 0x0000000000000000000000000000000000000100_address;
    constexpr auto c_eoa = 0x00000000000000000000000000000000000000ac_address;
    bcos::bytes delegation{0xef, 0x01, 0x00};
    delegation.insert(delegation.end(), c_addr100.bytes, c_addr100.bytes + sizeof(c_addr100.bytes));
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_eoa, 1, bcos::u256{0}, std::move(delegation));

    auto const r = runIsthmusDeposit(storage, plainDeposit(c_eoa), depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 21000);
}

// Contract creation derives the address from the PRE-execution nonce (5), not the
// host-bumped 6 — the derived address is pinned by value (keccak256(rlp([from, 5]))[12:],
// the same derivation op-geth's crypto.CreateAddress performs).
BOOST_AUTO_TEST_CASE(ContractCreationDerivesAddressFromPreExecutionNonce)
{
    MutableStorage storage{1};
    seedAccount(storage, c_from, 5, bcos::u256{0}, {});
    auto dep = plainDeposit(c_from);
    dep.to = std::nullopt;
    dep.data = {0x60, 0x00, 0x60, 0x00, 0xf3};  // PUSH1 0 PUSH1 0 RETURN: empty runtime

    auto const r = runIsthmusDeposit(storage, dep, depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    // keccak256(rlp([0x00..00cc, 5]))[12:] — independently derived, the same derivation
    // op-geth's crypto.CreateAddress performs over the pre-execution nonce. The receipt's
    // contractAddress is unprefixed hex (evmc::hex convention, as the original pinned it).
    BOOST_CHECK_EQUAL(std::string{r->contractAddress()},
        "3877807392e15d8472ad508195a5541f0f9950c2");
    auto const& meta = r->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->deposit_nonce.has_value());
    BOOST_CHECK_EQUAL(*meta->deposit_nonce, 5u);
    BOOST_CHECK_EQUAL(std::string{r->effectiveGasPrice()}, "0x0");
}

// op-geth state_transition.go execute failure branch: an EVM-level REVERT retains the mint and
// charges the ACTUAL gas (the Regolith+ contrast to Bedrock's full-gasLimit reporting, covered
// by OpEthForkMatrixTest's BedrockDepositSemantics). Gas bounds are the anchors the original
// pinned: >= intrinsic 21000, < gasLimit.
BOOST_AUTO_TEST_CASE(EvmRevertKeepsMintAndChargesActualGas)
{
    constexpr auto c_revert = 0x00000000000000000000000000000000000000dd_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    seedAccount(storage, c_revert, 0, bcos::u256{0}, {0x60, 0x00, 0x60, 0x00, 0xfd});

    auto dep = plainDeposit(c_revert);
    dep.mint = bcos::u256{100};
    auto const r = runIsthmusDeposit(storage, dep, depositBlock());
    BOOST_CHECK_NE(r->status(), 0);
    BOOST_CHECK_GE(receiptGasUsed(*r), 21000);
    BOOST_CHECK_LT(receiptGasUsed(*r), 100000);
    BOOST_CHECK_EQUAL(viewBalance(storage, c_from), bcos::u256{100});
    BOOST_CHECK_EQUAL(viewNonce(storage, c_from), "1");
}

// Entry failure (gasLimit 20999 below the 21000 intrinsic) charges the FULL gasLimit and still
// retains the mint / bumps the nonce (op-geth execute()'s pre-check failure path).
BOOST_AUTO_TEST_CASE(EntryFailureChargesFullGasLimitButKeepsMint)
{
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});

    auto dep = plainDeposit(c_from, /*gasLimit=*/20999);
    dep.mint = bcos::u256{50};
    auto const r = runIsthmusDeposit(storage, dep, depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 1);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 20999);
    BOOST_CHECK_EQUAL(viewBalance(storage, c_from), bcos::u256{50});
    BOOST_CHECK_EQUAL(viewNonce(storage, c_from), "1");
}

// value above the post-mint balance fails the transfer, charges the full gasLimit, and keeps
// the mint while the value stays put (op-geth innerExecute's insufficient-funds branch).
BOOST_AUTO_TEST_CASE(ValueOverPostMintBalanceFailsWithFullGasLimit)
{
    constexpr auto c_to = 0x00000000000000000000000000000000000000f1_address;
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});

    auto dep = plainDeposit(c_to);
    dep.mint = bcos::u256{5};
    dep.value = bcos::u256{60};
    auto const r = runIsthmusDeposit(storage, dep, depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 1);
    BOOST_CHECK_EQUAL(receiptGasUsed(*r), 100000);
    BOOST_CHECK_EQUAL(viewBalance(storage, c_from), bcos::u256{5});
    BOOST_CHECK_EQUAL(viewBalance(storage, c_to), bcos::u256{0});
    BOOST_CHECK_EQUAL(viewNonce(storage, c_from), "1");
}

// The mint addition wraps mod 2^256 like op-geth's uint256.Add: (2^256-1) + 2 = 1.
BOOST_AUTO_TEST_CASE(MintAdditionWrapsLikeOpGethUint256Add)
{
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, ~bcos::u256{0}, {});

    auto dep = plainDeposit(c_from);
    dep.mint = bcos::u256{2};
    auto const r = runIsthmusDeposit(storage, dep, depositBlock());
    BOOST_CHECK_EQUAL(r->status(), 0);
    BOOST_CHECK_EQUAL(viewBalance(storage, c_from), bcos::u256{1});
    BOOST_CHECK_EQUAL(viewNonce(storage, c_from), "1");
}

// Regolith+ rejects is_system_tx outright (op-geth ErrSystemTxNotSupported — the other of
// the two deposit preCheck block errors); pre-Regolith it is the unmetered path covered by
// OpEthForkMatrixTest's BedrockDepositSemantics.
BOOST_AUTO_TEST_CASE(SystemTxIsBlockErrorSinceRegolith)
{
    MutableStorage storage{1};
    seedAccount(storage, c_from, 0, bcos::u256{0}, {});
    auto dep = plainDeposit(c_from);
    dep.isSystemTx = true;
    BOOST_CHECK_EXCEPTION(runIsthmusDeposit(storage, dep, depositBlock()), std::runtime_error,
        [](std::runtime_error const& e) {
            return std::string{e.what()}.find("is_system_tx") != std::string::npos;
        });
}

// Deposit gasLimit over the remaining block gas is a block-level error (op-geth
// ErrGasLimitReached — one of exactly two deposit preCheck block errors); exactly equal is
// accepted (a ">=" comparison cheat fails this boundary).
BOOST_AUTO_TEST_CASE(BlockBudgetBoundary){
    {
        MutableStorage storage{1};
        seedAccount(storage, c_from, 0, bcos::u256{0}, {});
        auto block = depositBlock();
        block.gas_limit = 50000;
        BOOST_CHECK_THROW(runIsthmusDeposit(storage, plainDeposit(c_from, 60000), block),
            opeth::OpEthDepositGasLimitReached);
    }
    {
        MutableStorage storage{1};
        seedAccount(storage, c_from, 0, bcos::u256{0}, {});
        auto block = depositBlock();
        block.gas_limit = 60000;
        auto const r = runIsthmusDeposit(storage, plainDeposit(c_from, 60000), block);
        BOOST_CHECK_EQUAL(r->status(), 0);
    }
}


// R11: Karst's EIP-7825 per-tx cap (2^24) binds NORMAL transactions at Osaka; deposits are
// exempt (opRunDeposit clamps the intrinsic check's revision to Prague — OpEthDeposit.h
// revValidate). Pin both sides executably: a deposit over the cap is admitted under
// OP_KARST_SPEC; a normal tx at the same gas rejects under validateTransaction at Osaka,
// and the same tx passes at Prague (the cap is the only difference).
BOOST_AUTO_TEST_CASE(KarstDepositAbove7825CapAdmittedButNormalTxRejected)
{
    using bcos::protocol::MAX_TX_GAS_LIMIT;
    MutableStorage storage;
    seedAccount(storage, c_from, /*nonce=*/0, bcos::u256("1000000000000000000"), {});
    auto block = depositBlock();

    constexpr auto c_to = 0x00000000000000000000000000000000000000dd_address;
    auto dep = plainDeposit(c_to, MAX_TX_GAS_LIMIT + 1);
    eth::EthereumState<MutableStorage> state{storage};
    evmc::VM vm{evmc_create_evmone()};
    // The deposit is admitted under Karst — the exemption clamp keeps the intrinsic check
    // at Prague (no cap on deposits).
    BOOST_CHECK_NO_THROW((void)bcos::task::syncWait(opeth::opRunDeposit(state, block,
        /*blockHashLookup=*/{}, dep, opeth::OP_KARST_SPEC, vm, c_chainId, block.gas_limit,
        *makeReceiptFactory(), block.number)));

    // Control: a normal tx at the same gas under Osaka rejects via the cap.
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    auto txFactory = std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    // Version 2 (EIP-1559): the legacy (V0) factory overload drops gasLimit entirely —
    // only the typed path carries it (TransactionFactoryImpl.cpp's V0 short-circuit).
    auto tx = txFactory->createTransaction(2, "0x00000000000000000000000000000000000000bb",
        bcos::bytes{0x0a}, "0x0", 100000, "0x2105", "1", 7, /*_abi=*/{}, /*_value=*/{},
        /*_gasPrice=*/{}, MAX_TX_GAS_LIMIT + 1, /*_maxFeePerGas=*/"0x3e8",
        /*_maxPriorityFeePerGas=*/"0x1");
    eth::EthBlockInfo blk{};
    blk.number = 1;
    blk.gas_limit = 30'000'000;
    blk.base_fee = 1;
    eth::EthCallParams callParams{};
    auto verdict = eth::validateTransaction(state, blk, *tx, EVMC_OSAKA, blk.gas_limit,
        blk.gas_limit, callParams);
    BOOST_REQUIRE(std::holds_alternative<std::error_code>(verdict));
    BOOST_CHECK_EQUAL(std::get<std::error_code>(verdict).value(),
        static_cast<int>(eth::evm::ErrorCode::MAX_GAS_LIMIT_EXCEEDED));
    // Prague has no cap — the same tx passes the cap check (it may fail a LATER rule; the
    // point is the cap does not fire).
    auto verdictPrague = eth::validateTransaction(state, blk, *tx, EVMC_PRAGUE, blk.gas_limit,
        blk.gas_limit, callParams);
    if (std::holds_alternative<std::error_code>(verdictPrague))
    {
        BOOST_CHECK_NE(std::get<std::error_code>(verdictPrague).value(),
            static_cast<int>(eth::evm::ErrorCode::MAX_GAS_LIMIT_EXCEEDED));
    }
}

BOOST_AUTO_TEST_SUITE_END()
