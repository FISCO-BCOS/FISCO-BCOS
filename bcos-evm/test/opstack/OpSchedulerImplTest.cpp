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

#include <bcos-codec/rlp/OpReceiptMetaCodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
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
#include <bcos-evm/eth/state/system_contracts.hpp>
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
#include <bcos-evm/eth/utils/rlp_encode.hpp>
#include <bcos-evm/eth/utils/statetest.hpp>
#include <bcos-evm/eth/utils/test_state.hpp>

#include "support/ThrowOnNumber2Hash.h"
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

// The receipt's opReceiptMeta must reach the RPC layer. kVectorId is an Isthmus vector, so the
// execution layer's deriveOpReceiptMeta records operator fees (Isthmus+) but no DA footprint
// (Jovian-only). This pins the full path: OpTransition computes the meta → OpSchedulerImpl's
// mapOpReceipt serializes it into the bcos receipt → the RPC layer can decode it back into the
// op-geth field names.
TEST(OpSchedulerImpl, ExecuteOpBlockCarriesReceiptMetaToRpcLayer)
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

    ASSERT_EQ(result.receipts.size(), rawTxBytes.size());
    ASSERT_EQ(rawTxBytes.size(), 2u) << "vector: attributes deposit + one EIP-1559 transfer";
    // blockNumber is stamped by the execution layer (not the factory default 0).
    for (auto const& receipt : result.receipts)
    {
        ASSERT_NE(receipt, nullptr);
        EXPECT_EQ(receipt->blockNumber(), env.fiscoHeader.number());
    }

    // Tx 0 is the L1 attributes deposit: its meta carries depositNonce/depositReceiptVersion, no
    // L1/operator fees (deposits have no L1 cost — runDeposit, OpBlockExecute.cpp:113).
    {
        auto metaView = result.receipts[0]->opReceiptMeta();
        ASSERT_FALSE(metaView.empty());
        bcos::codec::rlp::OpReceiptMetaFields fields;
        ASSERT_EQ(
            bcos::codec::rlp::decodeOpReceiptMeta(
                bcos::bytesConstRef{(bcos::byte const*)metaView.data(), metaView.size()}, fields),
            nullptr);
        EXPECT_FALSE(fields.l1_gas_price.has_value());
        ASSERT_TRUE(fields.deposit_nonce.has_value());
        EXPECT_EQ(*fields.deposit_nonce, 0u);
        ASSERT_TRUE(fields.deposit_receipt_version.has_value());
        EXPECT_EQ(*fields.deposit_receipt_version, 1u);  // Canyon+
    }

    // Tx 1 is the user EIP-1559 transfer: full L1 passthrough + operator scalars (Isthmus), no
    // DA footprint (Jovian-only). This is the data the RPC layer turns into op-geth's
    // l1GasPrice/l1Fee/operatorFeeScalar/... fields.
    {
        auto metaView = result.receipts[1]->opReceiptMeta();
        ASSERT_FALSE(metaView.empty());
        bcos::codec::rlp::OpReceiptMetaFields fields;
        ASSERT_EQ(
            bcos::codec::rlp::decodeOpReceiptMeta(
                bcos::bytesConstRef{(bcos::byte const*)metaView.data(), metaView.size()}, fields),
            nullptr);
        // This vector seeds L1 fee params (slot1/3/7) but not slot8 (operator fee), so the user
        // tx carries L1 passthrough but no operator scalars — assert exactly that shape.
        ASSERT_TRUE(fields.l1_gas_price.has_value());
        ASSERT_TRUE(fields.l1_fee.has_value());
        EXPECT_FALSE(fields.operator_fee_scalar.has_value());
        EXPECT_FALSE(fields.deposit_nonce.has_value()) << "normal txs are not deposits";
        EXPECT_FALSE(fields.da_footprint.has_value()) << "DA footprint is Jovian-only";

        // G3: effective gas price lands on the user tx receipt (was empty -> RPC "0x0"), never on
        // the deposit receipt (deposits have no fee market; op-geth deposit effectiveGasPrice = 0).
        EXPECT_FALSE(result.receipts[1]->effectiveGasPrice().empty());
        EXPECT_TRUE(result.receipts[1]->effectiveGasPrice().starts_with("0x"));
        // Stronger: distinguish "set to a real value" from the empty-string (deposit) fallback — a
        // user tx whose effective price is 0 is set to "0x0", never left empty.
        EXPECT_NE(result.receipts[1]->effectiveGasPrice(), "0x0");
        EXPECT_TRUE(result.receipts[0]->effectiveGasPrice().empty());
    }
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

