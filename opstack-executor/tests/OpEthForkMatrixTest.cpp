// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthForkMatrixTest — C2 synthetic fork-matrix coverage for the bcos-evm-free OP executor
// migration. Where C1 (OpEthDualRunTest) replays the op-geth t8n corpus (Isthmus/Jovian only),
// these cases synthesize blocks directly and pin the fork-dependent semantics the corpus does
// not reach:
//   ① pre-Regolith deposit semantics (Bedrock/Regolith),
//   ② the Ecotone first-block L1-fee fallback (ecotoneParamsUnset → legacy formula),
//   ③ Isthmus operator-fee vault routing + gasLimit pre-charge refund,
//   ④ Jovian da_footprint seal-layer rules (deposits skipped, missing meta rejected),
//   ⑤ Karst P256VERIFY Osaka pricing (6900, no override entry),
//   ⑥ precompile max_input_size caps ({EVMC_FAILURE, 0} before gas accounting),
//   ⑦ always-warm override precompiles must not journal ghost accounts.
// ①②③ execute the new layer TWICE from independent MLS forks (a determinism twin — step 3.3
// retired the legacy bcos-evm baseline leg) and assert byte-identical outcomes plus the
// spec-dependent seal shape; the semantic pinning comes from the per-case exact-value
// assertions (vault balances, fee formulas) and the t8n golden suites. ④ is seal-layer
// direct; ⑤⑥⑦ are policy/host-level direct.

#include "support/DualRunHarness.h"

#include <opstack-executor/OpEthCommitments.h>    // computeOpEthTransactionsRoot
#include <opstack-executor/OpEthL1Attributes.h>   // encodeOpEthDepositEnvelope
#include <opstack-executor/OpEthReceipt.h>        // intxToBcosU256
#include <opstack-executor/OpExecutionPolicy.h>
#include <opstack-executor/OpRollupCost.h>  // computeLegacyL1Cost / computeL1Cost / computeOperatorCost

#include <ethereum-executor/EthExecutionPolicy.h>  // EthL1Policy (⑦ contrast)
#include <ethereum-executor/EthereumHost.h>
#include <ethereum-executor/EthereumState.h>

#include <bcos-crypto/signature/key/KeyImpl.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>  // encodeReceiptLeaf
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmone_precompiles/ecc.hpp>
#include <evmone_precompiles/secp256r1.hpp>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <optional>
#include <vector>

using namespace opstack_test;
using evmc::literals::operator""_address;
namespace eth = bcos::executor_v1::eth;

namespace
{

constexpr evmc::address kCoinbase = 0x4200000000000000000000000000000000000011_address;
constexpr evmc::address kTransferTarget = 0xb0b0000000000000000000000000000000000001_address;
constexpr evmc::address kRevertContract = 0x00000000000000000000000000000000e000e001_address;
constexpr evmc::address kDepositor2 = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0002_address;
constexpr evmc::address kDepositor3 = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0003_address;
constexpr evmc::address kDepositor4 = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0004_address;

const bcos::u256 kHeaderBaseFee{1'000'000'000};  // 1 gwei
constexpr uint64_t kBlockGasLimit = 10'000'000;

// ── synthetic header / envelope builders ────────────────────────────────────────────────────

/// Synthetic OP header. All strict-mode fields (baseFee/parentBeaconBlockRoot/blob fields) are
/// always populated; pre-Ecotone consumers are lenient and simply ignore them.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeForkHeader(
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
        bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"});
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

/// Sign a full EIP-1559 envelope (the makeDecodableWeb3Tx recipe from OpEngineServiceParityTest).
bcos::bytes signEip1559(bcos::crypto::KeyPairInterface const& keyPair, uint64_t nonce,
    uint64_t gasLimit, bcos::u256 maxFee, bcos::u256 maxPriority, bcos::Address const& to,
    bcos::u256 value, bcos::bytes data = {})
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
    w3.data = std::move(data);
    bcos::crypto::Secp256k1Crypto secp;
    auto const sig = secp.sign(keyPair, w3.hashForSign(), false);
    BOOST_REQUIRE(sig != nullptr);
    BOOST_REQUIRE_EQUAL(sig->size(), 65);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];  // typed tx: yParity
    return w3.encode();
}

/// 0x7e deposit envelope (new-layer DepositTx; wire-compatible with op-geth).
bcos::bytes depositEnvelope(evmc::address from, std::optional<evmc::address> to, uint64_t gasLimit,
    bool isSystemTx, uint8_t sourceTag)
{
    opeth::DepositTx dep;
    dep.sourceHash = evmc::bytes32{};
    dep.sourceHash.bytes[31] = sourceTag;
    dep.from = from;
    dep.to = to;
    dep.mint = std::nullopt;
    dep.value = bcos::u256{0};
    dep.gasLimit = static_cast<int64_t>(gasLimit);
    dep.isSystemTx = isSystemTx;
    dep.data = {};
    return opeth::encodeOpEthDepositEnvelope(dep);
}

// ── pre-state seeding (into a view's own mutable layer — the C1 dual-run lesson) ────────────

struct AccountSeed
{
    evmc::address addr{};
    bcos::u256 balance = 0;
    uint64_t nonce = 0;
    bcos::bytes code{};
    std::vector<std::pair<evmc::bytes32, evmc::bytes32>> slots{};
};

evmc::bytes32 slotKey(uint8_t slot)
{
    evmc::bytes32 k{};
    k.bytes[31] = slot;
    return k;
}

evmc::bytes32 wordFromU256(intx::uint256 v)
{
    evmc::bytes32 w{};
    intx::be::store(w.bytes, v);
    return w;
}

void putU32BE(evmc::bytes32& w, size_t offset, uint32_t v)
{
    w.bytes[offset] = static_cast<uint8_t>(v >> 24);
    w.bytes[offset + 1] = static_cast<uint8_t>(v >> 16);
    w.bytes[offset + 2] = static_cast<uint8_t>(v >> 8);
    w.bytes[offset + 3] = static_cast<uint8_t>(v);
}

void putU64BE(evmc::bytes32& w, size_t offset, uint64_t v)
{
    for (size_t i = 0; i < 8; ++i)
        w.bytes[offset + i] = static_cast<uint8_t>(v >> (56 - 8 * i));
}

