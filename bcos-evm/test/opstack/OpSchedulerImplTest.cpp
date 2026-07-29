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
#include <bcos-evm/opstack/OpPredeploys.h>
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
#include <evmone_precompiles/secp256k1.hpp>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <bcos-evm/eth/state/transaction.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <bcos-evm/eth/utils/statetest.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

#include "support/ThrowingStorage.h"

using Json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{
using namespace evmc::literals;

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

// ---- synthetic raw-tx builders for the malformed-input tests below (coordinator review items
// C2/C3/C4 + the yParity test gap) ----
//
// All four checks these builders exercise fire during Step 1 (decode), strictly before Step 2
// (Storage2Ledger bridge construction) — so none of the tests below need a real storage2 fixture,
// a genuine ECDSA signature, or even well-formed r/s values except where a test is *specifically*
// exercising the r/s range check itself: the gas-limit check (C4) fires while reading field 5
// (gas_limit), before yParity/r/s (fields 10-12) are ever decoded; the chain-id check (C2) fires
// while reading field 1 (chain_id), before everything else; the yParity range check fires while
// reading field 10, before r/s (fields 11-12); the low-s check (C3) is the only one that runs
// after full decode, inside recoverTxSender, which is why its test needs every earlier field
// (chain id, gas limit, yParity) to be valid.
//
// `to` is a fixed placeholder address (a plain value transfer), NOT contract-creation
// (evmc::bytes_view{}/nullopt): an earlier draft of these builders used contract-creation, which
// needs TX_BASE_COST + TX_CREATE_COST = 21000 + 32000 = 53000 intrinsic gas
// (eth/state/state.cpp::compute_tx_intrinsic_cost) — every "otherwise valid" test tx in this file
// uses gasLimit=21000, so that draft's contract-creation shape failed intrinsic-gas validation
// for reasons having nothing to do with whichever field a given test claims to isolate, silently
// making every C2/C3/(non-huge-gas-limit) test pass regardless of whether its target check
// actually fired — caught by this task's own red/green self-verification (see
// final-batch1-report.md). A plain transfer's floor is exactly TX_BASE_COST = 21000, matching
// every gasLimit=21000 test tx here precisely (not by a wide margin that could mask a different
// off-by-one).
//
// Every negative test in this file signs with `r = intx::uint256{1}` (see the individual TEST
// bodies below) and relies on `evmmax::secp256k1::ecrecover` actually recovering *some* address
// from it — every check under test except C3 lives *before* `recoverTxSender` runs, but if
// ecrecover itself threw ("sender ecrecover failed") first, that exception would also be an
// OpConsensusError and could silently make a test "pass" for the wrong reason again, the same
// class of bug this file's other builder-shape fixes were chasing. `r = 1` is not an arbitrary
// "small nonzero value" that happens to work — it IS a valid x-coordinate on secp256k1
// (`y^2 = x^3 + 7` at x=1 gives `y^2 = 8`, and 2 is a quadratic residue mod this curve's field
// prime `p` because `p ≡ 7 (mod 8)`, so a square root exists), so ecrecover always succeeds for
// it regardless of `s`/`yParity`/the message hash. This constraint is coordinate-specific, not
// value-specific: if a future edit changes `r` to some other small constant without checking it
// is also a valid x-coordinate, ecrecover may start failing and every test relying on execution
// reaching *past* sender recovery would go back to silently passing for the wrong reason.
inline constexpr evmc::address kPlaceholderTransferTo =
    0x0000000000000000000000000000000000dead_address;

bcos::bytes buildEip1559RawTx(uint64_t chainId, uint64_t gasLimit, const intx::uint256& yParity,
    const intx::uint256& r, const intx::uint256& s)
{
    auto body = evmone::rlp::encode_tuple(chainId, uint64_t{0}, intx::uint256{0}, intx::uint256{0},
        gasLimit, kPlaceholderTransferTo, intx::uint256{0}, evmc::bytes{},
        evmone::state::AccessList{}, yParity, r, s);
    bcos::bytes out{0x02};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bcos::bytes buildSetCodeRawTx(uint64_t chainId, uint64_t gasLimit, const intx::uint256& yParity,
    const intx::uint256& r, const intx::uint256& s)
{
    auto body = evmone::rlp::encode_tuple(chainId, uint64_t{0}, intx::uint256{0}, intx::uint256{0},
        gasLimit, kPlaceholderTransferTo, intx::uint256{0}, evmc::bytes{},
        evmone::state::AccessList{}, evmone::state::AuthorizationList{}, yParity, r, s);
    bcos::bytes out{0x04};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bcos::bytes buildDepositRawTx(uint64_t gasLimit)
{
    // [sourceHash, from, to, mint, value, gas, isSystemTransaction, data] — decodeDepositTx's
    // field order (OpSchedulerImpl.h). `from`/`to` are set to OP_DEPOSITOR/OP_L1_BLOCK
    // (bcos-evm/bcos-evm/opstack/OpPredeploys.h) — NOT arbitrary placeholders: as the block's
    // only transaction, this deposit must satisfy processOpBlock's own "first tx is the L1
    // attributes deposit" gate (OpBlockExecute.cpp) to reach the deeper runDeposit/
    // validate_transaction path the gas_limit narrowing bug (C4) actually lives in — an earlier
    // draft of this builder used zero addresses here and the resulting test passed even with the
    // narrowing fix reverted, for the wrong reason (it was tripping the unrelated "not the L1
    // attributes deposit" check, already covered by FirstTxNotAttributesDepositIsConsensusError,
    // never reaching the gas_limit code path at all). isSystemTransaction is encoded via the
    // uint64_t{0} "empty string" shape decodeBoolField expects (see OpSchedulerImpl.h's
    // decodeBoolField comment on why a native RLP bool encoding would be the wrong match here).
    auto body = evmone::rlp::encode_tuple(evmc::bytes32{}, bcos::evm::opstack::OP_DEPOSITOR,
        bcos::evm::opstack::OP_L1_BLOCK, intx::uint256{0}, intx::uint256{0}, gasLimit, uint64_t{0},
        evmc::bytes{});
    bcos::bytes out{0x7e};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/// A well-formed L1-attributes deposit (index-0 filler) for the eip1559/setcode malformed-input
/// tests below. Those tests need their malicious tx to be at index 1, NOT index 0: processOpBlock
/// requires the block's first transaction to satisfy "is the L1 attributes deposit"
/// (OpBlockExecute.cpp) — a bare single-tx block whose only transaction is a typed (non-deposit)
/// tx would, if the check under test happened to be broken and let decode succeed, still throw
/// `OpConsensusError` from that *unrelated* gate once Step 3 (processOpBlock) runs, producing the
/// exact same exception *type* the test asserts on and masking a real regression as a false
/// "still passes". (Caught during this task's own red/green self-verification: an earlier
/// single-tx-block draft of these tests stayed green even with the guard under test deleted, for
/// this reason — see the coordinator-review self-verification notes in
/// final-batch1-report.md.) Decode failures on the malicious tx (index 1) still fire during
/// Step 1, before processOpBlock/Step 3 ever runs — so this filler deposit's own semantic
/// validity under execution never matters, only that it decodes without error and passes the
/// first-tx gate; `buildDepositRawTx`'s own gas value is deliberately unrelated to whatever gas
/// value the malicious index-1 tx carries.
bcos::bytes leadingL1AttributesDeposit()
{
    return buildDepositRawTx(1'000'000);
}

/// A minimal, self-consistent OpBlockEnv for the decode-only failure tests below — field values
/// beyond `fiscoHeader` are never read (decode throws before `executeOpBlock` reaches
/// `detail::toBlockInfo`), so zero/empty defaults are sufficient.
bcos::evm::engine::OpBlockEnv minimalEnv(const bcos::protocol::BlockHeader& header)
{
    return bcos::evm::engine::OpBlockEnv{
        .fiscoHeader = header,
        .parentHash = {},
        .prevRandao = {},
        .baseFeePerGas = 0,
        .feeRecipient = {},
        .parentBeaconBlockRoot = {},
        .gasLimit = 30000000,
        .extraData = {},
        .blobGasUsed = 0,
    };
}

/// Asserts `invoke()` throws `bcos::evm::engine::OpConsensusError` whose `what()` contains
/// `expectedSubstring` (coordinator review I-1). Plain `EXPECT_THROW(..., OpConsensusError)` only
/// distinguishes exception *type*, and `OpSchedulerImpl::executeOpBlock`'s `catch(...)` fallback
/// (its own file header comment: "Typed-catch RTTI bypass") reclassifies *any* uncaught
/// exception from `processOpBlock` into the same `OpConsensusError` type with a generic,
/// check-independent message — so for a check whose removal still leaves some *other*,
/// unrelated block-level validation failing (this file's setcode gas-limit/chain-id sub-cases;
/// see `final-batch1-report.md`'s I-1 section for which specific sub-cases need this and why),
/// type-only assertion cannot tell "my check fired" apart from "something else fired instead".
/// Message-substring assertion can, because the guard under test's own throw site produces a
/// literal, field-name-qualified message (`narrowGasLimit`'s `fieldName` parameter, M-2) that
/// the catch(...) fallback's fixed diagnostic string never contains. Reuses this file's own
/// `ExecuteBlockThrowsInOpMode` test's `std::string(e.what()).find(...) != npos` idiom rather
/// than pulling in gmock's `HasSubstr` (this test binary does not otherwise link GTest::gmock).
template <class Invoke>
void expectOpConsensusErrorWithMessage(Invoke&& invoke, std::string_view expectedSubstring)
{
    bool threw = false;
    try
    {
        invoke();
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find(expectedSubstring), std::string::npos)
            << "e.what()=\"" << e.what() << "\" does not contain \"" << expectedSubstring << "\"";
    }
    EXPECT_TRUE(threw) << "expected OpConsensusError, none thrown";
}

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

// ---- final-batch1 coordinator review: C2/C3/C4 + yParity test-gap regression tests ----
// All six below use a bare MutableStorage with no seeding — decode failures happen in Step 1,
// strictly before the Storage2Ledger bridge is ever touched (see the builder-helpers comment
// above `minimalEnv`).

// (f) C4: a canonical 8-byte deposit gas scalar 0xFFFFFFFFFFFFFFFF must not silently become a
// negative int64_t (the coordinator-traced blockGasLeft-inflation / gasUsed-wraparound chain).
TEST(OpSchedulerImpl, DepositGasLimitOverflowIsConsensusError)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    std::vector<bcos::bytes> rawTxBytes{buildDepositRawTx(std::numeric_limits<uint64_t>::max())};

    EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
        bcos::evm::engine::OpConsensusError);
}

// (g) C4: the same narrowing guard applied at the eip1559 and set-code gas_limit sites — the
// coordinator's review explicitly calls out that these two must not rely on the "signature
// preimage happens to self-destruct" coincidence. The malicious tx is index 1 of a two-tx block
// (index 0 is a well-formed L1-attributes deposit filler) — see leadingL1AttributesDeposit's
// comment for why a bare single-tx block would falsely "pass" this test regardless of whether
// the guard under test actually fires.
TEST(OpSchedulerImpl, TypedTxGasLimitOverflowIsConsensusError)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);
    const auto hugeGas = std::numeric_limits<uint64_t>::max();

    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildEip1559RawTx(kChainId, hugeGas, intx::uint256{0},
                                              intx::uint256{1}, intx::uint256{1})};
        EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
            bcos::evm::engine::OpConsensusError)
            << "eip1559";
    }
    {
        // setcode sub-case (coordinator review I-1): plain EXPECT_THROW(..., OpConsensusError)
        // cannot tell this guard's own throw apart from executeOpBlock's catch(...) RTTI-bypass
        // fallback reclassifying some *other*, unrelated block-level failure into the same
        // exception type — both paths are OpConsensusError. Message-substring assertion against
        // narrowGasLimit's field-name-qualified text (M-2) distinguishes them; see
        // expectOpConsensusErrorWithMessage's doc comment and final-batch1-report.md's I-1
        // section for the full rationale and the delete-and-rebuild evidence that motivated it.
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildSetCodeRawTx(kChainId, hugeGas, intx::uint256{0},
                                              intx::uint256{1}, intx::uint256{1})};
        expectOpConsensusErrorWithMessage(
            [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
            "gas limit exceeds int64_t range: setcode.gasLimit");
    }
}

