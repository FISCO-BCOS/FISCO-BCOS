// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockSchedulerTest — structural verification of the OP scheduler facade (spec
// 2026-08-12-op-block-scheduler-design.md v2). Construction over a real MultiLayerStorage,
// SchedulerInterface refuse stubs, storage reads, and eth_call Error semantics. The call
// happy-path injection assertions live in Task 5 (they depend on the buildOpBlockInfo fix in
// Task 4).
#include <opstack-executor/OpBlockScheduler.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <memory>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
// Single-bucket CONCURRENT backend (required: OpNewPayloadRpcE2eTest.cpp:146-153 documents that
// multi-bucket breaks the RANGE_SEEK scan).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;
    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
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
using ViewType = typename MLS::ViewType;
using OpBlockScheduler = bcos::executor_v1::opstack::OpBlockScheduler<ViewType, MLS>;

constexpr uint64_t kChainId = 0x2105;
const bcos::Address kSender{"0x1000000000000000000000000000000000000000"};

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

/// A genesis header carrying every field coCallLatest's buildOpBlockInfo/toBlockInfo reads.
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeGenesisHeader(int64_t timestampMillis)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(0);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(bcos::protocol::ParentInfo{.blockNumber = 0, .blockHash = bcos::h256{}});
    h->setCoinbase(bcos::Address{});
    h->setStateRoot(bcos::h256{});
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
void seedGenesis(MLS& mls, bcos::protocol::BlockHeader::Ptr const& genesisHeader)
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

/// Build an EIP-1559 (type 2) web3 tx and wrap it as a tars Transaction::Ptr (lambda-holder form —
/// the only construction that compiles: TransactionImpl's copy ctor is deleted and it takes a
/// std::function holder, see OpstackExecutorTest.cpp:84-87).
/// EIP-1559 is deliberate (round-3 C2): an EIP-2930/legacy tx with maxPriorityFeePerGas=0 would
/// trigger BCOS2Evmone's access_list override (BCOS2Evmone.cpp:130-133, max_priority=max_gas), so
/// effectiveGasPrice = maxFeePerGas regardless of baseFee — a false positive. EIP-1559 keeps
/// max_priority=0, making effectiveGasPrice == baseFee exactly (injection-sensitive).
/// @param maxFeePerGas / @param maxPriorityFeePerGas: the EIP-1559 fee caps; the happy-path test
///   uses maxPriorityFeePerGas=0 so effectiveGasPrice == baseFee (injection-sensitive).
bcos::protocol::Transaction::Ptr buildTx(bcos::u256 maxFeePerGas, bcos::u256 maxPriorityFeePerGas)
{
    bcos::rpc::Web3Transaction w3{};
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 5;
    w3.nonce = 0;
    w3.maxFeePerGas = maxFeePerGas;
    w3.maxPriorityFeePerGas = maxPriorityFeePerGas;
    w3.gasLimit = 5000000;
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
    // The dummy r/s cannot recover a sender; force it so opValidate sees the funded account.
    tx->clearSenderAndHash();
    tx->forceSender(kSender.asBytes());
    return tx;
}

struct Fixture
{
    BackendMemStorage backendStorage{1};
    CheckpointBackend checkpointBackend{backendStorage};
    MLS multiLayerStorage{checkpointBackend};
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::evm::opstack::OpForkTimestamps forkTimestamps{.isthmusTime = 0,
        .jovianTime = std::numeric_limits<uint64_t>::max()};
    std::shared_ptr<OpBlockScheduler> scheduler;

    Fixture()
      : scheduler(std::make_shared<OpBlockScheduler>(
            receiptFactory, hashImpl, kChainId, forkTimestamps, blockFactory, multiLayerStorage))
    {
        // Genesis with timestamp 1_000_000 ms = 1000 s >= isthmusTime(0) -> isthmus config.
        seedGenesis(multiLayerStorage, makeGenesisHeader(1000000));
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpBlockSchedulerSuite)

BOOST_AUTO_TEST_CASE(RefuseStubs)
{
    Fixture f;
    bool called = false;
    f.scheduler->executeBlock(
        nullptr, true, [&](bcos::Error::Ptr err, bcos::protocol::BlockHeader::Ptr, bool) {
            called = true;
            BOOST_REQUIRE(err != nullptr);
        });
    BOOST_REQUIRE(called);
    called = false;
    f.scheduler->commitBlock(nullptr, [&](bcos::Error::Ptr err, bcos::ledger::LedgerConfig::Ptr) {
        called = true;
        BOOST_REQUIRE(err != nullptr);
    });
    BOOST_REQUIRE(called);
    called = false;
    f.scheduler->preExecuteBlock(nullptr, true, [&](bcos::Error::Ptr err) {
        called = true;
        BOOST_REQUIRE(err != nullptr);
    });
    BOOST_REQUIRE(called);
}

BOOST_AUTO_TEST_CASE(StatusAndResetNoOp)
{
    Fixture f;
    f.scheduler->status([&](bcos::Error::Ptr err, bcos::protocol::Session::ConstPtr) {
        BOOST_REQUIRE(err == nullptr);
    });
    f.scheduler->reset([&](bcos::Error::Ptr err) { BOOST_REQUIRE(err == nullptr); });
}

BOOST_AUTO_TEST_CASE(GetCodeEmpty)
{
    Fixture f;
    // Unknown address -> empty code, no error. (getCode reads features only — no getLedgerConfig,
    // so the OP header's empty dataHash never reaches BlockHeader::hash().)
    bool called = false;
    f.scheduler->getCode(
        "0x0000000000000000000000000000000000000001", [&](bcos::Error::Ptr err, bcos::bytes code) {
            called = true;
            BOOST_REQUIRE(err == nullptr);
            BOOST_REQUIRE(code.empty());
        });
    BOOST_REQUIRE(called);
}

BOOST_AUTO_TEST_CASE(CallInvalidReturnsError)
{
    Fixture f;
    // Double rejection path (round-3 closed): maxFeePerGas=1 < baseFee(1e9) trips evmone
    // validate_transaction's FEE_CAP_LESS_THAN_BLOCKS (state.cpp:393), and the unfunded sender
    // (no fundAccount) also trips the balance check (balance 0 < maxCost). Either way opValidate
    // -> OpTxValidationFailed -> JSON-RPC Error, never a status-0 receipt.
    auto tx = buildTx(/*maxFeePerGas=*/1, /*maxPriorityFeePerGas=*/0);
    bool called = false;
    f.scheduler->call(
        std::move(tx), [&](bcos::Error::Ptr err, bcos::protocol::TransactionReceipt::Ptr) {
            called = true;
            BOOST_REQUIRE(err != nullptr);  // Error (JSON-RPC), never a status-0 receipt
        });
    BOOST_REQUIRE(called);
}

BOOST_AUTO_TEST_SUITE_END()
