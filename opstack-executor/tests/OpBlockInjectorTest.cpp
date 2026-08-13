// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockInjectorTest — drives runOpBlockInjection (opstack-executor/OpBlockExecute.h) over a
// plain MutableStorage fixture (spec §7(a); the injector is a Storage template, so no MLS is
// needed). A minimal "L1 attributes deposit + eip1559" block verifies (review R3 P3):
//   (1) the system-call BlockInfo's gas_limit == header.gasLimit (toBlockInfo, trivially true);
//   (2) receipt count == tx count;
//   (3) the injector's block-level gasUsed == manual Σ per-receipt gasUsed.
// Per-tx BlockInfo gasLimit==header is deliberately NOT asserted here — that is Task 6's job
// (OpstackExecutorTest::BlockInfoGasLimitUsesHeaderGasLimit) and would be red at this phase.

#include <opstack-executor/OpBlockExecute.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
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

constexpr uint64_t kChainId = 0x2105;
constexpr int64_t kHeaderGasLimit = 30'000'000;
const bcos::Address kSender{"0x1000000000000000000000000000000000000000"};

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// A header carrying every optional field toBlockInfo reads via `.value()` (OpRlpDecode.h:106-121):
/// baseFee / parentBeaconBlockRoot / blobGasUsed must be set or toBlockInfo throws.
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
    h->setGasLimit(bcos::u256(kHeaderGasLimit));
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

/// The block's L1 attributes deposit: to==OP_L1_BLOCK && from==OP_DEPOSITOR satisfies the
/// stricter-than-spec content check (OpBlockExecute.h isL1AttributesTx). Isthmus config means
/// validateJovianBlockShape is a no-op, so the calldata can be minimal.
bcos::evm::opstack::OpBlockTx makeAttributesDeposit()
{
    bcos::evm::opstack::DepositTx dep{
        .source_hash = evmc::bytes32{},
        .from = bcos::evm::opstack::OP_DEPOSITOR,
        .to = bcos::evm::opstack::OP_L1_BLOCK,
        .mint = std::nullopt,
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = {},
    };
    return bcos::evm::opstack::OpBlockTx{dep, {}};
}

/// The block's normal EIP-1559 tx shell. The injector only reads `tx.type` from this variant
/// alternative (execution runs off the caller-prebuilt FISCO Transaction in normalTxs); the other
/// fields are filled for realism.
bcos::evm::opstack::OpBlockTx makeEip1559OpBlockTx()
{
    namespace detail = bcos::evm::engine::detail;
    evmone::state::Transaction evmTx;
    evmTx.type = evmone::state::Transaction::Type::eip1559;
    evmTx.sender = detail::toEvmcAddress(kSender);
    evmTx.to = 0x811a752c8cd697e3cb27279c330ed1ada745a8d7_address;
    evmTx.gas_limit = 5'000'000;
    evmTx.max_gas_price = intx::uint256{30'000'000'000ULL};
    evmTx.max_priority_gas_price = 0;
    evmTx.nonce = 0;
    evmTx.value = 0;
    evmTx.data = {};
    evmc::bytes envelope(64, 0x02);  // non-empty stand-in for the raw 0x02-prefixed envelope
    return bcos::evm::opstack::OpBlockTx{evmTx, envelope};
}

/// Caller-prebuilt FISCO Transaction for the eip1559 tx (review D6: buildFiscoTx is the caller's
/// responsibility — the injector only consumes Transaction::Ptr). EIP-1559 with
/// maxPriorityFeePerGas=0 keeps effectiveGasPrice == baseFee (BCOS2Evmone's access-list override
/// never fires), and the dummy r/s is neutralized by forceSender.
bcos::protocol::Transaction::Ptr buildEip1559FiscoTx()
{
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 5;
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
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsHolder]() { return tarsHolder.get(); });
    tx->clearSenderAndHash();
    tx->forceSender(kSender.asBytes());
    return tx;
}

