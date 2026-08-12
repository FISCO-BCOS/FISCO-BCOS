// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpCallSchedulerTest — unit tests for the OP-mode RPC-facing scheduler adapter
// (opstack-executor/OpCallScheduler.h). Focused on what the adapter itself owns:
//  1. the loud refusal of the block-execution methods (executeBlock/commitBlock/
//     preExecuteBlock) — the fix for executor_version>=3 silently saturating to the v2 ethereum
//     scheduler, where those forwarded to the ethereum executor;
//  2. the eth_call error path: a ledger fault (empty storage -> getCurrentBlockNumber -1 ->
//     getBlockData "Wrong argument") surfaces as a callback Error, never a crash or a swallowed
//     wrong answer.
// The full OP eth_call happy path (fee / baseFee / block-hash injection parity with processOpBlock)
// needs a seeded ledger (evmcRevision config + block tables + L1Block slots) and is deferred to a
// follow-up; OpstackExecutor's own execution semantics are covered by OpstackExecutorTest.

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage/Serialize.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/Common.h>
#include <opstack-executor/OpCallScheduler.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace bcos::executor_v1::opstack
{
namespace
{
// Minimal CheckpointStorage stub — same pattern as OpSchedulerImplSmokeTest's per-file local copy
// (do not cross-include another module's test-private header).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    // By value (not `Storage&`): MultiLayerStorage's OpenedStorage must be a VALUE type — a
    // reference OpenedStorage makes the fork view's `BackendStorage*` a pointer-to-reference,
    // which is ill-formed and breaks the ReadableStorage<StateKey> concept the ledger functions
    // (getCurrentBlockNumber/getLedgerConfig/getBlockData) require. MemoryStorage is not
    // copyable, so return a move of the (empty, read-only) initial state; the error-path test
    // never reads through the moved-from backend.
    Storage open() & { return std::move(m_storage); }
    [[noreturn]] Storage& open(CheckpointName const&) & { std::abort(); }
    void createCheckpoint(Storage&, CheckpointName const&) {}
    void deleteCheckpoint(CheckpointName const&) {}
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

using OpCallSchedulerT = OpCallScheduler<MLS>;

OpCallSchedulerT makeScheduler(MLS& storage)
{
    auto blockFactory = makeBlockFactory();
    return OpCallSchedulerT(blockFactory->receiptFactory(), makeCryptoSuite()->hashImpl(), 0x2105,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 1000, .jovianTime = 2000},
        blockFactory, storage);
}

/// A minimal Transaction whose content is never dereferenced on the empty-storage error path.
bcos::protocol::Transaction::Ptr makeAnyTransaction()
{
    auto tarsTx = std::make_shared<bcostars::Transaction>();
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tarsTx]() { return tarsTx.get(); });
}

void putEntry(BackendMemStorage& storage, std::string_view table, std::string_view key,
    std::string const& value)
{
    task::syncWait(bcos::storage2::writeOne(storage,
        bcos::executor_v1::StateKey{std::string(table), std::string(key)},
        bcos::storage::Entry{std::string_view{value}}));
}

/// Seed the minimal OP ledger state coCallLatest reads (into the checkpoint backend, which the
/// fork coCallLatest opens): current_number, the head block header (s_number_2_header) and the
/// SYS_CONFIG rows opValidate's evmcRevision gate needs (executor_version >= 2 so evmc_revision
/// is consumed; prague matches OpForkConfig rev).
void seedHeadBlock(
    BackendMemStorage& storage, bcos::protocol::BlockFactory& blockFactory,
    bcos::protocol::BlockNumber num)
{
    putEntry(storage, bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER,
        std::to_string(num));
    auto header = blockFactory.blockHeaderFactory()->createBlockHeader();
    header->setNumber(num);
    header->setTimestamp(static_cast<int64_t>(num) * 1000 + 1000);  // ms
    header->setGasLimit(bcos::u256(30000000));
    header->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = num - 1, .blockHash = bcos::h256{}});
    bcos::bytes headerBytes;
    header->encode(headerBytes);
    putEntry(storage, bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, std::to_string(num),
        std::string(headerBytes.begin(), headerBytes.end()));
    auto putConfig = [&](std::string_view key, std::string const& value) {
        putEntry(storage, bcos::ledger::SYS_CONFIG, key,
            bcos::storage::serialize::encode<bcos::ledger::SystemConfigEntry>({value, 0}));
    };
    putConfig("executor_version", "3");
    putConfig("evmc_revision", "prague");
}

/// Build an eth_call tx: no nonce field (CallRequest omits it for a plain call), gas=0
/// (CallRequest defaults), EIP-1559 value transfer to an EOA.
bcostars::protocol::TransactionImpl makeCallTx(bcos::bytes const& senderBytes)
{
    auto tarsTx = std::make_shared<bcostars::Transaction>();
    tarsTx->type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tarsTx->web3TypedTxKind = static_cast<tars::Char>(2);  // EIP-1559
    tarsTx->data.nonce = "";
    tarsTx->data.gasLimit = 0;
    tarsTx->data.chainID = "0x2105";
    tarsTx->data.to = "0x000000000000000000000000000000000000dEaD";
    tarsTx->data.value = "0x0";
    tarsTx->data.input = {};
    tarsTx->data.maxFeePerGas = "0x0";
    tarsTx->data.maxPriorityFeePerGas = "0x0";
    auto tx = bcostars::protocol::TransactionImpl([tarsTx]() { return tarsTx.get(); });
    tx.forceSender(senderBytes);
    return tx;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpCallSchedulerSuite)