// (h) C2: a transaction whose decoded chain_id does not match the scheduler's own chainId must
// be rejected — nothing downstream (validate_transaction) checks chain id, so an unchecked
// cross-chain replay would otherwise execute normally and produce a VALID block. Two-tx block,
// same rationale as (g).
TEST(OpSchedulerImpl, TypedTxChainIdMismatchIsConsensusError)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);
    constexpr uint64_t kWrongChainId = kChainId + 1;

    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildEip1559RawTx(kWrongChainId, 21000, intx::uint256{0},
                                              intx::uint256{1}, intx::uint256{1})};
        EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
            bcos::evm::engine::OpConsensusError)
            << "eip1559";
    }
    {
        // setcode sub-case (coordinator review I-1): same rationale as
        // TypedTxGasLimitOverflowIsConsensusError's setcode sub-case above — the chain-id check's
        // own throw text ("chain id mismatch (setcode)") is distinguishable from the catch(...)
        // fallback's generic message, which type-only EXPECT_THROW cannot tell apart.
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildSetCodeRawTx(kWrongChainId, 21000, intx::uint256{0},
                                              intx::uint256{1}, intx::uint256{1})};
        expectOpConsensusErrorWithMessage(
            [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
            "chain id mismatch (setcode)");
    }
}

