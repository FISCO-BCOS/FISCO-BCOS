// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// EngineNewPayloadGateTest.cpp — op-validator-minimal-loop Task 6 (design §7.1/§7.2/§7.3,
// task-6-brief.md). The acceptance core of the validator loop: the op-geth golden gate.
//
// Three GTest suites:
//   - `EngineNewPayloadGate`      : the 33-vector golden gate (Step 1) + the two-block chained
//                                   closure over the dedicated chained pair (Step 2);
//   - `EngineNewPayloadMutation`  : the 13-class / 18-case mutation matrix (Step 3, spec §7.3);
//   - (the generic-composition-root zero-drift case lives in the mutation suite, class #12).
//
// ── The hard constraint this file exists to satisfy (T5b review I4) ──────────────────────────
//
// `EngineOpBranchTest.cpp` (Task 5b) computes every one of its blockHashes with the very
// production mapping under test (`detail::rebuildOpEthHeader`), so a *systematically wrong*
// payload-field -> header-field mapping — swapping `prevRandao` and `parentBeaconBlockRoot`, say
// — moves both sides of its comparison together and passes silently. That file's own header says
// so and names Task 6 as the place the mapping gets pinned externally.
//
// Hence, in this file:
//   - `payload.blockHash` is ALWAYS `golden.blockHash`, i.e. op-geth's own `block.Hash()`, read
//     verbatim from `t8n/golden/engine/<id>.golden.json`. It is NEVER derived by calling
//     `rebuildOpEthHeader` and hashing. Feeding a self-computed hash into `newPayload` and then
//     asserting VALID would assert nothing at all about the mapping.
//   - `seal.txRoot` is asserted against `golden.transactionsRoot` (op-geth's `header.TxHash`),
//     not against a locally recomputed root (spec §7.1's "两侧独立来源交叉").
//   - `EthBlockHeader::encode() == golden.encodedHeaderHex` is asserted BEFORE the hash-level
//     verdict (裁定 C3): a byte-level RLP comparison localises a wrong field to a position in the
//     encoding, where the hash comparison only says "somewhere". On failure the 21 reconstructed
//     fields are dumped through `RecordProperty` for exactly that localisation.
//
// The explicitly-scoped exceptions, both inside the mutation matrix and both unavoidable (op-geth
// has no golden hash for a block that never existed): cases #8.1-#8.5 mutate a header field and
// re-seal via `resealBlockHash`, and case #7 hashes a header whose `requestsHash` was displaced.
// Those cases pin the *comparison chain* and the *bucket assignment*, not the mapping — which the
// 33-vector gate above them has already pinned externally. The invariant that matters:
// **neither the 33 vectors nor the chained pair passes through any self-computed blockHash**;
// every one of them is judged against op-geth's own `block.Hash()`.
//
// ── Where the golden values come from ────────────────────────────────────────────────────────
//
//   - `t8n/vectors/<id>.json`      : `env` (8 fields) + `_op_expected.header` (7 fields) — the
//                                    corpus's own 15 native header fields;
//   - `t8n/golden/engine/<id>.golden.json`
//                                  : `blockHash` / `transactionsRoot` / `extraData` (原样发射) /
//                                    `excessBlobGas` / `rawTransactions` (incl. the 0x7E deposit
//                                    envelopes, absent from `vectors/`) / `encodedHeaderHex`;
//   - `t8n/golden/engine/chained/` : the dedicated 1->2 chained pair (spec §7.1 裁定 A2) — full
//                                    `env`/`pre`/`postState`/`_op_expected` merged with the same
//                                    golden extension, so one document feeds both the seed and
//                                    the payload.
//
// `vectors/` and `golden/` are read-only here: this file opens them with `std::ifstream` and
// writes nothing anywhere near those paths.
//
// ── Fork-schedule note (why the fixture takes thresholds) ────────────────────────────────────
//
// Every vector in the corpus carries `currentTimestamp == 0x3f2`, Isthmus and Jovian alike — the
// corpus selects the fork by `_info.hardfork`, not by the clock (T8nReplayHarness.h:423-437).
// `OpSchedulerImpl`, by contrast, resolves the fork from the timestamp against injected
// thresholds (design §4.2 D2). So the gate injects, per vector, the threshold pair that makes
// that one timestamp land in the vector's declared fork — `forkTimestampsFor(jovian)` below.
// This is fixture configuration, not a golden-value edit.
//
// ── Fixture composition ──────────────────────────────────────────────────────────────────────
//
// Follows `EngineOpBranchTest.cpp`'s `OpFixture` verbatim in structure (member order
// storage -> memPool -> executor -> receiptFactory -> scheduler -> blockFactory -> service, so
// that everything the service holds a reference to outlives it), and its storage fixture /
// `registerVerifiedBlock` helper likewise. Those are duplicated rather than shared through a
// header: both live in anonymous namespaces (internal linkage, no ODR interaction), and this
// repo's own precedent for a fixture this small is local replication
// (EbT8nReplayTest.cpp:45-49's note on `TrivialCheckpointStorage`).
//
// **未编译验证**: written and committed without cmake/ctest, per the project's development-phase
// protocol; the unified build/run verification is scheduled after this task. See
// task-6-report.md for the API-precedent map that substitutes for it.

#include "engine/bcos-engine/EngineServiceImpl.h"

#include <bcos-codec/rlp/EthBlockHeader.h>
#include <bcos-concepts/ByteBuffer.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/engine/OpEngineSeam.h>
#include <bcos-evm/engine/OpSchedulerImpl.h>
#include <bcos-evm/ledger/LedgerSeed.h>
#include <bcos-evm/ledger/Storage2Ledger.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/LedgerTypeDef.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-framework/transaction-scheduler/TransactionScheduler.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/IOServicePool.h>
#include <gtest/gtest.h>
#include <boost/lexical_cast.hpp>
#include <algorithm>
#include <bcos-evm/eth/utils/statetest.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using Json = nlohmann::json;
namespace fs = std::filesystem;

// ─────────────────────── storage fixture (EngineOpBranchTest.cpp) ───────────────────────

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

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

struct StorageFixture
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend;
    MLS multiLayerStorage;

    StorageFixture() : checkpointBackend(backendStorage), multiLayerStorage(checkpointBackend) {}
};

/// Declares a block "already verified" per design §6.1 step 5's operational definition (裁定 C2:
/// presence in `SYS_HASH_2_NUMBER`). The spec calls fixture seeding of this table an explicit,
/// test-only exemption from the invariant that only the VALID branch writes it — the equivalent
/// of a trusted-genesis premise, and legitimate only in tests.
///
/// Encoding is the production one (`BaselineScheduler.h:207-220`): key = the hash's raw 32 bytes,
/// value = the number as a decimal string — it has to be, or the OP branch's own
/// `getBlockNumber(..., fromStorage)` lookup would not find it.
void registerVerifiedBlock(StorageFixture& fixture, bcos::h256 const& blockHash, int64_t number)
{
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    bcos::storage::Entry entry;
    entry.set(boost::lexical_cast<std::string>(number));
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(entry)));
    fixture.multiLayerStorage.pushView(std::move(view));
}

bool isBlockRegistered(StorageFixture& fixture, bcos::h256 const& blockHash)
{
    auto view = fixture.multiLayerStorage.fork();
    return bcos::task::syncWait(
        bcos::ledger::getBlockNumber(view, blockHash, bcos::ledger::fromStorage))
        .has_value();
}