// ---- Batch D-2 / D-4: system-call failure classification + rollback ----
// Both tests seed a write-then-revert contract at an EIP system-address (`0x600160005560006000fd`
// = PUSH1 1; PUSH1 0; SSTORE slot0=1; PUSH1 0; PUSH1 0; REVERT) so the pre-D-2 code would have
// leaked the SSTORE (top-level vm.execute has no journal boundary — the previous assert() was
// compiled away under -DNDEBUG) AND misclassified the failure. op-geth anchors in
// core/state_processor.go: ProcessBeaconBlockRoot *ignores* the call error (`_, _, _ = evm.Call`),
// ProcessParentBlockHash *panics* (`if err != nil { panic(err) }`).

// (d3) EIP-2935 history-storage failure is a LOCAL fault → -32603 (OpStorageError), not INVALID.
TEST(OpSchedulerImpl, SystemCallHistoryStorageRevertIsStorageError)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    StorageFixture fixture;
    auto pre = vec.at("pre");
    pre["0x0000F90827F1C53A10CB7A02335B175320002935"] = Json{
        {"balance", "0x0"},
        {"nonce", "0x1"},
        {"code", "0x600160005560006000fd"},
        {"storage", Json::object()},
    };
    seedFromVectorPre(fixture.view, pre);

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    // The reverted call was rolled back then surfaced as a local fault. The message can only come
    // from system_contracts.cpp's std::logic_error → executeOpBlock's typed-catch local-fault
    // branch (OpStorageError); the catch(...) fallback's fixed diagnostic string never contains
    // it, and the OpConsensusError branch is asserted-failed by the helper if misclassified.
    expectOpStorageErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes)); },
        "system contract call failed");
}

// (d4) EIP-4788 beacon-roots failure is IGNORED (op-geth parity) and its writes rolled back.
TEST(OpSchedulerImpl, SystemCallBeaconRootsRevertIsIgnored)
{
    auto loaded = LoadedVector::load();
    Json const& vec = vectorBody(loaded.vectors, kVectorId);
    Json const& golden = loaded.golden;

    StorageFixture fixture;
    auto pre = vec.at("pre");
    pre["0x000F3df6D732807Ef1319fB7B8bB8522d0Beac02"] = Json{
        {"balance", "0x0"},
        {"nonce", "0x1"},
        {"code", "0x600160005560006000fd"},
        {"storage", Json::object()},
    };
    seedFromVectorPre(fixture.view, pre);

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 100000});

    // No throw — the block executes exactly as if the beacon-roots call had succeeded.
    auto result = bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes));
    EXPECT_EQ(result.receipts.size(), rawTxBytes.size());

    // And the rollback worked: the write-then-revert contract left NO storage behind (the old
    // assert()-only code compiled away would have leaked slot0=1 into the diff).
    bcos::evm::ledger::Storage2Ledger<ViewType> readBridge(fixture.view);
    bool beaconRootsSeen = false;
    readBridge.visitAccounts([&](const auto& accountView) {
        if (accountView.addr == evmone::state::BEACON_ROOTS_ADDRESS)
        {
            beaconRootsSeen = true;
            EXPECT_TRUE(accountView.storage.empty())
                << "beacon-roots REVERT leaked writes into the state";
            return false;
        }
        return true;
    });
    EXPECT_TRUE(beaconRootsSeen) << "beacon-roots account not present after execution";
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

// ══════════════ final review batch B, 1-1: legacy & EIP-2930 (0x01) decode support ══════════════
//
// decodeOneRawTx previously accepted only {0x7e, 0x02, 0x04}; the execution variant
// (OpBlockExecute.h -> state.cpp:460-475) has always handled legacy/access_list too, so the gap was
// purely in the decoder (the old "only these three shapes are understood" comment was false — it
// mirrored the t8n corpus, not a capability boundary). These tests sign REAL transactions with
// secp256k1 and assert the decoder recovers the correct sender — the decisive correctness check,
// since a wrong signing-preimage or field parse yields a wrong recovered address. §6.4 item n:
// legacy's EIP-155/pre-155 forms and v-canonicality are exercised, plus the chain-id cross-check.
namespace
{
struct RealSigner
{
    std::shared_ptr<bcos::crypto::Keccak256> hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    std::shared_ptr<bcos::crypto::Secp256k1Crypto> sig =
        std::make_shared<bcos::crypto::Secp256k1Crypto>();
    std::unique_ptr<bcos::crypto::KeyPairInterface> keyPair = sig->generateKeyPair();

    /// The signer's address, derived independently of ecrecover (keccak(pubkey)[12:]), so matching
    /// it against the decoder's recovered sender is a real check rather than a tautology.
    [[nodiscard]] evmc::address expectedSender() const
    {
        auto addr = keyPair->address(hashImpl);
        evmc::address out{};
        std::memcpy(out.bytes, addr.data(), sizeof(out.bytes));
        return out;
    }