// (i) C3: a signature with s > secp256k1n/2 (the EIP-2 malleable form of an otherwise-valid
// signature) must be rejected, even though the plain ECRECOVER-PRECOMPILE semantics
// (evmmax::secp256k1::ecrecover) would happily recover a sender from it. r/s here are not a real
// signature over this preimage — the check under test fires before any consistency with the
// preimage would even matter, exactly like every other malformed-input test in this file.
// Two-tx block, same rationale as (g)/(h).
TEST(OpSchedulerImpl, HighSSignatureIsConsensusError)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    constexpr auto kSecpOrder = evmmax::secp256k1::Curve::ORDER;
    const intx::uint256 highS = kSecpOrder - 1;  // > n/2 for this curve's n.
    std::vector<bcos::bytes> rawTxBytes{leadingL1AttributesDeposit(),
        buildEip1559RawTx(kChainId, 21000, intx::uint256{0}, intx::uint256{1}, highS)};

    EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
        bcos::evm::engine::OpConsensusError);
}

// (j)/(k) test-gap item: yParity > 1 rejection had zero test coverage (the review's own repro:
// deleting the `if (yParity > 1) throw` lines still left 206/206 green). Two boundary values:
// yParity=2 (smallest invalid value) and yParity=256 (probes the exact truncation hazard the
// I1 fix's own comment describes — `static_cast<uint8_t>(256) == 0`, which would silently look
// like a valid parity=0 if the guard were absent). Two-tx blocks, same rationale as (g)/(h)/(i).
//
// I-2 (coordinator review, second-round): decodeEip1559Tx's and decodeSetCodeTx's
// `if (yParity > 1) throw` lines are two INDEPENDENT copy-pasted statements, not one shared
// function — unlike C3's `requireLowSSignature`, which is a single function both decoders call
// through. A first draft of this test file only ever exercised `buildEip1559RawTx`, leaving
// decodeSetCodeTx's copy with zero coverage (confirmed by the coordinator's own
// delete-and-rebuild experiment: removing just the setcode line still left 212/212 green). Both
// tests below now cover both decoders. The setcode sub-case's authorization_list stays empty
// (`buildSetCodeRawTx`'s own shape) rather than adding EIP-7702 authorization tuples — the
// coordinator's review separately ruled out a non-empty-authorization-list construction as a
// viable alternative test vector here: each authorization tuple adds a flat 25000 intrinsic-gas
// cost (`AUTHORIZATION_EMPTY_ACCOUNT_COST`, eth/state/state.cpp), which this file's fixed
// gasLimit=21000 test transactions could never cover regardless of what's being tested.
TEST(OpSchedulerImpl, YParityEquals2IsConsensusError)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildEip1559RawTx(kChainId, 21000, intx::uint256{2},
                                              intx::uint256{1}, intx::uint256{1})};
        EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
            bcos::evm::engine::OpConsensusError)
            << "eip1559";
    }
    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildSetCodeRawTx(kChainId, 21000, intx::uint256{2},
                                              intx::uint256{1}, intx::uint256{1})};
        // setcode sub-case needs message assertion, not plain EXPECT_THROW (coordinator review
        // I-1's rationale generalizes here too, confirmed by this task's own diagnostic run):
        // with r=1/yParity!=0 still recoverable, decode succeeds all the way through when the
        // guard is disabled, and *something* downstream in processOpBlock's execution of a
        // truncated tx.v still throws — via the same catch(...) RTTI-bypass fallback, same
        // generic message, same OpConsensusError type. Only the guard's own message text
        // ("invalid y parity (setcode)") distinguishes "my check fired" from "something else did".
        expectOpConsensusErrorWithMessage(
            [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
            "invalid y parity (setcode)");
    }
}

TEST(OpSchedulerImpl, YParityEquals256IsConsensusError)
{
    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildEip1559RawTx(kChainId, 21000, intx::uint256{256},
                                              intx::uint256{1}, intx::uint256{1})};
        EXPECT_THROW(bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)),
            bcos::evm::engine::OpConsensusError)
            << "eip1559";
    }
    {
        MutableStorage storage;
        auto receiptFactory = makeReceiptFactory();
        bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
            bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});
        std::vector<bcos::bytes> rawTxBytes{
            leadingL1AttributesDeposit(), buildSetCodeRawTx(kChainId, 21000, intx::uint256{256},
                                              intx::uint256{1}, intx::uint256{1})};
        // setcode sub-case: same message-assertion rationale as YParityEquals2's setcode
        // sub-case above.
        expectOpConsensusErrorWithMessage(
            [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
            "invalid y parity (setcode)");
    }
}
