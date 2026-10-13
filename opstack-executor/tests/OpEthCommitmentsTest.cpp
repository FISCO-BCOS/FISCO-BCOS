// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthCommitmentsTest — the bcos-evm-free six-way commitment surface
// (OpEthCommitments.h, step 3.1.4): projection field mapping, the
// presence-and-value fork-field comparison, the announced side's guards, the
// fork predicates (ms→s conversion pinned), and the empty-range txRoot.

#include <opstack-executor/OpEthCommitments.h>

#include <opstack-executor/OpCommon.h>  // bcos::evm::OpConsensusError (typed catch)
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

using namespace bcos::executor_v1::opstack;

namespace
{
constexpr uint64_t kJovianTime = 1000;  // seconds (the schedule's unit)
constexpr uint64_t kKarstTime = 2000;
const bcos::ledger::OpForkSchedule kSchedule{.m_jovianTime = kJovianTime,
    .m_karstTime = kKarstTime};
constexpr int64_t kIsthmusMs = static_cast<int64_t>(kJovianTime - 1) * 1000;
constexpr int64_t kJovianMs = static_cast<int64_t>(kKarstTime - 1) * 1000;
constexpr int64_t kKarstMs = static_cast<int64_t>(kKarstTime) * 1000;

OpEthBlockSeal makeSeal()
{
    OpEthBlockSeal seal{};
    seal.receiptsRoot =
        bcos::h256{"0x1111111111111111111111111111111111111111111111111111111111111111"};
    for (size_t i = 0; i < seal.logsBloom.size(); ++i)
        seal.logsBloom[i] = static_cast<bcos::byte>(i);
    seal.withdrawalsRoot =
        bcos::h256{"0x2222222222222222222222222222222222222222222222222222222222222222"};
    seal.requestsHash =
        bcos::h256{"0x3333333333333333333333333333333333333333333333333333333333333333"};
    seal.blobGasUsed = 12345;
    return seal;
}

OpEthBlockCommitments makeComputed()
{
    return opEthCommitmentsOf(makeSeal(),
        bcos::h256{"0x4444444444444444444444444444444444444444444444444444444444444444"},
        21000, bcos::h256{"0x5555555555555555555555555555555555555555555555555555555555555555"});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEthCommitmentsSuite)

BOOST_AUTO_TEST_CASE(CommitmentsOfProjectsAllFields)
{
    auto const seal = makeSeal();
    auto const computed = opEthCommitmentsOf(seal,
        bcos::h256{"0x4444444444444444444444444444444444444444444444444444444444444444"}, 21000,
        bcos::h256{"0x5555555555555555555555555555555555555555555555555555555555555555"});
    BOOST_CHECK_EQUAL(computed.receiptsRoot, seal.receiptsRoot);
    BOOST_CHECK_EQUAL_COLLECTIONS(computed.logsBloom.begin(), computed.logsBloom.end(),
        seal.logsBloom.begin(), seal.logsBloom.end());
    BOOST_CHECK(computed.withdrawalsRoot == seal.withdrawalsRoot);
    BOOST_CHECK_EQUAL(computed.stateRoot,
        bcos::h256{"0x4444444444444444444444444444444444444444444444444444444444444444"});
    BOOST_CHECK_EQUAL(computed.gasUsed, bcos::u256(21000));
    BOOST_CHECK_EQUAL(computed.txRoot,
        bcos::h256{"0x5555555555555555555555555555555555555555555555555555555555555555"});
    BOOST_CHECK_EQUAL(computed.blobGasUsed.has_value(), seal.blobGasUsed.has_value());
    BOOST_CHECK(computed.blobGasUsed == seal.blobGasUsed);
    BOOST_CHECK(computed.requestsHash == seal.requestsHash);
}

BOOST_AUTO_TEST_CASE(MismatchedFieldOfReportsEachField)
{
    auto const base = makeComputed();
    BOOST_CHECK(!opEthMismatchedFieldOf(base, base).has_value());

    auto flip = [&](auto mutate, std::string const& expected) {
        auto announced = base;
        mutate(announced);
        auto const field = opEthMismatchedFieldOf(base, announced);
        BOOST_REQUIRE(field.has_value());
        BOOST_CHECK_EQUAL(*field, expected);
    };
    flip([](auto& c) { c.receiptsRoot = bcos::h256{}; }, "receiptsRoot");
    flip([](auto& c) { c.logsBloom = bcos::h2048{}; }, "logsBloom");
    flip([](auto& c) { c.withdrawalsRoot = bcos::h256{}; }, "withdrawalsRoot");
    flip([](auto& c) { c.stateRoot = bcos::h256{}; }, "stateRoot");
    flip([](auto& c) { c.gasUsed = bcos::u256(1); }, "gasUsed");
    flip([](auto& c) { c.txRoot = bcos::h256{}; }, "transactionsRoot");
    flip([](auto& c) { c.blobGasUsed = uint64_t{1}; }, "blobGasUsed");
    flip([](auto& c) { c.requestsHash = bcos::h256{}; }, "requestsHash");

    // Presence asymmetry on the fork-gated fields is a real mismatch in both
    // directions (fork-config divergence between peers).
    auto absent = base;
    absent.blobGasUsed = std::nullopt;
    BOOST_CHECK(opEthMismatchedFieldOf(base, absent).has_value());
    BOOST_CHECK(opEthMismatchedFieldOf(absent, base).has_value());
}

BOOST_AUTO_TEST_CASE(AnnouncedCommitmentsOfProjectsPayload)
{
    bcos::engine::ExecutionPayload payload{};
    payload.receiptsRoot =
        bcos::h256{"0x1111111111111111111111111111111111111111111111111111111111111111"};
    for (size_t i = 0; i < payload.logsBloom.size(); ++i)
        payload.logsBloom[i] = static_cast<bcos::byte>(0xff - i);
    payload.stateRoot = bcos::h256{"0x4444444444444444444444444444444444444444444444444444444444444444"};
    payload.gasUsed = bcos::u256(21000);
    payload.blobGasUsed = bcos::u256(777);
    payload.withdrawalsRoot =
        bcos::h256{"0x2222222222222222222222222222222222222222222222222222222222222222"};

    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    header->setRequestsHash(
        bcos::h256{"0x3333333333333333333333333333333333333333333333333333333333333333"});

    auto const txRoot =
        bcos::h256{"0x5555555555555555555555555555555555555555555555555555555555555555"};
    auto const announced = announcedOpEthCommitmentsOf(payload, txRoot, *header);
    BOOST_CHECK_EQUAL(announced.receiptsRoot, payload.receiptsRoot);
    BOOST_CHECK_EQUAL_COLLECTIONS(announced.logsBloom.begin(), announced.logsBloom.end(),
        payload.logsBloom.begin(), payload.logsBloom.end());
    BOOST_CHECK(announced.withdrawalsRoot == payload.withdrawalsRoot);
    BOOST_CHECK_EQUAL(announced.stateRoot, payload.stateRoot);
    BOOST_CHECK_EQUAL(announced.gasUsed, payload.gasUsed);
    BOOST_CHECK_EQUAL(announced.txRoot, txRoot);
    BOOST_CHECK(announced.blobGasUsed == std::optional<uint64_t>(777));
    BOOST_CHECK(announced.requestsHash == header->requestsHash());
}

BOOST_AUTO_TEST_CASE(AnnouncedCommitmentsOfGuards)
{
    auto header = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    header->setRequestsHash(bcos::h256{});

    // Missing withdrawalsRoot: clean consensus reject, never bad_optional_access.
    {
        bcos::engine::ExecutionPayload payload{};
        payload.withdrawalsRoot = std::nullopt;
        BOOST_CHECK_THROW(
            (void)announcedOpEthCommitmentsOf(payload, bcos::h256{}, *header),
            bcos::evm::OpConsensusError);
    }
    // Over-wide announced blobGasUsed: consensus reject, never truncation.
    {
        bcos::engine::ExecutionPayload payload{};
        payload.withdrawalsRoot = bcos::h256{};
        payload.blobGasUsed = bcos::u256(1) << 64;
        BOOST_CHECK_THROW(
            (void)announcedOpEthCommitmentsOf(payload, bcos::h256{}, *header),
            bcos::evm::OpConsensusError);
    }
    // Absent blobGasUsed passes through as nullopt.
    {
        bcos::engine::ExecutionPayload payload{};
        payload.withdrawalsRoot = bcos::h256{};
        auto const announced = announcedOpEthCommitmentsOf(payload, bcos::h256{}, *header);
        BOOST_CHECK(!announced.blobGasUsed.has_value());
    }
}

BOOST_AUTO_TEST_CASE(ForkPredicatesPinMsToSecConversion)
{
    BOOST_CHECK(!isOpEthJovianActive(kSchedule, kIsthmusMs));
    BOOST_CHECK(isOpEthJovianActive(kSchedule, kJovianMs));
    // Karst is a superset of Jovian.
    BOOST_CHECK(isOpEthJovianActive(kSchedule, kKarstMs));
    BOOST_CHECK(!isOpEthKarstActive(kSchedule, kKarstMs - 1));
    BOOST_CHECK(isOpEthKarstActive(kSchedule, kKarstMs));
    // The boundary itself: kKarstTime*1000-1 ms is still Jovian (the /1000
    // conversion must not round up into the next fork).
    BOOST_CHECK(!isOpEthKarstActive(kSchedule, static_cast<int64_t>(kKarstTime) * 1000 - 1));
}

BOOST_AUTO_TEST_CASE(ComputeTxRootEmptyRangeIsEmptyTrieRoot)
{
    std::vector<bcos::bytes> emptyTxs;
    const bcos::h256 kEmptyTrieRoot{
        std::string{"0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421"}};
    BOOST_CHECK_EQUAL(computeOpEthTransactionsRoot(emptyTxs), kEmptyTrieRoot);
}

BOOST_AUTO_TEST_SUITE_END()