    /// Sign `preimage` (the exact bytes the decoder will keccak+ecrecover over) and return
    /// (r, s, recid). secp256k1Sign yields a low-s canonical signature, which recoverTxSender's
    /// EIP-2 guard requires.
    [[nodiscard]] std::tuple<intx::uint256, intx::uint256, uint8_t> sign(
        const bcos::bytes& preimage) const
    {
        auto hash = bcos::crypto::keccak256Hash(bcos::ref(preimage));
        auto sigBytes = sig->sign(*keyPair, hash, true);  // 65 bytes: r(32) || s(32) || recid(1)
        evmc::bytes32 rb{};
        evmc::bytes32 sb{};
        std::memcpy(rb.bytes, sigBytes->data(), 32);
        std::memcpy(sb.bytes, sigBytes->data() + 32, 32);
        return {
            intx::be::load<intx::uint256>(rb), intx::be::load<intx::uint256>(sb), sigBytes->at(64)};
    }
};

bcos::bytes toBcos(const evmc::bytes& in)
{
    return {in.begin(), in.end()};
}
}  // namespace

// 1-1 (legacy, pre-EIP-155): v ∈ {27,28}, 6-item signing preimage, no chain id. The decoder must
// recover the signer and classify it as legacy.
TEST(OpSchedulerImpl, LegacyPre155DecodesAndRecoversSender)
{
    RealSigner signer;
    const evmc::address to = kPlaceholderTransferTo;
    const uint64_t nonce = 7;
    const intx::uint256 gasPrice = 1'000'000'000;
    const uint64_t gasLimit = 21000;
    const intx::uint256 value = 12345;

    auto preimage = evmone::rlp::encode_tuple(
        nonce, gasPrice, gasLimit, evmc::bytes_view(to), value, evmc::bytes{});
    auto [r, s, recid] = signer.sign(toBcos(preimage));
    const intx::uint256 v = intx::uint256{27} + recid;  // pre-155
    auto env = evmone::rlp::encode_tuple(
        nonce, gasPrice, gasLimit, evmc::bytes_view(to), value, evmc::bytes{}, v, r, s);

    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(toBcos(env), kChainId);
    const auto& tx = std::get<evmone::state::Transaction>(decoded.tx);
    EXPECT_EQ(tx.type, evmone::state::Transaction::Type::legacy);
    EXPECT_EQ(tx.sender, signer.expectedSender());
    EXPECT_EQ(tx.nonce, nonce);
    EXPECT_EQ(tx.chain_id, 0u);  // pre-155 carries no chain id
    ASSERT_TRUE(tx.to.has_value());
    EXPECT_EQ(*tx.to, to);
}

// 1-1 (legacy, EIP-155): v = chainId*2 + 35 + parity, 9-item signing preimage (…chainId, 0, 0).
// With kChainId = 0x2105 the full v (16941+) overflows a uint8, which is exactly why the decoder
// reads v as a wide scalar and derives parity/chainId rather than round-tripping it through tx.v.
TEST(OpSchedulerImpl, LegacyEip155DecodesAndRecoversSender)
{
    RealSigner signer;
    const evmc::address to = kPlaceholderTransferTo;
    const uint64_t nonce = 3;
    const intx::uint256 gasPrice = 2'000'000'000;
    const uint64_t gasLimit = 21000;
    const intx::uint256 value = 99;

    auto preimage = evmone::rlp::encode_tuple(nonce, gasPrice, gasLimit, evmc::bytes_view(to),
        value, evmc::bytes{}, kChainId, uint64_t{0}, uint64_t{0});
    auto [r, s, recid] = signer.sign(toBcos(preimage));
    const intx::uint256 v = intx::uint256{kChainId} * 2 + 35 + recid;  // EIP-155
    auto env = evmone::rlp::encode_tuple(
        nonce, gasPrice, gasLimit, evmc::bytes_view(to), value, evmc::bytes{}, v, r, s);

    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(toBcos(env), kChainId);
    const auto& tx = std::get<evmone::state::Transaction>(decoded.tx);
    EXPECT_EQ(tx.type, evmone::state::Transaction::Type::legacy);
    EXPECT_EQ(tx.sender, signer.expectedSender());
    EXPECT_EQ(tx.chain_id, kChainId);
    EXPECT_EQ(tx.nonce, nonce);
}