std::optional<bcos::storage::Entry> readEntry(
    StorageFixture& fixture, std::string_view table, std::string_view key)
{
    auto view = fixture.multiLayerStorage.fork();
    return bcos::task::syncWait(
        bcos::storage2::readOne(view, bcos::executor_v1::StateKeyView{table, key}));
}

/// Seeds a vector's `pre` state through the production bridge + the production seeding path
/// (`Storage2Ledger` + `LedgerSeed.h`'s `seedFromTestState`, the same pair `EbT8nReplayTest.cpp`'s
/// `Storage2Backend::fromPre` uses), then publishes it as an immutable layer so that the view the
/// engine forks later can read it.
void seedPreState(StorageFixture& fixture, evmone::test::TestState const& pre)
{
    auto view = fixture.multiLayerStorage.fork();
    view.newMutable();
    {
        bcos::evm::ledger::Storage2Ledger<ViewType> bridge(view);
        bcos::evm::ledger::seedFromTestState(bridge, pre);
        EXPECT_FALSE(bridge.poisoned())
            << "pre-state seeding poisoned the ledger bridge: " << bridge.firstError();
    }
    fixture.multiLayerStorage.pushView(std::move(view));
}

// ─────────────────────────── stand-ins (EngineOpBranchTest.cpp) ───────────────────────────

/// The OP branch never touches the mempool.
struct StubMemPool
{
};

/// Satisfies the ExecutorType concept only; OP mode never routes a transaction through it.
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

/// Generic composition root's executor (transaction-scheduler/tests/testSchedulerSerial.cpp:20-46
/// shape — `SchedulerSerialImpl`'s pipeline calls `ExecuteContext::executeStep<N>()`).
struct MockExecutorSerial
{
    template <class Storage>
    struct ExecuteContext
    {
        template <int step>
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeStep()
        {
            co_return {};
        }
    };

    auto createExecuteContext(auto& storage, bcos::protocol::BlockHeader const& blockHeader,
        bcos::protocol::Transaction const& transaction, int32_t contextID,
        bcos::ledger::LedgerConfig const& ledgerConfig, bool call)
        -> bcos::task::Task<ExecuteContext<std::decay_t<decltype(storage)>>>
    {
        (void)storage;
        (void)blockHeader;
        (void)transaction;
        (void)contextID;
        (void)ledgerConfig;
        (void)call;
        co_return {};
    }

    bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> executeTransaction(auto& storage,
        bcos::protocol::BlockHeader const& blockHeader,
        bcos::protocol::Transaction const& transaction, int contextID,
        bcos::ledger::LedgerConfig const& /*unused*/, bool /*unused*/)
    {
        (void)storage;
        (void)blockHeader;
        (void)transaction;
        (void)contextID;
        co_return nullptr;
    }
};

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    // Keccak256 is not optional in OP mode: block registration derives transaction hashes as
    // keccak over the raw EIP-2718 envelope.
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

// The corpus's own chain id (every signed vector transaction carries `"chainId": "0x2105"`;
// T8nReplayHarness.h:509-517 cross-checks that all transactions in a vector agree on it).
constexpr uint64_t kChainId = 0x2105;
// Every vector's `env.currentTimestamp` (0x3f2 == 1010); see the fork-schedule note in the file
// header for why the thresholds move instead of the timestamp.
constexpr uint64_t kVectorTimestamp = 0x3f2;

/// Threshold pair that resolves `kVectorTimestamp` into the vector's declared fork:
///   - isthmus vector: [isthmusTime=0, jovianTime=+inf) -> `configAt` returns the Isthmus config
///     and `isJovianActiveAt` is false (so the `blobGasUsed == 0` rule applies, design §5.1);
///   - jovian vector : jovianTime=0 <= timestamp -> Jovian config, `isJovianActiveAt` true.
/// `isIsthmusActiveAt` is true either way, which is what the V4 version gate requires (§6.1 #1).
bcos::evm::opstack::OpForkTimestamps forkTimestampsFor(bool jovian)
{
    return bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = 0,
        .jovianTime = jovian ? 0 : std::numeric_limits<uint64_t>::max(),
    };
}

using OpScheduler = bcos::evm::engine::OpSchedulerImpl<ViewType>;
using OpEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, OpScheduler>;
using GenericEngineService = bcos::engine::EngineServiceImpl<StubMemPool, MLS, MockExecutorSerial,
    bcos::scheduler_v1::SchedulerSerialImpl>;

static_assert(OpEngineService::c_opMode, "OP composition root must be detected as OP mode");
static_assert(!GenericEngineService::c_opMode, "generic composition root must not be OP mode");

/// Mutation class #7's compile-time half (see the test for the other half): `NewPayloadRequest`
/// has no `executionRequests` carrier, which is precisely why design §6.1 step 2's
/// "executionRequests 在场且空" constraint is *vacuously* satisfied rather than enforced by
/// `validateOpNewPayloadRequest`. The day a carrier is added this assertion fires and forces the
/// real check (and a real test) to be written, instead of the constraint silently staying unmet.
///
/// The probe has to go through a *templated* concept: inside a requires-expression whose operand
/// is already a concrete (non-dependent) type, `request.executionRequests` is not a substitution
/// failure but a plain "no member named" error, so the naive
/// `static_assert(!requires(NewPayloadRequest r) { r.executionRequests; })` fails to compile
/// rather than evaluating to `true` (build-verification fix). Parameterising on the request type
/// makes the member access dependent, which is what turns the failure into an unsatisfied
/// constraint.
template <class Request>
concept HasExecutionRequestsCarrier = requires(Request request) { request.executionRequests; };

static_assert(!HasExecutionRequestsCarrier<bcos::engine::NewPayloadRequest>,
    "NewPayloadRequest gained an executionRequests carrier: design §6.1 step 2's non-empty -> "
    "INVALID check must now be implemented in validateOpNewPayloadRequest, and mutation case #7 "
    "below must stop using the requestsHash surrogate and mutate the real field");

// ───────────────────────────────── golden loading ─────────────────────────────────

Json loadJsonOrFail(const fs::path& path)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        ADD_FAILURE() << "cannot open " << path.string();
        return Json::object();
    }
    Json parsed;
    in >> parsed;
    return parsed;
}

bcos::h256 asH256(Json const& value)
{
    return bcos::h256{bcos::fromHex(value.get<std::string>())};
}
bcos::Address asAddress(Json const& value)
{
    return bcos::Address{bcos::fromHex(value.get<std::string>())};
}
bcos::u256 asU256(Json const& value)
{
    return bcos::fromBigQuantity(value.get<std::string>());
}
uint64_t asU64(Json const& value)
{
    return bcos::fromQuantity(value.get<std::string>());
}
bcos::bytes asBytes(Json const& value)
{
    return bcos::fromHex(value.get<std::string>());
}

/// `_op_expected.header.logsBloom` (512 hex chars) -> `ExecutionPayload::logsBloom`
/// (`std::array<byte, 256>`).
bcos::Bloom asBloom(Json const& value)
{
    const auto raw = asBytes(value);
    bcos::Bloom bloom{};
    EXPECT_EQ(raw.size(), bloom.size()) << "logsBloom must be exactly 256 bytes";
    std::copy_n(raw.begin(), std::min(raw.size(), bloom.size()), bloom.begin());
    return bloom;
}

