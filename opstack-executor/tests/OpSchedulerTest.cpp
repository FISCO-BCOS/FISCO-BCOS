// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerTest — execute/commit + classifyException + eth_call / callAtBlock for OpScheduler.
// Ported from the combined-branch suite. ReorgUndo codec cases stay with the reorg follow-up.
//
// 1. CommitPersistsSevenLedgerTables: deposit + 1 eip1559, executeBlock + commitBlock; announced
//    commitments come from a direct probe (preBlockOpSteps → SchedulerSerialImpl →
//    finalizeOpBlockResult).
// 2. ConsensusRejectionClassifiedAsOpConsensusRejected: 0x03 type byte → OpConsensusRejected.
// 3. classifyException: OpConsensusError→OpConsensusRejected / OpStorageError→OpStorageFault /
//    other→UnknownError.
#include <opstack-executor/OpCommitments.h>    // detail::toBcosH256
#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope
#include <opstack-executor/OpScheduler.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/Storage2StateHelpers.h>  // accountTableName (corrupt-slot seeding)

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-framework/ledger/FeaturesStorage.h>  // writeToStorage (seedL2CompatFeature)
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/Ledger.h>  // real bcos::ledger::Ledger for the commit hook
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-ledger/mpt/MPTBuilder.h>           // buildAndCollect (①a incremental cross-check)
#include <bcos-storage/KeyPrefixes.h>             // mptNodeStateKey (corrupt-root seeding)
#include <bcos-table/src/LegacyStorageWrapper.h>  // LegacyStorageWrapper<BackendMemStorage>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Error.h>
#include <boost/test/unit_test.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using evmc::literals::operator""_address;
using evmc::literals::operator""_bytes32;
namespace memory_storage = bcos::storage2::memory_storage;
namespace detail = bcos::evm::engine::detail;

namespace
{

constexpr uint64_t kChainId = 0x2105;  // 8453 — the FISCO OP chain id (vector eip1559 chainId)
const bcos::Address kSender{"0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"};  // eip1559 recovered
                                                                            // sender

// Corpus isthmus_transfer_basic.json: block.transactions[1]._op_raw (op-geth-signed eip1559
// envelope).
constexpr const char* kEip1559EnvelopeHex =
    "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de"
    "0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae"
    "693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714";

// Minimal CheckpointStorage stub (same as the source-branch fixture: do not cross-include other
// modules' test-private headers).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // this fixture never needs a historical checkpoint.
    }
    void createCheckpoint(Storage& /*unused*/, CheckpointName const& /*unused*/) {}
    void deleteCheckpoint(CheckpointName const& /*unused*/) {}
    [[nodiscard]] std::optional<CheckpointName> latestCheckpointName() const
    {
        return std::nullopt;
    }
    [[nodiscard]] std::optional<CheckpointName> oldestCheckpointName() const
    {
        return std::nullopt;
    }
};

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
using BackendMemStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
    std::hash<StateKey>>;
using CheckpointBackend = TrivialCheckpointStorage<StateKey, StateValue, BackendMemStorage>;
using MLS = bcos::storage2::MultiLayerStorage<MutableStorage, void, CheckpointBackend>;
using ViewType = typename MLS::ViewType;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

bcos::protocol::BlockFactory::Ptr makeBlockFactory()
{
    auto cryptoSuite = makeCryptoSuite();
    auto blockHeaderFactory =
        std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite);
    auto transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite);
    auto receiptFactory =
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
    return std::make_shared<bcostars::protocol::BlockFactoryImpl>(
        cryptoSuite, blockHeaderFactory, transactionFactory, receiptFactory);
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

/// L1 attributes deposit (the isthmus_transfer_basic corpus deposit shape): to==OP_L1_BLOCK &&
/// from==OP_DEPOSITOR satisfies isL1AttributesTx. data is empty — under Isthmus (pre-Jovian)
/// preBlockOpSteps skips the Jovian DA-footprint shape checks; isL1AttributesTx is content-only
/// and never validates calldata (precedent: the empty-data deposit in OpBlockInjectorTest
/// passes likewise).
bcos::evm::opstack::DepositTx makeDeposit()
{
    bcos::evm::opstack::DepositTx dep;
    dep.source_hash = 0x6ab967dfdd3aa359031bef6965cca32ed9a21ea969f7aeee2e58817142a645d7_bytes32;
    dep.from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
    dep.to = 0x4200000000000000000000000000000000000015_address;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;  // 1000000 (corpus gas: "0xf4240")
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

bcos::bytes depositEnvWithSource(evmc::bytes32 source)
{
    auto dep = makeDeposit();
    dep.source_hash = source;
    return encodeDepositEnvelope(dep);
}

/// OP header from the corpus environment (isthmus_transfer_basic env). timestamp is stored in
/// milliseconds (FISCO convention; /1000 gives OP seconds). Commitment fields (stateRoot/txsRoot/
/// receiptsRoot/gasUsed/withdrawalsRoot/logsBloom/requestsHash) are back-filled by the caller from
/// the direct execution probe (runExecutionProbe) result — the announced header carries the true
/// commitments, so the six-field comparison verifies.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeader()
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(1);
    h->setTimestamp(0x3f2 * 1000);  // 0x3f2 = 1010 s (OP seconds) → 1_010_000 ms
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0,
        .blockHash =
            bcos::h256{"0x45daac1c62119a8624509cd80f0b2543f6c78fd21457213af891d8a6d8b14f74"}});
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(0x989680));  // 10000000 (corpus currentGasLimit)
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(0x3a699d00));  // 981000000 (corpus currentBaseFee)
    h->setWithdrawalsRoot(bcos::h256{});
    // Seam requires blobGasUsed (Cancun+). Pre-Jovian seal omits it; announce 0.
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(
        bcos::h256{"0x0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b"});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Back-fill the announced header's commitment fields from the execution probe result
/// (finishExecute writes the same batch of fields, so verify compares equal).
void fillAnnouncedHeader(bcos::protocol::BlockHeader::Ptr const& header,
    bcos::evm::engine::OpExecuteBlockResult const& result)
{
    header->setStateRoot(result.stateRoot);
    header->setTxsRoot(result.txRoot);
    header->setReceiptsRoot(detail::toBcosH256(result.seal.receiptsRoot));
    header->setGasUsed(bcos::u256(result.gasUsed));
    header->setLogsBloom(bcos::bytesConstRef(result.seal.logsBloom.bytes, 256));
    header->setWithdrawalsRoot(detail::toBcosH256(result.seal.withdrawalsRoot));
    if (result.seal.requestsHash.has_value())
        header->setRequestsHash(detail::toBcosH256(*result.seal.requestsHash));
    if (result.seal.blobGasUsed.has_value())
        header->setBlobGasUsed(bcos::u256(*result.seal.blobGasUsed));
}

/// extraTransactionBytes = full envelope (the only bytes executeTransaction /
/// depositFromTransaction read). Hash is the keccak of those bytes. Engine's opEnvelopeToTars is
/// not in this slice.
bcos::protocol::Transaction::Ptr buildFiscoTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const txHash = hashImpl->hash(env);
    bcostars::Transaction tars;
    tars.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tars.extraTransactionHash.assign(txHash.begin(), txHash.end());
    tars.extraTransactionBytes.assign(env.begin(), env.end());
    if (!env.empty() && env[0] == static_cast<uint8_t>(bcos::evm::opstack::kDepositTxType))
    {
        tars.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    }
    else if (!env.empty())
    {
        // Corpus eip1559 (kEip1559EnvelopeHex): mirror fields must match the signed envelope
        // or envelopeExecutionFieldsMismatch rejects the block.
        tars.web3TypedTxKind = static_cast<tars::Char>(env[0]);
        tars.data.nonce = "0x0";
        tars.data.gasLimit = 0x5208;  // 21000
        tars.data.to = "0xb0b0000000000000000000000000000000000001";
        tars.data.value = "0x0de0b6b3a7640000";
        tars.data.chainID = "8453";
        tars.data.maxFeePerGas = "0x77359400";
        tars.data.maxPriorityFeePerGas = "0x05f5e100";
    }
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    if (!env.empty() && env[0] == static_cast<uint8_t>(bcos::evm::opstack::kDepositTxType))
    {
        auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
            bcos::bytesConstRef{env.data(), env.size()});
        tx->forceSender(bcos::bytes(dep.from.bytes, dep.from.bytes + sizeof(dep.from.bytes)));
    }
    else
    {
        tx->forceSender(kSender.asBytes());
    }
    return tx;
}

/// A one-byte 0x03 envelope — the execute hook's type-byte classification deterministically throws
/// OpConsensusError ("unsupported tx type byte", OpScheduler.h). Used to reliably drive the execute
/// hook into the consensus-rejected classification (independent of RTTI-boundary runtime behavior).
bcos::protocol::Transaction::Ptr buildUnsupportedTypeTx()
{
    bcostars::Transaction tars;
    tars.extraTransactionBytes.push_back(0x03);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    return tx;
}

/// Seed the eip1559 sender account into the MLS backend (StorageStateView::exists() needs a
/// non-zero codeHash — create() + setCode(empty); a bare setBalance would be deemed non-existent).
void seedSender(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Seed the SYS_TABLES meta-rows for the ledger SYS tables (each uses the SYS_VALUE field,
/// Ledger.cpp buildGenesisBlock). A real bcos::ledger::Ledger's asyncPrewriteBlock ends with
/// asyncGetTotalTransactionCount, which opens SYS_CURRENT_STATE through the Ledger's OWN
/// m_stateStorage (here: a LegacyStorageWrapper over the MLS backend, distinct from the
/// commit-hook MutableStorage that prewriteBlockToBuffer writes through). Without the SYS_TABLES
/// row the open fails (Table does not exist) and the whole asyncPrewriteBlock errors out.
void seedSysTables(MLS& mls)
{
    auto view = mls.fork();
    view.newMutable();
    constexpr std::string_view sysTables[] = {bcos::ledger::SYS_CURRENT_STATE,
        bcos::ledger::SYS_HASH_2_TX, bcos::ledger::SYS_HASH_2_NUMBER,
        bcos::ledger::SYS_NUMBER_2_HASH, bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
        bcos::ledger::SYS_NUMBER_2_TXS, bcos::ledger::SYS_HASH_2_RECEIPT,
        bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES};
    for (auto const& table : sysTables)
    {
        bcos::storage::Entry e;
        e.set(std::string(bcos::ledger::SYS_VALUE));
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, StateKey{bcos::ledger::SYS_TABLES, std::string(table)}, std::move(e)));
    }
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

struct Fixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::evm::opstack::OpForkFlags forkFlags{.jovianActive = false};
    // A real Ledger wired into the scheduler's m_ledger (the commit hook now calls
    // prewriteBlockToBuffer). prewriteBlockToBuffer writes through the commit hook's MutableStorage
    // (wrapped into a fresh LegacyStorageWrapper by prewriteBlock), so the Ledger's own
    // m_stateStorage is only read by asyncGetTotalTransactionCount (SYS_CURRENT_STATE) — the
    // seedSysTables call below makes that open succeed. LegacyStorageWrapper holds a reference;
    // backendStorage is declared first and outlives it.
    std::shared_ptr<bcos::storage::LegacyStorageWrapper<BackendMemStorage>> legacyLedgerStorage;
    std::shared_ptr<bcos::ledger::Ledger> ledger;
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::executor_v1::opstack::OpScheduler<MLS>> scheduler;

    Fixture()
      : legacyLedgerStorage(
            std::make_shared<bcos::storage::LegacyStorageWrapper<BackendMemStorage>>(
                backendStorage)),
        ledger(std::make_shared<bcos::ledger::Ledger>(blockFactory, legacyLedgerStorage, 1000)),
        scheduler(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(receiptFactory,
            hashImpl, kChainId, forkFlags, blockFactory, multiLayerStorage, ledger, ioServicePool))
    {
        seedSender(multiLayerStorage, kSender, hashImpl);
        seedSysTables(multiLayerStorage);
    }
};