// 1-1 (EIP-2930 / 0x01): [chainId, nonce, gasPrice, gasLimit, to, value, data, accessList, yParity,
// r, s]; signing preimage = 0x01 || rlp([…first 8 fields]).
TEST(OpSchedulerImpl, AccessListTxDecodesAndRecoversSender)
{
    RealSigner signer;
    const evmc::address to = kPlaceholderTransferTo;
    const uint64_t nonce = 5;
    const intx::uint256 gasPrice = 3'000'000'000;
    const uint64_t gasLimit = 30000;
    const intx::uint256 value = 42;
    const evmone::state::AccessList accessList{};

    auto preimage =
        evmc::bytes{0x01} + evmone::rlp::encode_tuple(kChainId, nonce, gasPrice, gasLimit,
                                evmc::bytes_view(to), value, evmc::bytes{}, accessList);
    auto [r, s, recid] = signer.sign(toBcos(preimage));
    auto env = evmc::bytes{0x01} + evmone::rlp::encode_tuple(kChainId, nonce, gasPrice, gasLimit,
                                       evmc::bytes_view(to), value, evmc::bytes{}, accessList,
                                       intx::uint256{recid}, r, s);

    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(toBcos(env), kChainId);
    const auto& tx = std::get<evmone::state::Transaction>(decoded.tx);
    EXPECT_EQ(tx.type, evmone::state::Transaction::Type::access_list);
    EXPECT_EQ(tx.sender, signer.expectedSender());
    EXPECT_EQ(tx.chain_id, kChainId);
    EXPECT_EQ(tx.nonce, nonce);
    // legacy/2930 single gasPrice is mirrored into both fee fields (validate_transaction needs
    // priority<=cap; state.cpp:466).
    EXPECT_EQ(tx.max_gas_price, gasPrice);
    EXPECT_EQ(tx.max_priority_gas_price, gasPrice);
}

// 1-1 (chain-id cross-check): an EIP-155 legacy tx signed for a DIFFERENT chain is rejected at
// decode — same scope as the typed decoders' chain-id check (a tx for another chain must not
// replay here as VALID). Signed for chainId 1, decoded against kChainId.
TEST(OpSchedulerImpl, LegacyEip155WrongChainIdRejected)
{
    RealSigner signer;
    const evmc::address to = kPlaceholderTransferTo;
    const uint64_t wrongChainId = 1;
    auto preimage = evmone::rlp::encode_tuple(uint64_t{0}, intx::uint256{1}, uint64_t{21000},
        evmc::bytes_view(to), intx::uint256{0}, evmc::bytes{}, wrongChainId, uint64_t{0},
        uint64_t{0});
    auto [r, s, recid] = signer.sign(toBcos(preimage));
    const intx::uint256 v = intx::uint256{wrongChainId} * 2 + 35 + recid;
    auto env = evmone::rlp::encode_tuple(uint64_t{0}, intx::uint256{1}, uint64_t{21000},
        evmc::bytes_view(to), intx::uint256{0}, evmc::bytes{}, v, r, s);

    bool threw = false;
    try
    {
        (void)bcos::evm::engine::detail::decodeOneRawTx(toBcos(env), kChainId);
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("chain id mismatch (legacy)"), std::string::npos)
            << "e.what()=\"" << e.what() << "\"";
    }
    EXPECT_TRUE(threw);
}

// 1-1 / §6.4 item n: legacy v-canonicality is enforced with the same strictness as every other
// scalar (decodeU256Scalar rejects a leading zero). A hand-built legacy list with v encoded
// non-minimally (0x82 0x00 0x1b == 27 with a padding zero) must be rejected — otherwise two
// encodings of one tx would silently derive one txRoot here but fail op-geth's canonical decode.
TEST(OpSchedulerImpl, LegacyNonCanonicalVRejected)
{
    // [nonce, gasPrice, gasLimit, to, value, data, v(non-canonical), r, s]
    bcos::bytes fields;
    auto append = [&fields](bcos::bytes const& item) {
        fields.insert(fields.end(), item.begin(), item.end());
    };
    append(rlpString({}));                     // nonce = 0
    append(rlpString({0x01}));                 // gasPrice = 1
    append(rlpString({0x52, 0x08}));           // gasLimit = 21000
    append(rlpString(bcos::bytes(20, 0x11)));  // to (20 bytes)
    append(rlpString({}));                     // value = 0
    append(rlpString({}));                     // data = empty
    append(rlpString({0x00, 0x1b}));           // v = 27, NON-canonical (leading zero)
    append(rlpString(bcos::bytes(32, 0x22)));  // r
    append(rlpString(bcos::bytes(32, 0x33)));  // s
    auto env = rlpList(fields);

    bool threw = false;
    try
    {
        (void)bcos::evm::engine::detail::decodeOneRawTx(env, kChainId);
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("non-canonical leading zero"), std::string::npos)
            << "e.what()=\"" << e.what() << "\"";
    }
    EXPECT_TRUE(threw);
}

