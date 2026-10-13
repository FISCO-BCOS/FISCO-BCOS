// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpSchedulerSeamSmokeTest — minimal compile-and-run verification that
// `bcos::evm::engine::OpSchedulerSeam` instantiates against the current branch's types and
// that its engine-facing seam surface works. Exercises only:
//   1. construction over a real MultiLayerStorage ViewType;
//   2. the static seam surface the engine reaches as dependent names
//      (computeTxRoot / commitmentsOf / isJovianActive);
//   3. synthesizeL1AttributesEnvelope, the seam's L1-attributes deposit path (the seam
//      itself no longer executes blocks — see the note at the end of this file).
#include <opstack-executor/OpEthCommitments.h>  // OpEthExecuteBlockResult / OpEthBlockSeal
#include <opstack-executor/OpEthDeposit.h>      // decodeOpDepositEnvelope / OP_DEPOSITOR
#include <opstack-executor/OpEthL1Attributes.h>  // OpEthL1BlockInfo / synthesizeOpEthL1AttributesDeposit
#include <opstack-executor/OpSchedulerSeam.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <vector>

using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;
namespace opeth = bcos::executor_v1::opstack;

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

/// Fully populated L1 info (snapshot + SystemConfig) for the offset pins.
opeth::OpEthL1BlockInfo filledL1Info()
{
    opeth::OpEthL1BlockInfo l1Info;
    l1Info.sequenceNumber = 0x1122334455667788ull;
    l1Info.time = 0x2233445566778899ull;
    l1Info.number = 0x33445566778899aaull;
    l1Info.baseFee = bcos::u256{0x445566778899aabbull};
    l1Info.blobBaseFee = bcos::u256{0x5566778899aabbccull};
    l1Info.baseFeeScalar = 0xa1b2c3d4u;
    l1Info.blobBaseFeeScalar = 0x11223344u;
    l1Info.operatorFeeScalar = 0x55667788u;
    l1Info.operatorFeeConstant = 0x99aabbccddeeff00ull;
    for (size_t i = 0; i < sizeof(l1Info.blockHash.bytes); ++i)
    {
        l1Info.blockHash.bytes[i] = static_cast<uint8_t>(0x60 + i);
        l1Info.batcherHash.bytes[i] = static_cast<uint8_t>(0x90 + i);
    }
    return l1Info;
}

}  // namespace

namespace
{
// One genesis schedule in SECONDS; the arms below differ only in the block timestamp
// (MILLISECONDS, the internal unit) handed to the seam.
constexpr uint64_t kSeamJovianTime = 1000;
constexpr uint64_t kSeamKarstTime = 2000;
const bcos::ledger::OpForkSchedule kSeamSchedule{.m_jovianTime = kSeamJovianTime,
    .m_karstTime = kSeamKarstTime};
/// Isthmus-era block time in ms (one second before Jovian activates).
constexpr int64_t kIsthmusMs = static_cast<int64_t>(kSeamJovianTime - 1) * 1000;
/// Jovian-era block time in ms (one second before Karst activates).
constexpr int64_t kJovianMs = static_cast<int64_t>(kSeamKarstTime - 1) * 1000;
/// Karst-era block time in ms (exactly karst_time).
constexpr int64_t kKarstMs = static_cast<int64_t>(kSeamKarstTime) * 1000;
}  // namespace

BOOST_AUTO_TEST_SUITE(OpSchedulerSeamSmokeSuite)

BOOST_AUTO_TEST_CASE(ConstructAndSeamSurface)
{
    BackendMemStorage backendStorage;
    CheckpointBackend checkpointBackend(backendStorage);
    MLS multiLayerStorage(checkpointBackend);
    auto view = multiLayerStorage.fork();
    view.newMutable();

    // OpEthL1BlockInfo is required (no silent default). Construction with the unset sentinel is
    // allowed; synthesizeL1AttributesEnvelope is what refuses it.
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, {});

    // Fork predicate: per block, from the block's own timestamp against the genesis schedule.
    // The ms->s conversion is pinned here: kSeamKarstTime * 1000 - 1 is still Jovian.
    BOOST_CHECK(!scheduler.isJovianActive(kIsthmusMs));
    BOOST_CHECK(scheduler.isJovianActive(kJovianMs));
    BOOST_CHECK(!scheduler.isKarstActive(kKarstMs - 1));
    BOOST_CHECK(scheduler.isKarstActive(kKarstMs));
    // Karst is a superset of Jovian and leaves the L1-attributes / DA-footprint shape alone,
    // so isJovianActive must follow the fork opForkSpecAt resolves: a Karst block still mints
    // the Jovian-shaped L1-attributes deposit.
    BOOST_CHECK(scheduler.isJovianActive(kKarstMs));

    // computeTxRoot over the empty range: the standard empty-trie root (0x56e81f...), which
    // proves the trie built and hashed end-to-end.
    std::vector<bcos::bytes> emptyTxs;
    const auto txRoot = scheduler.computeTxRoot(emptyTxs);
    const bcos::h256 kEmptyTrieRoot{
        std::string{"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"}};
    BOOST_CHECK_EQUAL(txRoot, kEmptyTrieRoot);

    // commitmentsOf projects the seal + result members into the engine-facing surface.
    opeth::OpEthBlockSeal seal;
    seal.receiptsRoot = bcos::h256{};
    seal.logsBloom = bcos::Bloom{};
    seal.withdrawalsRoot = bcos::h256{};
    opeth::OpEthExecuteBlockResult result{
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
    auto const env = opeth::synthesizeOpEthL1AttributesDeposit(opeth::OpEthL1BlockInfo{}, false);
    BOOST_REQUIRE(!env.empty());
    BOOST_CHECK_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
}