// ── call/getCode case migration (verbatim from the deleted OpBlockSchedulerTest) ──
// Once OpScheduler absorbed OpBlockScheduler's call/getCode pure-virtual implementations, the
// original OpBlockSchedulerTest RPC-face cases (StatusAndResetNoOp / GetCodeEmpty /
// CallInvalidReturnsError / CallHappyPathInjectsRealBaseFee) moved into this suite, with the driven
// object changed to OpScheduler (f.scheduler).
const bcos::Address kCallSender{"0x1000000000000000000000000000000000000000"};

/// A genesis header carrying every field coCallLatest's buildOpBlockInfo/toBlockInfo reads
/// (baseFee=1e9 is the target value of CallHappyPath's injection assertion). @p stateRoot:
/// the scenario-B genesis trie root when historical calls must resolve block 0 (zero
/// otherwise — the pre-historical-test behaviour).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeCallGenesisHeader(
    bcos::h256 stateRoot = bcos::h256{})
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(0);
    h->setTimestamp(1000000);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(stateRoot);
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30000000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1000000000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Seed the minimal OP ledger the RPC call()/read paths need: current head (SYS_CURRENT_STATE)
/// and the block-0 header (getBlockData(HEADER) reads SYS_NUMBER_2_BLOCK_HEADER by number).
void seedCallGenesis(MLS& mls, bcos::protocol::BlockHeader::Ptr const& genesisHeader)
{
    auto view = mls.fork();
    view.newMutable();
    {
        bcos::storage::Entry e;
        e.set(boost::lexical_cast<std::string>(genesisHeader->number()));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
            std::move(e)));
    }
    {
        bcos::bytes buf;
        genesisHeader->encode(buf);
        bcos::storage::Entry e;
        e.set(std::move(buf));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER,
                boost::lexical_cast<std::string>(genesisHeader->number())},
            std::move(e)));
    }
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// EIP-1559 call fixture. Avoids compiling Web3Transaction.cpp (#5496 RLP bind is out of this
/// slice). toEvmoneTransaction reads these knobs; forceSender sets the funded account.
class FakeCallTx : public bcos::protocol::Transaction
{
public:
    uint8_t m_kind = 2;
    bcos::bytes m_input;
    int64_t m_gasLimit = 5000000;
    std::optional<bcos::u256> m_gasPrice;
    std::optional<bcos::u256> m_maxFeePerGas;
    std::optional<bcos::u256> m_maxPriorityFeePerGas;
    std::optional<bcos::u256> m_maxFeePerBlobGas;
    std::string m_sender;
    std::string m_to;
    bcos::u256 m_value = 0;
    std::string m_chainId = "5";
    std::string m_nonce = "0x0";
    bcos::bytes m_extraBytes;
    bcos::protocol::Web3AccessList m_accessList;
    bcos::protocol::VersionedHashes m_blobHashes;
    bcos::protocol::AuthorizationList m_authList;

    uint8_t web3TypedTxKind() const override { return m_kind; }
    bcos::bytesConstRef input() const override
    {
        return bcos::bytesConstRef{m_input.data(), m_input.size()};
    }
    int64_t gasLimit() const override { return m_gasLimit; }
    std::optional<bcos::u256> gasPrice() const override { return m_gasPrice; }
    std::optional<bcos::u256> maxFeePerGas() const override { return m_maxFeePerGas; }
    std::optional<bcos::u256> maxPriorityFeePerGas() const override
    {
        return m_maxPriorityFeePerGas;
    }
    std::optional<bcos::u256> maxFeePerBlobGas() const override { return m_maxFeePerBlobGas; }
    std::string_view sender() const override { return m_sender; }
    std::string_view to() const override { return m_to; }
    bcos::u256 value() const override { return m_value; }
    std::string_view chainId() const override { return m_chainId; }
    std::string_view nonce() const override { return m_nonce; }
    bcos::bytesConstRef extraTransactionBytes() const override
    {
        return bcos::bytesConstRef{m_extraBytes.data(), m_extraBytes.size()};
    }
    bcos::protocol::Web3AccessList web3AccessList() const override { return m_accessList; }
    bcos::protocol::AuthorizationList authorizationList() const override { return m_authList; }
    bcos::protocol::VersionedHashes blobVersionedHashes() const override { return m_blobHashes; }

    void decode(bcos::bytesConstRef) override {}
    void encode(bcos::bytes&) const override {}
    bcos::crypto::HashType hash() const override { return {}; }
    int32_t version() const override { return 0; }
    std::string_view groupId() const override { return {}; }
    int64_t blockLimit() const override { return 0; }
    void setNonce(std::string) override {}
    std::string_view abi() const override { return {}; }
    bcos::bytesConstRef extension() const override { return {}; }
    std::string_view extraData() const override { return {}; }
    int64_t importTime() const override { return 0; }
    void setImportTime(int64_t) override {}
    uint8_t type() const override { return 1; }
    void forceSender(const bcos::bytes& sender) override
    {
        m_sender.assign(sender.begin(), sender.end());
    }
    void clearSenderAndHash() override {}
    void calculateHash(const bcos::crypto::Hash&) override {}
    bcos::bytesConstRef signatureData() const override { return {}; }
    int32_t attribute() const override { return 0; }
    void setAttribute(int32_t) override {}
};

bcos::protocol::Transaction::Ptr buildWeb3Tx(bcos::u256 maxFeePerGas,
    bcos::u256 maxPriorityFeePerGas,
    bcos::Address const& to = bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"),
    bcos::Address const& sender = kCallSender, bool withEnvelope = true)
{
    auto tx = std::make_shared<FakeCallTx>();
    tx->m_maxFeePerGas = maxFeePerGas;
    tx->m_maxPriorityFeePerGas = maxPriorityFeePerGas;
    auto toHex = to.hex();
    if (toHex.rfind("0x", 0) != 0)
    {
        toHex = "0x" + toHex;
    }
    tx->m_to = std::move(toHex);
    tx->forceSender(sender.asBytes());
    // opValidate rejects an empty signed envelope (errc::invalid_argument). A 1-byte stub is
    // enough for L1-cost input; eth_call does not bind envelope fields.
    if (withEnvelope)
    {
        tx->m_extraBytes = {0x02};
    }
    return tx;
}

/// Test-local mirrors of the later-branch StateRootCompute helpers (not on this #5495 slice).
bcos::ledger::mpt::TrieBuildResult collectAccountStorageTrie(
    const std::map<evmc::bytes32, evmc::bytes32>& storage)
{
    std::map<bcos::h256, bcos::bytes> entries;
    for (auto const& [key, value] : storage)
    {
        if (evmc::is_zero(value))
            continue;
        bcos::bytes leaf;
        bcos::codec::rlp::encode(leaf,
            bcos::evm::trimmedBigEndian(bcos::bytesConstRef{value.bytes, sizeof(value.bytes)}));
        entries[bcos::h256{evmone::keccak256(key).bytes, 32}] = std::move(leaf);
    }
    return bcos::ledger::mpt::computeTrieRoot(entries);
}

struct CollectedStateRoot
{
    evmone::hash256 root{};
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
};

template <class Ledger>
CollectedStateRoot collectStateRoot(const Ledger& ledger)
{
    std::map<bcos::h256, bcos::bytes> entries;
    CollectedStateRoot out;
    if (!ledger.visitAccounts([&](const auto& account) {
            auto storageTrie = collectAccountStorageTrie(account.storage);
            out.newNodes.merge(std::move(storageTrie.newNodes));
            evmone::hash256 storageRoot{};
            std::memcpy(storageRoot.bytes, storageTrie.root.data(), sizeof(storageRoot.bytes));
            auto const balanceBe = intx::be::store<evmc::uint256be>(account.balance);
            bcos::bytes leaf;
            bcos::codec::rlp::encode(leaf, account.nonce,
                bcos::evm::trimmedBigEndian(
                    bcos::bytesConstRef{balanceBe.bytes, sizeof(balanceBe.bytes)}),
                bcos::bytesConstRef{storageRoot.bytes, sizeof(storageRoot)},
                bcos::bytesConstRef{account.codeHash.bytes, sizeof(evmc::bytes32)});
            entries[bcos::h256{evmone::keccak256(account.addr).bytes, 32}] = std::move(leaf);
            return true;
        }))
    {
        throw std::runtime_error("collectStateRoot: account traversal incomplete");
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    out.newNodes.merge(std::move(result.newNodes));
    std::memcpy(out.root.bytes, result.root.data(), sizeof(out.root.bytes));
    return out;
}

/// Fund an EOA so opValidate passes (same create()+setCode(empty) existence pattern as seedSender).
void fundCallAccount(MLS& mls, bcos::Address const& addr, bcos::crypto::Hash::Ptr const& hashImpl,
    bcos::u256 const& balance)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(balance));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Seed an account whose table carries a wrong-length storage slot row (32-byte key, 4-byte
/// value). Storage2State::fetchAllStorage validates the slot value length (Storage2State.h:496-500)
/// and throws std::length_error — the finalize bridge's visitAccounts (OpBlockExecute.h:170-177)
/// hits it, so the block-level poison check (OpBlockExecute.h:178-183) rejects the block.
void seedCorruptAccount(
    MLS& mls, evmc::address const& addr, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*binaryAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(0)));
    bcos::storage::Entry e;
    e.set(std::string("abcd"));  // 4 bytes — not a valid 32-byte slot value
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::evm::evmstate::accountTableName(addr), std::string(32, '\x01')},
        std::move(e)));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// Direct execution probe (replaces the retired runOpBlockInjection, Task 5): assemble deposits +
/// block-order transactions + OpstackExecutor, then drive the SAME shared path OpScheduler::execute
/// uses — preBlockOpSteps → SchedulerSerialImpl(serial=true) → finalizeOpBlockResult — returning
/// the OpExecuteBlockResult. The announced header is back-filled from this probe's commitments so
/// the full executeBlock's six-way verify passes (equal by construction). preBlockOpSteps throws on
/// block-level faults — the caller wraps the probe accordingly.
bcos::evm::engine::OpExecuteBlockResult runExecutionProbe(Fixture& f, ViewType& view,
    bcos::protocol::BlockHeader const& header, std::vector<bcos::bytes> const& rawTxBytes)
{
    namespace op = bcos::evm::opstack;
    namespace detail = bcos::evm::engine::detail;
    const auto& cfg = op::configAt(f.forkFlags);
    // Build block-order transactions first (mirroring buildOpBlock: opEnvelopeToTars + full
    // envelope overwrite).
    std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
    transactions.reserve(rawTxBytes.size());
    for (auto const& raw : rawTxBytes)
    {
        transactions.push_back(buildFiscoTx(raw, f.hashImpl));
    }
    // Deposits built from the Transaction objects (mirroring the execute hook, no RLP parse).
    std::vector<op::DepositTx> deposits;
    deposits.reserve(rawTxBytes.size());
    for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
        if (rawTxBytes[i][0] == static_cast<uint8_t>(op::kDepositTxType))
            deposits.push_back(bcos::executor_v1::opstack::OpstackExecutor::depositFromTransaction(
                *transactions[i]));
    bcos::ledger::LedgerConfig execLedgerConfig;
    execLedgerConfig.setEVMCRevision(cfg.rev);
    bcos::executor_v1::opstack::OpstackExecutor executor{f.receiptFactory, f.hashImpl, cfg};

    std::optional<std::string> hashErr;
    std::optional<uint16_t> daFootprintGasScalar;
    std::optional<detail::RecentBlockHashes<ViewType>> hashes;
    bcos::evm::engine::preBlockOpSteps(
        view, header, cfg, rawTxBytes, deposits, executor, hashes, hashErr, daFootprintGasScalar);
    bcos::executor_v1::opstack::OpBlockExecutionContext ctx{.fee = {},
        .blockGasLeft = static_cast<int64_t>(header.gasLimit()),
        .blockHashes = &*hashes,
        .chainId = kChainId,
        .daFootprintGasScalar = daFootprintGasScalar};
    bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
        f.ioServicePool, /*chunkSize=*/1, /*serial=*/true);
    auto transactionsRefs =
        transactions |
        ::ranges::views::transform([](bcos::protocol::Transaction::ConstPtr const& ptr)
                                       -> bcos::protocol::Transaction const& { return *ptr; });
    auto receipts = bcos::task::syncWait(serialScheduler.executeBlock(
        view, executor, header, transactionsRefs, execLedgerConfig, ctx));
    return bcos::evm::engine::finalizeOpBlockResult(executor, view, header, execLedgerConfig, cfg,
        receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr);
}