// ══════════════ final review batch B, 失实1: whole-envelope canonical round-trip invariant ══════
//
// assertCanonicalRoundTrip (decode → re-encode → byte-compare) is defense-in-depth on top of the
// per-field (B4-2) and length-prefix (C1) checks: it turns "non-canonical input never survives
// decoding" from prose into a runtime invariant. Its two obligations: (a) NEVER false-reject a
// canonical tx — proven at scale by the 33-vector golden corpus + t8n legs (all pass with it live)
// and here by exact byte-reproduction of a deposit; (b) actually FIRE when the raw bytes differ
// from the canonical re-encoding.
TEST(OpSchedulerImpl, RoundTripInvariantReproducesDepositBytesExactly)
{
    // The deposit re-encoder is the trickiest (evmc→wire rebuild, isSystemTransaction bool shape),
    // so pin it: canonicalEnvelopeBytes must reproduce a valid deposit envelope byte-for-byte.
    auto raw = buildDepositRawTx(1'000'000);
    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(raw, kChainId);
    EXPECT_EQ(bcos::evm::engine::detail::canonicalEnvelopeBytes(decoded), raw);
}

TEST(OpSchedulerImpl, RoundTripInvariantFiresOnMismatch)
{
    auto raw = buildDepositRawTx(1'000'000);
    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(raw, kChainId);
    // A raw envelope that no longer equals the canonical re-encoding of `decoded` must be rejected.
    auto tampered = raw;
    tampered.back() ^= 0xFFU;
    bool threw = false;
    try
    {
        bcos::evm::engine::detail::assertCanonicalRoundTrip(tampered, decoded);
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("re-encode mismatch"), std::string::npos)
            << "e.what()=\"" << e.what() << "\"";
    }
    EXPECT_TRUE(threw) << "round-trip invariant did not fire on a mismatched envelope";
}

// ══════════════ A5: 解码严格性契约固化 (gaps 1 & 2) ══════════════
//
// OpSchedulerImpl.h 的三层严格性声明 (per-field B4-2 + length-prefix C1 + whole-envelope
// assertCanonicalRoundTrip) 以 "canonical 输入 raw-bytes txRoot == op-geth 重编码 DeriveSha;
// 非 canonical 输入不 survive 解码" 收尾。此前该断言的两条义务只被间接钉住:
//   * 非 deposit (typed-tx) 的 re-encode 路径没有 decode 期 round-trip 正例 —— 只有 deposit 有,
//     其余靠 *晚期* 的 block-hash 级 txRoot golden 兜底;
//   * raw-bytes txRoot == 重编码 txRoot 的等价契约没有直接测试。
// 下面两条测试补齐这两个缺口 (任务 A5)。

// Gap 1: 非 deposit round-trip 正例。若未来改坏 typed-tx 的 canonical 重编码, 唯一会被抓的将
// 是晚期的 txRoot 分叉 —— 这里把每条非 deposit wire tx 钉在 DECODE 期:
// canonicalEnvelopeBytes(decode(raw)) 必须逐字节复现 raw。
TEST(OpSchedulerImpl, RoundTripInvariantReproducesNonDepositBytesExactly)
{
    // vector id -> 该向量贡献的非 deposit wire 形状。注意: 33-vector 语料里不存在真正的
    // EIP-2930 (type 0x01) 交易 —— 逐一核对 golden/engine/*.golden.json, 所有非 deposit raw
    // 首字节都是 0x02 或 0x04。因此 "access-list RLP" 形状改用 isthmus_access_list 里**携带
    // access list 的 EIP-1559** 交易来钉 (与 0x01 走同一条 access-list RLP re-encode 分支)。
    struct Case
    {
        const char* id;
        const char* shape;
    };
    constexpr Case kCases[] = {
        {"isthmus_access_list", "eip1559 with access list (access-list RLP re-encode shape)"},
        {"isthmus_transfer_basic", "plain eip1559 value transfer"},
        {"isthmus_setcode_7702", "7702 set_code (type 0x04)"},
    };

    for (auto const& c : kCases)
    {
        SCOPED_TRACE(c.id);
        auto vectors = loadJsonOrFail(fs::path(OP_T8N_VECTORS_DIR) / (std::string(c.id) + ".json"));
        auto golden = loadJsonOrFail(
            fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / (std::string(c.id) + ".golden.json"));
        auto rawTxBytes = rawTxBytesOf(golden);
        ASSERT_FALSE(rawTxBytes.empty()) << c.id << ": no rawTransactions in golden";

        for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
        {
            auto const& raw = rawTxBytes[i];
            if (raw.empty() || raw[0] == 0x7e)
                continue;  // L1-attributes deposit — 由
                           // RoundTripInvariantReproducesDepositBytesExactly 钉住
            SCOPED_TRACE("raw tx [" + std::to_string(i) + "]");
            auto decoded = bcos::evm::engine::detail::decodeOneRawTx(raw, kChainId);
            EXPECT_EQ(bcos::evm::engine::detail::canonicalEnvelopeBytes(decoded), raw)
                << "non-deposit wire tx is not byte-identical to its canonical re-encoding ("
                << c.shape << ")";
        }
    }
}