template <class View>
void seedAccounts(View& view, std::vector<AccountSeed> const& accounts,
    bcos::crypto::Hash::Ptr const& hashImpl)
{
    for (auto const& seed : accounts)
    {
        auto account = eth::ethViewAccount(view, seed.addr);
        bcos::task::syncWait(account.create());
        // Same existence pattern as OpSchedulerTest's seedSender: a non-zero codeHash marks the
        // account as existing; empty code takes the empty-code hash.
        bcos::task::syncWait(account.setCode(seed.code, {},
            seed.code.empty() ? hashImpl->emptyHash() : hashImpl->hash(seed.code)));
        bcos::task::syncWait(account.setNonce(std::to_string(seed.nonce)));
        bcos::task::syncWait(account.setBalance(seed.balance));
        for (auto const& [key, value] : seed.slots)
            bcos::task::syncWait(account.setStorage(key, value));
    }
}

template <class View>
bcos::u256 viewBalance(View& view, evmc::address const& addr)
{
    auto account = eth::ethViewAccount(view, addr);
    return bcos::task::syncWait(account.balance());
}

// ── synthetic-block driver: the new layer executed twice from independent MLS forks ─────────

struct SyntheticOutcome
{
    MLS::ViewType viewA;  // first execution's view
    MLS::ViewType viewB;  // determinism twin's view
    NewPathOutcome outcomeA;
    NewPathOutcome outcomeB;
};

SyntheticOutcome runSyntheticBlock(DualRunFixture& f, bcos::protocol::BlockHeader const& header,
    opeth::OpForkSpec const& spec, std::vector<bcos::bytes> const& rawTxBytes,
    std::vector<AccountSeed> const& seeds)
{
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& env : rawTxBytes)
        transactions.push_back(buildFiscoTx(env, f.hashImpl));

    std::vector<opeth::OpEthBlockTx> txs;
    txs.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        bool const isDeposit = rawTxBytes[i][0] == opeth::OP_DEPOSIT_TX_TYPE;
        txs.push_back(opeth::OpEthBlockTx{
            .tx = isDeposit ? nullptr : transactions[i], .envelope = rawTxBytes[i]});
    }
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    seedAccounts(viewA, seeds, f.hashImpl);
    auto outcomeA = runNewPath(f, viewA, header, spec, txs);
    auto viewB = f.multiLayerStorage.fork();
    viewB.newMutable();
    seedAccounts(viewB, seeds, f.hashImpl);
    auto outcomeB = runNewPath(f, viewB, header, spec, txs);
    return SyntheticOutcome{
        std::move(viewA), std::move(viewB), std::move(outcomeA), std::move(outcomeB)};
}

// ── twin-agreement comparison helpers ───────────────────────────────────────────────────────

/// Field-by-field opStackMeta comparison of two receipts (a nullopt meta reads as an empty
/// meta — the tars layer reports an all-empty meta as nullopt).
void checkMetaEqual(bcos::protocol::TransactionReceipt const& a,
    bcos::protocol::TransactionReceipt const& b, std::size_t index)
{
    auto const metaA = a.opStackMeta().value_or(bcos::protocol::OpStackReceiptMeta{});
    auto const metaB = b.opStackMeta().value_or(bcos::protocol::OpStackReceiptMeta{});
    BOOST_TEST_CONTEXT("receipt " << index << " opStackMeta")
    {
        BOOST_CHECK_EQUAL(metaA.l1_gas_price.has_value(), metaB.l1_gas_price.has_value());
        if (metaA.l1_gas_price && metaB.l1_gas_price)
            BOOST_CHECK_EQUAL(*metaA.l1_gas_price, *metaB.l1_gas_price);
        BOOST_CHECK_EQUAL(metaA.l1_fee.has_value(), metaB.l1_fee.has_value());
        if (metaA.l1_fee && metaB.l1_fee)
            BOOST_CHECK_EQUAL(*metaA.l1_fee, *metaB.l1_fee);
        BOOST_CHECK_EQUAL(
            metaA.l1_blob_base_fee.has_value(), metaB.l1_blob_base_fee.has_value());
        if (metaA.l1_blob_base_fee && metaB.l1_blob_base_fee)
            BOOST_CHECK_EQUAL(*metaA.l1_blob_base_fee, *metaB.l1_blob_base_fee);
        BOOST_CHECK_EQUAL(metaA.l1_base_fee_scalar.has_value(),
            metaB.l1_base_fee_scalar.has_value());
        BOOST_CHECK(metaA.l1_base_fee_scalar == metaB.l1_base_fee_scalar);
        BOOST_CHECK(metaA.l1_blob_base_fee_scalar == metaB.l1_blob_base_fee_scalar);
        BOOST_CHECK(metaA.operator_fee_scalar == metaB.operator_fee_scalar);
        BOOST_CHECK(metaA.operator_fee_constant == metaB.operator_fee_constant);
        BOOST_CHECK(metaA.da_footprint_gas_scalar == metaB.da_footprint_gas_scalar);
        BOOST_CHECK(metaA.da_footprint == metaB.da_footprint);
        BOOST_CHECK(metaA.deposit_nonce == metaB.deposit_nonce);
        BOOST_CHECK(metaA.deposit_receipt_version == metaB.deposit_receipt_version);
        BOOST_CHECK(metaA.l1_gas_used == metaB.l1_gas_used);
        BOOST_CHECK_EQUAL(metaA.operator_fee.has_value(), metaB.operator_fee.has_value());
        if (metaA.operator_fee && metaB.operator_fee)
            BOOST_CHECK_EQUAL(*metaA.operator_fee, *metaB.operator_fee);
    }
}

