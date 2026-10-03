// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthExecutorTest — OpEthExecutor (the bcos-evm-free OP executor,
// opstack-executor/OpEthExecutor.h) concept conformance + per-tx lifecycle
// semantics, without the t8n corpus:
//   ① compile-time: the TransactionExecutor concept and SchedulerSerialImpl's
//     7-arg createExecuteContext probe both resolve against OpEthExecutor;
//   ② block path without a wired BlockHashLookup fails loud (OpConsensusError,
//     no txHash tag — a node-wiring fault, not an evictable tx);
//   ③ the 6-arg executeTransaction with call=false is rejected (block
//     execution needs a scheduler-provided BlockContext);
//   ④ eth_call: an unfunded sender executes against the fabricated max balance
//     (the CallSimulationView equivalent), a pricing-less call's effective
//     price is clamped to exactly the block base fee, and finish() rolls every
//     simulated write back (no sender/recipient accounts leak into the view).
//
// Block-level dual-run coverage (old production path vs this executor driven
// through SchedulerSerialImpl) lives in OpEthExecutorDualRunTest.cpp.

#include "support/DualRunHarness.h"

#include <opstack-executor/OpEthExecutor.h>

#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <memory>

using namespace opstack_test;

// ── ① compile-time concept conformance ──────────────────────────────────────

// The TransactionExecutor concept (6-arg executeTransaction + 6-arg
// createExecuteContext + move-constructible ExecuteContext with
// prepare/execute/finish).
static_assert(
    bcos::executor_v1::TransactionExecutor<opeth::OpEthExecutor, opstack_test::MutableStorage>);

// SchedulerSerialImpl's BlockContext probe form (SchedulerSerialImpl.h:125):
// when the executor declares a BlockContext, the scheduler calls the 7-arg
// createExecuteContext with it (an lvalue — the deleted rvalue overload must
// not interfere).
static_assert(requires(opeth::OpEthExecutor& executor, opstack_test::MutableStorage& storage,
                  bcos::protocol::BlockHeader const& header,
                  bcos::protocol::Transaction const& tx, bcos::ledger::LedgerConfig const& config,
                  opeth::OpEthBlockContext const& ctx) {
    executor.createExecuteContext(storage, header, tx, 0, config, false, ctx);
});

namespace
{

using evmc::literals::operator""_address;
constexpr evmc::address kCoinbase = 0x4200000000000000000000000000000000000011_address;
constexpr evmc::address kTransferTarget = 0xb0b0000000000000000000000000000000000001_address;
const bcos::u256 kHeaderBaseFee{1'000'000'000};  // 1 gwei
constexpr uint64_t kBlockGasLimit = 10'000'000;

/// Synthetic OP header with every strict-mode (Ecotone+) field populated
/// (same recipe as OpEthForkMatrixTest's makeForkHeader).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeExecutorHeader(
    bcos::protocol::BlockNumber number, bcos::u256 baseFee)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp((0x3f2 + number) * 1000);  // ms (FISCO convention)
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = number - 1,
        .blockHash =
            bcos::h256{"0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"}});
    h->setCoinbase(bcos::Address{bcos::bytesConstRef{kCoinbase.bytes, sizeof(kCoinbase.bytes)}});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(kBlockGasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(baseFee);
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(
        bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Deterministic sender key (fixed secret → fixed sender, so the account can be pre-funded).
bcos::crypto::KeyPairInterface::Ptr fixedSenderKeyPair()
{
    bcos::bytes secretBytes(32, 0x00);
    secretBytes[31] = 0x11;
    auto secret = std::make_shared<bcos::crypto::KeyImpl>(
        bcos::bytesConstRef{secretBytes.data(), secretBytes.size()});
    return bcos::crypto::Secp256k1Crypto{}.createKeyPair(std::move(secret));
}

/// Sign a full EIP-1559 envelope (the OpEthForkMatrixTest recipe).
bcos::bytes signEip1559(bcos::crypto::KeyPairInterface const& keyPair, uint64_t nonce,
    uint64_t gasLimit, bcos::u256 maxFee, bcos::u256 maxPriority, bcos::Address const& to,
    bcos::u256 value)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = kOpChainId;
    w3.nonce = nonce;
    w3.maxPriorityFeePerGas = maxPriority;
    w3.maxFeePerGas = maxFee;
    w3.gasLimit = gasLimit;
    w3.to = to;
    w3.value = value;
    w3.data = {};
    bcos::crypto::Secp256k1Crypto secp;
    auto const sig = secp.sign(keyPair, w3.hashForSign(), false);
    BOOST_REQUIRE(sig != nullptr);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];  // typed tx: yParity
    return w3.encode();
}