// Gap 2: raw-bytes txRoot == op-geth 式重编码 DeriveSha 的 txRoot —— 直接断言, 而非只靠整块
// golden txRoot 值。canonical 输入两者必等 (round-trip 使 raw == canonical, 同一计算);
// 非 canonical 输入 decode 必拒, 且 raw (非 canonical) wire bytes 的 txRoot 与 canonical
// 重编码的 txRoot **不同** —— 即注释宣称的 "divergent txRoot", 正是严格性阻止它进入
// block-hash 推导的那个晚期 catch。
TEST(OpSchedulerImpl, TxRootEqualsReencodedDeriveShaForCanonicalInput)
{
    // 语料中的 canonical 非 deposit 交易: isthmus_transfer_basic tx[1] (plain EIP-1559 转账;
    // tx[0] 是 L1-attributes deposit)。
    auto golden =
        loadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "isthmus_transfer_basic.golden.json");
    auto rawTxBytes = rawTxBytesOf(golden);
    ASSERT_EQ(rawTxBytes.size(), 2u);
    auto const& raw = rawTxBytes[1];
    ASSERT_EQ(raw[0], 0x02);  // EIP-1559

    // Canonical 路径: decode → 重编码逐字节复现 raw, 故两 txRoot 相等。
    auto decoded = bcos::evm::engine::detail::decodeOneRawTx(raw, kChainId);
    auto canonical = bcos::evm::engine::detail::canonicalEnvelopeBytes(decoded);
    ASSERT_EQ(canonical, raw);
    EXPECT_EQ(bcos::evm::engine::computeOpTxRoot(std::vector<bcos::bytes>{raw}),
        bcos::evm::engine::computeOpTxRoot(std::vector<bcos::bytes>{canonical}));

    // 非 canonical 路径: 同一信封加 leading-zero 长度前缀 —— C1 找到的那一类
    // (非最小 long-form list 头 0xf9 0x00 <len>, 替代 canonical 的 0xf8 <len>)。
    ASSERT_EQ(raw[1], 0xf8);  // 单字节 long-list 长度前缀 (body 56..255 bytes)
    bcos::bytes nonCanonical{raw[0], 0xf9, 0x00, raw[2]};
    nonCanonical.insert(nonCanonical.end(), raw.begin() + 3, raw.end());

    // (1) 解码器拒绝它 —— 非 canonical 输入不 survive 解码。消息断言到 "leading zero":
    //    只有 C1 的 throw site (bcos-codec/rlp/RLPDecode.h) 产生该子串, 证明确实是
    //    length-prefix 非 canonical 这一类别 (而非 chain-id 错、越界之类的其他失败)。
    bool threw = false;
    try
    {
        bcos::evm::engine::detail::decodeOneRawTx(nonCanonical, kChainId);
    }
    catch (const bcos::evm::engine::OpConsensusError& e)
    {
        threw = true;
        EXPECT_NE(std::string(e.what()).find("leading zero"), std::string::npos)
            << "e.what()=\"" << e.what() << "\"";
    }
    EXPECT_TRUE(threw) << "non-canonical (leading-zero length prefix) tx survived decoding";
    // (2) 且若它真走到 block-hash 路径, raw wire bytes 的 txRoot 会与 canonical 重编码的
    // txRoot **不同** —— 严格性所阻止的那个分叉, 在此被直接观测。
    EXPECT_NE(bcos::evm::engine::computeOpTxRoot(std::vector<bcos::bytes>{nonCanonical}),
        bcos::evm::engine::computeOpTxRoot(std::vector<bcos::bytes>{canonical}));
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

// D1: executeOpBlock 接入 RecentBlockHashes 后, isthmus_transfer_basic 向量仍正常执行
// (接线回归; 该向量含 EIP-2935 系统调用, 其 get_block_hash(N-1) 走 cache 种子)。
TEST(OpSchedulerImpl, RecentBlockHashesWiringKeepsVectorGreen)
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
    // 接线正确: 执行完成、无毒旗、六项承诺齐备。
    EXPECT_EQ(result.receipts.size(), 2u);
}