/// The full twin-agreement battery: the two independent executions must agree field-by-field
/// (seal, gasUsed, roots, per-receipt leaf bytes + status/gasUsed/log count + opStackMeta),
/// and the seal shape must follow the fork spec (withdrawalsRoot on Canyon+, requestsHash on
/// Isthmus+, blobGasUsed on Ecotone+). The txRoot is cross-checked against
/// computeOpEthTransactionsRoot over the raw envelopes.
void checkTwinsAgree(SyntheticOutcome& out, opeth::OpForkSpec const& spec,
    std::vector<bcos::bytes> const& rawTxBytes)
{
    auto const& outcomeA = out.outcomeA;
    auto const& outcomeB = out.outcomeB;

    BOOST_REQUIRE_EQUAL(outcomeA.result.receipts.size(), rawTxBytes.size());
    BOOST_REQUIRE_EQUAL(outcomeB.result.receipts.size(), rawTxBytes.size());
    BOOST_REQUIRE_EQUAL(outcomeB.result.txTypes.size(), rawTxBytes.size());

    BOOST_CHECK_EQUAL(outcomeA.seal.receiptsRoot.hexPrefixed(),
        outcomeB.seal.receiptsRoot.hexPrefixed());
    BOOST_CHECK(outcomeA.seal.logsBloom == outcomeB.seal.logsBloom);
    BOOST_CHECK_EQUAL(
        outcomeA.seal.withdrawalsRoot.has_value(), outcomeB.seal.withdrawalsRoot.has_value());
    if (outcomeA.seal.withdrawalsRoot && outcomeB.seal.withdrawalsRoot)
        BOOST_CHECK_EQUAL(outcomeA.seal.withdrawalsRoot->hexPrefixed(),
            outcomeB.seal.withdrawalsRoot->hexPrefixed());
    BOOST_CHECK_EQUAL(
        outcomeA.seal.requestsHash.has_value(), outcomeB.seal.requestsHash.has_value());
    if (outcomeA.seal.requestsHash && outcomeB.seal.requestsHash)
        BOOST_CHECK_EQUAL(outcomeA.seal.requestsHash->hexPrefixed(),
            outcomeB.seal.requestsHash->hexPrefixed());
    BOOST_CHECK_EQUAL(
        outcomeA.seal.blobGasUsed.has_value(), outcomeB.seal.blobGasUsed.has_value());
    if (outcomeA.seal.blobGasUsed && outcomeB.seal.blobGasUsed)
        BOOST_CHECK_EQUAL(*outcomeA.seal.blobGasUsed, *outcomeB.seal.blobGasUsed);
    BOOST_CHECK_EQUAL(outcomeA.result.gasUsed, outcomeB.result.gasUsed);
    BOOST_CHECK_EQUAL(outcomeA.txRoot.hexPrefixed(), outcomeB.txRoot.hexPrefixed());
    if (outcomeA.stateRoot != outcomeB.stateRoot)
        dumpRowDiff(out.viewA, out.viewB);
    BOOST_CHECK_EQUAL(outcomeA.stateRoot.hexPrefixed(), outcomeB.stateRoot.hexPrefixed());

    // Spec-dependent seal shape (Canyon+ withdrawals root, Isthmus+ requests hash — the
    // operator fee is the Isthmus marker on this ladder — Ecotone+ blobGasUsed).
    BOOST_CHECK_EQUAL(outcomeB.seal.withdrawalsRoot.has_value(), spec.has_withdrawals);
    BOOST_CHECK_EQUAL(outcomeB.seal.requestsHash.has_value(), spec.has_operator_fee);
    BOOST_CHECK_EQUAL(outcomeB.seal.blobGasUsed.has_value(), spec.rev >= EVMC_CANCUN);
    BOOST_CHECK_EQUAL(outcomeB.txRoot.hexPrefixed(),
        opeth::computeOpEthTransactionsRoot(rawTxBytes).hexPrefixed());

    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
    {
        auto const expectedType = opeth::opEthClassifyTxType(rawTxBytes[i][0]);
        BOOST_REQUIRE_EQUAL(expectedType, outcomeB.result.txTypes[i]);
        auto const leafA = bcos::ledger::mpt::encodeReceiptLeaf(
            *outcomeA.result.receipts[i], expectedType);
        auto const leafB = bcos::ledger::mpt::encodeReceiptLeaf(
            *outcomeB.result.receipts[i], expectedType);
        BOOST_CHECK_MESSAGE(leafA == leafB,
            "receipt " << i << " leaf mismatch: a=" << bcos::toHex(leafA)
                       << " b=" << bcos::toHex(leafB));
        BOOST_CHECK_EQUAL(
            outcomeA.result.receipts[i]->status(), outcomeB.result.receipts[i]->status());
        BOOST_CHECK_MESSAGE(
            outcomeA.result.receipts[i]->gasUsed() == outcomeB.result.receipts[i]->gasUsed(),
            "receipt " << i << " gasUsed mismatch: a=" << outcomeA.result.receipts[i]->gasUsed()
                       << " b=" << outcomeB.result.receipts[i]->gasUsed());
        BOOST_CHECK_EQUAL(outcomeA.result.receipts[i]->logEntries().size(),
            outcomeB.result.receipts[i]->logEntries().size());
        checkMetaEqual(*outcomeA.result.receipts[i], *outcomeB.result.receipts[i], i);
    }
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthForkMatrixSuite)

