// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthL1AttributesTest — the bcos-evm-free L1-attributes deposit synthesis
// (OpEthL1Attributes.h, step 3.1.5):
//   * encode/decode round-trip against decodeOpDepositEnvelope,
//   * the Isthmus 176B / Jovian 178B calldata layouts (selectors + offsets),
//   * the seam-level refusal of the unset sentinels,
//   * the Jovian activation-block rule (activation block keeps the Isthmus
//     layout; the next block switches),
//   * golden envelope bytes for both layouts (the exact bytes the legacy
//     parity leg proved equal to op-geth's encoder, now pinned by value).

#include <opstack-executor/OpEthL1Attributes.h>

#include <bcos-crypto/hash/Keccak256.h>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

using namespace bcos::executor_v1::opstack;

namespace
{
constexpr uint64_t kJovianTime = 1000;  // seconds (the schedule's unit)
constexpr uint64_t kKarstTime = 2000;
const bcos::ledger::OpForkSchedule kSchedule{.m_jovianTime = kJovianTime,
    .m_karstTime = kKarstTime};
constexpr int64_t kJovianActivationMs = static_cast<int64_t>(kJovianTime) * 1000;
constexpr int64_t kParentOfActivationMs = kJovianActivationMs - 1000;

/// Fully populated L1 info (snapshot + SystemConfig) for the offset pins.
OpEthL1BlockInfo filledL1Info()
{
    OpEthL1BlockInfo info;
    info.sequenceNumber = 0x1122334455667788ull;
    info.time = 0x2233445566778899ull;
    info.number = 0x33445566778899aaull;
    info.baseFee = bcos::u256{0x445566778899aabbull};
    info.blobBaseFee = bcos::u256{0x5566778899aabbccull};
    info.baseFeeScalar = 0xa1b2c3d4u;
    info.blobBaseFeeScalar = 0x11223344u;
    info.operatorFeeScalar = 0x55667788u;
    info.operatorFeeConstant = 0x99aabbccddeeff00ull;
    for (size_t i = 0; i < sizeof(info.blockHash.bytes); ++i)
    {
        info.blockHash.bytes[i] = static_cast<uint8_t>(0x60 + i);
        info.batcherHash.bytes[i] = static_cast<uint8_t>(0x90 + i);
    }
    return info;
}

void checkBE(bcos::bytes const& calldata, size_t offset, uint64_t value)
{
    std::array<uint8_t, 8> be{};
    bcos::toBigEndian(value, be);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
}

void checkBE256(bcos::bytes const& calldata, size_t offset, bcos::u256 const& value)
{
    std::array<uint8_t, 32> be{};
    bcos::toBigEndian(value, be);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + be.size()), be.begin(), be.end());
}

void checkU32(bcos::bytes const& calldata, size_t offset, uint32_t value)
{
    std::array<uint8_t, 4> be{static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + static_cast<ptrdiff_t>(offset),
        calldata.begin() + static_cast<ptrdiff_t>(offset + 4), be.begin(), be.end());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthL1AttributesSuite)

BOOST_AUTO_TEST_CASE(EnvelopeRoundTripsThroughDecode)
{
    auto const info = filledL1Info();
    for (bool jovian : {false, true})
    {
        auto const env = synthesizeOpEthL1AttributesDeposit(info, jovian);
        BOOST_REQUIRE(!env.empty());
        BOOST_CHECK_EQUAL(env.front(), static_cast<bcos::byte>(0x7e));
        auto const dep = decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});
        BOOST_CHECK(dep.from == OP_DEPOSITOR);
        BOOST_REQUIRE(dep.to.has_value());
        BOOST_CHECK(*dep.to == OP_L1_BLOCK);
        BOOST_CHECK(!dep.mint.has_value());
        BOOST_CHECK_EQUAL(dep.value, bcos::u256{0});
        BOOST_CHECK_EQUAL(dep.gasLimit, OP_ETH_L1_INFO_DEPOSIT_GAS);
        BOOST_CHECK(!dep.isSystemTx);
        BOOST_REQUIRE_EQUAL(dep.data.size(),
            jovian ? OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN : OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    }
}