// ══════════════ D1 (G2/G3 分类阶梯 + 旗舰正向, 审查补充 #9/#10): executeOpBlock 层 ══════════════
//
// 向量选择说明 (对 brief 的**唯一**技术修正, 详见下方三测试的共用注释):
// brief 原钉 `isthmus_transfer_basic` + `0xb0b0...0001` 的机制在运行时不可行 —— 该向量 tx1 是
// gasLimit=0x5208(21000) 的纯转账, intrinsic gas 恰为 21000 (state.cpp:526-530,
// compute_tx_intrinsic_cost), execution_gas_limit=0 → evmone Host::call 的 m_vm.execute
// (host.cpp:374) 首条 PUSH1 即 OUT_OF_GAS, BLOCKHASH 永远执行不到, hashErr 不会置位,
// 存储写回也不会发生 —— 三个测试全按 brief 原样写必红。改用 `jovian_da_mix`: 它是语料里
// 唯一**真实签名**地把 `to` 落在 brief 同一地址 `0xb0b0...0001`、且 gas 充足的向量
// (tx[2] gas=0x30d40=200000, tx[3] gas=0x61a80=400000, 均为 eip1559 值转账/调用);
// 现有 21000-gas 的 tx[1] 对 0xb0b0...0001 也会执行但立即 OOG (无害回滚, receipt 正常落账)。
// 该向量 `_info.hardfork=jovian`(与 harness jovianConfig 同档), 故本组调度器用
// {isthmusTime=0, jovianTime=0} 把整个块推到 Jovian 档执行 —— 与语料回放 (OpT8nReplay.Vectors)
// 的原始生成/金标准档位一致, 不引入"Jovian 向量在 Isthmus 档跑"的未验证路径。
// 其余契约不变: 只改内存副本 vec["env"]["currentNumber"]="0x7", 不动任何向量/golden 文件。
namespace
{
// jovian_da_mix 向量 + 其 golden 装载 (golden 只取 rawTransactions/extraData —— 本组不比对
// _op_expected/postState, 只验证异常分类与存储写回)。
std::pair<Json, Json> loadD1Vector()
{
    return {loadJsonOrFail(fs::path(OP_T8N_VECTORS_DIR) / "jovian_da_mix.json"),
        loadJsonOrFail(fs::path(OP_T8N_GOLDEN_ENGINE_DIR) / "jovian_da_mix.golden.json")};
}

constexpr const char* kD1VectorId = "jovian_da_mix";
// 与 brief 相同的目标地址: 向量 tx[2]/tx[3] 的真实 `to` (把 code 种到既有 to 上, 让现成签名
// 交易执行它)。600540600055 = PUSH1 5; BLOCKHASH(0x40); PUSH1 0; SSTORE —— 存 blockhash(5)。
constexpr const char* kD1TransferTo = "0xb0b0000000000000000000000000000000000001";

// 三测试共用的合约播种: 在 kD1TransferTo 上放 BLOCKHASH 合约。jovian_da_mix 的 pre 原本没有
// 该账户, 补全四字段 (balance/nonce/code/storage, 与 (d3)/(d4) 的 pre 覆写同一形状)。
void seedD1TransferToContract(ViewType& view, Json& pre)
{
    pre[kD1TransferTo] = Json{
        {"balance", "0x0"},
        {"nonce", "0x1"},
        {"code", "0x600540600055"},
        {"storage", Json::object()},
    };
    seedFromVectorPre(view, pre);
}
}  // namespace

// D1 (G2, 审查补充 #9): hashErr 毒旗(仅 SYS_NUMBER_2_HASH 读抛) → 正常返回路径查 hashErr
// → OpStorageError (-32603), 非 INVALID。bridge 未 poison (其他表直通), 证明 hashErr
// 通道独立置位。
TEST(OpSchedulerImpl, HashErrPoisonClassifiedAsStorageErrorNotInvalid)
{
    auto [vecs, golden] = loadD1Vector();
    Json vec = vectorBody(vecs, kD1VectorId);

    // 所有 33 个向量的 env.currentNumber 都是 0x1 (N=1 时窗口仅 [0,0], BLOCKHASH 无法触发
    // 表读)。内存覆盖为 7 → N-2=5 落窗口内且 < N-1, RecentBlockHashes 才真实读表。
    // 只改内存副本, 不动向量文件 (避免破坏 OpT8nReplay.Vectors 的 golden 比对)。
    vec["env"]["currentNumber"] = "0x7";

    StorageFixture fixture;
    auto pre = vec.at("pre");
    seedD1TransferToContract(fixture.view, pre);

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    // 只对 SYS_NUMBER_2_HASH 的 readOne 抛错, 其余直通 —— 不能用 ThrowingStorage (全表抛会让
    // bridge 的账户读先 poison, 测不到独立的 hashErr 通道)。Jovian 档: 向量 native 档位。
    bcos::evm::test::ThrowOnNumber2Hash<ViewType> throwing(fixture.view);
    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<decltype(throwing)> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 0});

    // BLOCKHASH(N-2=5) → RecentBlockHashes::get_block_hash(5) → SYS_NUMBER_2_HASH 读抛 →
    // hashErr 置位 → executeOpBlock 正常返回后查 (bridge.poisoned() || hashErr) → OpStorageError。
    expectOpStorageErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(throwing, env, rawTxBytes)); },
        "RecentBlockHashes");
}

