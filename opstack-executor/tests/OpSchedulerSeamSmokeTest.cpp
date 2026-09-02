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

// Note: the empty-block rejection test lives in PreBlockOpStepsTest (RejectsEmptyBlock).
// OpSchedulerSeam is a pure engine seam and no longer executes blocks, so there is no
// matching execution case here. The EIP-7702 authorization yParity width test was removed
// with the RLP decode primitives (decodeAuthYParityScalar retired in OpCommon.h).

BOOST_AUTO_TEST_SUITE_END()