BOOST_AUTO_TEST_CASE(IsthmusCalldataFieldOffsets)
{
    auto const info = filledL1Info();
    auto const env = synthesizeOpEthL1AttributesDeposit(info, false);
    auto const dep = decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});
    BOOST_REQUIRE_EQUAL(dep.data.size(), OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    auto const& calldata = dep.data;
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin(), calldata.begin() + 4,
        OP_ETH_ISTHMUS_L1_ATTRIBUTES_SELECTOR.begin(), OP_ETH_ISTHMUS_L1_ATTRIBUTES_SELECTOR.end());
    checkU32(calldata, 4, info.baseFeeScalar);
    checkU32(calldata, 8, info.blobBaseFeeScalar);
    checkBE(calldata, 12, info.sequenceNumber);
    checkBE(calldata, 20, info.time);
    checkBE(calldata, 28, info.number);
    checkBE256(calldata, 36, info.baseFee);
    checkBE256(calldata, 68, info.blobBaseFee);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + 100, calldata.begin() + 132,
        info.blockHash.bytes, info.blockHash.bytes + 32);
    BOOST_CHECK_EQUAL_COLLECTIONS(calldata.begin() + 132, calldata.begin() + 164,
        info.batcherHash.bytes, info.batcherHash.bytes + 32);
    checkU32(calldata, 164, info.operatorFeeScalar);
    checkBE(calldata, 168, info.operatorFeeConstant);
}

BOOST_AUTO_TEST_CASE(JovianCalldataLayout)
{
    auto const info = filledL1Info();
    auto const env = synthesizeOpEthL1AttributesDeposit(info, true);
    auto const dep = decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});
    BOOST_REQUIRE_EQUAL(dep.data.size(), OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
    BOOST_CHECK_EQUAL_COLLECTIONS(dep.data.begin(), dep.data.begin() + 4,
        OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.begin(), OP_ETH_JOVIAN_L1_ATTRIBUTES_SELECTOR.end());
    // [176:178] DA-footprint scalar is zero (op-node decodes zero as its default).
    BOOST_CHECK_EQUAL(dep.data[176], 0);
    BOOST_CHECK_EQUAL(dep.data[177], 0);
}

BOOST_AUTO_TEST_CASE(SourceHashDerivation)
{
    // sourceHash = keccak(bytes32(1) || keccak(l1Hash || bytes32(seq))); L2
    // time is deliberately not bound. All-zero fixture: the encoder, not the
    // seam (which refuses the unset sentinel).
    auto const env = synthesizeOpEthL1AttributesDeposit(OpEthL1BlockInfo{}, false);
    auto const dep = decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});
    std::array<uint8_t, 64> innerInput{};
    const auto inner =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(innerInput.data(), innerInput.size()));
    std::array<uint8_t, 64> domainInput{};
    domainInput[31] = 1;
    std::copy(inner.begin(), inner.end(), domainInput.begin() + 32);
    const auto expected =
        bcos::crypto::keccak256Hash(bcos::bytesConstRef(domainInput.data(), domainInput.size()));
    BOOST_CHECK_EQUAL_COLLECTIONS(
        dep.sourceHash.bytes, dep.sourceHash.bytes + 32, expected.begin(), expected.end());
}

BOOST_AUTO_TEST_CASE(SeamRefusesUnsetSentinels)
{
    BOOST_CHECK_THROW((void)synthesizeOpEthL1AttributesEnvelope(
                          kSchedule, OpEthL1BlockInfo{}, kJovianActivationMs, kJovianActivationMs),
        std::invalid_argument);

    auto info = filledL1Info();
    info.baseFeeScalar = 0;
    std::fill(info.batcherHash.bytes, info.batcherHash.bytes + 32, 0);
    BOOST_CHECK_THROW((void)synthesizeOpEthL1AttributesEnvelope(
                          kSchedule, info, kJovianActivationMs, kJovianActivationMs),
        std::invalid_argument);
}

