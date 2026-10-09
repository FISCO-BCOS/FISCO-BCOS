// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
/// @file OpBlockInjectorTest.cpp
/// @brief Block-injection paths of the OP scheduler: payload assembly, envelope checks and commit ordering.

// OpBlockInjectorTest — drives the shared block-execution path (preBlockOpEthSteps →
// SchedulerSerialImpl(serial=true) → finalizeOpEthBlockResult) over a plain MutableStorage
// fixture (the path is Storage templates, so no MLS is needed). A minimal "L1 attributes deposit + eip1559" block verifies:
//   (1) the system-call BlockInfo's gas_limit == header.gasLimit (buildOpEthBlockInfo,
//       trivially true);
//   (2) receipt count == tx count;
//   (3) the block-level gasUsed == manual Σ per-receipt gasUsed.
// Plus: preBlockOpEthSteps rejects an empty block with OpConsensusError (the retired injector's
// empty-block guard now lives there).
// Per-tx BlockInfo gasLimit==header is deliberately NOT asserted here — that belongs to
// OpSchedulerTest's BlockInfo gas-limit case.

#include "support/DualRunHarness.h"
#include <opstack-executor/OpEthBlockExecute.h>
#include <opstack-executor/OpEthL1Attributes.h>  // encodeOpEthDepositEnvelope

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/ledger/OpForkSchedule.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>  // per-tx loop (Task 5)
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
using evmc::literals::operator""_address;

namespace
{
using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

constexpr uint64_t c_chainId = 0x2105;
constexpr int64_t c_headerGasLimit = 30'000'000;
const bcos::Address kSender{"0x1000000000000000000000000000000000000000"};

/// A header carrying every optional field buildOpEthBlockInfo requires (OpEthBlockExecute.h):
/// baseFee / parentBeaconBlockRoot / blobGasUsed must be set or it throws OpEthBlockError.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader(int64_t timestampMillis)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(c_headerGasLimit));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// The block's L1 attributes deposit: to==OP_L1_BLOCK && from==OP_DEPOSITOR.
bcos::executor_v1::opstack::DepositTx makeAttributesDeposit()
{
    return bcos::executor_v1::opstack::DepositTx{
        .sourceHash = evmc::bytes32{},
        .from = bcos::executor_v1::opstack::OP_DEPOSITOR,
        .to = bcos::executor_v1::opstack::OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = bcos::u256{0},
        .gasLimit = 100000,
        .isSystemTx = false,
        .data = {},
    };
}

/// Caller-prebuilt FISCO Transaction for the eip1559 tx (buildFiscoTx is the caller's
/// responsibility — the injector only consumes Transaction::Ptr). EIP-1559 with
/// maxPriorityFeePerGas=0 keeps effectiveGasPrice == baseFee (BCOS2Evmone's access-list override
/// never fires), and the dummy r/s is neutralized by forceSender.
bcos::protocol::Transaction::Ptr buildEip1559FiscoTx()
{
    // chainId must match the block chainId (c_chainId, 0x2105): a mismatch is rejected in m_prepare
    // before opValidate.
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = c_chainId;
    w3.nonce = 0;
    w3.maxFeePerGas = bcos::u256(30'000'000'000ULL);
    w3.maxPriorityFeePerGas = 0;
    w3.gasLimit = 5'000'000;
    w3.to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7");
    w3.value = bcos::u256(0);
    w3.signatureV = 0;
    w3.signatureR = bcos::bytes(32, 0x01);
    w3.signatureS = bcos::bytes(32, 0x02);
    auto tarsHolder = std::make_shared<bcostars::Transaction>(w3.takeToTarsTransaction());
    auto const txHash = w3.txHash();
    tarsHolder->extraTransactionHash.assign(txHash.begin(), txHash.end());
    // takeToTarsTransaction stores the signing preimage (which a full-envelope consumer would
    // reject as a truncated typed envelope); overwrite with the full EIP-2718 envelope.
    auto const envelope = w3.encode();
    tarsHolder->extraTransactionBytes.assign(envelope.begin(), envelope.end());
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
    tx->clearSenderAndHash();
    tx->forceSender(kSender.asBytes());
    return tx;
}

/// Fund the sender EOA in the plain MutableStorage so opValidate's balance + EIP-3607 checks pass
/// (StorageStateView::exists() needs a non-zero codeHash — create + setCode(empty) makes it an
/// existing account with empty code; a bare setBalance would leave it nonexistent).
template <class StorageT>
void fundSender(StorageT& storage, bcos::crypto::Hash::Ptr const& hashImpl)
{
    bcos::ledger::account::EVMAccount<StorageT> account(
        storage, kSender, bcos::ledger::account::AddressTableMode::Hex);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
}

}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockInjector)