/// `golden/engine/manifest.txt` -> the 33 vector ids (same filtering as
/// `EthBlockHeaderTest.cpp::loadManifestIds`, Task 3).
std::vector<std::string> loadManifestIds()
{
    std::vector<std::string> ids;
    const fs::path manifestPath = fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "manifest.txt";
    std::ifstream in(manifestPath);
    if (!in.is_open())
    {
        ADD_FAILURE() << "cannot open " << manifestPath.string();
        return ids;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        constexpr std::string_view kSuffix = ".golden.json";
        if (line.size() > kSuffix.size() &&
            line.compare(line.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0)
        {
            ids.push_back(line.substr(0, line.size() - kSuffix.size()));
        }
    }
    return ids;
}

/// One gate sample. `vector` and `golden` are separate documents for the 33-vector corpus and the
/// *same* document for the chained pair (whose `.golden.json` is the full vector merged with the
/// golden extension — see `golden/engine/README.md`), which is why they are stored as two
/// independent handles rather than one.
struct GoldenSample
{
    std::string id;
    Json vector;
    Json golden;
    bool jovian = false;
};

bool isJovianVector(Json const& vec)
{
    // `_info.hardfork` is the corpus's own fork declaration; exactly "isthmus" | "jovian", no
    // default arm (T8nReplayHarness.h:423-437's discipline, replicated so an unexpected value
    // fails loudly instead of silently replaying under the wrong fork).
    const auto hardfork = vec.at("_info").at("hardfork").get<std::string>();
    if (hardfork == "jovian")
    {
        return true;
    }
    EXPECT_EQ(hardfork, "isthmus") << "_info.hardfork must be exactly isthmus|jovian";
    return false;
}

GoldenSample loadVectorSample(std::string const& id)
{
    GoldenSample sample;
    sample.id = id;
    // `vectors/<id>.json` is `{"_op_test_vectors": {...}, "<id>": {...}}`.
    sample.vector = loadJsonOrFail(fs::path(OP_T8N_VECTORS_DIR) / (id + ".json")).at(id);
    sample.golden = loadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / (id + ".golden.json"));
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

GoldenSample loadChainedSample(std::string const& name)
{
    GoldenSample sample;
    sample.id = name;
    sample.vector =
        loadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "chained" / (name + ".golden.json"));
    sample.golden = sample.vector;  // one flat document carries both halves
    sample.jovian = isJovianVector(sample.vector);
    return sample;
}

// ─────────────────────────── payload assembly (the 21-field mapping) ───────────────────────────

/// Wraps a golden sample into the `newPayload` request the OP branch consumes.
///
/// `blockHash` comes from `golden.blockHash` — op-geth's `block.Hash()` — and from nowhere else.
/// That is the whole point of this file (see the header): every other field is fed in, the engine
/// reconstructs the header from them, and the verdict says whether the engine's mapping agrees
/// with op-geth's. `rawTransactions` likewise comes from golden (`tx.MarshalBinary()`, including
/// the 0x7E deposit envelopes that `vectors/*.json` does not carry at all).
bcos::engine::NewPayloadRequest makeGoldenRequest(GoldenSample const& sample)
{
    auto const& env = sample.vector.at("env");
    auto const& header = sample.vector.at("_op_expected").at("header");

    bcos::engine::NewPayloadRequest request;
    auto& payload = request.executionPayload;
    payload.parentHash = asH256(env.at("parentHash"));
    payload.feeRecipient = asAddress(env.at("currentCoinbase"));
    payload.stateRoot = asH256(header.at("stateRoot"));
    payload.receiptsRoot = asH256(header.at("receiptsRoot"));
    payload.logsBloom = asBloom(header.at("logsBloom"));
    payload.prevRandao = asH256(env.at("currentRandom"));
    payload.blockNumber = static_cast<bcos::protocol::BlockNumber>(asU64(env.at("currentNumber")));
    payload.gasLimit = bcos::u256(asU64(env.at("currentGasLimit")));
    payload.gasUsed = bcos::u256(asU64(header.at("gasUsed")));
    payload.timestamp = asU64(env.at("currentTimestamp"));
    payload.extraData = asBytes(sample.golden.at("extraData"));
    payload.baseFeePerGas = asU256(env.at("currentBaseFee"));
    payload.blockHash = asH256(sample.golden.at("blockHash"));  // <- op-geth, never self-computed
    payload.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payload.blobGasUsed = bcos::u256(asU64(header.at("blobGasUsed")));
    payload.excessBlobGas = bcos::u256(asU64(sample.golden.at("excessBlobGas")));
    payload.withdrawalsRoot = asH256(header.at("withdrawalsRoot"));

    std::vector<bcos::bytes> rawTransactions;
    for (auto const& raw : sample.golden.at("rawTransactions"))
    {
        rawTransactions.push_back(asBytes(raw));
    }
    payload.rawTransactions = std::move(rawTransactions);

    request.parentBeaconBlockRoot = asH256(env.at("parentBeaconBlockRoot"));
    return request;
}

/// The header the production mapping reconstructs for this request — the object under test.
bcos::codec::rlp::EthBlockHeader productionHeaderOf(bcos::engine::NewPayloadRequest const& request)
{
    auto const& payload = request.executionPayload;
    const auto transactionsRoot = OpScheduler::computeTxRoot(*payload.rawTransactions);
    return bcos::engine::detail::rebuildOpEthHeader(
        payload, transactionsRoot, *request.parentBeaconBlockRoot);
}

/// Mutation-only (see the file header's scoped exception): recomputes `blockHash` so a mutated
/// payload survives the static blockHash check and reaches the branch actually under test.
/// NEVER called for an unmutated vector or for the chained pair.
void resealBlockHash(bcos::engine::NewPayloadRequest& request)
{
    request.executionPayload.blockHash = productionHeaderOf(request).hash();
}

std::string hexOfBytes(bcos::bytes const& data)
{
    return bcos::toHexStringWithPrefix(data);
}

/// `RecordProperty` OVERWRITES an existing key rather than appending (gtest's documented
/// behaviour), and the 33-vector sweep below records from inside one single test — so a fixed key
/// would leave only the last vector's value and silently discard the other 32. Every key this
/// file emits is therefore namespaced by vector id (task-6 review I3).
std::string recordKey(std::string const& id, std::string_view suffix)
{
    return "v_" + id + "_" + std::string(suffix);
}