// op-node emits the PREVIOUS fork's L1-attributes layout on the Jovian
// ACTIVATION block — isJovianButNotFirstBlock (derive/l1_block_info.go:462-470)
// — because the L1Block predeploy is upgraded by that very block and cannot
// already speak the 178-byte Jovian ABI.
BOOST_AUTO_TEST_CASE(JovianActivationBlockKeepsIsthmusLayout)
{
    auto const info = filledL1Info();
    auto layoutSize = [&](int64_t childMs, int64_t parentMs) {
        auto const env = synthesizeOpEthL1AttributesEnvelope(kSchedule, info, childMs, parentMs);
        return decodeOpDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()}).data.size();
    };
    // Activation block: child is Jovian, parent is not -> still Isthmus's 176 bytes.
    BOOST_CHECK_EQUAL(
        layoutSize(kJovianActivationMs, kParentOfActivationMs), OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    // The very next block: parent is Jovian too -> the 178-byte Jovian layout.
    BOOST_CHECK_EQUAL(layoutSize(kJovianActivationMs + 1000, kJovianActivationMs),
        OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
    // Pre-Jovian stays Isthmus; Karst (a Jovian superset) keeps the Jovian layout.
    BOOST_CHECK_EQUAL(layoutSize(kParentOfActivationMs, kParentOfActivationMs - 1000),
        OP_ETH_ISTHMUS_L1_ATTRIBUTES_LEN);
    constexpr int64_t kKarstMs = static_cast<int64_t>(kKarstTime) * 1000;
    BOOST_CHECK_EQUAL(layoutSize(kKarstMs, kKarstMs - 1000), OP_ETH_JOVIAN_L1_ATTRIBUTES_LEN);
}

// Golden envelope bytes for both layouts — the exact bytes the retired legacy
// parity leg proved equal to op-geth's encoder, now pinned by value. The Isthmus/
// Jovian offset cases above already bind every field; these pins freeze the full
// envelope framing (sourceHash derivation, gas, list header) against regression.
BOOST_AUTO_TEST_CASE(GoldenEnvelopeBytes)
{
    auto const info = filledL1Info();
    auto const isthmusEnv = synthesizeOpEthL1AttributesDeposit(info, false);
    BOOST_CHECK_EQUAL(bcos::toHex(isthmusEnv),
        "7ef90104a0282c4b8b519c60f391a388f34f7761cc9d98f8f59b7a636207699ccebcff74b294deaddeaddeaddeaddeaddeaddeaddeaddead00019442000000000000000000000000000000000000158080830f424080b8b0098999bea1b2c3d4112233441122334455667788223344556677889933445566778899aa000000000000000000000000000000000000000000000000445566778899aabb0000000000000000000000000000000000000000000000005566778899aabbcc606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf5566778899aabbccddeeff00");
    auto const jovianEnv = synthesizeOpEthL1AttributesDeposit(info, true);
    BOOST_CHECK_EQUAL(bcos::toHex(jovianEnv),
        "7ef90106a0282c4b8b519c60f391a388f34f7761cc9d98f8f59b7a636207699ccebcff74b294deaddeaddeaddeaddeaddeaddeaddeaddead00019442000000000000000000000000000000000000158080830f424080b8b23db6be2ba1b2c3d4112233441122334455667788223344556677889933445566778899aa000000000000000000000000000000000000000000000000445566778899aabb0000000000000000000000000000000000000000000000005566778899aabbcc606162636465666768696a6b6c6d6e6f707172737475767778797a7b7c7d7e7f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeaf5566778899aabbccddeeff000000");
}

BOOST_AUTO_TEST_SUITE_END()