BOOST_AUTO_TEST_CASE(SynthesizeRefusesUnsetL1BlockInfo)
{
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, {});
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(kJovianMs, kJovianMs),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SynthesizeRefusesZeroSystemConfig)
{
    auto l1Info = filledL1Info();
    l1Info.baseFeeScalar = 0;
    std::fill(l1Info.batcherHash.bytes, l1Info.batcherHash.bytes + 32, 0);
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, l1Info);
    BOOST_CHECK_THROW((void)scheduler.synthesizeL1AttributesEnvelope(kJovianMs, kJovianMs),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositMatchesIsthmusLayout)
{
    // All-zero L1 is a test-only fixture: call the encoder, not the production seam.
    const auto env = opeth::synthesizeOpEthL1AttributesDeposit(opeth::OpEthL1BlockInfo{}, false);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep =
        opeth::decodeOpDepositEnvelope(bcos::bytesConstRef(env.data(), env.size()));

    BOOST_CHECK(dep.from == opeth::OP_DEPOSITOR);
    BOOST_REQUIRE(dep.to.has_value());
    BOOST_CHECK(*dep.to == opeth::OP_L1_BLOCK);
    BOOST_CHECK(!dep.mint.has_value());
    BOOST_CHECK(dep.value == bcos::u256{0});
    BOOST_CHECK_EQUAL(dep.gasLimit, opeth::OP_ETH_L1_INFO_DEPOSIT_GAS);
    BOOST_CHECK(!dep.isSystemTx);
    BOOST_REQUIRE_EQUAL(dep.data.size(), opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
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
    BOOST_CHECK_EQUAL_COLLECTIONS(
        dep.sourceHash.bytes, dep.sourceHash.bytes + 32, expectedHash.begin(), expectedHash.end());
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsCalldataFieldOffsets)
{
    auto const l1Info = filledL1Info();
    std::array<uint8_t, 32> hashBytes{};
    std::array<uint8_t, 32> batcherBytes{};
    std::copy_n(l1Info.blockHash.bytes, 32, hashBytes.begin());
    std::copy_n(l1Info.batcherHash.bytes, 32, batcherBytes.begin());

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope(kJovianMs, kJovianMs);
    auto const dep =
        opeth::decodeOpDepositEnvelope(bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
    auto const& calldata = dep.data;

    auto checkBE = [&](size_t offset, uint64_t value) {
        std::array<uint8_t, 8> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkBE256 = [&](size_t offset, bcos::u256 const& value) {
        std::array<uint8_t, 32> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkU32 = [&](size_t offset, uint32_t value) {
        std::array<uint8_t, 4> be{static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value)};
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + 4), be.begin(), be.end());
    };
    // Selector [0:4].
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin(), calldata.begin() + 4, kJovianSelector.begin(), kJovianSelector.end());
    checkU32(4, l1Info.baseFeeScalar);
    checkU32(8, l1Info.blobBaseFeeScalar);
    checkBE(12, l1Info.sequenceNumber);  // seq
    checkBE(20, l1Info.time);            // l1 time
    checkBE(28, l1Info.number);          // l1 number
    checkBE256(36, l1Info.baseFee);      // l1 baseFee
    checkBE256(68, l1Info.blobBaseFee);  // l1 blobBaseFee
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(),
        hashBytes.end());  // l1 blockHash
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 132, calldata.begin() + 164, batcherBytes.begin(), batcherBytes.end());
    checkU32(164, l1Info.operatorFeeScalar);
    checkBE(168, l1Info.operatorFeeConstant);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositPinsIsthmusCalldataFieldOffsets)
{
    auto const l1Info = filledL1Info();
    std::array<uint8_t, 32> hashBytes{};
    std::array<uint8_t, 32> batcherBytes{};
    std::copy_n(l1Info.blockHash.bytes, 32, hashBytes.begin());
    std::copy_n(l1Info.batcherHash.bytes, 32, batcherBytes.begin());

    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, l1Info);
    const auto env = scheduler.synthesizeL1AttributesEnvelope(kIsthmusMs, kIsthmusMs);
    auto const dep =
        opeth::decodeOpDepositEnvelope(bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    auto const& calldata = dep.data;

    auto checkBE = [&](size_t offset, uint64_t value) {
        std::array<uint8_t, 8> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkBE256 = [&](size_t offset, bcos::u256 const& value) {
        std::array<uint8_t, 32> be{};
        bcos::toBigEndian(value, be);
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
    };
    auto checkU32 = [&](size_t offset, uint32_t value) {
        std::array<uint8_t, 4> be{static_cast<uint8_t>(value >> 24),
            static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
            static_cast<uint8_t>(value)};
        BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
            calldata.begin() + static_cast<ptrdiff_t>(offset + 4), be.begin(), be.end());
    };
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin(), calldata.begin() + 4, kIsthmusSelector.begin(), kIsthmusSelector.end());
    checkU32(4, l1Info.baseFeeScalar);
    checkU32(8, l1Info.blobBaseFeeScalar);
    checkBE(12, l1Info.sequenceNumber);
    checkBE(20, l1Info.time);
    checkBE(28, l1Info.number);
    checkBE256(36, l1Info.baseFee);
    checkBE256(68, l1Info.blobBaseFee);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 100, calldata.begin() + 132, hashBytes.begin(), hashBytes.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        calldata.begin() + 132, calldata.begin() + 164, batcherBytes.begin(), batcherBytes.end());
    checkU32(164, l1Info.operatorFeeScalar);
    checkBE(168, l1Info.operatorFeeConstant);
}