// ── Historical eth_call helpers (③ MVP + ①b node persistence) ─────────────────────────

/// Enable feature_l2_ethereum_compat (scenario B): the gate both the ①b trie-node flush and
/// OpScheduler::coCallAtBlock consult. Written through SYS_CONFIG like production.
/// @p enableNumber is the SYS_CONFIG activation height. Production always writes 0:
/// NodeConfig parses the config value as a BOOL (NodeConfig.cpp loadGenesisFeatures,
/// `get_value<bool>`) and Ledger::setGenesisFeatures re-stamps it at block 0
/// (Ledger.cpp:1813-1825); a non-zero activation is rejected at boot by
/// validateMPTFlagMatrix (LedgerInitializer.cpp:48). enableNumber=0 also ACTIVATES the flag
/// retroactively for already-committed heights in tests (feature semantics: active at
/// N >= enableNumber); non-zero values exist here only to pin the gate's height semantics.
void seedL2CompatFeature(MLS& mls, bcos::protocol::BlockNumber enableNumber = 0)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::Features features;
    features.set(bcos::ledger::Features::Flag::feature_l2_ethereum_compat);
    bcos::task::syncWait(bcos::ledger::writeToStorage(features, view, enableNumber));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// A makeHeader variant with the number and baseFee parameterized (block height and its fee
/// context are the two per-height observables the historical-call tests assert on).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeHeaderAt(
    bcos::protocol::BlockNumber number, bcos::u256 baseFee)
{
    auto h = makeHeader();
    h->setNumber(number);
    h->setTimestamp((0x3f2 + number) * 1000);
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = bcos::h256{}});
    h->setBaseFee(baseFee);
    return h;
}

/// Compute the scenario-B genesis state trie over the seeded accounts and persist every node
/// as "/mpt/" rows — the test-local mirror of Ledger::buildGenesisBlock's l2EthereumCompat
/// genesis import (Ledger.cpp:2391). Returns the root to stamp on the genesis header.
bcos::h256 computeAndPersistGenesisTrie(MLS& mls)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
    auto result = collectStateRoot(bridge);
    BOOST_REQUIRE_MESSAGE(
        !bridge.poisoned(), "genesis trie build poisoned: " << std::string(bridge.firstError()));
    bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
    bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, result.newNodes));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
    return detail::toBcosH256(result.root);
}

/// Seed a contract account holding real bytecode and one storage slot (same create()+
/// setCode existence pattern as seedSender; setStorage writes the raw 32-byte slot row the
/// full-rebuild trie enumerates). The bytecode is a read-or-set getter:
///   empty calldata  → return slot 0;
///   32-byte calldata → SSTORE it to slot 0, then return slot 0.
/// An eth_call (empty calldata) returns the stored value at whatever state the call ran
/// against, and a deposit carrying a 32-byte data word flips the slot INSIDE a block —
/// ①a requires every state change to flow through a block delta (no out-of-band writes).
void seedContractWithSlot(MLS& mls, bcos::Address const& addr, bcos::h256 const& slotValue,
    bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, addr, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    // CALLDATASIZE; PUSH1 0x0f; JUMPI;            (calldata? → setter at 0x0f)
    // PUSH1 0; SLOAD; PUSH1 0; MSTORE; PUSH1 32; PUSH1 0; RETURN;
    // JUMPDEST; PUSH1 0; CALLDATALOAD; PUSH1 0; SSTORE;
    // PUSH1 0; SLOAD; PUSH1 0; MSTORE; PUSH1 32; PUSH1 0; RETURN
    const bcos::bytes code{0x36, 0x60, 0x0f, 0x57, 0x60, 0x00, 0x54, 0x60, 0x00, 0x52, 0x60, 0x20,
        0x60, 0x00, 0xf3, 0x5b, 0x60, 0x00, 0x35, 0x60, 0x00, 0x55, 0x60, 0x00, 0x54, 0x60, 0x00,
        0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};
    bcos::task::syncWait(account.setCode(code, {}, hashImpl->hash(code)));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(0)));
    evmc_bytes32 slot{};  // slot 0
    evmc_bytes32 value{};
    std::memcpy(value.bytes, slotValue.data(), sizeof(value.bytes));
    bcos::task::syncWait(account.setStorage(slot, value));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

/// A deposit (0x7E — no signature needed) that CALLS the read-or-set contract with a 32-byte
/// data word, flipping slot 0 INSIDE the block that carries it. Replaces the retired
/// out-of-band overwriteSlot: with ①a incremental MPT, a flat write outside any block delta
/// never reaches the trie and breaks the next block's six-way stateRoot comparison.
bcos::bytes makeSetterDepositEnvelope(bcos::Address const& contract, bcos::h256 const& value)
{
    auto dep = makeDeposit();
    dep.source_hash = 0x7c5f2f1a9b4e4d3c2b1a0987654321fedcba9876543210abcdef0123456789_bytes32;
    evmc::address to{};
    std::memcpy(to.bytes, contract.data(), sizeof(to.bytes));
    dep.to = to;
    dep.data = evmc::bytes(value.data(), value.data() + bcos::h256::SIZE);
    return encodeDepositEnvelope(dep);
}

/// Drive one OP block through the full scheduler path: direct execution probe → back-filled
/// announced header → executeBlock(verify=true) → commitBlock. (The body mirrors the
/// single-block driver inside CommitPersistsSevenLedgerTables — the two were NOT refactored
/// into a shared helper; this copy exists so historical-call tests can build multi-block
/// chains without touching that test.)
void driveOpBlock(Fixture& f, std::shared_ptr<bcostars::protocol::BlockHeaderImpl> header,
    std::vector<bcos::bytes> const& rawTxBytes)
{
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult result =
        runExecutionProbe(f, viewA, *header, rawTxBytes);
    BOOST_REQUIRE_EQUAL(result.receipts.size(), rawTxBytes.size());
    fillAnnouncedHeader(header, result);

    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    for (auto const& env : rawTxBytes)
    {
        auto fiscoTx = buildFiscoTx(env, f.hashImpl);
        BOOST_REQUIRE(fiscoTx != nullptr);
        block->appendTransaction(fiscoTx);
    }

    bcos::Error::Ptr execErr;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            execErr = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(execErr == nullptr,
        "executeBlock " << header->number()
                        << " failed: " << (execErr ? execErr->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);

    bcos::Error::Ptr commitErr;
    called = false;
    f.scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(commitErr == nullptr,
        "commitBlock " << header->number()
                       << " failed: " << (commitErr ? commitErr->errorMessage() : ""));
}

struct ExecuteCb
{
    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr header;
};

ExecuteCb invokeExecute(Fixture& f, bcos::protocol::Block::Ptr block, bool verify)
{
    ExecuteCb out;
    bool called = false;
    f.scheduler->executeBlock(std::move(block), verify,
        [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            out.err = std::move(e);
            out.header = std::move(h);
        });
    BOOST_REQUIRE(called);
    return out;
}

bcos::protocol::Block::Ptr assembleBlock(
    Fixture& f, bcos::protocol::BlockHeader::Ptr header, std::vector<bcos::bytes> const& rawTxBytes)
{
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(std::move(header));
    for (auto const& env : rawTxBytes)
    {
        auto fiscoTx = buildFiscoTx(env, f.hashImpl);
        BOOST_REQUIRE(fiscoTx != nullptr);
        block->appendTransaction(fiscoTx);
    }
    return block;
}

/// Probe against the committed parent (matches OpScheduler::coExecuteBlock's forkCommitted),
/// fill the announced header, then executeBlock without commit.
ExecuteCb executeOpBlock(Fixture& f, std::shared_ptr<bcostars::protocol::BlockHeaderImpl> header,
    std::vector<bcos::bytes> const& rawTxBytes, bool verify)
{
    auto view = f.multiLayerStorage.forkCommitted();
    view.newMutable();
    auto const result = runExecutionProbe(f, view, *header, rawTxBytes);
    BOOST_REQUIRE_EQUAL(result.receipts.size(), rawTxBytes.size());
    fillAnnouncedHeader(header, result);
    return invokeExecute(f, assembleBlock(f, header, rawTxBytes), verify);
}

/// Synchronous callAtBlock driver: returns the callback's (Error, receipt) pair.
std::pair<bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr> callAt(
    Fixture& f, bcos::protocol::Transaction::Ptr tx, bcos::protocol::BlockNumber number)
{
    bcos::Error::Ptr err;
    bcos::protocol::TransactionReceipt::Ptr receipt;
    bool called = false;
    f.scheduler->callAtBlock(
        std::move(tx), number, [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            called = true;
            err = std::move(e);
            receipt = std::move(r);
        });
    BOOST_REQUIRE(called);
    return {std::move(err), std::move(receipt)};
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSuite)

/// After OpScheduler executeBlock + commitBlock, 7 SYS tables are persisted
/// (SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER / SYS_CURRENT_STATE /
/// SYS_NUMBER_2_TXS / SYS_HASH_2_RECEIPT / SYS_HASH_2_TX). OP block execution/commit is genuinely
/// persisted via OpScheduler (not a refused stub).
BOOST_AUTO_TEST_CASE(CommitPersistsSevenLedgerTables)
{
    Fixture f;

    // Corpus envelopes (same as ExecutesMinimalOpBlockEqualToDirectRouteB): deposit + eip1559.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    std::vector<bcos::bytes> rawTxBytes{depEnv, eipEnvBytes};

    auto header = makeHeader();

    // The execution probe yields the real commitments; back-fill the announced header (so verify's
    // six-field comparison passes). Same shared path as OpScheduler::execute (preBlockOpSteps →
    // SchedulerSerialImpl → finalizeOpBlockResult), so the full executeBlock's verify passes.
    auto viewA = f.multiLayerStorage.fork();
    viewA.newMutable();
    bcos::evm::engine::OpExecuteBlockResult resultA =
        runExecutionProbe(f, viewA, *header, rawTxBytes);
    BOOST_REQUIRE_EQUAL(resultA.receipts.size(), rawTxBytes.size());
    fillAnnouncedHeader(header, resultA);

    // Block assembly (full envelope).
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    // executeBlock → commitBlock (slot-3 driving).
    bcos::Error::Ptr execErr;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            execErr = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        execErr == nullptr, "executeBlock failed: " << (execErr ? execErr->errorMessage() : ""));
    BOOST_REQUIRE(executedHeader != nullptr);

    // O1 regression: the RPC block-number push channel. Install a counting notifier via the
    // composition-root setter; it must fire exactly once, with the committed number, after a
    // VALID commit (and must NOT have fired before commitBlock).
    bcos::protocol::BlockNumber notifiedNumber = -1;
    int notifyCount = 0;
    f.scheduler->setBlockNumberNotifier([&](bcos::protocol::BlockNumber number) {
        ++notifyCount;
        notifiedNumber = number;
    });
    BOOST_CHECK_EQUAL(notifyCount, 0);

    bcos::Error::Ptr commitErr;
    called = false;
    f.scheduler->commitBlock(
        executedHeader, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(commitErr == nullptr,
        "commitBlock failed: " << (commitErr ? commitErr->errorMessage() : ""));
    BOOST_CHECK_EQUAL(notifyCount, 1);
    BOOST_CHECK_EQUAL(notifiedNumber, header->number());

    // ── 7-table persistence assertions ──
    auto const blockNumberStr = boost::lexical_cast<std::string>(header->number());
    auto& hashImpl = *f.blockFactory->cryptoSuite()->hashImpl();
    auto view = f.multiLayerStorage.fork();
    const auto expectedBlockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    // 1. SYS_NUMBER_2_HASH[number] = blockHash (announced header opHeaderHash, commit hook key).
    auto number2Hash = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Hash.has_value(), "SYS_NUMBER_2_HASH must be written");
    {
        auto const& stored = number2Hash->get();
        BOOST_REQUIRE_EQUAL(stored.size(), size_t(32));
        // Compare as hex: the entry stores raw bytes as char (signed on AppleClang), so a
        // byte-wise std::equal against h256's unsigned bytes fails for high bytes (>= 0x80).
        BOOST_CHECK_EQUAL(bcos::toHex(stored), expectedBlockHash.hex());
    }

    // 2. SYS_HASH_2_NUMBER[blockHash] = number.
    auto hash2Number = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_NUMBER,
                                          bcos::concepts::bytebuffer::toView(expectedBlockHash)}));
    BOOST_REQUIRE_MESSAGE(hash2Number.has_value(), "SYS_HASH_2_NUMBER must be written");
    BOOST_CHECK_EQUAL(std::string(hash2Number->get()), blockNumberStr);

    // 3. SYS_NUMBER_2_BLOCK_HEADER[number] = tars header (non-empty).
    auto headerEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(headerEntry.has_value(), "SYS_NUMBER_2_BLOCK_HEADER must be written");
    BOOST_CHECK(!headerEntry->get().empty());

    // 4. SYS_CURRENT_STATE[SYS_KEY_CURRENT_NUMBER] = number (head advanced).
    auto currentState = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER}));
    BOOST_REQUIRE_MESSAGE(currentState.has_value(), "SYS_CURRENT_STATE head must advance");
    BOOST_CHECK_EQUAL(std::string(currentState->get()), blockNumberStr);

    // 5. SYS_NUMBER_2_TXS[number] = tx metadata (SEV-10's 7th table).
    auto number2Txs = bcos::task::syncWait(
        bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr}));
    BOOST_REQUIRE_MESSAGE(number2Txs.has_value(), "SYS_NUMBER_2_TXS (SEV-10) must be written");
    BOOST_CHECK(!number2Txs->get().empty());

    // 6/7. SYS_HASH_2_RECEIPT + SYS_HASH_2_TX per tx.
    for (auto const& env : rawTxBytes)
    {
        const auto txHash = hashImpl.hash(env);
        auto receiptEntry = bcos::task::syncWait(
            bcos::storage2::readOne(view, StateKey{bcos::ledger::SYS_HASH_2_RECEIPT,
                                              bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            receiptEntry.has_value(), "SYS_HASH_2_RECEIPT must be written for tx " << txHash.hex());
        auto txEntry = bcos::task::syncWait(bcos::storage2::readOne(view,
            StateKey{bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)}));
        BOOST_REQUIRE_MESSAGE(
            txEntry.has_value(), "SYS_HASH_2_TX must be written for tx " << txHash.hex());
    }

    // OP never writes SYS_BLOCK_NUMBER_2_NONCES (prewriteBlockToBuffer writeNonces=false) -
    // regression guard against the commit hook unexpectedly writing the nonce table.
    auto noncesEntry = bcos::task::syncWait(bcos::storage2::readOne(
        view, StateKey{bcos::ledger::SYS_BLOCK_NUMBER_2_NONCES, blockNumberStr}));
    BOOST_CHECK_MESSAGE(!noncesEntry.has_value(),
        "SYS_BLOCK_NUMBER_2_NONCES must NOT be written for OP commits (writeNonces=false)");
}

