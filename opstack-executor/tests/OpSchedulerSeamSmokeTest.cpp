// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerSeamSmokeTest — minimal compile-and-run verification that the ported
// `bcos::evm::engine::OpSchedulerSeam` header instantiates against the current branch's types and
// that its engine-facing seam surface works. Exercises only:
//   1. construction over a real MultiLayerStorage ViewType;
//   2. the static seam surface the engine reaches as dependent names
//      (computeTxRoot / commitmentsOf / isJovianActive).
//      (The block-pre shape checks live in PreBlockOpStepsTest; the seam itself no longer
//      executes blocks — see the note at the end of this file.)
#include "OpSchedulerSeamTestHelpers.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpSchedulerSeam.h>
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

}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSeamSmokeSuite)

BOOST_AUTO_TEST_CASE(ConstructAndSeamSurface)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    // The ctor takes only fork flags (feature-driven fork selection; this is a pure seam shim —
    // no receipt factory / chain id / VM).
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false});

    // Fork predicate: feature-driven (feature_op_jovian), constant across blocks — no timestamps.
    BOOST_CHECK(!scheduler.isJovianActive());
    bcos::evm::engine::OpSchedulerSeam<ViewType> jovianScheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true});
    BOOST_CHECK(jovianScheduler.isJovianActive());

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

BOOST_AUTO_TEST_CASE(SynthesizeL1AttributesIsDepositEnvelope)
{
    auto const env = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(false);
    BOOST_REQUIRE(!env.empty());
    BOOST_CHECK_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
}

// Production-synthesis layout anchor: the seam's synthesizeL1AttributesEnvelope must produce
// a 0x7e envelope whose rlp fields carry the op-geth L1-attributes deposit shape — from ==
// OP_DEPOSITOR, to == OP_L1_BLOCK, gas == c_l1InfoDepositGas, data == Isthmus 176B with the
// Isthmus selector, and sourceHash == keccak256(blockHash || minimal-be(seq) || minimal-be(time)).
// The fixture helper (testutil) is NOT the anchor: this decodes the seam's own output.
BOOST_AUTO_TEST_CASE(SynthesizedDepositMatchesIsthmusLayout)
{
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false});
    constexpr uint64_t kL2Time = 0x123456789abcdef0ull;
    const auto env = scheduler.synthesizeL1AttributesEnvelope(kL2Time);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));

    bcos::bytesRef cursor(const_cast<bcos::byte*>(env.data() + 1), env.size() - 1);
    std::vector<bcos::bytes> fields;
    BOOST_REQUIRE(bcos::codec::rlp::decode(cursor, fields) == nullptr);
    BOOST_REQUIRE_EQUAL(fields.size(), 8);

    BOOST_CHECK_EQUAL(fields[0].size(), 32);  // sourceHash
    BOOST_CHECK_EQUAL_COLLECTIONS(fields[1].begin(), fields[1].end(),
        std::begin(bcos::evm::opstack::OP_DEPOSITOR.bytes),
        std::end(bcos::evm::opstack::OP_DEPOSITOR.bytes));
    BOOST_CHECK_EQUAL_COLLECTIONS(fields[2].begin(), fields[2].end(),
        std::begin(bcos::evm::opstack::OP_L1_BLOCK.bytes),
        std::end(bcos::evm::opstack::OP_L1_BLOCK.bytes));
    // mint absent / value 0 / isSystemTx false all RLP-encode as the empty string and
    // decode back to an empty byte vector.
    BOOST_CHECK(fields[3].empty());
    BOOST_CHECK(fields[4].empty());
    BOOST_CHECK_EQUAL(fields[5], (bcos::bytes{0x0f, 0x42, 0x40}));  // gas 1'000'000, payload only
    BOOST_CHECK(fields[6].empty());
    BOOST_REQUIRE_EQUAL(fields[7].size(), bcos::evm::opstack::IsthmusL1AttributesLen);
    BOOST_CHECK_EQUAL_COLLECTIONS(fields[7].begin(), fields[7].begin() + 4,
        bcos::evm::opstack::IsthmusL1AttributesSelector.begin(),
        bcos::evm::opstack::IsthmusL1AttributesSelector.end());

    // sourceHash == keccak256(0^32 || 0x00 || minimal-be(kL2Time)) for the zero L1 info.
    bcos::bytes preimage(32, 0);
    preimage.push_back(0);  // minimal big-endian of sequenceNumber 0
    const std::array<uint8_t, 8> kTimeBe{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0};
    preimage.insert(preimage.end(), kTimeBe.begin(), kTimeBe.end());
    const auto expectedHash = bcos::crypto::keccak256Hash(bcos::ref(preimage));
    BOOST_CHECK_EQUAL_COLLECTIONS(fields[0].begin(), fields[0].end(), expectedHash.begin(),
        expectedHash.end());
}

// Jovian: the synthesized deposit carries the Jovian selector and 178B data (with the
// zero DA-footprint scalar), and the sourceHash is unique across L2 times.
BOOST_AUTO_TEST_CASE(SynthesizedDepositJovianLayoutAndUniqueness)
{
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true});
    const auto env = scheduler.synthesizeL1AttributesEnvelope(1000);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    bcos::bytesRef cursor(const_cast<bcos::byte*>(env.data() + 1), env.size() - 1);
    std::vector<bcos::bytes> fields;
    BOOST_REQUIRE(bcos::codec::rlp::decode(cursor, fields) == nullptr);
    BOOST_REQUIRE_EQUAL(fields.size(), 8);
    BOOST_REQUIRE_EQUAL(fields[7].size(), bcos::evm::opstack::JovianL1AttributesLen);
    BOOST_CHECK_EQUAL_COLLECTIONS(fields[7].begin(), fields[7].begin() + 4,
        bcos::evm::opstack::JovianL1AttributesSelector.begin(),
        bcos::evm::opstack::JovianL1AttributesSelector.end());
    // [176:178] DA-footprint scalar is zero for the stand-in.
    BOOST_CHECK_EQUAL(fields[7][176], 0);
    BOOST_CHECK_EQUAL(fields[7][177], 0);

    const auto envLater = scheduler.synthesizeL1AttributesEnvelope(1001);
    BOOST_CHECK(env != envLater);  // sourceHash binds the L2 time
}

// Note: the empty-block rejection test lives in PreBlockOpStepsTest (RejectsEmptyBlock).
// OpSchedulerSeam is a pure engine seam and no longer executes blocks, so there is no
// matching execution case here. The EIP-7702 authorization yParity width test was removed
// with the RLP decode primitives (decodeAuthYParityScalar retired in OpCommon.h).

BOOST_AUTO_TEST_SUITE_END()