BOOST_AUTO_TEST_CASE(BlockExecutionMethodsRefuse)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto scheduler = makeScheduler(multiLayerStorage);

    // OP block execution is engine-driven (newPayload -> executeOpBlock); these must fail loudly
    // instead of running any executor silently (the old version-3 saturation path).
    scheduler.executeBlock(
        nullptr, false, [](bcos::Error::Ptr error, bcos::protocol::BlockHeader::Ptr, bool) {
            BOOST_REQUIRE(error != nullptr);
        });
    scheduler.commitBlock(
        nullptr, [](bcos::Error::Ptr error, bcos::ledger::LedgerConfig::Ptr) {
            BOOST_REQUIRE(error != nullptr);
        });
    scheduler.preExecuteBlock(nullptr, false, [](bcos::Error::Ptr error) {
        BOOST_REQUIRE(error != nullptr);
    });
}

BOOST_AUTO_TEST_CASE(CallOnEmptyStorageReturnsError)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto scheduler = makeScheduler(multiLayerStorage);

    // Empty storage: getCurrentBlockNumber -> -1, then getBlockData(-1) throws (Wrong argument).
    // The adapter must surface that as a callback Error — the "storage fault is an Error, not a
    // crash" contract — and must call the callback exactly once.
    bool called = false;
    scheduler.call(makeAnyTransaction(),
        [&called](bcos::Error::Ptr error, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(error != nullptr);
        });
    BOOST_REQUIRE(called);
}

BOOST_AUTO_TEST_CASE(CallWithSeededHeadBlockSucceeds)
{
    // The OP eth_call happy path on a seeded ledger. Regression for four phase-1 real-node
    // findings that each made every eth_call fail or crash:
    //  - SIGSEGV: coCallLatest held `auto const& header = *block->blockHeader()` without owning
    //    the fresh Ptr BlockImpl::blockHeader() returns per call (dangling reference);
    //  - "Invalid argument": opValidate rejected the empty signed envelope of an unsigned call tx;
    //  - "intrinsic gas too low": gas=0 call tx must fall back to blockGasLeft;
    //  - "nonce too low": a sender with an existing nonce must be honoured (op-geth semantics).
    BackendMemStorage backendStorage;
    auto blockFactory = makeBlockFactory();

    // Seed the backend BEFORE constructing the MLS: MultiLayerStorage::fork() references
    // m_latestBackend, which is populated from the checkpoint backend's open() at construction
    // time (TrivialCheckpointStorage::open() MOVES the backend), so anything written to
    // backendStorage after MLS construction is invisible to the fork coCallLatest opens.
    constexpr bcos::protocol::BlockNumber kHead = 10;
    seedHeadBlock(backendStorage, *blockFactory, kHead);
    bcos::bytes senderBytes = bcos::fromHex("6afa9580383E6627dA926B6f6ed9Ab2B9c8cC693");
    // Seed the sender account (table = /apps/ + hex, raw_address=false) with an existing nonce
    // (5) and a non-zero balance, plus the canonical empty-code hash (EIP-3607). coCallLatest
    // reads sender() as raw 20 bytes; the nonce must be honored, not reset to 0.
    {
        auto tableName = std::string(bcos::ledger::SYS_DIRECTORY::USER_APPS) +
                         "6afa9580383e6627da926b6f6ed9ab2b9c8cc693";
        // StorageStateView::getAccount gates on EVMAccount::exists() (SYS_TABLES registration),
        // so the account table must be registered or the sender reads as non-existent (nonce 0).
        putEntry(backendStorage, bcos::ledger::SYS_TABLES, tableName, "value");
        putEntry(backendStorage, tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::BALANCE,
            "1000000000000000000000000000");
        putEntry(backendStorage, tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE, "5");
        // code_hash is stored as the raw 32-byte digest (EVMAccount::codeHash() returns it as
        // bytes; an ASCII hex string would be misread as a 63-byte code hash → "sender not an
        // eoa" under EIP-3607). EMPTY_CODE_HASH of an EOA:
        auto emptyCodeHash = bcos::crypto::HashType(
            "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
        putEntry(backendStorage, tableName, bcos::ledger::ACCOUNT_TABLE_FIELDS::CODE_HASH,
            std::string(reinterpret_cast<const char*>(emptyCodeHash.data()), 32));
    }

    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto scheduler = makeScheduler(multiLayerStorage);

    auto tx = makeCallTx(senderBytes);

    bool called = false;
    bcos::Error::Ptr error;
    bcos::protocol::TransactionReceipt::Ptr receipt;
    scheduler.call(std::make_shared<bcostars::protocol::TransactionImpl>(std::move(tx)),
        [&](bcos::Error::Ptr e, bcos::protocol::TransactionReceipt::Ptr r) {
            called = true;
            error = std::move(e);
            receipt = std::move(r);
        });
    BOOST_REQUIRE(called);
    BOOST_REQUIRE_MESSAGE(error == nullptr, "eth_call failed: "
                                               << (error ? error->errorMessage() : std::string{}));
    BOOST_REQUIRE(receipt != nullptr);
    // FISCO internal convention: 0 = success (the Ethereum RPC 0<->1 flip happens later).
    BOOST_CHECK_EQUAL(receipt->status(), 0);
    BOOST_CHECK_GE(receipt->gasUsed(), 21000u);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::executor_v1::opstack
