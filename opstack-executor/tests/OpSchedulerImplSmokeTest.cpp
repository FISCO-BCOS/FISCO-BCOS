// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerImplSmokeTest — minimal compile-and-run verification that the ported
// `bcos::evm::engine::OpSchedulerImpl` header (op-validator-loop engine OP branch) instantiates
// against the current branch's types and that its engine-facing seam surface works. The full
// vector-replay suite lives on the source branch (OpSchedulerImplTest.cpp, t8n-corpus bound);
// this file deliberately exercises only:
//   1. construction over a real MultiLayerStorage ViewType;
//   2. the static seam surface the engine reaches as dependent names
//      (computeTxRoot / commitmentsOf / isIsthmusActiveAt / isJovianActiveAt);
//   3. executeOpBlock's empty-block rejection (processOpBlock throws -> classified escape).
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

namespace
{
// Minimal CheckpointStorage stub — per-file local copy (the source-branch fixture's rationale:
// do not cross-include another module's test-private header).
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

/// A header carrying every optional field executeOpBlock's `toBlockInfo` reads (all `.value()`).
std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeOpHeader(
    bcos::protocol::BlockNumber number, int64_t timestampMillis)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(timestampMillis);
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = bcos::h256{}});
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
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerImplSmokeSuite)

BOOST_AUTO_TEST_CASE(ConstructAndSeamSurface)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    constexpr uint64_t kIsthmusTime = 1000;
    constexpr uint64_t kJovianTime = 2000;
    // The receipt factory is never dereferenced on the paths this smoke test exercises (the seam
    // surface and the empty-block rejection both throw before receipt mapping), so nullptr is
    // enough here — it avoids dragging bcos-crypto into this module's test target.
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler(nullptr, 0x2105,
        bcos::evm::opstack::OpForkTimestamps{
            .isthmusTime = kIsthmusTime, .jovianTime = kJovianTime},
        nullptr, multiLayerStorage, {});

    // Fork predicates: threshold comparison stays on the OP side of the seam.
    BOOST_CHECK(scheduler.isIsthmusActiveAt(kIsthmusTime));
    BOOST_CHECK(!scheduler.isIsthmusActiveAt(kIsthmusTime - 1));
    BOOST_CHECK(scheduler.isJovianActiveAt(kJovianTime));
    BOOST_CHECK(!scheduler.isJovianActiveAt(kJovianTime - 1));

    // computeTxRoot over the empty range: the standard empty-trie root (0x56e81f...), which
    // proves the trie built and hashed end-to-end.
    std::vector<bcos::bytes> emptyTxs;
    const auto txRoot = scheduler.computeTxRoot(emptyTxs);
    const bcos::h256 kEmptyTrieRoot{
        std::string{"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"}};
    BOOST_CHECK_EQUAL(txRoot, kEmptyTrieRoot);

    // commitmentsOf projects the seal + result members into the engine-facing surface.
    bcos::evm::opstack::OpBlockSeal seal;
    seal.receiptsRoot = evmone::hash256{};
    seal.logsBloom = evmone::state::BloomFilter{};
    seal.withdrawalsRoot = evmone::hash256{};
    bcos::evm::engine::OpExecuteBlockResult result{
        .receipts = {},
        .seal = seal,
        .stateRoot = bcos::h256{},
        .gasUsed = 0,
        .txRoot = txRoot,
    };
    const auto commitments = scheduler.commitmentsOf(result);
    BOOST_CHECK_EQUAL(commitments.stateRoot, bcos::h256{});
    BOOST_CHECK_EQUAL(commitments.gasUsed, bcos::u256(0));
    BOOST_CHECK_EQUAL(commitments.txRoot, txRoot);
}

BOOST_AUTO_TEST_CASE(EmptyBlockRejected)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    constexpr uint64_t kIsthmusTime = 1000;
    bcos::evm::engine::OpSchedulerImpl<ViewType, MLS> scheduler(nullptr, 0x2105,
        bcos::evm::opstack::OpForkTimestamps{
            .isthmusTime = kIsthmusTime, .jovianTime = kIsthmusTime + 1},
        nullptr, multiLayerStorage, {});

    auto header = makeOpHeader(1, static_cast<int64_t>(kIsthmusTime) * 1000);
    std::vector<bcos::bytes> emptyTxs;
    // processOpBlock rejects an empty block; executeOpBlock's classification surfaces it as a
    // runtime_error subclass (OpConsensusError or OpStorageError, depending on the documented
    // RTTI-bypass behaviour at the -fno-rtti evmone boundary).
    BOOST_CHECK_THROW(bcos::task::syncWait(scheduler.executeOpBlock(view, *header, emptyTxs)),
        std::runtime_error);
}

// C2 (W8 review): EIP-7702 authorization yParity is a uint8 in op-geth
// (core/types/tx_setcode.go:76). A wider RLP scalar — e.g. 0x82 0x01 0x00 (256) — overflows that
// uint8 and must be rejected at decode time by readCanonicalScalar(in, 1, "authorization
// yParity"). Without this width check the value-range guard (OpTransition.cpp:67, auth.v > 1)
// would merely *skip* the authorization and leave the block VALID where op-geth rejects the whole
// transaction — a consensus split. The value-RANGE case (yParity in [2,255]) is deliberately NOT
// rejected here (EIP-7702 says skip, not fatal); only the >1-byte encoding is a decode-time error.
BOOST_AUTO_TEST_CASE(OverWideAuthYParityIsConsensusError)
{
    namespace engine = bcos::evm::engine;

    // 0x82 0x01 0x00 = RLP string with payloadLength 2, value 256 → wider than uint8. The
    // "too wide" message proves the throw comes from the width guard
    // (readCanonicalScalar: payloadLength > maxBytes), not from the leading-zero or list checks.
    bcos::bytes wide{0x82, 0x01, 0x00};
    auto inWide = bcos::ref(wide);
    BOOST_CHECK_EXCEPTION(engine::detail::decodeAuthYParityScalar(inWide), engine::OpConsensusError,
        [](const engine::OpConsensusError& e) {
            return std::string(e.what()).find("too wide") != std::string::npos;
        });

    // Boundary: the canonical single-byte encodings still decode (yParity 0 and 1).
    bcos::bytes zero{0x80};  // empty string → 0
    auto inZero = bcos::ref(zero);
    BOOST_CHECK(engine::detail::decodeAuthYParityScalar(inZero) == intx::uint256(0));

    bcos::bytes one{0x01};  // 0x01 → 1
    auto inOne = bcos::ref(one);
    BOOST_CHECK(engine::detail::decodeAuthYParityScalar(inOne) == intx::uint256(1));

    // Value-RANGE case: a single-byte 0x02 (yParity=2) is NOT rejected at decode — EIP-7702
    // requires it to be *skipped* at execution (OpTransition.cpp:67), not fatal. Only the
    // >1-byte encoding is a decode-time error. This pins the width-vs-range boundary.
    bcos::bytes two{0x02};  // 0x02 → 2
    auto inTwo = bcos::ref(two);
    BOOST_CHECK(engine::detail::decodeAuthYParityScalar(inTwo) == intx::uint256(2));
}

BOOST_AUTO_TEST_SUITE_END()