// D1 (G3, 审查补充 #10): SYS_NUMBER_2_HASH 条目非 32 字节 → G3 守卫记毒旗 → OpStorageError。
TEST(OpSchedulerImpl, BadLengthNumberToHashEntryPoisons)
{
    auto [vecs, golden] = loadD1Vector();
    Json vec = vectorBody(vecs, kD1VectorId);

    vec["env"]["currentNumber"] = "0x7";  // 同 HashErrPoison 的原因 (N=1 无法触发表读)。

    StorageFixture fixture;
    auto pre = vec.at("pre");
    seedD1TransferToContract(fixture.view, pre);

    // N-2=5 的表条目写 16 字节坏值 (非 32)。
    bcos::storage::Entry bad;
    bad.set(std::string(16, 0xcc));
    bcos::task::syncWait(bcos::storage2::writeOne(fixture.view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "5"}, std::move(bad)));

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 0});

    // BLOCKHASH(5) 读到 16 字节坏值 → G3 守卫记毒旗 ("length != 32") → OpStorageError。
    expectOpStorageErrorWithMessage(
        [&] { bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes)); },
        "length != 32");
}

// D1 (spec 测试 #1 旗舰正向): 非抛错 storage 下, 合约执行 BLOCKHASH(N-2) 真实写入种子 hash
// (非零) —— 这是 ParentOnly(返回零) 与 RecentBlockHashes(返回真实 hash) 的分叉判据。
TEST(OpSchedulerImpl, BlockHashAncestorWritesRealHashToState)
{
    auto [vecs, golden] = loadD1Vector();
    Json vec = vectorBody(vecs, kD1VectorId);

    vec["env"]["currentNumber"] = "0x7";

    StorageFixture fixture;
    auto pre = vec.at("pre");
    seedD1TransferToContract(fixture.view, pre);

    // N-2=5 的表条目写已知 32 字节 (0xdd...dd) → 合约 BLOCKHASH(5) 应原样拿到它。
    bcos::storage::Entry e;
    std::string v(32, 0xdd);
    e.set(std::move(v));
    bcos::task::syncWait(bcos::storage2::writeOne(fixture.view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "5"}, std::move(e)));

    auto header = buildHeaderForVector(vec);
    auto env = buildEnv(vec, golden, *header);
    auto rawTxBytes = rawTxBytesOf(golden);

    auto receiptFactory = makeReceiptFactory();
    bcos::evm::engine::OpSchedulerImpl<ViewType> scheduler(receiptFactory, kChainId,
        bcos::evm::opstack::OpForkTimestamps{.isthmusTime = 0, .jovianTime = 0});

    auto result = bcos::task::syncWait(scheduler.executeOpBlock(fixture.view, env, rawTxBytes));
    EXPECT_EQ(result.receipts.size(), rawTxBytes.size());

    // 执行后从视图读回 kTransferTo 的 storage slot 0, 断言 == 0xdd..dd (32 字节种子 hash)。
    // evmc::address 由 detail::toEvmcAddress(asAddress(...)) 构造 (OpSchedulerImpl.h:133)。
    bcos::evm::ledger::Storage2Ledger<ViewType> reader(fixture.view);
    const auto addr = bcos::evm::engine::detail::toEvmcAddress(asAddress(kD1TransferTo));
    const auto slot0 = reader.get_storage(addr, evmc::bytes32{});
    ASSERT_FALSE(reader.poisoned()) << "storage read-back poisoned: " << reader.firstError();
    evmc::bytes32 expected{};
    std::fill_n(expected.bytes, sizeof(expected.bytes), 0xdd);
    EXPECT_EQ(std::memcmp(slot0.bytes, expected.bytes, sizeof(expected.bytes)), 0)
        << "contract must have SSTORE'd blockhash(5) = 0xdd..dd into slot 0 (ParentOnly would "
           "have stored all-zero)";
}