BOOST_AUTO_TEST_CASE(SynthesizedDepositJovianLayout)
{
    // The sourceHash is domain-1(l1Hash, seq) — L2 time is deliberately not bound, so the
    // builder takes no L2-time argument a caller could vary.
    opeth::OpEthL1BlockInfo const unset{};
    const auto env = opeth::synthesizeOpEthL1AttributesDeposit(unset, true);
    BOOST_REQUIRE_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
    auto const dep =
        opeth::decodeOpDepositEnvelope(bcos::bytesConstRef(env.data(), env.size()));
    BOOST_REQUIRE_EQUAL(dep.data.size(), opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        dep.data.begin(), dep.data.begin() + 4, kJovianSelector.begin(), kJovianSelector.end());
    // [176:178] DA-footprint scalar is zero.
    BOOST_CHECK_EQUAL(dep.data[176], 0);
    BOOST_CHECK_EQUAL(dep.data[177], 0);
}

// Note: the empty-block rejection coverage lives with the block-pre shape checks
// (OpEthBlockSteps). OpSchedulerSeam is a pure engine seam and no longer executes blocks, so
// there is no matching execution case here.

// op-node emits the PREVIOUS fork's L1-attributes layout on the Jovian ACTIVATION block —
// isJovianButNotFirstBlock (derive/l1_block_info.go:462-470) — because the L1Block predeploy
// is upgraded by that very block and cannot already speak the 178-byte Jovian ABI. The parent
// is what distinguishes the activation block from every later Jovian block.
BOOST_AUTO_TEST_CASE(JovianActivationBlockKeepsIsthmusL1AttributesLayout)
{
    auto const l1Info = filledL1Info();
    bcos::evm::engine::OpSchedulerSeam<ViewType> scheduler(kSeamSchedule, l1Info);

    constexpr int64_t kJovianActivationMs = static_cast<int64_t>(kSeamJovianTime) * 1000;
    constexpr int64_t kParentOfActivationMs = kJovianActivationMs - 1000;

    auto layoutSize = [&](int64_t childMs, int64_t parentMs) {
        auto const env = scheduler.synthesizeL1AttributesEnvelope(childMs, parentMs);
        return opeth::decodeOpDepositEnvelope(bcos::bytesConstRef(env.data(), env.size()))
            .data.size();
    };

    // Activation block: child is Jovian, parent is not -> still Isthmus's 176 bytes.
    BOOST_CHECK_EQUAL(
        layoutSize(kJovianActivationMs, kParentOfActivationMs),
        opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    // The very next block: parent is Jovian too -> the 178-byte Jovian layout.
    BOOST_CHECK_EQUAL(layoutSize(kJovianActivationMs + 1000, kJovianActivationMs),
        opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
    // Well before the fork: Isthmus on both sides.
    BOOST_CHECK_EQUAL(
        layoutSize(kIsthmusMs, kIsthmusMs - 1000), opeth::OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    // Karst is a superset of Jovian and has no activation-block exception of its own.
    BOOST_CHECK_EQUAL(
        layoutSize(kKarstMs, kKarstMs - 1000), opeth::OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
}

BOOST_AUTO_TEST_SUITE_END()