BOOST_AUTO_TEST_CASE(InjectsDepositAndEip1559Block)
{
    namespace detail = bcos::evm::engine::detail;

    opstack_test::DualRunFixture fixture;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    auto hashImpl = fixture.hashImpl;
    auto receiptFactory = fixture.receiptFactory;

    auto header = makeHeader(1'000'000);  // 1000 s

    fundSender(view, hashImpl);

    auto depTx = makeAttributesDeposit();
    auto normFisco = buildEip1559FiscoTx();
    bcos::bytes depEnv = opstack_test::opeth::encodeOpEthDepositEnvelope(depTx);
    auto const normRef = normFisco->extraTransactionBytes();
    bcos::bytes normEnv(normRef.begin(), normRef.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, normEnv};

    auto depFiscoTx = opstack_test::buildFiscoTxFromEnvelope(depEnv, hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions{depFiscoTx, normFisco};

    auto result = opstack_test::runExecutorPath(
        fixture, view, *header, opstack_test::opeth::OP_ISTHMUS_SPEC, transactions, rawTxBytes);

    // System-call BlockInfo gas_limit == header.gasLimit (buildOpEthBlockInfo,
    // trivially true here).
    const auto sysBlk = opstack_test::opeth::buildOpEthBlockInfo(
        *header, opstack_test::opeth::OP_ISTHMUS_SPEC);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit, c_headerGasLimit);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit,
        static_cast<int64_t>(detail::narrowU256ToU64(header->gasLimit(), "test")));

    // Receipt count == tx count (both txs execute).
    BOOST_CHECK_EQUAL(result.receipts.size(), rawTxBytes.size());

    // gasUsed == manual Σ per-receipt gasUsed (the block-level cumulative accumulator).
    int64_t manual = 0;
    for (auto const& r : result.receipts)
        manual += static_cast<int64_t>(r->gasUsed());
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<uint64_t>(manual));
    BOOST_CHECK_GT(manual, 0);  // both txs actually consumed gas
}

/// Empty-block rejection: preBlockOpEthSteps with empty rawTxBytes → OpConsensusError (a
/// std::runtime_error subclass). The retired runOpBlockInjection's empty-block guard lives here
/// now.
BOOST_AUTO_TEST_CASE(EmptyBlockRejectedByBlockPreSteps)
{
    namespace opeth = opstack_test::opeth;

    opstack_test::DualRunFixture fixture;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    auto hashImpl = fixture.hashImpl;
    auto receiptFactory = fixture.receiptFactory;

    auto header = makeHeader(1'000'000);

    // Empty deposits/rawTxBytes → "op block: missing L1 attributes deposit (empty block)" →
    // OpConsensusError.
    std::vector<opeth::DepositTx> deposits;
    std::vector<bcos::bytes> rawTxBytes;
    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<opeth::OpRecentBlockHashes<std::decay_t<decltype(view)>>> hashes;
    auto sharedError = std::make_shared<opeth::OpStorageErrorSlot>();
    opeth::OpEthExecutor executor{receiptFactory, opeth::OP_ISTHMUS_SPEC, sharedError};
    BOOST_CHECK_THROW(
        bcos::task::syncWait(opeth::preBlockOpEthSteps(view, *header, opeth::OP_ISTHMUS_SPEC,
                                 rawTxBytes, deposits, executor.vm(), sharedError, hashes, hashErr,
                                 daFootprintGasScalar, /*noUserTxActivationBlock=*/false)),
        std::runtime_error);
}