/// Dumps all 21 reconstructed header fields so a failing `encode()` comparison can be localised
/// to a field rather than to "the hash differs" (裁定 C3's stated purpose).
void recordHeaderFields(std::string const& id, bcos::codec::rlp::EthBlockHeader const& header)
{
    using ::testing::Test;
    Test::RecordProperty(recordKey(id, "hdr_01_parentHash"), header.parentHash.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_02_ommersHash"), header.ommersHash.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_03_feeRecipient"), header.feeRecipient.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_04_stateRoot"), header.stateRoot.hexPrefixed());
    Test::RecordProperty(
        recordKey(id, "hdr_05_transactionsRoot"), header.transactionsRoot.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_06_receiptsRoot"), header.receiptsRoot.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_07_logsBloom"), header.logsBloom.hexPrefixed());
    Test::RecordProperty(
        recordKey(id, "hdr_08_difficulty"), boost::lexical_cast<std::string>(header.difficulty));
    Test::RecordProperty(recordKey(id, "hdr_09_number"), std::to_string(header.number));
    Test::RecordProperty(recordKey(id, "hdr_10_gasLimit"), std::to_string(header.gasLimit));
    Test::RecordProperty(recordKey(id, "hdr_11_gasUsed"), std::to_string(header.gasUsed));
    Test::RecordProperty(recordKey(id, "hdr_12_timestamp"), std::to_string(header.timestamp));
    Test::RecordProperty(recordKey(id, "hdr_13_extraData"), hexOfBytes(header.extraData));
    Test::RecordProperty(recordKey(id, "hdr_14_prevRandao"), header.prevRandao.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_15_nonce"), header.nonce.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_16_baseFeePerGas"),
        boost::lexical_cast<std::string>(header.baseFeePerGas));
    Test::RecordProperty(
        recordKey(id, "hdr_17_withdrawalsRoot"), header.withdrawalsRoot.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_18_blobGasUsed"), std::to_string(header.blobGasUsed));
    Test::RecordProperty(
        recordKey(id, "hdr_19_excessBlobGas"), std::to_string(header.excessBlobGas));
    Test::RecordProperty(
        recordKey(id, "hdr_20_parentBeaconBlockRoot"), header.parentBeaconBlockRoot.hexPrefixed());
    Test::RecordProperty(recordKey(id, "hdr_21_requestsHash"), header.requestsHash.hexPrefixed());
}

// ────────────────────────────────── composition root ──────────────────────────────────

/// Everything the OP composition root needs, as named members so scheduler/executor/storage all
/// outlive the service that holds references to them (the lifetime trap `EngineOpBranchTest.cpp`
/// calls out). Declaration order is initialisation order, hence receiptFactory before scheduler
/// and blockFactory before service.
struct GateFixture
{
    StorageFixture storage;
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    OpScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    OpEngineService service;

    explicit GateFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps),
        service(memPool, storage.multiLayerStorage, executor, scheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4)
    {}
};

/// A ready-to-drive golden scenario: pre-state seeded, parent optionally pre-registered, request
/// wrapped. `GateFixture` is non-movable (it holds an `OpSchedulerImpl`, which deletes its move
/// constructor), so it is handed out behind a `unique_ptr`.
struct Scenario
{
    std::unique_ptr<GateFixture> fixture;
    GoldenSample sample;
    bcos::engine::NewPayloadRequest request;
    bcos::h256 parentHash;
};

Scenario prepareScenario(std::string const& id, bool registerParent = true)
{
    Scenario scenario;
    scenario.sample = loadVectorSample(id);
    scenario.fixture = std::make_unique<GateFixture>(forkTimestampsFor(scenario.sample.jovian));
    seedPreState(scenario.fixture->storage,
        evmone::test::from_json<evmone::test::TestState>(scenario.sample.vector.at("pre")));
    scenario.request = makeGoldenRequest(scenario.sample);
    scenario.parentHash = scenario.request.executionPayload.parentHash;
    if (registerParent)
    {
        // The 33 isolated vectors are all block 1, so their parent is the (trusted) genesis at
        // height 0 — design §7.2's "33 条孤立向量的 parent 仍由 fixture 预登记".
        registerVerifiedBlock(scenario.fixture->storage, scenario.parentHash, 0);
    }
    return scenario;
}

// ─────────────────── the one branch real execution cannot be steered into ───────────────────

/// Decorator over the REAL `OpSchedulerImpl` that perturbs a single output: `result.txRoot`.
///
/// Why it exists: the engine's sixth comparison (`commitments.txRoot != transactionsRoot`) is
/// structurally unreachable with the real scheduler, because both sides call the same
/// `computeOpTxRoot` over the same raw bytes — mutating the transactions moves both sides
/// together, exactly as `EngineServiceImpl.h:802-808` says. The comparison is nonetheless kept in
/// production as the guard that fires the day execution starts deriving txRoot from its own
/// *parsed* interpretation of the transactions instead of from the wire bytes. This decorator
/// simulates precisely that day, and nothing else: the block is really executed by the real
/// scheduler over the real seeded state, and only the returned `txRoot` is displaced.
///
/// The seam is pure duck typing (see `OpSchedulerImpl.h:539-559`), so forwarding the published
/// names is all that is required; `c_opMode`'s probe resolves against `executeOpBlock`'s single
/// invented template parameter, which the static_assert below pins.
class TxRootDriftScheduler
{
public:
    TxRootDriftScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        uint64_t chainId, bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : m_inner(std::move(receiptFactory), chainId, forkTimestamps)
    {}

    using BlockEnv = OpScheduler::BlockEnv;
    using ExecuteResult = OpScheduler::ExecuteResult;
    using ConsensusError = OpScheduler::ConsensusError;
    using StorageError = OpScheduler::StorageError;
    static constexpr std::string_view c_ethBlockHeaderTable = OpScheduler::c_ethBlockHeaderTable;

    static bcos::evm::engine::OpBlockCommitments commitmentsOf(const ExecuteResult& result)
    {
        return OpScheduler::commitmentsOf(result);
    }
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return OpScheduler::computeTxRoot(rawTxBytes);
    }
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return m_inner.isIsthmusActiveAt(timestamp);
    }
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return m_inner.isJovianActiveAt(timestamp);
    }

    /// Concept satisfaction only, same throw-before-any-suspend shape as the real one.
    bcos::task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(ViewType&,
        auto&, bcos::protocol::BlockHeader const&, ::ranges::input_range auto const&,
        bcos::ledger::LedgerConfig const&)
    {
        throw std::logic_error("TxRootDriftScheduler::executeBlock: OP mode must not call this");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

    bcos::task::Task<ExecuteResult> executeOpBlock(
        ViewType& storage, BlockEnv const& env, ::ranges::input_range auto const& rawTxBytes)
    {
        auto result = co_await m_inner.executeOpBlock(storage, env, rawTxBytes);
        // The one displacement. The zero hash is chosen because it can never collide with the
        // real value: a transactions trie root is a keccak256 output (`computeOpTxRoot` ->
        // `MPT::hash()`; even the empty-list case yields the well-known
        // 0x56e81f...b421 empty-root constant, not zero), so `h256{}` is provably != the value
        // the comparison is fed — the mutation cannot silently degenerate into a no-op.
        result.txRoot = bcos::h256{};
        co_return result;
    }

private:
    OpScheduler m_inner;
};

using DriftEngineService =
    bcos::engine::EngineServiceImpl<StubMemPool, MLS, StubExecutor, TxRootDriftScheduler>;
static_assert(DriftEngineService::c_opMode,
    "TxRootDriftScheduler must satisfy the same opMode probe as OpSchedulerImpl");

/// Same composition/lifetime discipline as `GateFixture`, driven by the drift decorator.
struct DriftFixture
{
    StorageFixture storage;
    StubMemPool memPool;
    StubExecutor executor;
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{makeReceiptFactory()};
    TxRootDriftScheduler scheduler;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    DriftEngineService service;