/// Fund the sender EOA in the plain MutableStorage so opValidate's balance + EIP-3607 checks pass
/// (StorageStateView::exists() needs a non-zero codeHash — create + setCode(empty) makes it an
/// existing account with empty code; a bare setBalance would leave it nonexistent).
void fundSender(MutableStorage& storage, bcos::crypto::Hash::Ptr const& hashImpl)
{
    bcos::ledger::account::EVMAccount<MutableStorage> account(storage, kSender, false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockInjector)

BOOST_AUTO_TEST_CASE(InjectsDepositAndEip1559Block)
{
    namespace op = bcos::evm::opstack;
    namespace engine = bcos::evm::engine;
    namespace detail = bcos::evm::engine::detail;

    // Isthmus-active fork config (configAt resolution == path A executeOpBlock, fork parity).
    constexpr uint64_t kIsthmusTime = 0;
    constexpr uint64_t kJovianTime = std::numeric_limits<uint64_t>::max();
    const auto& cfg = op::configAt(1000, op::OpForkTimestamps{kIsthmusTime, kJovianTime});

    MutableStorage storage;
    auto cryptoSuite = makeCryptoSuite();
    auto hashImpl = cryptoSuite->hashImpl();
    auto receiptFactory = makeReceiptFactory();
    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, cfg};

    auto header = makeHeader(1'000'000);  // 1000 s
    bcos::ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setEVMCRevision(cfg.rev);

    fundSender(storage, hashImpl);

    auto depTx = makeAttributesDeposit();
    auto normTx = makeEip1559OpBlockTx();
    std::vector<op::OpBlockTx> txs{depTx, normTx};
    std::vector<bcos::protocol::Transaction::Ptr> normalTxs{buildEip1559FiscoTx()};
    bcos::bytes depEnv(depTx.signedEnvelope.begin(), depTx.signedEnvelope.end());
    bcos::bytes normEnv(normTx.signedEnvelope.begin(), normTx.signedEnvelope.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, normEnv};

    auto result = engine::runOpBlockInjection(executor, storage, *header, txs, normalTxs, cfg,
        kChainId, ledgerConfig, rawTxBytes, hashImpl);

    // R3 P3: system-call BlockInfo gas_limit == header.gasLimit (toBlockInfo, trivially true here).
    const auto sysBlk = detail::toBlockInfo(*header);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit, kHeaderGasLimit);
    BOOST_CHECK_EQUAL(sysBlk.gas_limit,
        static_cast<int64_t>(detail::narrowU256ToU64(header->gasLimit(), "test")));

    // Receipt count == tx count (both txs execute).
    BOOST_CHECK_EQUAL(result.receipts.size(), txs.size());

    // gasUsed == manual Σ per-receipt gasUsed (the injector's cumulative accumulator).
    int64_t manual = 0;
    for (auto const& r : result.receipts)
        manual += op::narrowGasUsed(r->gasUsed());
    BOOST_CHECK_EQUAL(result.gasUsed, static_cast<uint64_t>(manual));
    BOOST_CHECK_GT(manual, 0);  // both txs actually consumed gas
}

/// Empty-block rejection (after route A executeOpBlock retired, the empty-block rejection moved to
/// the injector with route B): runOpBlockInjection with empty txs → OpConsensusError (a
/// std::runtime_error subclass). The former OpSchedulerImpl SmokeTest::EmptyBlockRejected covered
/// the same classification for executeOpBlock — this is the route-B equivalent.
BOOST_AUTO_TEST_CASE(EmptyBlockRejectedByInjector)
{
    namespace op = bcos::evm::opstack;
    namespace engine = bcos::evm::engine;

    constexpr uint64_t kIsthmusTime = 0;
    constexpr uint64_t kJovianTime = std::numeric_limits<uint64_t>::max();
    const auto& cfg = op::configAt(1000, op::OpForkTimestamps{kIsthmusTime, kJovianTime});

    MutableStorage storage;
    auto cryptoSuite = makeCryptoSuite();
    auto hashImpl = cryptoSuite->hashImpl();
    auto receiptFactory = makeReceiptFactory();
    bcos::executor_v1::opstack::OpstackExecutor executor{receiptFactory, hashImpl, cfg};

    auto header = makeHeader(1'000'000);
    bcos::ledger::LedgerConfig ledgerConfig;
    ledgerConfig.setEVMCRevision(cfg.rev);

    // Empty txs → "op block: missing L1 attributes deposit (empty block)" → OpConsensusError.
    std::vector<op::OpBlockTx> txs;
    std::vector<bcos::protocol::Transaction::Ptr> normalTxs;
    std::vector<bcos::bytes> rawTxBytes;
    BOOST_CHECK_THROW(engine::runOpBlockInjection(executor, storage, *header, txs, normalTxs, cfg,
                          kChainId, ledgerConfig, rawTxBytes, hashImpl),
        std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