// ── RPC-face case migration (verbatim from the deleted OpBlockSchedulerTest; driven object
//    changed to OpScheduler — call/getCode/status/reset inherited) ──

/// Skeleton defaults to no-op status/reset (same semantics as OpBlockScheduler).
BOOST_AUTO_TEST_CASE(StatusAndResetNoOp)
{
    Fixture f;
    f.scheduler->status([&](bcos::Error::Ptr err, bcos::protocol::Session::ConstPtr) {
        BOOST_REQUIRE(err == nullptr);
    });
    f.scheduler->reset([&](bcos::Error::Ptr err) { BOOST_REQUIRE(err == nullptr); });
}

/// Pending-slot state machine on the scheduler (not just classifyPendingConflict):
/// RefuseOtherHeight, KeepProbe, ReplaceSameHeight + popFront, reset watermark restore.
BOOST_AUTO_TEST_CASE(PendingSlotStateMachine)
{
    Fixture f;
    auto depEnv = encodeDepositEnvelope(makeDeposit());
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    auto dep2a = depositEnvWithSource(
        0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_bytes32);
    auto depProbe = depositEnvWithSource(
        0xbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb_bytes32);
    auto dep2b = depositEnvWithSource(
        0xcccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc_bytes32);
    auto dep3 = depositEnvWithSource(
        0xdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd_bytes32);

    driveOpBlock(f, makeHeader(), {depEnv, eipEnvBytes});

    auto siblingTip = invokeExecute(
        f, assembleBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv}), /*verify=*/true);
    BOOST_REQUIRE(siblingTip.err != nullptr);
    BOOST_CHECK_EQUAL(
        siblingTip.err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidBlockNumber);
    BOOST_CHECK_MESSAGE(
        siblingTip.err->errorMessage().find("sibling of the committed tip") != std::string::npos,
        "committed-tip sibling must fail closed, got: " << siblingTip.err->errorMessage());

    auto block2a =
        executeOpBlock(f, makeHeaderAt(2, bcos::u256(2'000'000'000)), {dep2a}, /*verify=*/true);
    BOOST_REQUIRE_MESSAGE(block2a.err == nullptr,
        "execute block 2a: " << (block2a.err ? block2a.err->errorMessage() : ""));
    BOOST_REQUIRE(block2a.header != nullptr);

    // (a) another height while pending is occupied.
    auto refused = invokeExecute(
        f, assembleBlock(f, makeHeaderAt(3, bcos::u256(3'000'000'000)), {depEnv}), /*verify=*/true);
    BOOST_REQUIRE(refused.err != nullptr);
    BOOST_CHECK_EQUAL(
        refused.err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK_MESSAGE(
        refused.err->errorMessage().find("Uncommitted pending block 2") != std::string::npos,
        "RefuseOtherHeight must pin pending height 2, got: " << refused.err->errorMessage());

    // KeepProbe: verify=false sibling at the pending height must not drop the slot.
    auto probe =
        executeOpBlock(f, makeHeaderAt(2, bcos::u256(2'000'000'000)), {depProbe}, /*verify=*/false);
    BOOST_REQUIRE_MESSAGE(probe.err == nullptr,
        "KeepProbe execute: " << (probe.err ? probe.err->errorMessage() : ""));
    auto stillRefused = invokeExecute(
        f, assembleBlock(f, makeHeaderAt(3, bcos::u256(3'000'000'000)), {depEnv}), /*verify=*/true);
    BOOST_REQUIRE(stillRefused.err != nullptr);
    BOOST_CHECK_EQUAL(
        stillRefused.err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK_MESSAGE(
        stillRefused.err->errorMessage().find("Uncommitted pending block 2") != std::string::npos,
        "KeepProbe must leave pending 2 occupied, got: " << stillRefused.err->errorMessage());

    // (b) verify=true replace at height 2; commit must find exactly one pushed layer.
    int txNotify = 0;
    f.scheduler->setTransactionNotifier(
        [&](bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
            std::function<void(bcos::Error::Ptr)> cb) {
            ++txNotify;
            cb(nullptr);
        });
    auto block2b =
        executeOpBlock(f, makeHeaderAt(2, bcos::u256(2'000'000'000)), {dep2b}, /*verify=*/true);
    BOOST_REQUIRE_MESSAGE(block2b.err == nullptr,
        "ReplaceSameHeight execute: " << (block2b.err ? block2b.err->errorMessage() : ""));
    BOOST_REQUIRE(block2b.header != nullptr);

    // Committing the replaced block's header must be refused: the slot now holds block 2b,
    // whose announced hash differs from block 2a's.
    bcos::Error::Ptr staleCommitErr;
    bool staleCalled = false;
    f.scheduler->commitBlock(
        block2a.header, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            staleCalled = true;
            staleCommitErr = std::move(e);
        });
    BOOST_REQUIRE(staleCalled);
    BOOST_REQUIRE(staleCommitErr != nullptr);
    BOOST_CHECK_EQUAL(
        staleCommitErr->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidBlockNumber);
    BOOST_CHECK_MESSAGE(staleCommitErr->errorMessage().find("does not match the announced block") !=
                            std::string::npos,
        "stale-header commit must be refused by the announced-hash binding, got: "
            << staleCommitErr->errorMessage());

    bcos::Error::Ptr commitErr;
    bool called = false;
    f.scheduler->commitBlock(
        block2b.header, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        commitErr == nullptr, "replace commit must merge the single remaining layer: "
                                  << (commitErr ? commitErr->errorMessage() : ""));
    BOOST_CHECK_EQUAL(txNotify, 1);

    // Occupied pending at 3, then reset restores lastExecuted to lastCommitted (2).
    auto block3 =
        executeOpBlock(f, makeHeaderAt(3, bcos::u256(3'000'000'000)), {dep3}, /*verify=*/true);
    BOOST_REQUIRE_MESSAGE(block3.err == nullptr,
        "execute block 3: " << (block3.err ? block3.err->errorMessage() : ""));
    f.scheduler->reset([&](bcos::Error::Ptr err) { BOOST_REQUIRE(err == nullptr); });

    // (c) non-contiguous execute after reset.
    auto discontinuous = invokeExecute(
        f, assembleBlock(f, makeHeaderAt(5, bcos::u256(5'000'000'000)), {depEnv}), /*verify=*/true);
    BOOST_REQUIRE(discontinuous.err != nullptr);
    BOOST_CHECK_EQUAL(
        discontinuous.err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidBlockNumber);
    BOOST_CHECK_MESSAGE(discontinuous.err->errorMessage().find("expect: 3") != std::string::npos &&
                            discontinuous.err->errorMessage().find("input: 5") != std::string::npos,
        "reset must restore lastExecuted to committed tip 2, got: "
            << discontinuous.err->errorMessage());
}

/// execute-only construction (null ledger) must not deref; commit returns InvalidStatus.
BOOST_AUTO_TEST_CASE(CommitWithoutLedgerReturnsInvalidStatus)
{
    Fixture f;
    auto execOnly =
        std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(f.receiptFactory, f.hashImpl,
            kChainId, f.forkFlags, f.blockFactory, f.multiLayerStorage, nullptr, f.ioServicePool);
    auto saved = f.scheduler;
    f.scheduler = execOnly;

    auto depEnv = encodeDepositEnvelope(makeDeposit());
    auto executed = executeOpBlock(f, makeHeader(), {depEnv}, /*verify=*/true);
    BOOST_REQUIRE_MESSAGE(executed.err == nullptr,
        "execute-only executeBlock: " << (executed.err ? executed.err->errorMessage() : ""));
    BOOST_REQUIRE(executed.header != nullptr);

    bcos::Error::Ptr commitErr;
    bool called = false;
    execOnly->commitBlock(
        executed.header, [&](bcos::Error::Ptr e, bcos::ledger::LedgerConfig::Ptr) {
            called = true;
            commitErr = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(commitErr != nullptr);
    BOOST_CHECK_EQUAL(commitErr->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK_MESSAGE(
        commitErr->errorMessage().find("execute-only construction") != std::string::npos,
        "null-ledger commit must name execute-only construction, got: "
            << commitErr->errorMessage());
    f.scheduler = std::move(saved);
}

/// Unknown address → empty code, no error (getCode only reads features, never calls
/// getLedgerConfig; the OP header's empty dataHash doesn't touch BlockHeader::hash()).
BOOST_AUTO_TEST_CASE(GetCodeEmpty)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    bool called = false;
    f.scheduler->getCode(
        "0x0000000000000000000000000000000000000001", [&](bcos::Error::Ptr err, bcos::bytes code) {
            called = true;
            BOOST_REQUIRE(err == nullptr);
            BOOST_REQUIRE(code.empty());
        });
    BOOST_REQUIRE(called);
}

/// Invalid call (maxFeePerGas=1 < baseFee(1e9)) → JSON-RPC Error, never a status-0 receipt.
/// call() classifies the validation fault (OpConsensusError → OpConsensusRejected) and returns
/// rpcSafeReason ("consensus rejection"); the evmone detail is only in the node log.
BOOST_AUTO_TEST_CASE(CallInvalidReturnsError)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    auto tx = buildWeb3Tx(/*maxFeePerGas=*/1, /*maxPriorityFeePerGas=*/0,
        bcos::Address("0x811a752c8cd697e3cb27279c330ed1ada745a8d7"), kCallSender,
        /*withEnvelope=*/false);
    bool called = false;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr err, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(err != nullptr);  // Error (JSON-RPC), never a status-0 receipt
            // maxFeePerGas=1 below the block base fee → FEE_CAP_LESS_THAN_BLOCKS. The
            // OpConsensusError is classified by the call path's exception ladder (round-3 F3
            // made the catch(...) arm classify like the catch(std::exception) arm already did).
            BOOST_CHECK_EQUAL(
                err->errorCode(), (int)bcos::scheduler::SchedulerError::OpConsensusRejected);
            const auto msg = err->errorMessage();
            BOOST_CHECK_MESSAGE(msg.find("consensus rejection") != std::string::npos,
                "invalid call must not return a status-0 receipt, got: " << msg);
        });
    BOOST_REQUIRE(called);
}

/// Scheduler-level call-chain (OpScheduler::call → coCallLatest → buildOpBlockInfo) baseFee
/// injection cross-check: maxPriorityFeePerGas=0 (EIP-1559, BCOS2Evmone access_list override not
/// triggered) → effectiveGasPrice == base_fee + min(0, maxFee-base_fee) == base_fee exactly.
/// Pre-fix buildOpBlockInfo injected base_fee=0 → egp "0x0"; post-fix the header baseFee(1e9)
/// shines through — proving buildOpBlockInfo's baseFee fix takes effect on the scheduler-level
/// call chain.
BOOST_AUTO_TEST_CASE(CallHappyPathInjectsRealBaseFee)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);

    auto tx = buildWeb3Tx(
        /*maxFeePerGas=*/bcos::u256(30'000'000'000ULL), /*maxPriorityFeePerGas=*/0);

    bcos::protocol::TransactionReceipt::Ptr got;
    bcos::Error::Ptr err;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            err = std::move(e);
            got = std::move(r);
        });
    BOOST_REQUIRE_MESSAGE(
        err == nullptr, "eth_call must succeed, got error: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(got != nullptr);
    // effectiveGasPrice is a hex string ("0x...", TransactionReceipt.h:75). Parse to u256 and
    // assert
    // == header baseFee(1e9) — exact (EIP-1559 + maxPriority=0 → effectiveGasPrice == baseFee).
    const auto egp = bcos::u256(std::string(got->effectiveGasPrice()));
    const auto baseFee = bcos::u256(1'000'000'000);
    BOOST_CHECK_MESSAGE(
        egp == baseFee, "effectiveGasPrice " << egp << " must equal header baseFee " << baseFee);
}

/// execute hook throws OpConsensusError (the 0x03 envelope is deterministically thrown by the
/// type-byte classification) → skeleton classify → Error code == OpConsensusRejected.
BOOST_AUTO_TEST_CASE(ConsensusRejectionClassifiedAsOpConsensusRejected)
{
    Fixture f;

    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(makeHeader());
    auto badTx = buildUnsupportedTypeTx();
    BOOST_REQUIRE(badTx != nullptr);
    block->appendTransaction(badTx);

    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpConsensusRejected);
    BOOST_CHECK_MESSAGE(err->errorMessage().find("0x03") != std::string::npos,
        "0x03 rejection must name the type byte, got: " << err->errorMessage());
    BOOST_CHECK(executedHeader == nullptr);
}

/// Three-way classification direct call: OpConsensusError→OpConsensusRejected /
/// OpStorageError→OpStorageFault / other→Unknown.
BOOST_AUTO_TEST_CASE(ClassifyExceptionMapping)
{
    Fixture f;

    auto consensus = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::OpConsensusError{"block-level consensus"}));
    BOOST_CHECK_EQUAL(consensus, bcos::scheduler::SchedulerError::OpConsensusRejected);

    auto storage = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::evm::engine::OpStorageError{"ledger bridge poison"}));
    BOOST_CHECK_EQUAL(storage, bcos::scheduler::SchedulerError::OpStorageFault);

    // mpt read-path faults (escaped Storage2State's poison ladder) are storage faults too:
    // a missing referenced node / an undecodable persisted node both mean the trie rows
    // under the pinned root are unreadable, never a generic UnknownError.
    auto missingNode = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::ledger::mpt::MPTInvariantViolation{}));
    BOOST_CHECK_EQUAL(missingNode, bcos::scheduler::SchedulerError::OpStorageFault);

    auto corruptNode = f.scheduler->classifyException(
        std::make_exception_ptr(bcos::ledger::mpt::MPTDecodeError{}));
    BOOST_CHECK_EQUAL(corruptNode, bcos::scheduler::SchedulerError::OpStorageFault);

    auto unknown = f.scheduler->classifyException(
        std::make_exception_ptr(std::runtime_error{"generic ethereum-mode fault"}));
    BOOST_CHECK_EQUAL(unknown, bcos::scheduler::SchedulerError::UnknownError);
}

/// A storage-read fault during block execution rejects the whole block as OpStorageFault: the
/// per-tx Storage2State instances share the block error slot with the finalize bridge (Part 2 of
/// the StorageStateView→Storage2State merge), so a corrupt row discovered at finalize
/// (visitAccounts → fetchAllStorage length check) poisons the shared sink and
/// finalizeOpBlockResult throws OpStorageError — classified OpStorageFault, never a consensus
/// INVALID or an UnknownError.
BOOST_AUTO_TEST_CASE(StorageReadFaultRejectsBlockAsStorageFault)
{
    Fixture f;
    constexpr evmc::address kPoisonAddr = 0x00000000000000000000000000000000deadc0de_address;
    seedCorruptAccount(f.multiLayerStorage, kPoisonAddr, f.hashImpl);

    // The minimal OP block from CommitPersistsSevenLedgerTables: L1 attributes deposit + one
    // eip1559 transfer. Neither tx touches the corrupt account — it is only reached by the
    // finalize bridge's visitAccounts.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());

    auto header = makeHeader();
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(err != nullptr);
    // Corrupt row → fetchAllStorage length_error → poisoned shared sink → OpStorageError,
    // classified OpStorageFault (not a consensus INVALID, not an UnknownError).
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
    BOOST_CHECK(executedHeader == nullptr);
    // The message names the poison cause (diagnostic_information preserves what()).
    BOOST_CHECK(err->errorMessage().find("poisoned") != std::string::npos);
}

// Round-3 F1: a storage fault under the TX SENDER surfaces at the VALIDATION stage (m_prepare's
// opValidate reads the sender account; Storage2State swallows the fault into the shared slot and
// returns a default, so validation fails as an insufficient-funds-style OpConsensusError). The
// execute() catch ladder must reclassify that as OpStorageError — the same treatment coCallOnView
// already gives every exception type on the eth_call path — not report the announced payload
// INVALID on a local disk fault.
BOOST_AUTO_TEST_CASE(SenderAccountFaultRejectsAsStorageFaultNotConsensus)
{
    Fixture f;
    // Corrupt the sender's NONCE row: fetchAccount's intx::from_string throws on the 4-byte
    // "abcd" value → poison → get_account returns nullopt → validation fails as "insufficient
    // funds" (OpConsensusError). Before the fix, execute()'s catch(OpConsensusError) rethrew
    // without consulting the slot, so the EL would classify the fault as a consensus rejection.
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::storage::Entry e;
        e.set(std::string("abcd"));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::evm::evmstate::accountTableName(
                         bcos::evm::engine::detail::toEvmcAddress(kSender)),
                std::string(bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE)},
            std::move(e)));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }

    // Minimal OP block: L1 attributes deposit + one eip1559 transfer whose sender is kSender
    // (buildFiscoTx forceSenders kSender for non-deposit envelopes).
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());

    auto header = makeHeader();
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto depFiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    auto eipFiscoTx = buildFiscoTx(eipEnvBytes, f.hashImpl);
    BOOST_REQUIRE(depFiscoTx != nullptr);
    BOOST_REQUIRE(eipFiscoTx != nullptr);
    block->appendTransaction(depFiscoTx);
    block->appendTransaction(eipFiscoTx);

    bcos::Error::Ptr err;
    bcos::protocol::BlockHeader::Ptr executedHeader;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr h, bool) {
            called = true;
            err = std::move(e);
            executedHeader = std::move(h);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE(err != nullptr);
    // Validation-stage poison → OpStorageError, classified OpStorageFault — never a consensus
    // INVALID for a local disk fault.
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
    BOOST_CHECK(executedHeader == nullptr);
    BOOST_CHECK(err->errorMessage().find("poisoned") != std::string::npos);
}

// ── Historical eth_call (③ MVP + ①b trie-node persistence) ──────────────────────────

/// N > latest → InvalidBlockNumber with the BaselineScheduler message shape, never a
/// latest-state answer dressed up as a historical one.
BOOST_AUTO_TEST_CASE(CallAtBlockBeyondLatestIsInvalidBlockNumber)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 5);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidBlockNumber);
    BOOST_CHECK(err->errorMessage().find("does not exist") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// N < 0 → the same InvalidBlockNumber refusal as beyond-latest (the boundary check is
/// `blockNumber < 0 || blockNumber > latestNumber`), never a storage read on a negative height
/// (BaselineScheduler shares this hole; OP closes it explicitly).
BOOST_AUTO_TEST_CASE(CallAtBlockNegativeNumberIsInvalidBlockNumber)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), -1);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidBlockNumber);
    BOOST_CHECK(err->errorMessage().find("does not exist") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// N == latest → the coCallLatest fast path: identical observable behaviour to call()
/// (effectiveGasPrice == header baseFee, the CallHappyPathInjectsRealBaseFee assertion).
BOOST_AUTO_TEST_CASE(CallAtBlockLatestEqualsLatestCall)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE_MESSAGE(err == nullptr,
        "callAtBlock at latest must succeed, got: " << (err ? err->errorMessage() : ""));
    BOOST_REQUIRE(receipt != nullptr);
    const auto egp = bcos::u256(std::string(receipt->effectiveGasPrice()));
    BOOST_CHECK_MESSAGE(egp == bcos::u256(1'000'000'000),
        "effectiveGasPrice " << egp << " must equal the latest header baseFee");
}

/// The OQ6 gate: a chain WITHOUT feature_l2_ethereum_compat never committed its complete
/// state to the trie — the historical path refuses loudly (InvalidStatus), never silently
/// downgrading to the latest state.
BOOST_AUTO_TEST_CASE(CallAtBlockRefusesNonScenarioB)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    // Commit block 1 so block 0 is a historical (non-latest) height. No L2 flag seeded.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    driveOpBlock(f, makeHeader(), {depEnv, eipEnvBytes});

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK(err->errorMessage().find("feature_l2_ethereum_compat") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// The empty-root gate: a historical header with stateRoot == 0 (never recorded) must refuse
/// loudly with InvalidStatus — not walk an "empty" trie and answer as if every account were
/// absent. Blocks are committed with the flag OFF (full-rebuild roots, no trie reads needed);
/// the flag is then written retroactively so the historical read at block 0 passes the
/// feature gate and reaches the empty-root gate.
BOOST_AUTO_TEST_CASE(CallAtBlockEmptyStateRootIsInvalidStatus)
{
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());  // zero stateRoot

    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);  // retroactive enable@0

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK(err->errorMessage().find("no MPT state root") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// The M5 node-existence gate: feature ON + a NON-zero historical root whose "/mpt/" rows
/// were never persisted (a chain that committed blocks before node persistence) must refuse
/// loudly with InvalidStatus — never walk into a missing-node trie read. Root-node existence
/// is the O(1) probe; inner-node faults mid-execution are caught by the poison checks
/// (coCallAtBlock throws OpStorageError → OpStorageFault).
BOOST_AUTO_TEST_CASE(CallAtBlockMissingTrieNodesIsInvalidStatus)
{
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    // Blocks 1-2 with the flag OFF: full-rebuild (non-zero) roots on the headers, zero
    // "/mpt/" rows persisted — the shape of a chain that ran before node persistence.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    driveOpBlock(f, makeHeaderAt(2, bcos::u256(2'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);  // retroactive enable@0

    // Block 1: historical (latest=2), feature gate passes, root non-zero — M5 must fire.
    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 1);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK(err->errorMessage().find("no persisted MPT nodes") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// Feature-gate height semantics, pinned with a non-zero activation: the gate reads features
/// at the PINNED height, so a call at block 0 with the flag activating at block 1 is refused
/// (InvalidStatus). This shape cannot occur in production — the config value is a bool
/// re-stamped to activation 0 at genesis (Ledger.cpp:1813-1825) and a non-zero activation is
/// rejected at boot (validateMPTFlagMatrix, LedgerInitializer.cpp:48), so block 0 is always
/// served on a real L2 chain, matching op-geth — but the gate's per-height semantics are
/// defense-in-depth worth pinning (and they are what make the retroactive enable=0 seeding in
/// the other tests meaningful).
BOOST_AUTO_TEST_CASE(CallAtBlockGenesisRefusedWhenFeatureActivatesAtOne)
{
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    // Block 1 with the flag OFF, then activate the flag AT BLOCK 1 retroactively — active for
    // N >= 1, still inactive at the genesis height the call is pinned to.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage, /*enableNumber=*/1);

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE(err != nullptr);
    BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::InvalidStatus);
    BOOST_CHECK(err->errorMessage().find("feature_l2_ethereum_compat") != std::string::npos);
    BOOST_CHECK(receipt == nullptr);
}

/// The poison tripwire behind M5: root row persisted (M5 passes) but INNER nodes missing —
/// the first trie walk past the root throws inside Storage2State, the catch ladder swallows
/// it into poison, and coCallAtBlock's poison checks must turn that into a loud
/// OpStorageFault, never a status-ok receipt built on zero-value reads. Two genesis accounts
/// guarantee the root is a branch/extension whose children are exactly the missing rows.
BOOST_AUTO_TEST_CASE(CallAtBlockInnerNodeMissingIsStorageFault)
{
    const bcos::Address kContract{"0x3000000000000000000000000000000000000000"};
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedContractWithSlot(f.multiLayerStorage, kContract,
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000a1"},
        f.hashImpl);

    // Persist ONLY the root node row of the genesis trie.
    bcos::h256 genesisRoot;
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
        auto result = collectStateRoot(bridge);
        BOOST_REQUIRE(!bridge.poisoned());
        genesisRoot = detail::toBcosH256(result.root);
        auto const it = result.newNodes.find(genesisRoot);
        BOOST_REQUIRE(it != result.newNodes.end());
        std::vector<std::pair<bcos::h256, bcos::bytes>> rootOnly{{it->first, it->second}};
        bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
        bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, rootOnly));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader(genesisRoot));

    // Block 1 with the flag OFF (full-rebuild root, no trie reads) so genesis becomes a
    // historical height; then flip the flag on retroactively.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE_MESSAGE(err != nullptr,
        "inner-node-missing historical call must fail loudly, not answer from zero values");
    BOOST_TEST_CONTEXT("err message: " << err->errorMessage())
    {
        BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
        // Round-2 F4: the RPC-bound message is generic ("storage fault"); the full diagnostic
        // (which node is missing) goes to the node log, not the RPC response.
        BOOST_CHECK(err->errorMessage().find("storage fault") != std::string::npos);
    }
    BOOST_CHECK(receipt == nullptr);
}

/// The EXECUTION-stage tripwire behind M5 (complement of InnerNodeMissing, which faults at the
/// fee-param stage): persist the genesis trie MINUS the contract's storage sub-trie root row.
/// The account-trie walk (fee params, sender) stays intact, so the fee-stage poison check
/// passes; the getter's first SLOAD walks into the withheld storage root, the executor's
/// internal Storage2State poisons the shared error slot, and coCallOnView's sharedError check
/// turns it into OpStorageFault — never a receipt built on a swallowed zero slot.
BOOST_AUTO_TEST_CASE(CallAtBlockExecutionStageNodeMissingIsStorageFault)
{
    const bcos::Address kContract{"0x3000000000000000000000000000000000000000"};
    const bcos::h256 kV1{"0x00000000000000000000000000000000000000000000000000000000000000a1"};
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedContractWithSlot(f.multiLayerStorage, kContract, kV1, f.hashImpl);

    bcos::h256 genesisRoot;
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::evm::evmstate::Storage2State<ViewType> bridge(view);
        auto result = collectStateRoot(bridge);
        BOOST_REQUIRE(!bridge.poisoned());
        genesisRoot = detail::toBcosH256(result.root);
        // Independently rebuild the contract's single-slot storage trie to name the row to
        // withhold (the account leaf embeds this hash as its storageRoot).
        std::map<evmc::bytes32, evmc::bytes32> contractStorage;
        evmc::bytes32 slotValue{};
        std::memcpy(slotValue.bytes, kV1.data(), sizeof(slotValue.bytes));
        contractStorage.emplace(evmc::bytes32{}, slotValue);
        auto const withheld = collectAccountStorageTrie(contractStorage).root;
        BOOST_REQUIRE_EQUAL(result.newNodes.erase(withheld), 1);
        bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
        bcos::task::syncWait(bcos::ledger::mpt::flushTrieNodes(nodeStorage, result.newNodes));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader(genesisRoot));

    // Block 1 with the flag OFF (full-rebuild root, no trie reads) so genesis becomes a
    // historical height; then flip the flag on retroactively.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);

    auto [err, receipt] =
        callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 0);
    BOOST_REQUIRE_MESSAGE(err != nullptr,
        "execution-stage node-missing historical call must fail loudly, not answer slot 0");
    BOOST_TEST_CONTEXT("err message: " << err->errorMessage())
    {
        BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
        // Round-2 F4: generic RPC-bound message; the missing-node detail is in the node log.
        BOOST_CHECK(err->errorMessage().find("storage fault") != std::string::npos);
    }
    BOOST_CHECK(receipt == nullptr);
}

/// A CORRUPT (undecodable) persisted root node: M5's existence probe passes (the row is there),
/// but the first trie decode throws MPTDecodeError — classified OpStorageFault either via the
/// Storage2State poison ladder (fee-param stage) or classifyException's mpt tier
/// (ClassifyExceptionMapping), never a zero-state answer.
BOOST_AUTO_TEST_CASE(CallAtBlockCorruptTrieNodeIsStorageFault)
{
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    auto const genesisRoot = computeAndPersistGenesisTrie(f.multiLayerStorage);
    // Overwrite the root node row with bytes that are not a valid MPT node RLP (0xde claims a
    // 30-byte (0x1e) list payload that is not there).
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::storage::Entry e;
        e.set(bcos::bytes{0xde, 0xad, 0xbe, 0xef});
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, bcos::storage2::mptNodeStateKey(genesisRoot), std::move(e)));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader(genesisRoot));

    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);

    auto [err, receipt] = callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0)), 0);
    BOOST_REQUIRE_MESSAGE(err != nullptr,
        "corrupt-root historical call must fail loudly, not decode garbage as an empty trie");
    BOOST_TEST_CONTEXT("err message: " << err->errorMessage())
    {
        BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
        BOOST_CHECK(err->errorMessage().find("storage fault") != std::string::npos);
    }
    BOOST_CHECK(receipt == nullptr);
}