    explicit DriftFixture(bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : scheduler(receiptFactory, kChainId, forkTimestamps),
        service(memPool, storage.multiLayerStorage, executor, scheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, /*maxEngineVersion=*/4)
    {}
};

// ───────────────────────────────── the gate body ─────────────────────────────────

/// One vector, end to end (design §7.2's gate flow): seed pre -> pre-register parent -> wrap
/// payload -> `newPayload(request, 4)` -> VALID, with the golden cross-assertions around it.
void runGoldenVector(std::string const& id)
{
    SCOPED_TRACE(id);
    auto scenario = prepareScenario(id);
    auto const& payload = scenario.request.executionPayload;
    auto const& golden = scenario.sample.golden;

    const auto goldenBlockHash = asH256(golden.at("blockHash"));
    const auto goldenTxRoot = asH256(golden.at("transactionsRoot"));

    // (1) transactionsRoot cross-assertion (六项比对面 #6, one of the two independent-source
    //     crossings spec §7.1 requires): the value the engine derives from the raw envelopes and
    //     feeds into the header must be op-geth's `header.TxHash`. Asserted here, before the
    //     verdict, because a wrong txRoot would otherwise surface only as an opaque blockHash
    //     mismatch. VALID below then additionally proves `result.txRoot == transactionsRoot`
    //     (EngineServiceImpl.h:802-808's comparison), so VALID + this assertion together give
    //     `result.txRoot == golden.transactionsRoot`.
    const auto transactionsRoot = OpScheduler::computeTxRoot(*payload.rawTransactions);
    EXPECT_EQ(transactionsRoot, goldenTxRoot) << id << ": computeTxRoot != golden.transactionsRoot";

    // (2) Field-level RLP assertion BEFORE the hash-level one (裁定 C3). This is what localises a
    //     wrong payload-field -> header-field mapping; the 21-field dump makes it actionable.
    const auto header = productionHeaderOf(scenario.request);
    const auto encoded = header.encode();
    const auto goldenEncoded = asBytes(golden.at("encodedHeaderHex"));
    if (encoded != goldenEncoded)
    {
        recordHeaderFields(id, header);
        ::testing::Test::RecordProperty(recordKey(id, "hdr_encoded_actual"), hexOfBytes(encoded));
        ::testing::Test::RecordProperty(
            recordKey(id, "hdr_encoded_golden"), hexOfBytes(goldenEncoded));
    }
    EXPECT_EQ(encoded, goldenEncoded) << id
                                      << ": rebuildOpEthHeader().encode() != "
                                         "golden.encodedHeaderHex (21 fields dumped via "
                                         "RecordProperty)";

    // (3) The payload carries op-geth's hash verbatim — restated as an assertion so that a future
    //     refactor of `makeGoldenRequest` cannot quietly switch it to a self-computed value and
    //     turn the whole gate into a tautology.
    ASSERT_EQ(payload.blockHash, goldenBlockHash)
        << id << ": the gate must feed golden.blockHash, never a locally recomputed hash";

    // (4) The verdict.
    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    // Per-vector keys (review I3): the 33-vector sweep runs this whole function 33 times inside
    // ONE test, and `RecordProperty` overwrites by key — fixed keys would keep only the last.
    ::testing::Test::RecordProperty(recordKey(id, "status"), static_cast<int>(status.status));
    ::testing::Test::RecordProperty(recordKey(id, "block_hash"), goldenBlockHash.hexPrefixed());
    ::testing::Test::RecordProperty(recordKey(id, "tx_root"), goldenTxRoot.hexPrefixed());
    ::testing::Test::RecordProperty(
        recordKey(id, "tx_count"), static_cast<int>(payload.rawTransactions->size()));

    ASSERT_EQ(status.status, bcos::engine::PayloadValidationStatus::Valid)
        << id << ": expected VALID, validationError=" << status.validationError.value_or("<none>");
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, goldenBlockHash)
        << id << ": latestValidHash must be the accepted block's (golden) hash";
    EXPECT_FALSE(status.validationError.has_value());

    // (5) The accepted block is registered, so it is a legal parent for the next one — the
    //     property the chained test below turns into a causal chain.
    EXPECT_TRUE(isBlockRegistered(scenario.fixture->storage, goldenBlockHash));
    auto headerEntry = readEntry(scenario.fixture->storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER,
        boost::lexical_cast<std::string>(payload.blockNumber));
    ASSERT_TRUE(headerEntry.has_value()) << id << ": ETH header RLP was not registered";
    EXPECT_EQ(headerEntry->get(),
        std::string_view(reinterpret_cast<const char*>(goldenEncoded.data()), goldenEncoded.size()))
        << id << ": the registered header RLP must be op-geth's encodedHeaderHex byte for byte";
}

}  // namespace

// ══════════════════════════ Step 1: the 33-vector golden gate ══════════════════════════

// Three named single-vector cases first (fast, precise failure localisation), then the full
// sweep. Chosen to span the corpus's structural axes: Isthmus/Jovian, single/multi transaction
// (non-trivial trie), deposit-only — the same three `EthBlockHeaderTest.cpp` singles out.
TEST(EngineNewPayloadGate, IsthmusSingleTransfer)
{
    runGoldenVector("isthmus_transfer_basic");
}

TEST(EngineNewPayloadGate, JovianMultiTransfer)
{
    runGoldenVector("jovian_transfer_multi");
}

TEST(EngineNewPayloadGate, IsthmusDepositOnly)
{
    runGoldenVector("isthmus_deposit_only");
}

/// Design §8's first acceptance line: 33/33 VALID, `latestValidHash == blockHash`, with the
/// `encode()`/`blockHash`/`transactionsRoot` golden crossings asserted per vector.
TEST(EngineNewPayloadGate, AllThirtyThreeGoldenVectors)
{
    auto ids = loadManifestIds();
    ASSERT_EQ(ids.size(), 33U)
        << "manifest.txt id count drifted from Task 2's 33-vector corpus (vectors/ and golden/ "
           "are read-only for this task — a drift here means the corpus changed, not the gate)";
    for (auto const& id : ids)
    {
        runGoldenVector(id);
    }
}

// ══════════════════ Step 2: the two-block chained closure (spec §7.2, 裁定 A2) ══════════════════

