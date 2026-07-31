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
#include "support/WriteFailingStorage.h"

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
        // release-3.18.0 (#5368) split `executeStep<N>()` into prepare/execute/finish; the
        // `TransactionExecutor` concept (TransactionExecutor.h:32-36) now requires all three.
        bcos::task::Task<void> prepare() { co_return; }
        bcos::task::Task<void> execute() { co_return; }
        bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> finish() { co_return {}; }
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

/// ---- hand-rolled RLP writers for the NON-canonical encodings B4-2 must reject ----
///
/// `evmone::rlp::encode*` only ever emits canonical output, so the malformed inputs below have to
/// be assembled byte by byte. These two writers are canonical *framing* around a caller-supplied
/// payload: the non-canonicality lives in the payload the test hands them (a leading zero byte, a
/// 9-byte integer, a 19-byte address, ...), never in the framing itself — otherwise the decoder
/// would reject the frame and the test would pass without ever reaching the field check.
bcos::bytes rlpString(bcos::bytes const& payload)
{
    bcos::bytes out;
    if (payload.size() == 1 && payload[0] < 0x80)
    {
        return payload;  // single low byte encodes as itself
    }
    if (payload.size() < 56)
    {
        out.push_back(static_cast<bcos::byte>(0x80 + payload.size()));
    }
    else
    {
        bcos::bytes lengthBytes;
        for (auto size = payload.size(); size > 0; size >>= 8U)
        {
            lengthBytes.insert(lengthBytes.begin(), static_cast<bcos::byte>(size & 0xFFU));
        }
        out.push_back(static_cast<bcos::byte>(0xb7 + lengthBytes.size()));
        out.insert(out.end(), lengthBytes.begin(), lengthBytes.end());
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bcos::bytes rlpList(bcos::bytes const& payload)
{
    bcos::bytes out;
    if (payload.size() < 56)
    {
        out.push_back(static_cast<bcos::byte>(0xc0 + payload.size()));
    }
    else
    {
        bcos::bytes lengthBytes;
        for (auto size = payload.size(); size > 0; size >>= 8U)
        {
            lengthBytes.insert(lengthBytes.begin(), static_cast<bcos::byte>(size & 0xFFU));
        }
        out.push_back(static_cast<bcos::byte>(0xf7 + lengthBytes.size()));
        out.insert(out.end(), lengthBytes.begin(), lengthBytes.end());
    }
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// The 8 deposit fields as ALREADY-ENCODED RLP items, each defaulting to the canonical encoding a
/// well-formed L1-attributes deposit would carry. A test overrides exactly one of them with a
/// non-canonical encoding, so any rejection can only be attributed to that field.
struct DepositFieldEncodings
{
    bcos::bytes sourceHash{rlpString(bcos::bytes(32, 0x00))};
    bcos::bytes from{rlpString(bcos::bytes(std::begin(bcos::evm::opstack::OP_DEPOSITOR.bytes),
        std::end(bcos::evm::opstack::OP_DEPOSITOR.bytes)))};
    bcos::bytes to{rlpString(bcos::bytes(std::begin(bcos::evm::opstack::OP_L1_BLOCK.bytes),
        std::end(bcos::evm::opstack::OP_L1_BLOCK.bytes)))};
    bcos::bytes mint{rlpString({})};
    bcos::bytes value{rlpString({})};
    bcos::bytes gas{rlpString({0x0f, 0x42, 0x40})};  // 1'000'000
    bcos::bytes isSystemTx{rlpString({})};           // false == empty string
    bcos::bytes data{rlpString({})};

    [[nodiscard]] bcos::bytes envelope() const
    {
        bcos::bytes payload;
        for (auto const* field : {&sourceHash, &from, &to, &mint, &value, &gas, &isSystemTx, &data})
        {
            payload.insert(payload.end(), field->begin(), field->end());
        }
        bcos::bytes out{0x7e};
        auto list = rlpList(payload);
        out.insert(out.end(), list.begin(), list.end());
        return out;
    }
};

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

/// `expectOpConsensusErrorWithMessage` 的对称另一半(终审批 9 F-1)。断言 `invoke()` 抛
/// `OpStorageError`,并按 §11 通则同时配正反两个标识:
///   * **正例标识** —— `what()` 必须含 `expectedSubstring`,即**桥那一层自己**的判据文字。
///     它只可能来自 `Storage2Ledger::poison()` 记下的 `firstError()`;`executeOpBlock` 的
///     两条兜底路径都构造不出这段文字(`catch(...)` 分支自带的是一句固定诊断串)。
///   * **反例标识** —— `what()` **不得**含比对桶 `OpConsensusError` 兜底串的特征前缀
///     ("typed catch bypassed"),且 `OpConsensusError` 这一支被**单独 catch 并当场判失败**、
///     连同它的 `what()` 一起报出,而不是让 `EXPECT_THROW` 只回一句 "wrong exception type"。
/// 反例标识就是本用例存在的全部理由:F-1 之前,`applyDiff` 的 tripwire 抛出时毒旗是 false,
/// 同一注入抛的正是 `OpConsensusError` → 调用侧 INVALID → 本节点对一个**合法** payload 投
/// 反对票并永久分叉(spec §4.3;既有用例 `StorageLayoutFaultIsInternalErrorNotInvalid` 防的
/// 是读路的同一类错误分类,写路此前无人守)。
template <class Invoke>
void expectOpStorageErrorWithMessage(Invoke&& invoke, std::string_view expectedSubstring)
{
    bool threw = false;
    try
    {
        invoke();
    }
    catch (const bcos::evm::engine::OpStorageError& e)
    {
        threw = true;
        const std::string what(e.what());
        EXPECT_NE(what.find(expectedSubstring), std::string::npos)
            << "e.what()=\"" << what << "\" does not contain \"" << expectedSubstring << "\"";
        EXPECT_EQ(what.find("typed catch bypassed"), std::string::npos)
            << "poison message was lost and replaced by the catch(...) fallback diagnostic: "
            << what;
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        ADD_FAILURE() << "classified as OpConsensusError (caller maps it to INVALID — the node "
                         "votes against a legitimate payload) instead of OpStorageError "
                         "(-32603): "
                      << e.what();
    }
    catch (const std::exception& e)
    {
        ADD_FAILURE() << "escaped executeOpBlock unclassified: " << e.what();
    }
    EXPECT_TRUE(threw) << "expected OpStorageError, none thrown";
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

// (d2) 终审批 9 F-1 的分类层见证:`applyDiff` **写回路径**的 tripwire 触发时,
// `executeOpBlock` 必须把它分类成 `OpStorageError`(-32603),而**不是** `OpConsensusError`
// (INVALID)。(d) 覆盖的是**读**路径(ThrowingStorage → 读方法 noexcept 吞异常 + 置毒旗),
// 写路径此前在本层零覆盖:F-1 之前 `applyDiff` 抛出时毒旗是 false,`executeOpBlock` 的
// `poisoned()` 判据落到 else 分支 → `OpConsensusError` → INVALID。
//
// 注入用 support/WriteFailingStorage.h(写路抛、读路直通)。**为什么不是** 桥层同一守护用的
// support/LeakyDeleteStorage.h:实测过,不可达——把 leaky 装饰器包在本用例的向量执行上,
// `removeOne` 一次都没被调到(kVectorId 这个块既无零值槽写入、也无账户删除),用例报
// "none thrown"。applyDiff 的三条形状相关 tripwire(/sys/ 路由、ghost-delete、契约②泄漏)
// 在本层都要靠真实块执行凑出特定的 StateDiff 形状,而"底层存储写入失败"对**任何**块都成立
// (applyDiff 必写 nonce/balance),所以它是分类层唯一稳的写路径见证。见 WriteFailingStorage.h
// 的头注释。读路刻意直通,保证毒旗的**唯一来源**是 applyDiff 的 catch,见证归属不糊。
//
// 与 (d) 的分工:(d) 钉读路毒旗通道(读方法 noexcept 吞异常 + 置旗),(d2) 钉写路毒旗通道
// (applyDiff 置旗后重抛)。注入点、throw 点、置旗点全不同,不是同一条链上的两次断言。
TEST(OpSchedulerImpl, ApplyDiffWriteFailureIsStorageErrorNotInvalid)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    StorageFixture fixture;
    // 种子经**未包装**的 view 落库(注入只在块执行期生效),与 (b)/(c) 完全相同的 seeding
    // 路径,免得把"种子是否落全"这一变量也搅进来。
    seedFromVectorPre(fixture.view, vec.at("pre"));

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    bcos::evm::test::WriteFailingStorage<ViewType> writeFailing(fixture.view);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<decltype(writeFailing)> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    // 正例标识 "injected write failure" 是注入自己的原文:它只能经 applyDiff 的 catch →
    // poison() → firstError() → OpStorageError 这一条通道传到这里(executeOpBlock 的
    // catch(...) 兜底拿不到 what(),会换成自己那句固定诊断串)。反例标识见 helper 注释。
    expectOpStorageErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(writeFailing, env, rawTxBytes)); },
        "injected write failure");
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

// ══════════════ final review batch B, C2: EIP-7702 authorization yParity is uint8-width ═════════
//
// The YParity{2,256} tests above cover the OUTER tx's yParity value-range check (batch 1: v>1 is a
// consensus error). C2 is a DIFFERENT field and a DIFFERENT check: the authorization TUPLE's
// yParity. op-geth's SetCodeAuthorization.V is a uint8 (core/types/tx_setcode.go:76), so a >1-byte
// encoding (256 -> 0x82 0x01 0x00) overflows and fails op-geth's RLP decode, making the whole tx —
// and the block — INVALID. This decoder previously read that field as a full uint256
// (decodeU256Scalar), which silently accepted 256; OpTransition.cpp:67's `auth.v > 1` guard then
// merely SKIPPED that one authorization (correct EIP-7702 value-range semantics) and the block was
// VALID — a consensus split from op-geth (measured). The fix is the encoding-WIDTH check in
// decodeAuthYParityScalar; the value-range side stays a skip, not a reject, matching op-geth.
namespace
{
/// A setcode (0x04) tx carrying exactly one EIP-7702 authorization tuple whose yParity is `authV`.
/// evmone's rlp_encode(Authorization) emits `authV` as a minimal big-endian scalar, so authV=256
/// becomes the 2-byte 0x82 0x01 0x00 — the over-wide encoding C2 must reject at decode.
bcos::bytes buildSetCodeRawTxWithAuth(uint64_t chainId, uint64_t gasLimit,
    const intx::uint256& authV, const intx::uint256& outerYParity, const intx::uint256& r,
    const intx::uint256& s)
{
    evmone::state::Authorization auth;
    auth.chain_id = intx::uint256{chainId};
    auth.addr = kPlaceholderTransferTo;
    auth.nonce = 0;
    auth.v = authV;
    auth.r = intx::uint256{1};
    auth.s = intx::uint256{1};
    evmone::state::AuthorizationList authList{auth};
    auto body = evmone::rlp::encode_tuple(chainId, uint64_t{0}, intx::uint256{0}, intx::uint256{0},
        gasLimit, kPlaceholderTransferTo, intx::uint256{0}, evmc::bytes{},
        evmone::state::AccessList{}, authList, outerYParity, r, s);
    bcos::bytes out{0x04};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/// The RLP bytes of a one-tuple authorization LIST (the argument decodeAuthorizationList consumes),
/// with the tuple's yParity encoded from `authV`. Isolated from tx signing/execution so the width
/// check can be exercised without the outer-tx ecrecover (which a placeholder r/s would fail).
bcos::bytes authorizationListRlp(const intx::uint256& authV)
{
    evmone::state::Authorization auth;
    auth.chain_id = intx::uint256{1};
    auth.addr = kPlaceholderTransferTo;
    auth.nonce = 0;
    auth.v = authV;
    auth.r = intx::uint256{1};
    auth.s = intx::uint256{1};
    evmone::state::AuthorizationList authList{auth};
    auto encoded = evmone::rlp::encode(authList);  // wraps the tuples in a list header
    return bcos::bytes(encoded.begin(), encoded.end());
}
}  // namespace

// C2 (unit, width REJECT): a 2-byte authorization yParity (256) is rejected at decode by the
// width check — op-geth's uint8 cannot hold it. Exercised directly on decodeAuthorizationList so
// the failure is unambiguously the width check and nothing downstream.
TEST(OpSchedulerImpl, AuthorizationYParityOverWideRejectedAtDecode)
{
    auto buf = authorizationListRlp(intx::uint256{256});
    bool threw = false;
    try
    {
        bcos::bytesRef in(buf.data(), buf.size());
        (void)bcos::evm::engine::detail::decodeAuthorizationList(in);
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(
            std::string(e.what()).find("too wide for authorization yParity"), std::string::npos)
            << "e.what()=\"" << e.what() << "\"";
    }
    EXPECT_TRUE(threw) << "expected width rejection for a 2-byte authorization yParity";
}

// C2 (unit, width ACCEPT / boundary): a 1-byte authorization yParity (here 2) is a valid ENCODING
// and must pass the width check — its value-range handling (v>1 -> skip the authorization) is
// EIP-7702 execution semantics (OpTransition.cpp:67), NOT a decode-time rejection. Proves the
// width check does not over-reject; guards against a future "reject v>1 at decode too" edit that
// would diverge from op-geth (which decodes v in [2,255] fine and skips at execution).
TEST(OpSchedulerImpl, AuthorizationYParityOneByteAcceptedAtDecode)
{
    auto buf = authorizationListRlp(intx::uint256{2});
    bcos::bytesRef in(buf.data(), buf.size());
    auto decoded = bcos::evm::engine::detail::decodeAuthorizationList(in);
    ASSERT_EQ(decoded.size(), 1u);
    EXPECT_EQ(decoded[0].v, intx::uint256{2});
}

// C2 (end-to-end, block INVALID): the same over-wide authorization yParity, driven through the
// full scheduler, makes the block INVALID — the consensus outcome op-geth reaches and this
// implementation previously did NOT (it accepted 256 and skipped the authorization, block VALID).
// Index-1 filler pattern (as the B4-2 tests) so the failure is not the L1-attributes-deposit gate.
TEST(OpSchedulerImpl, AuthorizationYParityOverWideIsBlockInvalid)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    std::vector<bcos::bytes> rawTxBytes{leadingL1AttributesDeposit(),
        buildSetCodeRawTxWithAuth(kChainId, 200000, intx::uint256{256}, intx::uint256{0},
            intx::uint256{1}, intx::uint256{1})};

    expectOpConsensusErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
        "too wide for authorization yParity");
}