/// The latest-path poison tripwire (coCallLatest shares coCallOnView with the historical path —
/// round-2 F1): a wrong-length slot row at the call target poisons the executor's internal
/// Storage2State during the getter's SLOAD, the sharedError check throws, and call()'s catch
/// returns OpStorageFault ("storage fault", round-4 F1) instead of a status-ok receipt on zero
/// values.
BOOST_AUTO_TEST_CASE(CallLatestStorageReadFaultFailsLoudly)
{
    const bcos::Address kContract{"0x3000000000000000000000000000000000000000"};
    Fixture f;
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedContractWithSlot(f.multiLayerStorage, kContract,
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000a1"},
        f.hashImpl);
    // Corrupt slot 0 in place (same write shape as seedCorruptAccount): fetchStorage's length
    // check (Storage2State.h:748) throws on the 4-byte value.
    {
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        bcos::storage::Entry e;
        e.set(std::string("abcd"));
        evmc::address contractEvmc{};
        std::memcpy(contractEvmc.bytes, kContract.data(), sizeof(contractEvmc.bytes));
        bcos::task::syncWait(bcos::storage2::writeOne(view,
            StateKey{bcos::evm::evmstate::accountTableName(contractEvmc), std::string(32, '\x00')},
            std::move(e)));
        bcos::task::syncWait(f.multiLayerStorage.mergeView(std::move(view)));
    }

    bcos::Error::Ptr err;
    bcos::protocol::TransactionReceipt::Ptr receipt;
    bool called = false;
    f.scheduler->call(buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract),
        [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            called = true;
            err = std::move(e);
            receipt = std::move(r);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(
        err != nullptr, "latest call on a corrupt slot must fail loudly, not return slot 0 = 0");
    BOOST_TEST_CONTEXT("err message: " << err->errorMessage())
    {
        // Round-4 F1: call() no longer has a dedicated OpStorageError clause — the fault
        // classifies as OpStorageFault ("storage fault") exactly like callAtBlock, so latest
        // and historical calls report the same node-local fault identically.
        BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
        BOOST_CHECK(err->errorMessage().find("storage fault") != std::string::npos);
    }
    BOOST_CHECK(receipt == nullptr);
}

/// The end-to-end ③+①a case: genesis + three blocks committed through the real OpScheduler
/// (①a incremental buildAndCollect computes each block's stateRoot from its delta and flushes
/// the new trie nodes), then historical calls at each height against a contract whose slot 0
/// changes in block 2 (a setter deposit):
///  - the contract's getter (SLOAD slot 0 → RETURN) answers V1 at blocks 0/1 and V2 at
///    blocks 2/3 — the receipt output IS the stored value at the pinned root, so a wrong
///    (latest-state) read flips the bytes, not just a status code;
///  - each height's call answers with ITS header's fee context (egp == baseFee@N);
///  - block 0 (genesis) is queryable through its persisted genesis trie.
/// (A balance-based discriminator is impossible here: the call path runs opValidate with
/// skipBalanceCheck=true, so an absent-at-N account does not fail validation.)
BOOST_AUTO_TEST_CASE(CallAtBlockServesEachHeightWithOpSemantics)
{
    const bcos::Address kContract{"0x3000000000000000000000000000000000000000"};
    const bcos::h256 kV1{"0x00000000000000000000000000000000000000000000000000000000000000a1"};
    const bcos::h256 kV2{"0x00000000000000000000000000000000000000000000000000000000000000b2"};
    auto outputIs = [](bcos::protocol::TransactionReceipt::Ptr const& receipt,
                        bcos::h256 const& expected) {
        auto const out = receipt->output();
        bcos::bytes const outBytes(out.begin(), out.end());
        bcos::bytes const expectedBytes(expected.data(), expected.data() + bcos::h256::SIZE);
        return outBytes == expectedBytes;
    };

    Fixture f;
    seedL2CompatFeature(f.multiLayerStorage);
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedContractWithSlot(f.multiLayerStorage, kContract, kV1, f.hashImpl);

    // Scenario-B genesis: persist the genesis trie nodes and stamp the root on the header
    // (production: Ledger::buildGenesisBlock's l2EthereumCompat import).
    auto const genesisRoot = computeAndPersistGenesisTrie(f.multiLayerStorage);
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader(genesisRoot));

    // Block 1 (baseFee 1e9): deposit + eip1559 transfer. Block 2 (baseFee 2e9) carries a
    // SETTER deposit flipping slot 0 to V2 inside the block delta (①a forbids out-of-band
    // writes), block 3 (baseFee 3e9) makes block 2 a HISTORICAL height.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());
    bcos::bytes setterEnv = makeSetterDepositEnvelope(kContract, kV2);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv, eipEnvBytes});
    driveOpBlock(f, makeHeaderAt(2, bcos::u256(2'000'000'000)), {depEnv, setterEnv});
    driveOpBlock(f, makeHeaderAt(3, bcos::u256(3'000'000'000)), {depEnv});

    // Block 0 (genesis): the contract is in the genesis trie with slot=V1. Note blocks 0 and 1
    // expose the SAME observables here (V1, baseFee 1e9) — block 0's discriminating power comes
    // from err==nullptr while being a NON-latest height served through the genesis trie, not
    // from a distinct value.
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 0);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "genesis historical call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        BOOST_CHECK_MESSAGE(outputIs(receipt, kV1), "genesis call must read slot=V1");
        const auto egp = bcos::u256(std::string(receipt->effectiveGasPrice()));
        BOOST_CHECK_MESSAGE(egp == bcos::u256(1'000'000'000),
            "genesis call must see the genesis header baseFee, got " << egp);
    }

    // Block 1: still V1 (the setter deposit rides block 2, not block 1).
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 1);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "block-1 historical call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        BOOST_CHECK_MESSAGE(outputIs(receipt, kV1),
            "block-1 call must read slot=V1 (the setter deposit lands in block 2)");
        const auto egp = bcos::u256(std::string(receipt->effectiveGasPrice()));
        BOOST_CHECK_MESSAGE(egp == bcos::u256(1'000'000'000),
            "block-1 call must see block 1's baseFee, got " << egp);
    }

    // Block 2: V2 through the block-2 MPT (block 3 is the latest, so this is NOT the
    // coCallLatest fast path — the read resolved along block 2's persisted trie nodes).
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 2);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "block-2 historical call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        BOOST_CHECK_MESSAGE(outputIs(receipt, kV2), "block-2 call must read slot=V2");
        const auto egp = bcos::u256(std::string(receipt->effectiveGasPrice()));
        BOOST_CHECK_MESSAGE(egp == bcos::u256(2'000'000'000),
            "block-2 call must see block 2's baseFee, got " << egp);
    }

    // Block 3 IS the latest: the fast path serves the same V2 from the flat state, with block
    // 3's own baseFee (3e9) as the fee context.
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 3);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "block-3 (latest) call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        BOOST_CHECK_MESSAGE(outputIs(receipt, kV2), "latest call must read slot=V2");
        const auto egp = bcos::u256(std::string(receipt->effectiveGasPrice()));
        BOOST_CHECK_MESSAGE(egp == bcos::u256(3'000'000'000),
            "latest call must see block 3's baseFee, got " << egp);
    }
}