// ── ① pre-Regolith deposit semantics (Bedrock) ──────────────────────────────────────────────
// op-geth state_transition.go preCheck/innerExecute: a system deposit pre-Regolith never touches
// the block gas pool and reports gasUsed=0 — even with a gasLimit ABOVE the block gas limit
// (the Bedrock L1-attributes deposit carries 150M). A metered deposit reports its full gasLimit
// on EVERY outcome (success / EVM revert / entry failure). Receipts carry no deposit_nonce
// pre-Regolith.
BOOST_AUTO_TEST_CASE(BedrockDepositSemantics)
{
    DualRunFixture f;
    auto const header = makeForkHeader(1, kHeaderBaseFee);

    std::vector<bcos::bytes> const rawTxBytes = {
        // 0: unmetered system deposit, gasLimit far above the block gas limit (10M).
        depositEnvelope(opeth::OP_DEPOSITOR, opeth::OP_L1_BLOCK, 150'000'000, /*system=*/true, 0x01),
        // 1: metered success (call into an empty-code account).
        depositEnvelope(kDepositor2, kTransferTarget, 100'000, false, 0x02),
        // 2: metered EVM revert (contract code PUSH1 0, PUSH1 0, REVERT — Paris-valid).
        depositEnvelope(kDepositor3, kRevertContract, 100'000, false, 0x03),
        // 3: metered entry failure (gasLimit below the 21000 intrinsic).
        depositEnvelope(kDepositor4, kTransferTarget, 1'000, false, 0x04),
    };
    std::vector<AccountSeed> const seeds = {
        {.addr = kRevertContract, .code = {0x60, 0x00, 0x60, 0x00, 0xfd}},
    };

    auto out = runSyntheticBlock(f, *header, opeth::OP_BEDROCK_SPEC, rawTxBytes, seeds);
    checkTwinsAgree(out, opeth::OP_BEDROCK_SPEC, rawTxBytes);

    auto const& receipts = out.outcomeB.result.receipts;
    BOOST_CHECK_EQUAL(receipts[0]->status(), 0);
    BOOST_CHECK_EQUAL(receipts[0]->gasUsed(), bcos::u256(0));  // system tx: unmetered
    BOOST_CHECK_EQUAL(receipts[1]->status(), 0);
    BOOST_CHECK_EQUAL(receipts[1]->gasUsed(), bcos::u256(100'000));  // success → full gasLimit
    BOOST_CHECK_EQUAL(receipts[2]->status(), 1);                     // REVERT
    BOOST_CHECK_EQUAL(receipts[2]->gasUsed(), bcos::u256(100'000));  // revert → full gasLimit
    BOOST_CHECK_EQUAL(receipts[3]->status(), 1);                     // entry failure
    BOOST_CHECK_EQUAL(receipts[3]->gasUsed(), bcos::u256(1'000));    // … → full gasLimit
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        // The tars layer reports an all-empty meta as nullopt ("legacy receipt"), so a Bedrock
        // deposit receipt reads as no-meta — the semantic assertion is per-field absence.
        auto const meta = receipts[i]->opStackMeta().value_or(bcos::protocol::OpStackReceiptMeta{});
        BOOST_CHECK_MESSAGE(!meta.deposit_nonce.has_value(),
            "pre-Regolith deposit receipt " << i << " must not carry deposit_nonce");
        BOOST_CHECK(!meta.deposit_receipt_version.has_value());
        BOOST_CHECK(receipts[i]->effectiveGasPrice() == "0x0");
    }
    // Block gas: the system deposit contributes 0, the metered ones their full gasLimit.
    BOOST_CHECK_EQUAL(
        static_cast<uint64_t>(out.outcomeB.result.gasUsed), 100'000 + 100'000 + 1'000);
}

// Regolith twin of ①: deposits count a nonce (deposit_nonce = the sender's pre-execution nonce,
// seeded to observable values), but pre-Canyon the trie leaf does NOT encode it (the EncodeIndex
// quirk: nonce+version are appended only when depositReceiptVersion is set).
BOOST_AUTO_TEST_CASE(RegolithDepositNonce)
{
    DualRunFixture f;
    auto const header = makeForkHeader(1, kHeaderBaseFee);

    std::vector<bcos::bytes> const rawTxBytes = {
        depositEnvelope(opeth::OP_DEPOSITOR, opeth::OP_L1_BLOCK, 100'000, false, 0x11),
        depositEnvelope(kDepositor2, kTransferTarget, 100'000, false, 0x12),
    };
    std::vector<AccountSeed> const seeds = {
        {.addr = opeth::OP_DEPOSITOR, .nonce = 7},
        {.addr = kDepositor2, .nonce = 3},
    };

    auto out = runSyntheticBlock(f, *header, opeth::OP_REGOLITH_SPEC, rawTxBytes, seeds);
    checkTwinsAgree(out, opeth::OP_REGOLITH_SPEC, rawTxBytes);

    auto const& receipts = out.outcomeB.result.receipts;
    for (std::size_t i = 0; i < receipts.size(); ++i)
    {
        auto const meta = receipts[i]->opStackMeta();
        BOOST_REQUIRE(meta.has_value());
        BOOST_CHECK_MESSAGE(!meta->deposit_receipt_version.has_value(),
            "pre-Canyon deposit receipt " << i << " must not carry deposit_receipt_version");

        // Pre-Canyon leaf quirk: dropping deposit_nonce from the meta must leave the encoded
        // leaf byte-identical (the nonce is not in the RLP).
        auto twin = f.receiptFactory->createReceipt(receipts[i]->gasUsed(),
            std::string{receipts[i]->contractAddress()},
            bcos::protocol::LogEntries(
                receipts[i]->logEntries().begin(), receipts[i]->logEntries().end()),
            receipts[i]->status(), receipts[i]->output(), receipts[i]->blockNumber());
        twin->setLogsBloom(receipts[i]->logsBloom());
        twin->setCumulativeGasUsed(std::string{receipts[i]->cumulativeGasUsed()});
        auto twinMeta = *meta;
        twinMeta.deposit_nonce.reset();
        twin->setOpStackMeta(twinMeta);
        twin->setEffectiveGasPrice(std::string{receipts[i]->effectiveGasPrice()});
        auto const leaf = bcos::ledger::mpt::encodeReceiptLeaf(
            *receipts[i], opeth::OP_DEPOSIT_TX_TYPE);
        auto const twinLeaf =
            bcos::ledger::mpt::encodeReceiptLeaf(*twin, opeth::OP_DEPOSIT_TX_TYPE);
        BOOST_CHECK_MESSAGE(leaf == twinLeaf,
            "Regolith deposit leaf " << i << " must not encode deposit_nonce: leaf="
                                     << bcos::toHex(leaf) << " twin=" << bcos::toHex(twinLeaf));
    }
    BOOST_CHECK_EQUAL(receipts[0]->opStackMeta()->deposit_nonce.value_or(999), 7);
    BOOST_CHECK_EQUAL(receipts[1]->opStackMeta()->deposit_nonce.value_or(999), 3);
    // Regolith reports the ACTUAL gas used (21000 for an empty-data call into code-less
    // accounts), not the gasLimit.
    BOOST_CHECK_EQUAL(receipts[0]->gasUsed(), bcos::u256(21'000));
    BOOST_CHECK_EQUAL(receipts[1]->gasUsed(), bcos::u256(21'000));
}

// ── ② Ecotone first-block fallback (ecotoneParamsUnset → Bedrock legacy formula) ────────────
// op-geth rollup_cost.go NewL1CostFunc "firstEcotoneBlock": Ecotone is active but the L1Block
// predeploy's Ecotone slots (3/7) still read zero, so the Bedrock overhead/scalar formula
// applies (with isRegolith=true). Run under both seedings; the l1_fee must differ across the
// two.
BOOST_AUTO_TEST_CASE(EcotoneFirstBlockFallback)
{
    auto const keyPair = fixedSenderKeyPair();
    auto const sender = keyPair->address(makeCryptoSuite()->hashImpl());

    DualRunFixture f;
    auto const header = makeForkHeader(1, kHeaderBaseFee);
    auto const transferEnv = signEip1559(*keyPair, /*nonce=*/0, /*gasLimit=*/100'000,
        bcos::u256(2'000'000'000), bcos::u256(1'000'000),
        bcos::Address{kTransferTarget.bytes, sizeof(kTransferTarget.bytes)}, bcos::u256(1));
    std::vector<bcos::bytes> const rawTxBytes = {
        depositEnvelope(opeth::OP_DEPOSITOR, opeth::OP_L1_BLOCK, 100'000, false, 0x21),
        transferEnv,
    };

    auto const l1BaseFee = intx::uint256{1'000'000'000};
    auto runWith = [&](std::vector<std::pair<evmc::bytes32, evmc::bytes32>> const& l1Slots)
        -> SyntheticOutcome {
        std::vector<AccountSeed> seeds = {
            {.addr = evmc::address{}, .balance = bcos::u256(1) << 200},  // placeholder, fixed below
            // nonce=1 keeps L1Block non-empty under EIP-161: the attributes deposit's CALL
            // touches the account, and an empty (nonce/balance 0, empty code) touched account
            // is deleted at applyToStorage — slots included (EthereumState.h phase 2).
            {.addr = opeth::OP_L1_BLOCK, .nonce = 1, .slots = l1Slots},
        };
        std::memcpy(seeds[0].addr.bytes, sender.data(), sizeof(seeds[0].addr.bytes));
        return runSyntheticBlock(f, *header, opeth::OP_ECOTONE_SPEC, rawTxBytes, seeds);
    };

    // (a) Ecotone params unset: slots 3/7 all zero; legacy slots 1/5/6 populated.
    auto outUnset = runWith({
        {slotKey(1), wordFromU256(l1BaseFee)},
        {slotKey(5), wordFromU256(intx::uint256{2'100})},      // l1_fee_overhead
        {slotKey(6), wordFromU256(intx::uint256{1'000'000})},  // l1_fee_scalar (1e6 = 1.0)
    });
    // (b) Ecotone params set: slot3 scalars + slot7 blob base fee; legacy slots 5/6 carry stale
    // values that MUST be ignored.
    evmc::bytes32 slot3{};
    putU32BE(slot3, 16, 1'000);  // base_fee_scalar
    putU32BE(slot3, 20, 2'000);  // blob_base_fee_scalar
    auto outSet = runWith({
        {slotKey(1), wordFromU256(l1BaseFee)},
        {slotKey(3), slot3},
        {slotKey(5), wordFromU256(intx::uint256{2'100})},
        {slotKey(6), wordFromU256(intx::uint256{1'000'000})},
        {slotKey(7), wordFromU256(intx::uint256{2'000'000'000})},  // blob_base_fee
    });

    checkTwinsAgree(outUnset, opeth::OP_ECOTONE_SPEC, rawTxBytes);
    checkTwinsAgree(outSet, opeth::OP_ECOTONE_SPEC, rawTxBytes);

    auto const metaUnset = outUnset.outcomeB.result.receipts[1]->opStackMeta();
    auto const metaSet = outSet.outcomeB.result.receipts[1]->opStackMeta();
    BOOST_REQUIRE(metaUnset.has_value() && metaUnset->l1_fee.has_value());
    BOOST_REQUIRE(metaSet.has_value() && metaSet->l1_fee.has_value());

    // (a) legacy formula (op-geth l1CostHelper, regolith=true): the receipt reports the
    // overhead-inclusive l1_gas_used and OMITS the Ecotone-only blob/scalar fields.
    opeth::OpFeeParams const legacyParams{.l1_base_fee = l1BaseFee,
        .blob_base_fee = 0,
        .l1_fee_overhead = intx::uint256{2'100},
        .l1_fee_scalar = intx::uint256{1'000'000}};
    auto const expectedLegacy = opeth::computeLegacyL1Cost(legacyParams,
        evmc::bytes_view{transferEnv.data(), transferEnv.size()}, /*regolithActive=*/true);
    BOOST_CHECK_EQUAL(*metaUnset->l1_fee, opeth::intxToBcosU256(expectedLegacy.fee));
    BOOST_REQUIRE(metaUnset->l1_gas_used.has_value());
    BOOST_CHECK_EQUAL(*metaUnset->l1_gas_used, expectedLegacy.gas_used);
    BOOST_CHECK(!metaUnset->l1_blob_base_fee.has_value());
    BOOST_CHECK(!metaUnset->l1_base_fee_scalar.has_value());

    // (b) Ecotone formula: calldataGas * (l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/16e6.
    opeth::OpFeeParams const ecotoneParams{.l1_base_fee = l1BaseFee,
        .base_fee_scalar = 1'000,
        .blob_base_fee_scalar = 2'000,
        .blob_base_fee = intx::uint256{2'000'000'000}};
    BOOST_CHECK_EQUAL(*metaSet->l1_fee,
        opeth::intxToBcosU256(opeth::computeL1Cost(ecotoneParams,
            evmc::bytes_view{transferEnv.data(), transferEnv.size()}, opeth::OP_ECOTONE_SPEC)));
    BOOST_REQUIRE(metaSet->l1_blob_base_fee.has_value());
    BOOST_CHECK_EQUAL(*metaSet->l1_blob_base_fee, bcos::u256(2'000'000'000));
    BOOST_CHECK_EQUAL(metaSet->l1_base_fee_scalar.value_or(0), 1'000);

    // The fallback is observable: the two seedings price the same envelope differently.
    BOOST_CHECK_MESSAGE(*metaUnset->l1_fee != *metaSet->l1_fee,
        "ecotoneParamsUnset fallback must diverge from the Ecotone formula (both "
            << *metaUnset->l1_fee << ")");
}

// ── ③ Isthmus operator fee: three-vault routing + gasLimit pre-charge refund ────────────────
// settleFees: base vault += gasUsed*baseFee, coinbase += gasUsed*priority, L1 vault += l1_cost,
// operator vault += opCost(gasUsed) with the opCost(gasLimit)-opCost(gasUsed) pre-charge
// difference refunded to the sender. L1Block slot 8 carries the operator scalar/constant.
BOOST_AUTO_TEST_CASE(IsthmusOperatorFeeVaults)
{
    auto const keyPair = fixedSenderKeyPair();
    auto const hashImpl = makeCryptoSuite()->hashImpl();
    auto const sender = keyPair->address(hashImpl);

    DualRunFixture f;
    auto const header = makeForkHeader(1, kHeaderBaseFee);
    constexpr uint64_t kTxGasLimit = 100'000;  // > 21000 → the pre-charge refund is observable
    const bcos::u256 kMaxFee{2'000'000'000};
    const bcos::u256 kPriority{1'000'000};
    auto const transferEnv = signEip1559(*keyPair, 0, kTxGasLimit, kMaxFee, kPriority,
        bcos::Address{kTransferTarget.bytes, sizeof(kTransferTarget.bytes)}, bcos::u256(1));
    std::vector<bcos::bytes> const rawTxBytes = {
        depositEnvelope(opeth::OP_DEPOSITOR, opeth::OP_L1_BLOCK, 100'000, false, 0x31),
        transferEnv,
    };

    constexpr uint32_t kBaseScalar = 6'849;
    constexpr uint32_t kBlobScalar = 987'654;
    constexpr uint32_t kOperatorScalar = 500'000;      // 0.5
    constexpr uint64_t kOperatorConstant = 12'345;
    evmc::bytes32 slot3{};
    putU32BE(slot3, 16, kBaseScalar);
    putU32BE(slot3, 20, kBlobScalar);
    evmc::bytes32 slot8{};
    putU32BE(slot8, 20, kOperatorScalar);
    putU64BE(slot8, 24, kOperatorConstant);
    auto const l1BaseFee = intx::uint256{1'000'000'000};
    auto const blobBaseFee = intx::uint256{3'000'000'000};

    evmc::address senderEvmc{};
    std::memcpy(senderEvmc.bytes, sender.data(), sizeof(senderEvmc.bytes));
    std::vector<AccountSeed> const seeds = {
        {.addr = senderEvmc, .balance = bcos::u256(1) << 200},
        // nonce=1: see the EIP-161 note in EcotoneFirstBlockFallback.
        {.addr = opeth::OP_L1_BLOCK,
            .nonce = 1,
            .slots = {
                {slotKey(1), wordFromU256(l1BaseFee)},
                {slotKey(3), slot3},
                {slotKey(7), wordFromU256(blobBaseFee)},
                {slotKey(8), slot8},
            }},
    };

    auto out = runSyntheticBlock(f, *header, opeth::OP_ISTHMUS_SPEC, rawTxBytes, seeds);
    checkTwinsAgree(out, opeth::OP_ISTHMUS_SPEC, rawTxBytes);

    // Expected economics (Isthmus operator formula: gas*scalar/1e6 + constant):
    constexpr uint64_t kGasUsed = 21'000;  // plain value transfer
    auto const effectivePrice = kHeaderBaseFee + kPriority;  // 1001000000, under the 2 gwei cap
    opeth::OpFeeParams const params{.l1_base_fee = l1BaseFee,
        .base_fee_scalar = kBaseScalar,
        .blob_base_fee_scalar = kBlobScalar,
        .blob_base_fee = blobBaseFee,
        .operator_fee_scalar = kOperatorScalar,
        .operator_fee_constant = kOperatorConstant};
    auto const l1Cost = opeth::computeL1Cost(
        params, evmc::bytes_view{transferEnv.data(), transferEnv.size()}, opeth::OP_ISTHMUS_SPEC);
    auto const opAtUsed = opeth::computeOperatorCost(params, kGasUsed, /*jovianFormula=*/false);
    auto const opAtLimit = opeth::computeOperatorCost(params, kTxGasLimit, false);
    BOOST_REQUIRE(opAtLimit > opAtUsed);  // the refund delta exists

    auto& view = out.viewB;
    BOOST_CHECK_EQUAL(
        viewBalance(view, opeth::OP_BASE_FEE_VAULT), bcos::u256(kGasUsed) * kHeaderBaseFee);
    BOOST_CHECK_EQUAL(viewBalance(view, kCoinbase), bcos::u256(kGasUsed) * kPriority);
    BOOST_CHECK_EQUAL(viewBalance(view, opeth::OP_L1_FEE_VAULT), opeth::intxToBcosU256(l1Cost));
    BOOST_CHECK_EQUAL(
        viewBalance(view, opeth::OP_OPERATOR_FEE_VAULT), opeth::intxToBcosU256(opAtUsed));

    // Sender: -(gasUsed*effective) - l1Cost - opCost(gasUsed) - value(1 wei); the
    // opCost(gasLimit) pre-charge's excess comes back (kTxGasLimit > kGasUsed makes the delta
    // non-zero).
    auto const expectedSender = (bcos::u256(1) << 200) -
                                bcos::u256(kGasUsed) * effectivePrice -
                                opeth::intxToBcosU256(l1Cost) - opeth::intxToBcosU256(opAtUsed) -
                                bcos::u256(1);  // the transferred value
    BOOST_CHECK_EQUAL(viewBalance(view, senderEvmc), expectedSender);

    auto const meta = out.outcomeB.result.receipts[1]->opStackMeta();
    BOOST_REQUIRE(meta.has_value());
    BOOST_REQUIRE(meta->operator_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->operator_fee, opeth::intxToBcosU256(opAtUsed));
    BOOST_CHECK_EQUAL(meta->operator_fee_scalar.value_or(0), kOperatorScalar);
    BOOST_CHECK_EQUAL(meta->operator_fee_constant.value_or(0), kOperatorConstant);
    BOOST_REQUIRE(meta->l1_fee.has_value());
    BOOST_CHECK_EQUAL(*meta->l1_fee, opeth::intxToBcosU256(l1Cost));
}

// ── ④ Jovian da_footprint seal layer ────────────────────────────────────────────────────────
// sealOpEthBlock under has_da_footprint: deposits are skipped (a deposits-only block sums to 0 ≡
// op-geth's first-Jovian-block case); a non-deposit receipt missing meta.da_footprint is a
// consensus reject (OpEthBlockError), not a silent 0.
BOOST_AUTO_TEST_CASE(JovianDaFootprintSeal)
{
    DualRunFixture f;
    auto makeReceipt = [&](bcos::u256 gasUsed) {
        auto receipt = f.receiptFactory->createReceipt(gasUsed, std::string{},
            bcos::protocol::LogEntries{}, 0, bcos::bytesConstRef{}, 1);
        bcos::bytes bloom(256, 0);
        receipt->setLogsBloom(bcos::bytesConstRef{bloom.data(), bloom.size()});
        // encodeReceiptLeaf parses this field (decimal on the OP path).
        receipt->setCumulativeGasUsed(std::to_string(static_cast<uint64_t>(gasUsed)));
        receipt->setEffectiveGasPrice("0x0");
        return receipt;
    };
    auto depositReceipt = [&]() {
        auto receipt = makeReceipt(bcos::u256(100'000));
        bcos::protocol::OpStackReceiptMeta meta;
        meta.deposit_nonce = 0;
        meta.deposit_receipt_version = 1;  // Canyon+ seal guard requires both
        receipt->setOpStackMeta(meta);
        return receipt;
    };

    // (a) deposits-only block: the footprint sum is 0.
    {
        opeth::OpEthBlockResult result;
        result.receipts.push_back(depositReceipt());
        result.txTypes.push_back(opeth::OP_DEPOSIT_TX_TYPE);
        auto const seal = opeth::sealOpEthBlock(result, opeth::OP_JOVIAN_SPEC, {});
        BOOST_REQUIRE(seal.blobGasUsed.has_value());
        BOOST_CHECK_EQUAL(*seal.blobGasUsed, 0);
    }
    // (b) a non-deposit receipt with NO meta at all → reject.
    {
        opeth::OpEthBlockResult result;
        result.receipts.push_back(depositReceipt());
        result.txTypes.push_back(opeth::OP_DEPOSIT_TX_TYPE);
        result.receipts.push_back(makeReceipt(bcos::u256(21'000)));
        result.txTypes.push_back(0x02);
        auto sealFn = [&] { return opeth::sealOpEthBlock(result, opeth::OP_JOVIAN_SPEC, {}); };
        BOOST_CHECK_THROW(sealFn(), opeth::OpEthBlockError);
    }
    // (c) a non-deposit receipt whose meta lacks da_footprint → reject.
    {
        opeth::OpEthBlockResult result;
        result.receipts.push_back(depositReceipt());
        result.txTypes.push_back(opeth::OP_DEPOSIT_TX_TYPE);
        auto normal = makeReceipt(bcos::u256(21'000));
        normal->setOpStackMeta(bcos::protocol::OpStackReceiptMeta{});  // present, but no footprint
        result.receipts.push_back(normal);
        result.txTypes.push_back(0x02);
        auto sealFn = [&] { return opeth::sealOpEthBlock(result, opeth::OP_JOVIAN_SPEC, {}); };
        BOOST_CHECK_THROW(sealFn(), opeth::OpEthBlockError);
    }
    // (d) the happy path: footprint sums over non-deposit receipts.
    {
        opeth::OpEthBlockResult result;
        result.receipts.push_back(depositReceipt());
        result.txTypes.push_back(opeth::OP_DEPOSIT_TX_TYPE);
        auto normal = makeReceipt(bcos::u256(21'000));
        bcos::protocol::OpStackReceiptMeta meta;
        meta.da_footprint = 777;
        normal->setOpStackMeta(meta);
        result.receipts.push_back(normal);
        result.txTypes.push_back(0x02);
        auto const seal = opeth::sealOpEthBlock(result, opeth::OP_JOVIAN_SPEC, {});
        BOOST_REQUIRE(seal.blobGasUsed.has_value());
        BOOST_CHECK_EQUAL(*seal.blobGasUsed, 777);
    }
}

// ── ⑤ Karst p256verify: Osaka built-in pricing (6900) ───────────────────────────────────────
// OP_KARST_SPEC carries no 0x100 override entry (the Fjord–Jovian 3450 override stops at Jovian),
// so callPrecompile falls through to the revision's own table: EVMC_OSAKA gates EIP-7951
// P256VERIFY at gas 6900. The Jovian twin shows the 3450 override for contrast.

// A valid P-256 signature is constructed algebraically (the vendored precompiles ship verify
// only): pick secret d and nonce k, Q = d·G, R = k·G, r = R.x mod n, s = (h + r·d)/k mod n.
// Field-element arithmetic over the curve ORDER models the scalar arithmetic mod n. (Namespace
// scope: a local struct may not carry a static data member.)
struct Secp256r1OrderSpec
{
    static constexpr auto ORDER = evmmax::secp256r1::Curve::ORDER;
};
using Secp256r1Scalar = evmmax::ecc::FieldElement<Secp256r1OrderSpec>;

BOOST_AUTO_TEST_CASE(KarstP256OsakaPricing)
{
    namespace secp256r1 = evmmax::secp256r1;
    using Fn = Secp256r1Scalar;
    const intx::uint256 d = 0xdeadbeef;
    const intx::uint256 k = 0x12345;
    ethash::hash256 h{};
    h.bytes[31] = 0x42;
    auto const hVal = intx::be::load<intx::uint256>(h.bytes) % secp256r1::Curve::ORDER;
    auto const Q = evmmax::ecc::to_affine(evmmax::ecc::mul(secp256r1::G, d));
    auto const R = evmmax::ecc::to_affine(evmmax::ecc::mul(secp256r1::G, k));
    auto const r = R.x.value() % secp256r1::Curve::ORDER;
    auto const s = ((Fn(hVal) + Fn(r) * Fn(d)) * Fn(k).inv()).value();
    auto const qx = Q.x.value();
    auto const qy = Q.y.value();
    BOOST_REQUIRE(secp256r1::verify(h, r, s, qx, qy));  // the construction itself is sound

    std::array<uint8_t, 160> validInput{};
    intx::be::unsafe::store(validInput.data() + 0, intx::be::unsafe::load<intx::uint256>(h.bytes));
    intx::be::unsafe::store(validInput.data() + 32, r);
    intx::be::unsafe::store(validInput.data() + 64, s);
    intx::be::unsafe::store(validInput.data() + 96, qx);
    intx::be::unsafe::store(validInput.data() + 128, qy);
    std::array<uint8_t, 160> zeroInput{};

    auto msg = [&](uint8_t const* input, size_t size, int64_t gas) {
        evmc_message m{};
        m.kind = EVMC_CALL;
        m.gas = gas;
        m.recipient = opeth::OP_P256_VERIFY_ADDRESS;
        m.code_address = opeth::OP_P256_VERIFY_ADDRESS;
        m.input_data = input;
        m.input_size = size;
        return m;
    };

    eth::EthBlockInfo block{};
    opeth::OpPolicy const karstPolicy{opeth::OP_KARST_SPEC, block};
    opeth::OpPolicy const jovianPolicy{opeth::OP_JOVIAN_SPEC, block};

    // Karst (Osaka table): priced at 6900.
    {
        auto res = karstPolicy.callPrecompile(EVMC_OSAKA, msg(zeroInput.data(), 160, 6'899));
        BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_OUT_OF_GAS));
    }
    {
        // 160 zero bytes: exactly priced, invalid signature → SUCCESS with empty output and the
        // remaining gas kept (op-geth p256 semantics).
        auto res = karstPolicy.callPrecompile(EVMC_OSAKA, msg(zeroInput.data(), 160, 6'900));
        BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_SUCCESS));
        BOOST_CHECK_EQUAL(res.gas_left, 0);
        BOOST_CHECK_EQUAL(res.output_size, 0);
    }
    {
        auto res = karstPolicy.callPrecompile(EVMC_OSAKA, msg(validInput.data(), 160, 6'942));
        BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_SUCCESS));
        BOOST_CHECK_EQUAL(res.gas_left, 42);
        BOOST_REQUIRE_EQUAL(res.output_size, 32);
        std::array<uint8_t, 32> expected{};
        expected[31] = 1;
        BOOST_CHECK(std::equal(expected.begin(), expected.end(), res.output_data));
    }

    // Jovian (override entry): priced at 3450 even though Prague's own table lacks 0x100.
    {
        auto res = jovianPolicy.callPrecompile(EVMC_PRAGUE, msg(zeroInput.data(), 160, 3'449));
        BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_OUT_OF_GAS));
    }
    {
        auto res = jovianPolicy.callPrecompile(EVMC_PRAGUE, msg(validInput.data(), 160, 3'492));
        BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_SUCCESS));
        BOOST_CHECK_EQUAL(res.gas_left, 42);
        BOOST_REQUIRE_EQUAL(res.output_size, 32);
        BOOST_CHECK_EQUAL(res.output_data[31], 1);
    }
}

// ── ⑥ max_input_size caps: {EVMC_FAILURE, 0} BEFORE any gas accounting ──────────────────────
// An over-cap input is rejected with EVMC_FAILURE and zero gas left even when the supplied gas
// could never afford the generic path — proving the cap fires before pricing. The boundary size
// falls through to the revision's own table (which then OOGs on the tiny gas).
BOOST_AUTO_TEST_CASE(PrecompileMaxInputSize)
{
    eth::EthBlockInfo block{};
    opeth::OpPolicy const jovianPolicy{opeth::OP_JOVIAN_SPEC, block};
    opeth::OpPolicy const isthmusPolicy{opeth::OP_ISTHMUS_SPEC, block};

    constexpr size_t kJovianBn256Limit = 81'984;
    std::vector<uint8_t> const overCap(kJovianBn256Limit + 1, 0xaa);
    std::vector<uint8_t> const atCap(kJovianBn256Limit, 0xaa);
    auto msg = [&](uint8_t const* input, size_t size, int64_t gas) {
        evmc_message m{};
        m.kind = EVMC_CALL;
        m.gas = gas;
        m.recipient = evmc::address{0x08};
        m.code_address = evmc::address{0x08};
        m.input_data = input;
        m.input_size = size;
        return m;
    };
    constexpr int64_t kTinyGas = 1'000;  // far below the bn256 pairing price (~13.9M at the cap)

    // Over the Jovian cap: EVMC_FAILURE + gas_left 0 (not OUT_OF_GAS — no pricing happened).
    auto res = jovianPolicy.callPrecompile(EVMC_PRAGUE, msg(overCap.data(), overCap.size(), kTinyGas));
    BOOST_CHECK_EQUAL(static_cast<int>(res.status_code), static_cast<int>(EVMC_FAILURE));
    BOOST_CHECK_EQUAL(res.gas_left, 0);

    // Exactly at the cap: admitted to the generic table → priced → OUT_OF_GAS.
    auto resAt = jovianPolicy.callPrecompile(EVMC_PRAGUE, msg(atCap.data(), atCap.size(), kTinyGas));
    BOOST_CHECK_EQUAL(static_cast<int>(resAt.status_code), static_cast<int>(EVMC_OUT_OF_GAS));

    // The same over-Jovian-cap input is within the Isthmus cap (112687): generic path again.
    auto resIsthmus =
        isthmusPolicy.callPrecompile(EVMC_PRAGUE, msg(overCap.data(), overCap.size(), kTinyGas));
    BOOST_CHECK_EQUAL(static_cast<int>(resIsthmus.status_code), static_cast<int>(EVMC_OUT_OF_GAS));
}

// ── ⑦ always-warm override precompiles journal no ghost accounts ────────────────────────────
// EthereumHost::access_account: an override-table address reports WARM before get_or_insert
// would journal a ghost empty account (op-geth statedb.Prepare warms every active precompile,
// including 0x100 which Prague's own table gates at Osaka). The L1 policy keeps the insert-first
// behaviour — the contrast makes the no-ghost property observable.
BOOST_AUTO_TEST_CASE(AlwaysWarmPrecompilesNoGhost)
{
    opstack_test::MutableStorage opStorage{1};
    eth::EthereumState<opstack_test::MutableStorage> opState{opStorage};
    evmc::VM vm{evmc_create_evmone()};
    eth::EthBlockInfo block{};
    opeth::OpPolicy const opPolicy{opeth::OP_JOVIAN_SPEC, block};
    eth::EthereumHost<opstack_test::MutableStorage, opeth::OpPolicy> opHost{EVMC_PRAGUE, vm,
        opState, block, {}, std::nullopt, eth::EthCallParams{}, kOpChainId, opPolicy};

    BOOST_CHECK_EQUAL(
        static_cast<int>(opHost.access_account(opeth::OP_P256_VERIFY_ADDRESS)),
        static_cast<int>(EVMC_ACCESS_WARM));
    BOOST_CHECK_EQUAL(static_cast<int>(opHost.access_account(evmc::address{0x08})),
        static_cast<int>(EVMC_ACCESS_WARM));
    BOOST_CHECK_MESSAGE(opState.modified().empty(),
        "always-warm override precompiles must not journal ghost accounts");

    // L1 contrast: no always-warm hook — the first access inserts the (erase_if_empty) ghost.
    opstack_test::MutableStorage l1Storage{1};
    eth::EthereumState<opstack_test::MutableStorage> l1State{l1Storage};
    eth::EthL1Policy const l1Policy{};
    eth::EthereumHost<opstack_test::MutableStorage, eth::EthL1Policy> l1Host{EVMC_PRAGUE, vm,
        l1State, block, {}, std::nullopt, eth::EthCallParams{}, kOpChainId, l1Policy};

    // 0x08 is a Prague precompile: warm on first access, but only AFTER the ghost insert.
    BOOST_CHECK_EQUAL(static_cast<int>(l1Host.access_account(evmc::address{0x08})),
        static_cast<int>(EVMC_ACCESS_WARM));
    BOOST_CHECK(l1State.modified().contains(evmc::address{0x08}));
    // 0x100 is not a Prague L1 precompile at all: cold first access + ghost.
    BOOST_CHECK_EQUAL(
        static_cast<int>(l1Host.access_account(opeth::OP_P256_VERIFY_ADDRESS)),
        static_cast<int>(EVMC_ACCESS_COLD));
    BOOST_CHECK(l1State.modified().contains(opeth::OP_P256_VERIFY_ADDRESS));
    BOOST_CHECK_EQUAL(
        static_cast<int>(l1Host.access_account(opeth::OP_P256_VERIFY_ADDRESS)),
        static_cast<int>(EVMC_ACCESS_WARM));
}

BOOST_AUTO_TEST_SUITE_END()
