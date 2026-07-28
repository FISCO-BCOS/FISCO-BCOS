// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerImplTest.cpp — op-validator-minimal-loop Task 4 (design §4, task-4-brief.md Step
// 1 (a)-(e) + Step 3). Suite name pinned to `OpSchedulerImpl` (brief constraint).
//
// Fixture (test (b)/(c)/(d)): a fresh MultiLayerStorage<memory> per test, forked to a mutable
// ViewType — same shape as bcos-evm/test/opstack/EbT8nReplayTest.cpp's Storage2Backend (fork()
// -> newMutable()); TrivialCheckpointStorage is a per-file local copy rather than a cross-module
// include, matching EbT8nReplayTest.cpp's own precedent comment (which in turn cites
// engine/test/unittests/engine/EngineServiceTest.cpp:84-98 for the same "each test module keeps
// its own tiny copy" convention). Test (b) is the "全链路" happy path: pre-seed via LedgerSeed +
// golden rawTransactions fed straight into executeOpBlock, asserting the six-way comparison
// surface (design §4.1) against the vector's own `_op_expected.header` fields plus
// golden.transactionsRoot — the same golden sources (and the same asH256/asAddress/asU256/asU64
// helper shapes) Task 3's EthBlockHeaderTest.cpp already uses for this exact vector's header
// fields (bcos-evm/test/opstack/EthBlockHeaderTest.cpp:82-140), reused here rather than
// reinvented.
//
// Untested here (deferred, per brief's five-group Step 1 list): version-gate (-38005),
// blockHash reconstruction, attributes rejection, latestValidHash bookkeeping — all belong to
// the engine newPayload OP branch (T5b), not this component.
//
// **未编译验证**: written and committed without cmake/ctest per the project's development-phase
// protocol; see task-4-report.md for the static walkthrough / API-precedent cross-check that
// substitutes for it.

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/engine/OpReceiptMap.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <bcos-evm/eth/utils/statetest.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

#include "support/ThrowingStorage.h"

using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{

// ---- hex-string -> bcos:: type conversions (EthBlockHeaderTest.cpp:82-101 precedent, same
// task family, same golden sources) ----

bcos::h256 asH256(std::string const& hex)
{
    return bcos::h256{bcos::fromHex(hex)};
}
bcos::Address asAddress(std::string const& hex)
{
    return bcos::Address{bcos::fromHex(hex)};
}
bcos::u256 asU256(std::string const& hex)
{
    return bcos::fromBigQuantity(hex);
}
uint64_t asU64(std::string const& hex)
{
    return bcos::fromQuantity(hex);
}
bcos::bytes asBytes(std::string const& hex)
{
    return bcos::fromHex(hex);
}

Json loadJsonOrFail(const fs::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        ADD_FAILURE() << "cannot open " << path.string();
        return Json::object();
    }
    Json j;
    in >> j;
    return j;
}

// vectors/<id>.json's top level is {"_op_test_vectors": {...}, "<id>": {env, pre, block,
// _op_expected, postState}} (same shape EthBlockHeaderTest.cpp's vectorBody / T8nReplayHarness.h
// parse).
Json const& vectorBody(Json const& doc, std::string const& id)
{
    return doc.at(id);
}

// ---- MultiLayerStorage fixture (EbT8nReplayTest.cpp's Storage2Backend::Impl precedent) ----

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

// Minimal CheckpointStorage stub, per-file local copy (EbT8nReplayTest.cpp:45-49's own
// rationale for not cross-including another module's test-private header).
template <class Key, class Value, bcos::storage2::ReadWriteStorage<Key, Value> Storage>
struct TrivialCheckpointStorage
{
    using CheckpointName = bcos::h256;

    Storage& m_storage;
    explicit TrivialCheckpointStorage(Storage& storage) noexcept : m_storage(storage) {}
    Storage& open() & { return m_storage; }
    [[noreturn]] Storage& open(CheckpointName const& /*unused*/) &
    {
        std::abort();  // this fixture never needs historical checkpoints.
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

/// Owns the full storage stack (backend + MLS + a forked mutable view) with a stable address, so
/// tests can seed via a Storage2Ledger<ViewType> bridge and then hand the *same* underlying view
/// to a freshly-constructed OpSchedulerImpl<ViewType> (design §4.1 "一块一实例" — the seeding
/// bridge and OpSchedulerImpl's internal execution bridge are two different Storage2Ledger
/// instances over the same storage2 view, exactly as a real block-N-seeds-block-N+1 sequence
/// would look, just compressed into one test).
struct StorageFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;
    ViewType view;

    StorageFixture()
      : checkpointBackend(backendStorage),
        multiLayerStorage(checkpointBackend),
        view(multiLayerStorage.fork())
    {
        view.newMutable();
    }
};

void seedFromVectorPre(ViewType& view, Json const& pre)
{
    bcos::evm::ledger::Storage2Ledger<ViewType> seedBridge(view);
    auto testState = evmone::test::from_json<evmone::test::TestState>(pre);
    bcos::evm::ledger::seedFromTestState(seedBridge, testState);
    ASSERT_FALSE(seedBridge.poisoned()) << "seeding failed: " << seedBridge.firstError();
}

bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite);
}