/// The causal evidence that the loop closes: block B's parent becomes known *because* block A was
/// accepted and registered, not because a fixture said so.
///
/// The sequence is deliberately ordered so the same payload B is submitted twice, before and
/// after A's acceptance, with nothing else changed between the two calls:
///   1. seed chainA's `pre`, pre-register only chainA's parent (the trusted genesis);
///   2. newPayload(B) -> SYNCING          (design §7.2's "跳过 FCU 直接投未知 parent 的块");
///   3. newPayload(A) -> VALID;
///   4. forkchoiceUpdated(head=A) -> VALID;
///   5. newPayload(B) -> VALID            (parent-known now satisfied by block registration).
///
/// B is never re-seeded: its `pre` IS A's `postState` (README's `chainB.pre.json` ==
/// `chainA.post.json` key-set check), so if step 5 passes, A's execution really did leave its
/// post-state in the storage the engine reads — the storage half of the closure, on top of the
/// registration half.
TEST(EngineNewPayloadGate, ChainedPairParentKnownThroughBlockRegistration)
{
    auto chainA = loadChainedSample("chainA");
    auto chainB = loadChainedSample("chainB");

    // Real `InsertChain`-validated linkage, asserted rather than assumed (README: rev.2's
    // hand-splice scheme is retired precisely because it faked this).
    ASSERT_EQ(
        asH256(chainB.vector.at("env").at("parentHash")), asH256(chainA.golden.at("blockHash")))
        << "chainB.env.parentHash must be chainA's golden blockHash";

    GateFixture fixture(forkTimestampsFor(chainA.jovian));
    seedPreState(
        fixture.storage, evmone::test::from_json<evmone::test::TestState>(chainA.vector.at("pre")));

    auto requestA = makeGoldenRequest(chainA);
    auto requestB = makeGoldenRequest(chainB);
    const auto hashA = requestA.executionPayload.blockHash;
    const auto hashB = requestB.executionPayload.blockHash;

    // Only A's parent is pre-registered. B's parent is NOT — that is the whole experiment.
    registerVerifiedBlock(fixture.storage, requestA.executionPayload.parentHash, 0);

    // 2. B before A: SYNCING, and nothing written.
    auto earlyB = bcos::task::syncWait(fixture.service.newPayload(requestB, 4));
    EXPECT_EQ(earlyB.status, bcos::engine::PayloadValidationStatus::Syncing)
        << "block B must be SYNCING while its parent (block A) is unknown";
    EXPECT_FALSE(earlyB.latestValidHash.has_value());
    EXPECT_FALSE(isBlockRegistered(fixture.storage, hashB));

    // 3. A: VALID.
    auto statusA = bcos::task::syncWait(fixture.service.newPayload(requestA, 4));
    ASSERT_EQ(statusA.status, bcos::engine::PayloadValidationStatus::Valid)
        << "chainA: validationError=" << statusA.validationError.value_or("<none>");
    ASSERT_TRUE(statusA.latestValidHash.has_value());
    EXPECT_EQ(*statusA.latestValidHash, hashA);
    EXPECT_TRUE(isBlockRegistered(fixture.storage, hashA));

    // 4. forkchoiceUpdated(head = A). Read-only + in-memory this cycle (裁定 A4): it advances the
    //    tracked head, it does not write the chain-head progress table.
    bcos::engine::ForkchoiceState forkchoiceState{
        .headBlockHash = hashA,
        .safeBlockHash = hashA,
        .finalizedBlockHash = hashA,
    };
    auto forkchoiceResult =
        bcos::task::syncWait(fixture.service.updateForkchoice(forkchoiceState, nullptr, 3));
    EXPECT_EQ(forkchoiceResult.payloadStatus.status, bcos::engine::PayloadValidationStatus::Valid);
    ASSERT_TRUE(forkchoiceResult.payloadStatus.latestValidHash.has_value());
    EXPECT_EQ(*forkchoiceResult.payloadStatus.latestValidHash, hashA);

    // 5. The same payload B, now VALID — the parent-known predicate flipped because of step 3.
    auto statusB = bcos::task::syncWait(fixture.service.newPayload(requestB, 4));
    ASSERT_EQ(statusB.status, bcos::engine::PayloadValidationStatus::Valid)
        << "chainB: validationError=" << statusB.validationError.value_or("<none>");
    ASSERT_TRUE(statusB.latestValidHash.has_value());
    EXPECT_EQ(*statusB.latestValidHash, hashB);
    EXPECT_TRUE(isBlockRegistered(fixture.storage, hashB));

    // Both blocks' header RLPs are registered under their own heights (1 and 2) — the chain index
    // really grew by one block, rather than block B overwriting block A's entries.
    EXPECT_TRUE(
        readEntry(fixture.storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER, "1").has_value());
    EXPECT_TRUE(
        readEntry(fixture.storage, bcos::evm::engine::SYS_ETH_BLOCK_HEADER, "2").has_value());
}

// ═════════════ Step 3: the mutation matrix — 13 classes / 18 cases (spec §7.3) ═════════════
//
// latestValidHash discipline, stated once — and stated honestly about its provenance:
//
//   - classes #3-#6 (and #2/#7) reject inside design §6.1 step 2, i.e. BEFORE parentKnown is
//     evaluated, and the implementation returns `latestValidHash = null` for all of them
//     (`EngineServiceImpl.h:664-687`).
//   - classes #8.1-#8.6 reject after parentKnown succeeded, so they assert
//     `latestValidHash == parentHash`.
//
// **Provenance (task-6 review I1/I2, do not read past this):** spec §7.3's note as written says
// "非 blockHash 桶的 INVALID 用例同时断言 latestValidHash==parentHash", which taken literally
// would demand parentHash for #3-#6. The null answer above is an **implementation choice made in
// task-5b**, and the argument for it (the "bucket is defined by where the rejection happens")
// comes from that implementation's own comments — NOT from spec text, and it must not be cited as
// though it were. The coordinator has since adjudicated the conflict in the implementation's
// favour, on the merits: at step 2 the parent has not been looked up at all, so claiming it as
// the "latest valid ancestor" would be an unverified assertion; the Engine API permits null; and
// null is the more honest answer. Spec §7.3's note and §8's third acceptance line are to be
// revised in T7 to "非 blockHash 桶**且已过 parentKnown**的 INVALID 才断言 parentHash". Until
// that edit lands, these cases are correct against the implementation and knowingly divergent
// from the spec's current wording — recorded in task-6-report.md §5, not papered over.
//
// Each case below asserts whichever of the two applies, never neither.

// ── #1 timestamp x version gate -> JSON-RPC -38005 ────────────────────────────────────────────
TEST(EngineNewPayloadMutation, TimestampVersionGateRejectsMismatch)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");

    // Spec §7.3 row 1: an Isthmus payload submitted on V3.
    EXPECT_THROW(bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 3)),
        bcos::engine::UnsupportedFork);

    // The other direction, on the same golden payload: a node whose Isthmus threshold sits above
    // this payload's timestamp must refuse V4. (`configAt` cannot answer this — it resolves
    // sub-isthmusTime timestamps to the Isthmus config too, OpForkSchedule.h — which is why the
    // gate uses the raw threshold via `isIsthmusActiveAt`; task-4 review item M4.)
    GateFixture preIsthmus(bcos::evm::opstack::OpForkTimestamps{
        .isthmusTime = kVectorTimestamp + 1, .jovianTime = std::numeric_limits<uint64_t>::max()});
    EXPECT_THROW(bcos::task::syncWait(preIsthmus.service.newPayload(scenario.request, 4)),
        bcos::engine::UnsupportedFork);
}

// ── #2 blockHash reconstruction: one tampered byte -> INVALID + latestValidHash = null ────────
TEST(EngineNewPayloadMutation, TamperedBlockHashIsInvalidWithNullLatestValidHash)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    auto& blockHash = scenario.request.executionPayload.blockHash;
    const auto goldenBlockHash = blockHash;
    blockHash.data()[0] ^= 0x01;  // exactly one byte, exactly one bit
    ASSERT_NE(blockHash, goldenBlockHash);

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    // `Invalid`, never the deprecated-since-Shanghai `InvalidBlockHash` (design §6.1 step 2).
    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blockHash"), std::string::npos);
    EXPECT_FALSE(isBlockRegistered(scenario.fixture->storage, blockHash));
}

// ── #3 withdrawals non-empty -> INVALID ───────────────────────────────────────────────────────
TEST(EngineNewPayloadMutation, NonEmptyWithdrawalsIsInvalid)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    scenario.request.executionPayload.withdrawals =
        std::vector<bcos::engine::WithdrawalV1>{bcos::engine::WithdrawalV1{}};

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("withdrawals"), std::string::npos);
}

// ── #4 expectedBlobVersionedHashes non-empty -> INVALID ───────────────────────────────────────
TEST(EngineNewPayloadMutation, NonEmptyExpectedBlobVersionedHashesIsInvalid)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    scenario.request.expectedBlobVersionedHashes.push_back(bcos::h256{});

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("expectedBlobVersionedHashes"), std::string::npos);
}