/// Finding J (round-2 review #5429): pin the M2 accept path — a deposit AFTER a non-deposit is
/// ACCEPTED (D demoted it from OpConsensusError to BCOS_LOG(WARNING) + continue in f974bb1a9).
/// op-geth/op-reth enforce deposit-first only at the sequencer (construction), never at
/// validation, so this block must execute with all three txs. Without this anchor, a regression
/// to the old hard reject (which would re-divergence from the reference clients by rejecting a
/// block they accept) has no CI signal.
BOOST_AUTO_TEST_CASE(DepositAfterNonDepositAccepted)
{

    opstack_test::DualRunFixture fixture;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    auto hashImpl = fixture.hashImpl;
    auto receiptFactory = fixture.receiptFactory;

    auto header = makeHeader(1'000'000);
    fundSender(view, hashImpl);

    // Block: [L1 attributes deposit, normal tx, deposit] — the third tx is a deposit after a
    // non-deposit, the M2 order-gate case that was demoted to an observable WARNING.
    auto attrDep = makeAttributesDeposit();
    auto normFisco = buildEip1559FiscoTx();
    auto lateDep = makeAttributesDeposit();
    bcos::bytes attrEnv = opstack_test::opeth::encodeOpEthDepositEnvelope(attrDep);
    auto const normRef = normFisco->extraTransactionBytes();
    bcos::bytes normEnv(normRef.begin(), normRef.end());
    bcos::bytes lateEnv = opstack_test::opeth::encodeOpEthDepositEnvelope(lateDep);
    std::vector<bcos::bytes> rawTxBytes{attrEnv, normEnv, lateEnv};

    auto attrFiscoTx = opstack_test::buildFiscoTxFromEnvelope(attrEnv, hashImpl);
    auto lateFiscoTx = opstack_test::buildFiscoTxFromEnvelope(lateEnv, hashImpl);
    BOOST_REQUIRE(attrFiscoTx != nullptr);
    BOOST_REQUIRE(lateFiscoTx != nullptr);
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions{
        attrFiscoTx, normFisco, lateFiscoTx};

    auto result = opstack_test::runExecutorPath(
        fixture, view, *header, opstack_test::opeth::OP_ISTHMUS_SPEC, transactions, rawTxBytes);

    // All three txs execute — the late deposit is accepted, not rejected.
    BOOST_CHECK_EQUAL(result.receipts.size(), rawTxBytes.size());
    BOOST_CHECK_EQUAL(result.receipts.size(), 3u);
    int64_t manual = 0;
    for (auto const& r : result.receipts)
        manual += static_cast<int64_t>(r->gasUsed());
    BOOST_CHECK_GT(manual, 0);  // all three txs actually consumed gas
}

/// Finding J (round-2 review #5429): pin the isL1AttributesTx accept path — a first deposit that
/// is NOT the L1-attributes tx (to/from != OP_L1_BLOCK/OP_DEPOSITOR) is ACCEPTED (D demoted the
/// content check from a hard reject to BCOS_LOG(WARNING) in f974bb1a9; op-geth's extract_l1_info
/// and op-reth's validation do not validate L1-attributes identity). The block must execute with
/// both txs. Isthmus config keeps the Jovian DA-footprint shape checks (which read
/// deposits[0].data) out of the way.
BOOST_AUTO_TEST_CASE(FirstDepositNotL1AttributesAccepted)
{

    opstack_test::DualRunFixture fixture;
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    auto hashImpl = fixture.hashImpl;
    auto receiptFactory = fixture.receiptFactory;

    auto header = makeHeader(1'000'000);
    fundSender(view, hashImpl);

    // First deposit deliberately NOT the L1-attributes tx: arbitrary from/to (the L1-attributes
    // content check is demoted to a WARNING, so this does not reject the block).
    bcos::executor_v1::opstack::DepositTx nonAttrDep{
        .sourceHash = evmc::bytes32{},
        .from = 0x811a752c8cd697e3cb27279c330ed1ada745a8d7_address,
        .to = 0x811a752c8cd697e3cb27279c330ed1ada745a8d7_address,
        .mint = std::nullopt,
        .value = bcos::u256{0},
        .gasLimit = 100000,
        .isSystemTx = false,
        .data = {},
    };
    auto normFisco = buildEip1559FiscoTx();
    bcos::bytes depEnv = opstack_test::opeth::encodeOpEthDepositEnvelope(nonAttrDep);
    auto const normRef = normFisco->extraTransactionBytes();
    bcos::bytes normEnv(normRef.begin(), normRef.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, normEnv};

    auto depFiscoTx = opstack_test::buildFiscoTxFromEnvelope(depEnv, hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions{depFiscoTx, normFisco};

    auto result = opstack_test::runExecutorPath(
        fixture, view, *header, opstack_test::opeth::OP_ISTHMUS_SPEC, transactions, rawTxBytes);

    // Both txs execute — the non-L1-attributes first deposit is accepted, not rejected.
    BOOST_CHECK_EQUAL(result.receipts.size(), rawTxBytes.size());
    BOOST_CHECK_EQUAL(result.receipts.size(), 2u);
    int64_t manual = 0;
    for (auto const& r : result.receipts)
        manual += static_cast<int64_t>(r->gasUsed());
    BOOST_CHECK_GT(manual, 0);  // both txs actually consumed gas
}

BOOST_AUTO_TEST_SUITE_END()