/// env (8 required fields, per T8nReplayHarness.h's own comment) -> OpBlockEnv. `extraData`
/// comes from the golden file (原样发射, not the vector — see golden/engine/README.md);
/// `blobGasUsed` is 0 for every Isthmus vector in this corpus (T8nReplayHarness.h's own
/// blobGasUsed handling note).
bcos::evm::engine::OpBlockEnv buildEnv(
    Json const& vec, Json const& golden, bcos::protocol::BlockHeader const& header)
{
    auto const& env = vec.at("env");
    return bcos::evm::engine::OpBlockEnv{
        .fiscoHeader = header,
        .parentHash = asH256(env.at("parentHash").get<std::string>()),
        .prevRandao = asH256(env.at("currentRandom").get<std::string>()),
        .baseFeePerGas = asU256(env.at("currentBaseFee").get<std::string>()),
        .feeRecipient = asAddress(env.at("currentCoinbase").get<std::string>()),
        .parentBeaconBlockRoot = asH256(env.at("parentBeaconBlockRoot").get<std::string>()),
        .gasLimit = asU64(env.at("currentGasLimit").get<std::string>()),
        .extraData = asBytes(golden.at("extraData").get<std::string>()),
        .blobGasUsed = 0,
    };
}

std::unique_ptr<bcostars::protocol::BlockHeaderImpl> buildHeaderForVector(Json const& vec)
{
    auto const& env = vec.at("env");
    auto header = std::make_unique<bcostars::protocol::BlockHeaderImpl>();
    header->setNumber(static_cast<bcos::protocol::BlockNumber>(
        asU64(env.at("currentNumber").get<std::string>())));
    header->setTimestamp(
        static_cast<int64_t>(asU64(env.at("currentTimestamp").get<std::string>())));
    return header;
}

std::vector<bcos::bytes> rawTxBytesOf(Json const& golden)
{
    std::vector<bcos::bytes> out;
    for (auto const& hex : golden.at("rawTransactions"))
        out.push_back(asBytes(hex.get<std::string>()));
    return out;
}

// isthmus_transfer_basic: 1 L1-attributes deposit + 1 EIP-1559 value transfer — the smallest
// vector exercising both tx shapes this component decodes in its happy path (Step 1(b)'s "一条
// 最简向量").
constexpr const char* kVectorId = "isthmus_transfer_basic";
constexpr uint64_t kChainId =
    0x2105;  // T8nReplayHarness.h's kCorpusChainId (this vector's tx.chainId).

// `vec` is deliberately NOT cached as a pointer computed before return — NRVO for a named local
// is an optional optimization, not guaranteed, and a plain move of the outer `Json` wrapper
// (even though nlohmann::json's internal object storage is typically pointer-stable across a
// move) is not a risk worth taking here. Callers derive `vec` via vectorBody() themselves once
// `vectors`/`golden` are already in their final (caller-owned) storage location.
struct LoadedVector
{
    Json vectors;
    Json golden;

    static LoadedVector load()
    {
        LoadedVector out;
        out.vectors =
            loadJsonOrFail(fs::path(OP_T8N_VECTORS_DIR) / (std::string(kVectorId) + ".json"));
        out.golden = loadJsonOrFail(
            fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / (std::string(kVectorId) + ".golden.json"));
        return out;
    }
};

// ---- Step 3: concept self-check (brief 裁定 B3) ----
// StubExecutor: local recreation of engine/test/unittests/engine/EngineServiceTest.cpp:147-169
// (satisfies executor_v1::TransactionExecutor<Storage>) — explicitly NOT copying that file's
// StubScheduler/BloomScheduler dangling-factory pattern (brief's "勿抄其悬垂工厂模式" warning;
// this file only needs the executor half).
struct StubExecutor
{
    template <class Storage>
    struct ExecuteContext
    {
    };

    template <class Storage>
    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return nullptr;
    }

    template <class Storage>
    bcos::task::Task<ExecuteContext<Storage>> createExecuteContext(Storage&,
        const bcos::protocol::BlockHeader&, const bcos::protocol::Transaction&, int,
        const bcos::ledger::LedgerConfig&, bool)
    {
        co_return ExecuteContext<Storage>{};
    }
};

// The Storage argument MUST be ViewType (a storage2 view), not a GlobalStateStorage-shaped
// type — this is the double-signature strategy's machine-checkable evidence (brief Step 3).
static_assert(bcos::scheduler_v1::TransactionScheduler<bcos::evm::engine::OpSchedulerImpl<ViewType>,
    ViewType, StubExecutor, std::vector<bcos::protocol::Transaction::Ptr>>);

}  // namespace

// (a) dummy executeBlock: throws immediately, message contains "OP mode".
TEST(OpSchedulerImpl, ExecuteBlockThrowsInOpMode)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    bcos::ledger::LedgerConfig ledgerConfig;
    std::vector<bcos::protocol::Transaction::Ptr> transactions;
    StubExecutor executor;

    bool threw = false;
    try
    {
        bcos::task::syncWait(
            scheduler.executeBlock(storage, executor, header, transactions, ledgerConfig));
    }
    catch (const std::exception& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("OP mode"), std::string::npos) << e.what();
    }
    EXPECT_TRUE(threw);
}