// ── ①a incremental MPT cross-check ───────────────────────────────────────────────────

/// The ⑤ loud-halt half of the ①a contract (BaselineScheduler.h:505-519 shape): a chain that
/// committed scenario-B blocks BEFORE node persistence existed (feature flipped on mid-chain)
/// has a non-zero parent root with zero "/mpt/" rows — buildAndCollect must throw, wrapped as
/// OpStorageError naming the cause ("no persisted trie nodes"), classified OpStorageFault.
/// Rebuilding over an empty trie instead would fork the stateRoot away from the announced
/// header; halting is the intended behaviour.
BOOST_AUTO_TEST_CASE(ExecuteFailsLoudlyWhenParentTrieNodesMissing)
{
    Fixture f;
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader());

    // Block 1 with the flag OFF: full-rebuild root on the header, zero "/mpt/" rows persisted.
    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    driveOpBlock(f, makeHeaderAt(1, bcos::u256(1'000'000'000)), {depEnv});
    seedL2CompatFeature(f.multiLayerStorage);  // retroactive enable@0 — the mid-chain shape

    // Block 2 via a custom driver (driveOpBlock REQUIREs success): the probe still succeeds
    // (verify=false → full rebuild, no trie reads), but the canonical executeBlock must die at
    // ⑤ — buildAndCollect finds no persisted nodes under block 1's root.
    auto header = makeHeaderAt(2, bcos::u256(2'000'000'000));
    auto view = f.multiLayerStorage.fork();
    view.newMutable();
    auto const result = runExecutionProbe(f, view, *header, {depEnv});
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 1);
    fillAnnouncedHeader(header, result);
    auto block = f.blockFactory->createBlock();
    block->setBlockHeader(header);
    auto fiscoTx = buildFiscoTx(depEnv, f.hashImpl);
    BOOST_REQUIRE(fiscoTx != nullptr);
    block->appendTransaction(fiscoTx);

    bcos::Error::Ptr err;
    bool called = false;
    f.scheduler->executeBlock(
        block, /*verify=*/true, [&](bcos::Error::Ptr e, bcos::protocol::BlockHeader::Ptr, bool) {
            called = true;
            err = std::move(e);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(err != nullptr,
        "mid-chain scenario-B activation must halt the next block loudly, not rebuild over an "
        "empty trie");
    BOOST_TEST_CONTEXT("err message: " << err->errorMessage())
    {
        BOOST_CHECK_EQUAL(err->errorCode(), (int)bcos::scheduler::SchedulerError::OpStorageFault);
        BOOST_CHECK(err->errorMessage().find("no persisted trie nodes") != std::string::npos);
    }
}

/// The ①a gate (design §7): on a chain where EVERY state change flows through a committed
/// block, the incremental build (MPTBuilder::buildAndCollect over the block's delta layer,
/// parent root = previous header's stateRoot) must reproduce the full rebuild's root
/// (stateRootOf via runExecutionProbe) byte-for-byte — otherwise switching OpScheduler to the
/// incremental root would break the verify-time six-field comparison against op-geth. This
/// exercises the riskiest OP write shapes against the scanner's classification rules:
///  - block 1: plain deposit + eip1559 transfer (balance/nonce updates);
///  - block 2: a deposit with mint>0 (mint write shape) and a CREATE deposit (to=nullopt —
///    the new account's s_code_binary row is the content-addressed bypass the scanner must
///    classify, not choke on);
///  - block 3: a setter deposit writing ZERO over the genesis-seeded slot 0 — the
///    slot-tombstone (delete) shape, where incremental/full divergence would hide.
/// Any UnknownAccountRowField / UnexpectedBCOSFieldInL2 surfaces here as a thrown exception.
/// Note the chain deliberately avoids out-of-band flat writes (no overwriteSlot): the
/// incremental contract requires the block delta to be the ONLY change since the parent.
BOOST_AUTO_TEST_CASE(IncrementalMPTRootMatchesFullRebuild)
{
    Fixture f;
    seedL2CompatFeature(f.multiLayerStorage);
    // Round-3 F2: drive execute()'s ①a path with the full-rebuild cross-check ENABLED (the
    // scheduler-side branch at OpScheduler.h:840-860 — shared-error Storage2State, poisoned()
    // check, divergence throw). Default off means nothing else exercises it; the blocks below
    // then assert both that the branch runs and that the incremental root matches.
    f.scheduler->setCrossCheckIncrementalRoot(true);
    fundCallAccount(f.multiLayerStorage, kCallSender, f.hashImpl, bcos::u256(1) << 200);
    const bcos::Address kContract{"0x3000000000000000000000000000000000000000"};
    seedContractWithSlot(f.multiLayerStorage, kContract,
        bcos::h256{"0x00000000000000000000000000000000000000000000000000000000000000a1"},
        f.hashImpl);
    auto const genesisRoot = computeAndPersistGenesisTrie(f.multiLayerStorage);
    seedCallGenesis(f.multiLayerStorage, makeCallGenesisHeader(genesisRoot));

    auto depTx = makeDeposit();
    bcos::bytes depEnv = encodeDepositEnvelope(depTx);
    auto eipEvmcBytes = evmc::from_hex(kEip1559EnvelopeHex).value();
    bcos::bytes eipEnvBytes(eipEvmcBytes.begin(), eipEvmcBytes.end());

    // mint>0 deposit: exercises the mint-balance write shape.
    auto mintDep = makeDeposit();
    mintDep.source_hash =
        0x1111111111111111111111111111111111111111111111111111111111111111_bytes32;
    mintDep.mint = intx::uint256{0xdeaf};
    bcos::bytes mintEnv = encodeDepositEnvelope(mintDep);

    // CREATE deposit (to=nullopt): writes a new account + its s_code_binary row.
    // initcode: CODECOPY 1 byte from offset 12, RETURN it — runtime is the single byte 0x2a.
    auto createDep = makeDeposit();
    createDep.source_hash =
        0x2222222222222222222222222222222222222222222222222222222222222222_bytes32;
    createDep.to = std::nullopt;
    createDep.data = evmc::from_hex("0x6001600c60003960016000f32a").value();
    bcos::bytes createEnv = encodeDepositEnvelope(createDep);

    // Tombstone deposit: SSTORE 0 over the genesis-seeded slot 0 (0xa1) — the delete shape.
    bcos::bytes tombstoneEnv = makeSetterDepositEnvelope(kContract, bcos::h256{});

    bcos::h256 parentRoot = genesisRoot;
    const std::vector<std::pair<bcos::protocol::BlockNumber, std::vector<bcos::bytes>>> blocks{
        {1, {depEnv, eipEnvBytes}},
        {2, {mintEnv, createEnv}},
        {3, {tombstoneEnv}},
    };
    for (auto const& [number, rawTxBytes] : blocks)
    {
        auto header = makeHeaderAt(number, bcos::u256(number) * 1'000'000'000);

        // Probe view: top mutable layer == exactly this block's delta, reads resolve to the
        // committed parent — the two buildAndCollect preconditions (MPTBuilder.h:380-395).
        // Discarded after the check; the chain itself advances through driveOpBlock below.
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        auto const result = runExecutionProbe(f, view, *header, rawTxBytes);
        auto const fullRoot = result.stateRoot;
        BOOST_REQUIRE(fullRoot != bcos::h256{});
        // Positive anchor (round-2 F3): every probe tx must be a SUCCESS — the root comparison
        // below is only meaningful on a healthy block; a silently-reverted probe tx would still
        // produce a matching root and mask a real divergence. (FISCO receipt status: 0 =
        // success.)
        BOOST_REQUIRE_EQUAL(result.receipts.size(), rawTxBytes.size());
        for (auto const& r : result.receipts)
            BOOST_REQUIRE_MESSAGE(r->status() == 0,
                "block " << number << " probe tx reverted (status " << r->status()
                         << ") — the incremental/full root comparison would be vacuous");

        bcos::ledger::mpt::MPTDeltaLayer delta;
        try
        {
            bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
            delta = bcos::task::syncWait(
                bcos::ledger::mpt::buildAndCollect(nodeStorage, parentRoot, view, /*l2Mode=*/true));
        }
        catch (std::exception const& e)
        {
            BOOST_FAIL("buildAndCollect threw at block " << number << ": " << e.what());
        }
        // REQUIRE (not CHECK): on a mismatch the chain state is unrecoverable for the
        // remaining blocks — driveOpBlock would only add a second, redundant six-field
        // failure on top of this one.
        BOOST_REQUIRE_MESSAGE(delta.stateRoot == fullRoot,
            "block " << number << ": incremental root " << delta.stateRoot.hex()
                     << " != full-rebuild root " << fullRoot.hex());
        BOOST_CHECK_MESSAGE(!delta.newNodes.empty(),
            "block " << number << " touched accounts but produced no new trie nodes");

        // execute()'s ①a path commits the incremental root as the header's stateRoot, and
        // the six-field verify holds it equal to the probe's full root — so the next block's
        // parent root is the value both builds just agreed on.
        parentRoot = fullRoot;
        driveOpBlock(f, header, rawTxBytes);
    }

    // Tombstone read-back (round-2 F3): a historical call at block 3 (the tombstone block)
    // must answer ZERO for slot 0 through the block-3 MPT — the delete shape must have removed
    // the slot from the trie, not just from the flat state. Block 4 makes block 3 a historical
    // (non-latest) height so the read resolves along block 3's persisted nodes.
    driveOpBlock(f, makeHeaderAt(4, bcos::u256(4'000'000'000)), {depEnv});
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 3);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "block-3 historical call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        auto const out = receipt->output();
        bcos::bytes const outBytes(out.begin(), out.end());
        bcos::h256 const zeroH256{};
        bcos::bytes const zeroBytes(zeroH256.data(), zeroH256.data() + bcos::h256::SIZE);
        BOOST_CHECK_MESSAGE(outBytes == zeroBytes,
            "block-3 call must read the tombstoned slot as zero (trie-level delete shape)");
    }

    // Block-2 control (round-3 S6): the block-3 zero above could equally be produced by a
    // wrong latest-state pass-through (the latest state has slot 0 deleted too). A block-2
    // call must still answer the pre-tombstone value — only the pinned block-2 trie has it.
    const bcos::h256 kPreTombstone{
        "0x00000000000000000000000000000000000000000000000000000000000000a1"};
    {
        auto [err, receipt] =
            callAt(f, buildWeb3Tx(bcos::u256(30'000'000'000ULL), bcos::u256(0), kContract), 2);
        BOOST_REQUIRE_MESSAGE(
            err == nullptr, "block-2 historical call failed: " << (err ? err->errorMessage() : ""));
        BOOST_REQUIRE(receipt != nullptr);
        BOOST_CHECK_EQUAL(receipt->status(), 0);  // FISCO receipt status: 0 = success
        auto const out = receipt->output();
        bcos::bytes const outBytes(out.begin(), out.end());
        bcos::bytes const v1Bytes(kPreTombstone.data(), kPreTombstone.data() + bcos::h256::SIZE);
        BOOST_CHECK_MESSAGE(outBytes == v1Bytes,
            "block-2 call must read slot 0 as the pre-tombstone value (pinned historical trie, "
            "not a latest-state pass-through)");
    }
}

namespace
{
/// Deterministic corruption marker de.ad.00...00.<tag>: pins "this value differs" without
/// hand-counted hex literals, and names uniquely so rejections can assert the exact field.
bcos::h256 commitmentCorruption(unsigned char tag)
{
    bcos::h256 out{};
    out.data()[0] = 0xde;
    out.data()[1] = 0xad;
    out.data()[bcos::h256::SIZE - 1] = tag;
    return out;
}
}  // namespace

/// N2 regression: with verify=true, tampering exactly ONE back-filled commitment field on the
/// announced header must be rejected with OpConsensusRejected, naming that field
/// ("commitment mismatch on field <name>"). Exercises six discriminating rejections: five names
/// from mismatchedFieldOf's ordered chain (withdrawalsRoot is tampered in VALUE — presence stays
/// equal — so it hits the comparison arm after the dedicated presence gate passes) plus the
/// blobGasUsed guard: pre-Jovian seals omit the field, so announcing a non-zero value is invalid
/// rather than merely unequal to execution.
BOOST_AUTO_TEST_CASE(VerifyRejectsMismatchedAnnouncedCommitments)
{
    using Mutator = void (*)(bcostars::protocol::BlockHeaderImpl&);
    struct Case
    {
        const char* fieldName;
        Mutator mutate;
        // Message fragment each rejection must contain; defaults would all be the commitment
        // prefix, but the blobGasUsed guard throws before any comparison happens.
        std::string expectedFragment;
    };
    Fixture f;
    auto commitmentMsg = [](const char* fieldName) {
        return std::string{"commitment mismatch on field "} + fieldName;
    };
    const std::vector<Case> cases{
        {"stateRoot",
            [](bcostars::protocol::BlockHeaderImpl& h) {
                h.setStateRoot(commitmentCorruption(0xa1));
            },
            {}},
        {"transactionsRoot",
            [](bcostars::protocol::BlockHeaderImpl& h) {
                h.setTxsRoot(commitmentCorruption(0xa2));
            },
            {}},
        {"receiptsRoot",
            [](bcostars::protocol::BlockHeaderImpl& h) {
                h.setReceiptsRoot(commitmentCorruption(0xa3));
            },
            {}},
        {"gasUsed", [](bcostars::protocol::BlockHeaderImpl& h) { h.setGasUsed(h.gasUsed() + 1); },
            {}},
        {"withdrawalsRoot",
            [](bcostars::protocol::BlockHeaderImpl& h) {
                h.setWithdrawalsRoot(commitmentCorruption(0xa4));
            },
            {}},
        {"blobGasUsed",
            [](bcostars::protocol::BlockHeaderImpl& h) { h.setBlobGasUsed(bcos::u256{7}); },
            "must announce blobGasUsed=0"},
    };

    std::vector<bcos::bytes> const rawTxBytes{encodeDepositEnvelope(makeDeposit())};
    for (auto const& [fieldName, mutate, expectedFragment] : cases)
    {
        // Every execute below fails by construction, so nothing ever pushes a view or commits:
        // the committed parent state stays stable across iterations and the probe stays valid.
        auto announced = makeHeader();
        auto view = f.multiLayerStorage.forkCommitted();
        view.newMutable();
        auto const result = runExecutionProbe(f, view, *announced, rawTxBytes);
        BOOST_REQUIRE_EQUAL(result.receipts.size(), rawTxBytes.size());
        fillAnnouncedHeader(announced, result);  // true commitments back-filled...
        mutate(*announced);                      // ...then exactly one field corrupted

        auto cb = invokeExecute(f, assembleBlock(f, announced, rawTxBytes), /*verify=*/true);
        BOOST_CHECK_MESSAGE(
            cb.err != nullptr, fieldName << ": mismatched announcement must be rejected");
        if (cb.err == nullptr)
            continue;
        BOOST_CHECK_EQUAL(
            cb.err->errorCode(), (int)bcos::scheduler::SchedulerError::OpConsensusRejected);
        BOOST_CHECK_MESSAGE(cb.err->errorMessage().find(expectedFragment.empty() ?
                                                            commitmentMsg(fieldName) :
                                                            expectedFragment) != std::string::npos,
            fieldName << ": rejection must name the corrupt field, got: "
                      << cb.err->errorMessage());
    }
}

/// N1 regression: executedHeader must mirror the announced header's six Ethereum metadata
/// fields (coinbase/gasLimit/prevRandao/baseFee/excessBlobGas/parentBeaconBlockRoot).
/// populateBlockHeader copies only the 13 framework fields and the verify gate does not
/// compare these — without this pin, finishExecute silently returned factory defaults.
/// Optional presence is asserted too: a copied-absent value must stay absent, not zeroed.
BOOST_AUTO_TEST_CASE(ExecutedHeaderMirrorsAnnouncedEthMetadataFields)
{
    Fixture f;
    std::vector<bcos::bytes> const rawTxBytes{encodeDepositEnvelope(makeDeposit())};

    auto announced = makeHeader();
    // De-default prevRandao/excessBlobGas so an unmirrored clone cannot pass by coinciding
    // with factory zero-values; the rest of makeHeader already carries distinctive payload.
    bcos::h256 wantPrevRandao{};
    std::memset(wantPrevRandao.data(), 0x10, bcos::h256::SIZE);
    bcos::u256 const wantExcessBlobGas{0x1234};
    announced->setPrevRandao(wantPrevRandao);
    announced->setExcessBlobGas(wantExcessBlobGas);

    auto cb = executeOpBlock(f, announced, rawTxBytes, /*verify=*/true);
    BOOST_REQUIRE_MESSAGE(cb.err == nullptr,
        "verify=true execute must succeed: " << (cb.err ? cb.err->errorMessage() : ""));
    BOOST_REQUIRE(cb.header != nullptr);

    const auto& executed = *cb.header;
    BOOST_CHECK_MESSAGE(
        executed.coinbase() == announced->coinbase(), "executed coinbase must mirror announced");
    BOOST_CHECK_MESSAGE(
        executed.gasLimit() == announced->gasLimit(), "executed gasLimit must mirror announced");
    BOOST_CHECK_MESSAGE(
        executed.prevRandao() == wantPrevRandao, "executed prevRandao must mirror announced");
    BOOST_REQUIRE_MESSAGE(executed.baseFee().has_value(), "baseFee presence must survive");
    BOOST_CHECK(*executed.baseFee() == *announced->baseFee());
    BOOST_REQUIRE_MESSAGE(
        executed.excessBlobGas().has_value(), "excessBlobGas presence must survive");
    BOOST_CHECK(*executed.excessBlobGas() == wantExcessBlobGas);
    BOOST_REQUIRE_MESSAGE(executed.parentBeaconBlockRoot().has_value(),
        "parentBeaconBlockRoot presence must survive");
    BOOST_CHECK(*executed.parentBeaconBlockRoot() == *announced->parentBeaconBlockRoot());
}

BOOST_AUTO_TEST_SUITE_END()
