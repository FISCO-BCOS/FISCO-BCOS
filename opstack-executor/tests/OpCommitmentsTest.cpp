// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpCommitmentsTest — runtime coverage for the six-way commitment comparison surface in
// OpCommitments.h: mismatchedFieldOf's per-field name reporting (incl. the txRoot slot reporting
// "transactionsRoot" and the bidirectional presence-asymmetry rejection for blobGasUsed /
// requestsHash), commitmentsOf's seal->commitments projection, and payloadBloomToH2048's byte
// fidelity.

#include <opstack-executor/OpCommitments.h>

#include <boost/test/unit_test.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>

using bcos::evm::engine::OpBlockCommitments;
namespace engine = bcos::evm::engine;
namespace op = bcos::evm::opstack;

namespace
{
bcos::h256 filledH256(uint8_t fill)
{
    bcos::h256 h;
    std::memset(h.data(), fill, h.size());
    return h;
}

evmone::hash256 filledEvmc32(uint8_t fill)
{
    evmone::hash256 h{};
    std::memset(h.bytes, fill, sizeof(h.bytes));
    return h;
}

/// A fully-populated baseline: every comparison starts from two equal commitments.
OpBlockCommitments baseCommitments()
{
    OpBlockCommitments c;
    c.receiptsRoot = filledH256(0x11);
    c.logsBloom = bcos::h2048{};
    for (size_t i = 0; i < c.logsBloom.size(); ++i)
        c.logsBloom.data()[i] = static_cast<bcos::byte>(i & 0xff);
    c.withdrawalsRoot = filledH256(0x22);
    c.stateRoot = filledH256(0x33);
    c.gasUsed = bcos::u256{12345};
    c.txRoot = filledH256(0x44);
    c.blobGasUsed = uint64_t{777};
    c.requestsHash = filledH256(0x55);
    return c;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpCommitmentsTest)

BOOST_AUTO_TEST_CASE(EqualCommitmentsReportNoMismatch)
{
    const auto a = baseCommitments();
    const auto b = baseCommitments();
    BOOST_CHECK(!engine::mismatchedFieldOf(a, b).has_value());
}

BOOST_AUTO_TEST_CASE(EachFieldMismatchReportsExactName)
{
    const auto base = baseCommitments();
    const std::pair<const char*, std::function<void(OpBlockCommitments&)>> cases[] = {
        {"receiptsRoot", [](OpBlockCommitments& c) { c.receiptsRoot = filledH256(0x99); }},
        {"logsBloom", [](OpBlockCommitments& c) { c.logsBloom.data()[128] ^= 0xff; }},
        {"withdrawalsRoot", [](OpBlockCommitments& c) { c.withdrawalsRoot = filledH256(0x99); }},
        {"stateRoot", [](OpBlockCommitments& c) { c.stateRoot = filledH256(0x99); }},
        {"gasUsed", [](OpBlockCommitments& c) { c.gasUsed += 1; }},
        // The txRoot slot deliberately reports the payload-side field name.
        {"transactionsRoot", [](OpBlockCommitments& c) { c.txRoot = filledH256(0x99); }},
        {"blobGasUsed", [](OpBlockCommitments& c) { c.blobGasUsed = uint64_t{778}; }},
        {"requestsHash", [](OpBlockCommitments& c) { c.requestsHash = filledH256(0x99); }},
    };
    for (const auto& [expectedName, perturb] : cases)
    {
        auto announced = base;
        perturb(announced);
        const auto mismatch = engine::mismatchedFieldOf(base, announced);
        BOOST_REQUIRE_MESSAGE(mismatch.has_value(), "expected mismatch for " << expectedName);
        BOOST_CHECK_MESSAGE(*mismatch == expectedName,
            "field " << expectedName << " reported as '" << *mismatch << "'");
    }
}

BOOST_AUTO_TEST_CASE(BlobGasUsedPresenceAsymmetryIsMismatchBothWays)
{
    const auto base = baseCommitments();  // blobGasUsed engaged
    {
        auto announced = base;
        announced.blobGasUsed = std::nullopt;  // only computed has a value
        const auto mismatch = engine::mismatchedFieldOf(base, announced);
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, "blobGasUsed");
    }
    {
        auto computed = base;
        computed.blobGasUsed = std::nullopt;  // only announced has a value
        const auto mismatch = engine::mismatchedFieldOf(computed, base);
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, "blobGasUsed");
    }
}

BOOST_AUTO_TEST_CASE(RequestsHashPresenceAsymmetryIsMismatchBothWays)
{
    const auto base = baseCommitments();  // requestsHash engaged
    {
        auto announced = base;
        announced.requestsHash = std::nullopt;
        const auto mismatch = engine::mismatchedFieldOf(base, announced);
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, "requestsHash");
    }
    {
        auto computed = base;
        computed.requestsHash = std::nullopt;
        const auto mismatch = engine::mismatchedFieldOf(computed, base);
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, "requestsHash");
    }
}

BOOST_AUTO_TEST_CASE(CommitmentsOfProjectsEveryField)
{
    op::OpBlockSeal seal;
    seal.receiptsRoot = filledEvmc32(0xa1);
    for (size_t i = 0; i < sizeof(seal.logsBloom.bytes); ++i)
        seal.logsBloom.bytes[i] = static_cast<uint8_t>((i * 7) & 0xff);
    seal.withdrawalsRoot = filledEvmc32(0xb2);
    seal.requestsHash = filledEvmc32(0xc3);
    seal.blobGasUsed = uint64_t{4242};

    const auto stateRoot = filledH256(0xd4);
    const auto txRoot = filledH256(0xe5);
    const auto c = engine::commitmentsOf(seal, stateRoot, 9001, txRoot);

    BOOST_CHECK(std::memcmp(c.receiptsRoot.data(), seal.receiptsRoot.bytes, 32) == 0);
    BOOST_CHECK(std::memcmp(c.logsBloom.data(), seal.logsBloom.bytes, 256) == 0);
    BOOST_CHECK(std::memcmp(c.withdrawalsRoot.data(), seal.withdrawalsRoot.bytes, 32) == 0);
    BOOST_CHECK(c.stateRoot == stateRoot);
    BOOST_CHECK(c.gasUsed == bcos::u256{9001});
    BOOST_CHECK(c.txRoot == txRoot);
    BOOST_REQUIRE(c.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*c.blobGasUsed, 4242u);
    BOOST_REQUIRE(c.requestsHash.has_value());
    BOOST_CHECK(std::memcmp(c.requestsHash->data(), seal.requestsHash->bytes, 32) == 0);
}

BOOST_AUTO_TEST_CASE(CommitmentsOfWithoutRequestsHashLeavesItEmpty)
{
    op::OpBlockSeal seal;  // CANCUN-family: requestsHash disengaged
    seal.blobGasUsed = std::nullopt;

    const auto c = engine::commitmentsOf(seal, bcos::h256{}, 0, bcos::h256{});
    BOOST_CHECK(!c.requestsHash.has_value());
    BOOST_CHECK(!c.blobGasUsed.has_value());
    BOOST_CHECK(c.gasUsed == bcos::u256{0});
}

BOOST_AUTO_TEST_CASE(PayloadBloomToH2048IsByteFaithful)
{
    std::array<bcos::byte, 256> bloom;
    for (size_t i = 0; i < bloom.size(); ++i)
        bloom[i] = static_cast<bcos::byte>((i * 31 + 17) & 0xff);

    const auto out = engine::payloadBloomToH2048(bloom);
    BOOST_CHECK(std::memcmp(out.data(), bloom.data(), bloom.size()) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