template <class View>
void seedAccount(View& view, evmc::address const& addr, bcos::u256 balance,
    bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto account = bcos::executor_v1::eth::ethViewAccount(view, addr);
    bcos::task::syncWait(account.create());
    // A non-zero codeHash marks the account as existing; empty code takes the empty-code hash.
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(balance));
}

bcos::ledger::LedgerConfig ecotoneLedgerConfig()
{
    bcos::ledger::LedgerConfig config;
    config.setEVMCRevision(opeth::OP_ECOTONE_SPEC.rev);
    return config;
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthExecutorSuite)

// ② A missing block-hash source on the block path must fail loud
// (OpConsensusError, no txHash tag) instead of silently zeroing BLOCKHASH.
BOOST_AUTO_TEST_CASE(BlockPathRequiresWiredBlockHashLookup)
{
    auto const keyPair = fixedSenderKeyPair();
    auto const sender = keyPair->address(makeCryptoSuite()->hashImpl());

    DualRunFixture f;
    auto const header = makeExecutorHeader(1, kHeaderBaseFee);
    auto const env = signEip1559(*keyPair, /*nonce=*/0, /*gasLimit=*/100'000,
        bcos::u256(2'000'000'000), bcos::u256(1'000'000),
        bcos::Address{kTransferTarget.bytes, sizeof(kTransferTarget.bytes)}, bcos::u256(1));
    auto const tx = buildFiscoTx(env, f.hashImpl);

    auto view = f.multiLayerStorage.fork();
    view.newMutable();
    evmc::address senderEvmc{};
    std::memcpy(senderEvmc.bytes, sender.data(), sizeof(senderEvmc.bytes));
    seedAccount(view, senderEvmc, bcos::u256(1) << 200, f.hashImpl);

    auto ledgerConfig = ecotoneLedgerConfig();
    opeth::OpEthExecutor executor{f.receiptFactory, opeth::OP_ECOTONE_SPEC};
    opeth::OpEthBlockContext ctx{};
    ctx.blockGasLeft = static_cast<int64_t>(kBlockGasLimit);
    ctx.chainId = kOpChainId;
    // ctx.blockHashLookup deliberately left empty.

    auto execCtx = bcos::task::syncWait(
        executor.createExecuteContext(view, *header, *tx, 0, ledgerConfig, false, ctx));
    // prepare() must succeed: the guard belongs to execute() (the only phase
    // that can consult BLOCKHASH).
    bcos::task::syncWait(execCtx.prepare());
    try
    {
        bcos::task::syncWait(execCtx.execute());
        BOOST_FAIL("execute() without a wired BlockHashLookup must throw OpConsensusError");
    }
    catch (bcos::evm::OpConsensusError const& e)
    {
        BOOST_CHECK(std::string(e.what()).find("RecentBlockHashes") != std::string::npos);
        // Node-wiring fault: no per-tx txHash tag (a tagged tx would be evicted
        // from the pool, which cannot fix the wiring).
        BOOST_CHECK(!e.txHash.has_value());
        BOOST_CHECK(!e.capacity);
    }
}

// ③ The 6-arg executeTransaction is the eth_call entry point; block execution
// through it is rejected (it needs the scheduler-provided BlockContext).
BOOST_AUTO_TEST_CASE(SixArgBlockExecutionThrows)
{
    DualRunFixture f;
    auto const header = makeExecutorHeader(1, kHeaderBaseFee);
    auto const keyPair = fixedSenderKeyPair();
    auto const env = signEip1559(*keyPair, 0, 100'000, bcos::u256(2'000'000'000),
        bcos::u256(1'000'000), bcos::Address{kTransferTarget.bytes, sizeof(kTransferTarget.bytes)},
        bcos::u256(1));
    auto const tx = buildFiscoTx(env, f.hashImpl);

    auto view = f.multiLayerStorage.fork();
    view.newMutable();
    bcos::ledger::LedgerConfig ledgerConfig;  // unconfigured: the call gate throws first
    opeth::OpEthExecutor executor{f.receiptFactory, opeth::OP_ECOTONE_SPEC};
    BOOST_CHECK_THROW(
        bcos::task::syncWait(executor.executeTransaction(view, *header, *tx, 0, ledgerConfig,
                                 /*call=*/false)),
        bcos::evm::OpConsensusError);
}

