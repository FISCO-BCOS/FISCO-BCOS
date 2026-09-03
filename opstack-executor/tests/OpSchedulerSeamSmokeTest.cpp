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
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <opstack-executor/OpstackExecutor.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <span>
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

// Named selectors so BOOST_CHECK_EQUAL_COLLECTIONS does not dangle temporaries.
constexpr std::array<uint8_t, 4> kIsthmusSelector{0x09, 0x89, 0x99, 0xbe};
constexpr std::array<uint8_t, 4> kJovianSelector{0x3d, 0xb6, 0xbe, 0x2b};

}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSeamSmokeSuite)

BOOST_AUTO_TEST_CASE(ConstructAndSeamSurface)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    // L1BlockInfo is required (no silent default). Construction with the unset sentinel is
    // allowed; synthesizeL1AttributesEnvelope is what refuses it.
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, {});

    // Fork predicate: feature-driven (feature_op_jovian), constant across blocks — no timestamps.
    BOOST_CHECK(!scheduler.isJovianActive());
    bcos::evm::engine::OpSchedulerSeam<ViewType> jovianScheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true}, {});
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

BOOST_AUTO_TEST_CASE(SynthesizeRefusesUnsetL1BlockInfo)
{
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, {});
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(1000), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositMatchesIsthmusLayout)
{
    // All-zero L1 is a test-only fixture: call the encoder, not the production seam.
    constexpr uint64_t kL2Time = 0x123456789abcdef0ull;
    const auto env = bcos::evm::opstack::synthesizeL1AttributesDeposit(
        bcos::evm::opstack::L1BlockInfo{}, false, kL2Time);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));

    BOOST_CHECK(dep.from == bcos::evm::opstack::OP_DEPOSITOR);
    BOOST_REQUIRE(dep.to.has_value());
    BOOST_CHECK(*dep.to == bcos::evm::opstack::OP_L1_BLOCK);
    BOOST_CHECK(!dep.mint.has_value());
    BOOST_CHECK(dep.value == intx::uint256{0});
    BOOST_CHECK_EQUAL(dep.gas_limit, bcos::evm::opstack::c_l1InfoDepositGas);
    BOOST_CHECK(!dep.is_system_tx);
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::IsthmusL1AttributesLen);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        dep.data.begin(), dep.data.begin() + 4, kIsthmusSelector.begin(), kIsthmusSelector.end());

    // sourceHash = keccak(bytes32(1) || keccak(l1Hash || bytes32(seq))).
    std::array<uint8_t, 64> innerInput{};
    const auto inner =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(innerInput.data(), innerInput.size()));
    std::array<uint8_t, 64> domainInput{};
    domainInput[31] = 1;
    std::copy(inner.begin(), inner.end(), domainInput.begin() + 32);
    const auto expectedHash =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(domainInput.data(), domainInput.size()));
    BOOST_CHECK_EQUAL_COLLECTIONS(dep.source_hash.bytes, dep.source_hash.bytes + 32,
        expectedHash.begin(), expectedHash.end());
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsCalldataFieldOffsets)
{
    bcos::evm::opstack::L1BlockInfo l1Info;
    l1Info.sequenceNumber = 0x1122334455667788ull;
    l1Info.time = 0x2233445566778899ull;
    l1Info.number = 0x33445566778899aaull;
    l1Info.baseFee = intx::uint256{0x445566778899aabbull};
    l1Info.blobBaseFee = intx::uint256{0x5566778899aabbccull};
    std::array<uint8_t, 32> hashBytes{};
    for (size_t i = 0; i < hashBytes.size(); ++i)
    {
        hashBytes[i] = static_cast<uint8_t>(0x60 + i);
    }
    std::copy(hashBytes.begin(), hashBytes.end(), l1Info.blockHash.bytes);

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = true}, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope(1000);
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::JovianL1AttributesLen);
    auto const& calldata = dep.data;

    auto checkBE = [&](size_t offset, uint64_t value) {
        std::array<uint8_t, 8> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkBE256 = [&](size_t offset, intx::uint256 const& value) {
        std::array<uint8_t, 32> be{};
        intx::be::store(std::span<uint8_t, 32>(be.data(), be.size()), value);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    // Selector [0:4].
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin(), calldata.begin() + 4, kJovianSelector.begin(), kJovianSelector.end());
    checkBE(12, l1Info.sequenceNumber);  // seq
    checkBE(20, l1Info.time);            // l1 time
    checkBE(28, l1Info.number);          // l1 number
    checkBE256(36, l1Info.baseFee);      // l1 baseFee
    checkBE256(68, l1Info.blobBaseFee);  // l1 blobBaseFee
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(),
        hashBytes.end());  // l1 blockHash
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsIsthmusCalldataFieldOffsets)
{
    bcos::evm::opstack::L1BlockInfo l1Info;
    l1Info.sequenceNumber = 0x1122334455667788ull;
    l1Info.time = 0x2233445566778899ull;
    l1Info.number = 0x33445566778899aaull;
    l1Info.baseFee = intx::uint256{0x445566778899aabbull};
    l1Info.blobBaseFee = intx::uint256{0x5566778899aabbccull};
    std::array<uint8_t, 32> hashBytes{};
    for (size_t i = 0; i < hashBytes.size(); ++i)
    {
        hashBytes[i] = static_cast<uint8_t>(0x60 + i);
    }
    std::copy(hashBytes.begin(), hashBytes.end(), l1Info.blockHash.bytes);

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(
        bcos::evm::opstack::OpForkFlags{.jovianActive = false}, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope(1000);
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::IsthmusL1AttributesLen);
    auto const& calldata = dep.data;

    auto checkBE = [&](size_t offset, uint64_t value) {
        std::array<uint8_t, 8> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkBE256 = [&](size_t offset, intx::uint256 const& value) {
        std::array<uint8_t, 32> be{};
        intx::be::store(std::span<uint8_t, 32>(be.data(), be.size()), value);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin(), calldata.begin() + 4, kIsthmusSelector.begin(), kIsthmusSelector.end());
    checkBE(12, l1Info.sequenceNumber);
    checkBE(20, l1Info.time);
    checkBE(28, l1Info.number);
    checkBE256(36, l1Info.baseFee);
    checkBE256(68, l1Info.blobBaseFee);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(), hashBytes.end());
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositJovianLayoutAndUniqueness)
{
    bcos::evm::opstack::L1BlockInfo const unset{};
    const auto env = bcos::evm::opstack::synthesizeL1AttributesDeposit(unset, true, 1000);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep = bcos::executor_v1::opstack::decodeDepositEnvelope(
        bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), bcos::evm::opstack::JovianL1AttributesLen);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        dep.data.begin(), dep.data.begin() + 4, kJovianSelector.begin(), kJovianSelector.end());
    // [176:178] DA-footprint scalar is zero.
    BOOST_CHECK_EQUAL(dep.data[176], 0);
    BOOST_CHECK_EQUAL(dep.data[177], 0);

    const auto envLater = bcos::evm::opstack::synthesizeL1AttributesDeposit(unset, true, 1001);
    BOOST_CHECK(env == envLater);  // sourceHash is domain-1(l1Hash, seq), not L2 time
}

// Note: the empty-block rejection test lives in PreBlockOpStepsTest (RejectsEmptyBlock).
// OpSchedulerSeam is a pure engine seam and no longer executes blocks, so there is no
// matching execution case here. The EIP-7702 authorization yParity width test was removed
// with the RLP decode primitives (decodeAuthYParityScalar retired in OpCommon.h).

BOOST_AUTO_TEST_SUITE_END()