// ══════════════ final review batch 4: B4-2 non-canonical RLP is rejected ══════════════
//
// Go's `rlp` — which produced every byte this decoder will legitimately see — rejects each of
// these outright (`ErrCanonInt`, "input string too long for uint64", "input string too
// short/long for common.Address|common.Hash", "rlp: invalid boolean value"), and any such failure
// makes op-geth's `DecodeTransactions` reject the whole block. The permissive primitives in
// `bcos-codec/rlp/RLPDecode.h` did not: over-wide integers kept their low 8 bytes, and
// wrong-width fixed fields were right-padded or truncated into a DIFFERENT address/hash — on a
// deposit envelope, which carries no signature, nothing else would have caught that.
//
// Construction notes (both load-bearing, per this file's own earlier lessons):
//   * the malformed transaction is at index 1, behind a well-formed L1-attributes deposit filler.
//     A single-tx block would throw `OpConsensusError` from the unrelated "first tx must be the
//     L1 attributes deposit" gate even with the strictness removed — a false pass.
//   * assertions are on the message, not just the type: `executeOpBlock`'s `catch (...)`
//     reclassifies anything into the same type, so type-only assertions cannot tell "my check
//     fired" from "something else did".
namespace
{
void expectDepositFieldRejected(
    DepositFieldEncodings const& fields, std::string_view expectedSubstring)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    std::vector<bcos::bytes> rawTxBytes{leadingL1AttributesDeposit(), fields.envelope()};

    expectOpConsensusErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
        expectedSubstring);
}
}  // namespace

// (l) B4-2: a scalar with a leading zero byte (`ErrCanonInt`).
TEST(OpSchedulerImpl, NonCanonicalLeadingZeroScalarIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.mint = rlpString({0x00, 0x01});  // canonical would be a bare 0x01
    expectDepositFieldRejected(fields, "non-canonical leading zero");
}

// (m) B4-2: a uint64 field wider than 8 bytes. Previously `fromBigEndian` kept only the low 8
// bytes, i.e. a 9-byte gas value silently became a different, smaller gas value.
TEST(OpSchedulerImpl, OverWideUint64ScalarIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.gas = rlpString({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});  // 9 bytes
    expectDepositFieldRejected(fields, "too wide for uint64");
}

// (n) B4-2: a 19-byte address. This is the sharpest case in the set — a deposit envelope has no
// signature, so a right-padded 19-byte `from` would have been executed as a DIFFERENT account
// with nothing to contradict it.
TEST(OpSchedulerImpl, ShortAddressFieldIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.from = rlpString(bcos::bytes(19, 0x11));
    expectDepositFieldRejected(fields, "wrong length for address");
}

// (o) B4-2: a 33-byte hash (truncation, the mirror of the case above).
TEST(OpSchedulerImpl, OverLongHashFieldIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.sourceHash = rlpString(bcos::bytes(33, 0x22));
    expectDepositFieldRejected(fields, "wrong length for hash");
}