// ④ eth_call: fabricated max balance + base-fee clamp + rollback of every
// simulated write.
BOOST_AUTO_TEST_CASE(EthCallFakeBalanceClampAndRollback)
{
    DualRunFixture f;
    auto const header = makeExecutorHeader(1, kHeaderBaseFee);

    // Unsigned pricing-less call shape (the RPC default): no gasPrice /
    // maxFeePerGas, no nonce, no signed envelope — the sizing envelope is
    // synthesized for the L1-cost estimate.
    auto const sender =
        bcos::Address{"0xdead0000000000000000000000000000000000beef"};  // never funded
    bcostars::Transaction tars;
    tars.data.chainID = "8453";  // decimal (tars convention)
    tars.data.gasLimit = 100'000;
    tars.data.to = "0xb0b0000000000000000000000000000000000001";
    tars.data.value = "0x1";
    tars.web3TypedTxKind = 0;
    bcos::bytes hashBytes(32, 0x42);  // hash() reads extraTransactionHash; must be populated
    tars.extraTransactionHash.assign(hashBytes.begin(), hashBytes.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    tx->forceSender(sender.asBytes());

    auto view = f.multiLayerStorage.fork();
    view.newMutable();
    auto ledgerConfig = ecotoneLedgerConfig();
    opeth::OpEthExecutor executor{f.receiptFactory, opeth::OP_ECOTONE_SPEC};

    auto const receipt = bcos::task::syncWait(
        executor.executeTransaction(view, *header, *tx, 0, ledgerConfig, /*call=*/true));
    BOOST_REQUIRE(receipt);
    BOOST_CHECK_EQUAL(receipt->status(), 0);  // EVMC_SUCCESS
    // Plain transfer: exactly the intrinsic 21000.
    BOOST_CHECK(receipt->gasUsed() == bcos::u256(21'000));
    // The pricing-less call is clamped to exactly the block base fee
    // (1 gwei = 0x3b9aca00) — never above it.
    BOOST_CHECK_EQUAL(receipt->effectiveGasPrice(), "0x3b9aca00");

    // finish() rolled the simulation back: neither the fabricated sender nor
    // the funded recipient leaked into the view.
    namespace eth = bcos::executor_v1::eth;
    evmc::address senderEvmc{};
    std::memcpy(senderEvmc.bytes, sender.data(), sizeof(senderEvmc.bytes));
    auto senderAcc = eth::ethViewAccount(view, senderEvmc);
    BOOST_CHECK(!bcos::task::syncWait(senderAcc.exists()));
    auto targetAcc = eth::ethViewAccount(view, kTransferTarget);
    BOOST_CHECK(!bcos::task::syncWait(targetAcc.exists()));
}

BOOST_AUTO_TEST_SUITE_END()