// ── #5 excessBlobGas != 0 -> INVALID ──────────────────────────────────────────────────────────
TEST(EngineNewPayloadMutation, NonZeroExcessBlobGasIsInvalid)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    scenario.request.executionPayload.excessBlobGas = bcos::u256(1);

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("excessBlobGas"), std::string::npos);
}

// ── #6 blobGasUsed != 0 under Isthmus -> INVALID ──────────────────────────────────────────────
// Fork-gated on purpose: from Jovian on, the same header slot is the DA footprint and is checked
// by seal comparison instead (design §5.1 / OpBlockSeal.h:31-38). The Jovian half of that rule is
// covered positively in the gate above by exactly ONE vector — `jovian_da_mix`, the only one of
// the 17 jovian vectors whose `_op_expected.header.blobGasUsed` is non-zero (0x90ec0); it is
// VALID there, which is what proves the Isthmus rule is not applied past Jovian. The other 16
// jovian vectors carry blobGasUsed = 0 and so exercise neither side of this distinction.
TEST(EngineNewPayloadMutation, NonZeroBlobGasUsedIsInvalidUnderIsthmus)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    ASSERT_FALSE(scenario.sample.jovian);
    scenario.request.executionPayload.blobGasUsed = bcos::u256(1);

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blobGasUsed"), std::string::npos);
}

// ── #7 executionRequests non-empty -> INVALID + null (the blockHash bucket) ───────────────────
//
// `NewPayloadRequest` has no `executionRequests` carrier (the static_assert at the top of this
// file pins that, and fires the day one is added), so the list cannot be set directly. It is not
// silently ignored either: the reconstructed header pins `requestsHash` to the OP empty-requests
// constant, so a block that really carried a non-empty list would arrive with a blockHash
// committing to a *different* requestsHash — and land in exactly the bucket the spec assigns it
// to. This case reproduces that arrival: the golden header with only `requestsHash` displaced,
// hashed, and submitted as the payload's blockHash.
TEST(EngineNewPayloadMutation, NonEmptyExecutionRequestsLandsInBlockHashBucket)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    auto header = productionHeaderOf(scenario.request);

    // sha256("") — `OP_EMPTY_REQUESTS_HASH`, cross-checked against the golden corpus by Task 3.
    const bcos::h256 emptyRequestsHash{bcos::fromHex(
        std::string{"0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"})};
    ASSERT_EQ(header.requestsHash, emptyRequestsHash);

    header.requestsHash.data()[0] ^= 0x01;  // stands for "the list was not empty"
    scenario.request.executionPayload.blockHash = header.hash();

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    EXPECT_FALSE(status.latestValidHash.has_value());
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("blockHash"), std::string::npos);
}

// ── #8.1-#8.5 five of the six comparison surfaces, one mutation each ──────────────────────────
//
// Each case mutates exactly one payload field, re-seals the blockHash so the payload survives the
// static check (the file header's scoped exception), and lets the REAL scheduler execute the REAL
// block over the REAL seeded state — so the disagreement is produced by genuine execution, not by
// a scripted stand-in. One perturbation per case: a comparison silently missing from the chain
// shows up as exactly one failing case rather than being masked by its neighbours.
namespace
{

/// Runs one comparison-surface mutation and asserts the shared verdict shape.
void expectComparisonSurfaceMismatch(
    const char* fieldName, void (*perturb)(bcos::engine::ExecutionPayload&))
{
    SCOPED_TRACE(fieldName);
    auto scenario = prepareScenario("isthmus_transfer_basic");
    perturb(scenario.request.executionPayload);
    resealBlockHash(scenario.request);

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.latestValidHash.has_value())
        << "post-parentKnown rejections must report the parent as the latest valid ancestor";
    EXPECT_EQ(*status.latestValidHash, scenario.parentHash);
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find(fieldName), std::string::npos)
        << "validationError must name the offending field, got: " << *status.validationError;
    EXPECT_FALSE(
        isBlockRegistered(scenario.fixture->storage, scenario.request.executionPayload.blockHash));
}

}  // namespace

TEST(EngineNewPayloadMutation, ComparisonSurfaceReceiptsRoot)
{
    expectComparisonSurfaceMismatch("receiptsRoot",
        [](bcos::engine::ExecutionPayload& payload) { payload.receiptsRoot.data()[0] ^= 0x01; });
}

TEST(EngineNewPayloadMutation, ComparisonSurfaceLogsBloom)
{
    expectComparisonSurfaceMismatch(
        "logsBloom", [](bcos::engine::ExecutionPayload& payload) { payload.logsBloom[0] ^= 0x01; });
}

TEST(EngineNewPayloadMutation, ComparisonSurfaceWithdrawalsRoot)
{
    expectComparisonSurfaceMismatch("withdrawalsRoot", [](bcos::engine::ExecutionPayload& payload) {
        auto root = *payload.withdrawalsRoot;
        root.data()[0] ^= 0x01;
        payload.withdrawalsRoot = root;
    });
}

TEST(EngineNewPayloadMutation, ComparisonSurfaceStateRoot)
{
    expectComparisonSurfaceMismatch("stateRoot",
        [](bcos::engine::ExecutionPayload& payload) { payload.stateRoot.data()[0] ^= 0x01; });
}

TEST(EngineNewPayloadMutation, ComparisonSurfaceGasUsed)
{
    expectComparisonSurfaceMismatch("gasUsed",
        [](bcos::engine::ExecutionPayload& payload) { payload.gasUsed = payload.gasUsed + 1; });
}

// ── #8.6 the sixth surface: transactionsRoot ──────────────────────────────────────────────────
// The only one unreachable by mutating the payload (both sides of the comparison are derived from
// the same raw bytes by the same function). Driven by `TxRootDriftScheduler`, which really
// executes the block with the real scheduler and displaces only the returned `txRoot` — i.e. it
// simulates exactly the future regression that comparison exists to catch.
TEST(EngineNewPayloadMutation, ComparisonSurfaceTransactionsRoot)
{
    auto sample = loadVectorSample("isthmus_transfer_basic");
    DriftFixture fixture(forkTimestampsFor(sample.jovian));
    seedPreState(
        fixture.storage, evmone::test::from_json<evmone::test::TestState>(sample.vector.at("pre")));

    auto request = makeGoldenRequest(sample);
    const auto parentHash = request.executionPayload.parentHash;
    registerVerifiedBlock(fixture.storage, parentHash, 0);

    auto status = bcos::task::syncWait(fixture.service.newPayload(request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Invalid);
    ASSERT_TRUE(status.latestValidHash.has_value());
    EXPECT_EQ(*status.latestValidHash, parentHash);
    ASSERT_TRUE(status.validationError.has_value());
    EXPECT_NE(status.validationError->find("transactionsRoot"), std::string::npos)
        << "got: " << *status.validationError;
    EXPECT_FALSE(isBlockRegistered(fixture.storage, request.executionPayload.blockHash));
}

// ── #9 unknown parent -> SYNCING, nothing written ─────────────────────────────────────────────
TEST(EngineNewPayloadMutation, UnknownParentIsSyncing)
{
    auto scenario = prepareScenario("isthmus_transfer_basic", /*registerParent=*/false);

    auto status = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));

    EXPECT_EQ(status.status, bcos::engine::PayloadValidationStatus::Syncing);
    EXPECT_FALSE(status.latestValidHash.has_value());
    EXPECT_FALSE(status.validationError.has_value());
    // "不入库": neither the block nor its parent may have been registered by this call.
    EXPECT_FALSE(
        isBlockRegistered(scenario.fixture->storage, scenario.request.executionPayload.blockHash));
    EXPECT_FALSE(isBlockRegistered(scenario.fixture->storage, scenario.parentHash));
}