// (p) B4-2: a bool that is neither the empty string nor `0x01` ("rlp: invalid boolean value").
// Two encodings meaning one transaction would mean two block hashes for one block.
TEST(OpSchedulerImpl, NonBooleanBoolFieldIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.isSystemTx = rlpString({0x02});
    expectDepositFieldRejected(fields, "invalid boolean value");
}

// (q) B4-2: the single byte `0x00` as a bool — Go rejects it as a non-canonical integer rather
// than as a bad boolean, and so does this decoder (the canonical encoding of false is the empty
// string). Kept as its own case because it is the encoding a naive writer produces.
TEST(OpSchedulerImpl, ZeroByteBoolFieldIsConsensusError)
{
    DepositFieldEncodings fields;
    fields.isSystemTx = rlpString({0x00});
    expectDepositFieldRejected(fields, "non-canonical leading zero");
}

// ══════════════ final review batch B, C1: non-canonical OUTER FRAMING is rejected at DECODE ══════
//
// C1 is a different failure surface from the B4-2 field-strictness cases above: those mangle a
// *field payload*; this mangles the transaction envelope's own outer RLP list header, writing the
// payload length with a leading-zero-padded multi-byte prefix (0xf9 0x00 <len> instead of the
// canonical 0xf8 <len>). op-geth's rlp readUint() rejects that with ErrCanonSize.
//
// Why this test earns its place (per this file's own B4-2 note, verbatim in spirit):
//   * WITHOUT the C1 fix in bcos-codec/rlp/RLPDecode.h, the shared decodeHeader() ACCEPTS the
//     leading-zero length prefix; the deposit then decodes into the very same Transaction as its
//     canonical twin, and the ONLY thing that would flag the divergence is computeOpTxRoot hashing
//     the raw wire bytes -> a different txRoot from op-geth (a *late*, block-hash-level catch that
//     this test deliberately does not rely on). This asserts the rejection happens at DECODE time.
//   * the mangled tx is at index 1 behind a well-formed L1-attributes deposit filler, so the
//     failure cannot be the unrelated "first tx must be the L1 attributes deposit" gate.
//   * the assertion is on the message substring "leading zero", which only the C1 throw site in
//     RLPDecode.h produces — executeOpBlock's catch(...) fallback cannot synthesise it.
namespace
{
/// Rebuilds a default (valid) L1-attributes-shaped deposit envelope but reframes its OUTER list
/// header non-canonically: the payload length is emitted as a 2-byte big-endian prefix whose high
/// byte is a padding zero (0xf9 0x00 <len>) rather than the minimal single-byte form (0xf8 <len>).
/// The field payloads themselves are the canonical bytes a well-formed deposit carries, so the
/// only non-canonicality is the framing under test.
bcos::bytes nonCanonicalOuterFramedDeposit()
{
    DepositFieldEncodings fields;
    bcos::bytes payload;
    for (auto const* field : {&fields.sourceHash, &fields.from, &fields.to, &fields.mint,
             &fields.value, &fields.gas, &fields.isSystemTx, &fields.data})
    {
        payload.insert(payload.end(), field->begin(), field->end());
    }
    // A default deposit payload is 56..255 bytes, so the canonical header is the single-byte-length
    // long-list form 0xf8 <len>. Emit the non-canonical 2-byte-length form with a leading zero.
    EXPECT_LT(payload.size(), 256u);
    EXPECT_GE(payload.size(), 56u);
    bcos::bytes out{0x7e};
    out.push_back(0xf9);  // LONG_LIST_HEAD_BASE (0xf7) + 2 length bytes
    out.push_back(0x00);  // leading zero -> non-canonical
    out.push_back(static_cast<bcos::byte>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
}  // namespace

TEST(OpSchedulerImpl, NonCanonicalOuterFramingIsRejectedAtDecode)
{
    MutableStorage storage;
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<MutableStorage> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    bcostars::protocol::BlockHeaderImpl header;
    header.setNumber(1);
    header.setTimestamp(1000);
    auto env = minimalEnv(header);

    std::vector<bcos::bytes> rawTxBytes{
        leadingL1AttributesDeposit(), nonCanonicalOuterFramedDeposit()};

    expectOpConsensusErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(storage, env, rawTxBytes)); },
        "leading zero");
}