// (b) full round-trip: LedgerSeed pre + golden rawTransactions -> six-way comparison surface.
TEST(OpSchedulerImpl, ExecuteOpBlockSixWayComparisonSurface)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    StorageFixture fixture;
    seedFromVectorPre(fixture.view, vec.at("pre"));

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    auto result = bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes));

    auto const& expectedHeader = vec.at("_op_expected").at("header");

    // seal's three fields.
    EXPECT_EQ(asH256(expectedHeader.at("receiptsRoot").get<std::string>()),
        bcos::h256(reinterpret_cast<const bcos::byte*>(result.seal.receiptsRoot.bytes),
            sizeof(result.seal.receiptsRoot.bytes)));
    {
        // BloomFilter converts to bytes_view via an implicit operator (bloom_filter.hpp:21,
        // same mechanism evmc::address/bytes32 use, and the same one T8nReplayHarness.h relies
        // on via hexBytes(evmc::bytes_view(seal.logsBloom))) — not a begin()/end() range.
        const evmc::bytes_view bloomView(result.seal.logsBloom);
        EXPECT_EQ(asBytes(expectedHeader.at("logsBloom").get<std::string>()),
            bcos::bytes(bloomView.begin(), bloomView.end()));
    }
    EXPECT_EQ(asH256(expectedHeader.at("withdrawalsRoot").get<std::string>()),
        bcos::h256(reinterpret_cast<const bcos::byte*>(result.seal.withdrawalsRoot.bytes),
            sizeof(result.seal.withdrawalsRoot.bytes)));

    // result's three independent members.
    EXPECT_EQ(asH256(expectedHeader.at("stateRoot").get<std::string>()), result.stateRoot);
    EXPECT_EQ(asU64(expectedHeader.at("gasUsed").get<std::string>()), result.gasUsed);
    EXPECT_EQ(asH256(golden.at("transactionsRoot").get<std::string>()), result.txRoot);

    // receipts: mapped count matches tx count (status/gasUsed/logs mapping itself is
    // OpReceiptMap's own concern, not re-asserted field-by-field here — six-way surface is this
    // test's contract).
    EXPECT_EQ(result.receipts.size(), rawTxBytes.size());
    for (auto const& receipt : result.receipts)
        EXPECT_NE(receipt, nullptr);
}

// (c) first tx is not the L1 attributes deposit -> OpConsensusError.
TEST(OpSchedulerImpl, FirstTxNotAttributesDepositIsConsensusError)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    StorageFixture fixture;
    seedFromVectorPre(fixture.view, vec.at("pre"));

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);

    // golden.rawTransactions[1] is the eip1559 transfer (a well-formed, independently decodable
    // envelope) — feeding it alone (skipping index 0, the deposit) violates processOpBlock's
    // "first tx must be the L1 attributes deposit" invariant (OpBlockExecute.cpp) without
    // touching decode correctness.
    auto allRaw = rawTxBytesOf(golden);
    ASSERT_GE(allRaw.size(), 2U);
    std::vector<bcos::bytes> rawTxBytes{allRaw[1]};

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes)),
        bcos::evm::engine::OpConsensusError);
}

// (d) ThrowingStorage -> OpStorageError (not OpConsensusError, not a raw propagated exception —
// design §4.3's error-classification table).
TEST(OpSchedulerImpl, ThrowingStorageIsStorageError)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    MutableStorage rawStorage;  // deliberately unseeded: ThrowingStorage rejects every read
                                // unconditionally (support/ThrowingStorage.h), so the very first
                                // storage2 read inside processOpBlock's system call step is
                                // enough to trip the poison flag — no seed needed to observe it.
    bcos::evm::test::ThrowingStorage<MutableStorage> throwing(rawStorage);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<decltype(throwing)> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(throwing, env, rawTxBytes)),
        bcos::evm::engine::OpStorageError);
}

// (e) OpForkSchedule::configAt threshold judgement (decision A5): [isthmusTime, jovianTime) ->
// Isthmus, [jovianTime, +inf) -> Jovian.
TEST(OpSchedulerImpl, ConfigAtThresholds)
{
    bcos::evm::opstack::OpForkTimestamps thresholds{.isthmusTime = 1000, .jovianTime = 2000};

    EXPECT_EQ(
        bcos::evm::opstack::configAt(1000, thresholds).fork, bcos::evm::opstack::OpFork::Isthmus);
    EXPECT_EQ(
        bcos::evm::opstack::configAt(1999, thresholds).fork, bcos::evm::opstack::OpFork::Isthmus);
    EXPECT_EQ(
        bcos::evm::opstack::configAt(2000, thresholds).fork, bcos::evm::opstack::OpFork::Jovian);
    EXPECT_EQ(bcos::evm::opstack::configAt(1'000'000, thresholds).fork,
        bcos::evm::opstack::OpFork::Jovian);
}