// ── #10 the same payload, resubmitted after the parent becomes known (裁定 C1) ─────────────────
// op-node's actual retry behaviour: SYNCING is not a verdict, so the identical payload must be
// accepted once the gap closes. Nothing about the request changes between the two calls.
TEST(EngineNewPayloadMutation, SamePayloadResubmittedAfterParentBecomesKnown)
{
    auto scenario = prepareScenario("isthmus_transfer_basic", /*registerParent=*/false);

    auto first = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));
    ASSERT_EQ(first.status, bcos::engine::PayloadValidationStatus::Syncing);

    registerVerifiedBlock(scenario.fixture->storage, scenario.parentHash, 0);

    auto second = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));
    ASSERT_EQ(second.status, bcos::engine::PayloadValidationStatus::Valid)
        << "validationError=" << second.validationError.value_or("<none>");
    ASSERT_TRUE(second.latestValidHash.has_value());
    EXPECT_EQ(*second.latestValidHash, scenario.request.executionPayload.blockHash);
    EXPECT_TRUE(
        isBlockRegistered(scenario.fixture->storage, scenario.request.executionPayload.blockHash));
}

// ── #11 forkchoiceUpdated with attributes -> -38003, head still advances (two assertions) ─────
TEST(EngineNewPayloadMutation, ForkchoiceWithAttributesRefusedButHeadStillAdvances)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");
    const auto blockHash = scenario.request.executionPayload.blockHash;

    auto accepted = bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4));
    ASSERT_EQ(accepted.status, bcos::engine::PayloadValidationStatus::Valid);

    bcos::engine::ForkchoiceState forkchoiceState{
        .headBlockHash = blockHash,
        .safeBlockHash = blockHash,
        .finalizedBlockHash = blockHash,
    };
    bcos::engine::PayloadAttributes attributes;
    attributes.timestamp = scenario.request.executionPayload.timestamp + 2;
    attributes.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    attributes.parentBeaconBlockRoot = *scenario.request.parentBeaconBlockRoot;

    // Assertion 1: the refusal.
    EXPECT_THROW(bcos::task::syncWait(
                     scenario.fixture->service.updateForkchoice(forkchoiceState, &attributes, 3)),
        bcos::engine::UnsupportedOpPayloadAttributes);

    // Assertion 2: the forkchoice state update was NOT rolled back. `getSafeBlockNumber()` is set
    // only by `updateTrackedBlockNumbers`, inside the same locked section as the head update, so
    // an engaged value proves the update took effect before the throw.
    auto safeBlockNumber = scenario.fixture->service.getSafeBlockNumber();
    ASSERT_TRUE(safeBlockNumber.has_value());
    EXPECT_EQ(*safeBlockNumber, scenario.request.executionPayload.blockNumber);

    // And an attribute-less forkchoiceUpdated on the same head is still accepted (it would throw
    // InvalidForkchoiceState if the tracked head had been rolled back).
    auto followUp = bcos::task::syncWait(
        scenario.fixture->service.updateForkchoice(forkchoiceState, nullptr, 3));
    EXPECT_EQ(followUp.payloadStatus.status, bcos::engine::PayloadValidationStatus::Valid);
}

// ── #12 generic composition root, zero drift on V4 ────────────────────────────────────────────
// The V4 relaxation is a per-composition-root constructor argument (task-5a): the generic root
// leaves `maxEngineVersion` at its V3 default, so it must reject V4 exactly as before. Asserted
// here as a regression guard on the OP branch's `if constexpr` dispatch, which the generic root
// must never instantiate (the `!c_opMode` static_assert near the top of this file is the
// compile-time half of the same claim).
TEST(EngineNewPayloadMutation, GenericCompositionRootStillRejectsV4)
{
    StorageFixture storage;
    StubMemPool memPool;
    MockExecutorSerial executor;
    auto ioServicePool = std::make_shared<bcos::IOServicePool>(1, "engineNewPayloadGateTest");
    bcos::scheduler_v1::SchedulerSerialImpl scheduler(ioServicePool);
    auto blockFactory = makeBlockFactory();

    GenericEngineService service(
        memPool, storage.multiLayerStorage, executor, scheduler, blockFactory);

    // A golden request, so the rejection cannot be blamed on a malformed payload: it is the
    // version gate and nothing else. V4 is refused before any executor/scheduler work (裁定 C4).
    auto sample = loadVectorSample("isthmus_transfer_basic");
    auto request = makeGoldenRequest(sample);
    EXPECT_THROW(bcos::task::syncWait(service.newPayload(request, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
    EXPECT_THROW(bcos::task::syncWait(service.getPayload(bcos::engine::PayloadID{"0x1"}, 4)),
        bcos::engine::UnsupportedEngineApiVersion);
}

// ── #13 storage fault -> JSON-RPC -32603, never INVALID ───────────────────────────────────────
//
// The fault is injected into storage2 itself, not scripted at the scheduler seam: one row is
// written into a seeded account's table under a key that is neither a known
// `ACCOUNT_TABLE_FIELDS` name nor a 32-byte storage-slot key — a storage-layout invariant
// violation `Storage2Ledger::fetchAllStorage` refuses to interpret (Storage2Ledger.h:467-471). It
// throws, `visitAccounts` catches and raises the poison flag, and `executeOpBlock`'s step-5
// poison check turns that into `OpStorageError` -> -32603.
//
// The target table is the L2ToL1MessagePasser predeploy's (0x42..16). Deliberate: `executeOpBlock`
// stops the account traversal as soon as it finds that account, so a bogus row in any
// later-sorting table would never be reached — while a bogus row in *its own* table is hit by
// `fetchAllStorage` before the visitor ever runs, whatever the iteration order.
//
// The block executes to completion first; the failure is a fault in this node's storage, not a
// verdict on the payload — which is exactly why it must not come back INVALID (that would make
// the node vote against a block it merely failed to read).
TEST(EngineNewPayloadMutation, StorageLayoutFaultIsInternalErrorNotInvalid)
{
    auto scenario = prepareScenario("isthmus_transfer_basic");

    // "/apps/<hex(addr)>" — `Storage2Ledger::accountTableName`'s encoding, for
    // `bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER`.
    constexpr std::string_view messagePasserTable =
        "/apps/4200000000000000000000000000000000000016";
    constexpr std::string_view injectedFaultKey = "op_gate_injected_layout_fault";
    {
        auto view = scenario.fixture->storage.multiLayerStorage.fork();
        view.newMutable();
        bcos::storage::Entry entry;
        entry.set(std::string{"corrupted"});
        bcos::task::syncWait(bcos::storage2::writeOne(
            view, StateKey{messagePasserTable, injectedFaultKey}, std::move(entry)));
        scenario.fixture->storage.multiLayerStorage.pushView(std::move(view));
    }

    EXPECT_THROW(bcos::task::syncWait(scenario.fixture->service.newPayload(scenario.request, 4)),
        bcos::engine::OpExecutionInternalError);
    // A storage fault leaves no trace of the block: not registered, not published.
    EXPECT_FALSE(
        isBlockRegistered(scenario.fixture->storage, scenario.request.executionPayload.blockHash));
}
