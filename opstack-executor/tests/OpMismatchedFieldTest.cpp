// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the OP commitments comparison pure function (OpEngineSeam.h): 8 fields,
// comparison order (first mismatch wins), the "transactionsRoot" literal, and the optional
// computed-side-only gating (blobGasUsed/requestsHash).

#include <opstack-executor/OpEngineSeam.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <boost/test/unit_test.hpp>

namespace bcos::evm::engine
{
namespace
{
using C = OpBlockCommitments;

C match()  // two identical default commitments: all-zero, both optionals nullopt
{
    return C{};
}

/// h256 with a distinctive first byte (deterministic, avoids hex-string ctor dependence).
bcos::h256 makeH256(bcos::byte firstByte)
{
    bcos::h256 h;
    h.data()[0] = firstByte;
    return h;
}
}  // namespace

namespace
{
bcos::engine::ExecutionPayload makePayload()
{
    bcos::engine::ExecutionPayload p;
    p.receiptsRoot = makeH256(0x11);
    p.logsBloom[0] = 0xaa;  // Bloom = std::array<byte,256>
    p.logsBloom[255] = 0xbb;
    p.withdrawalsRoot = makeH256(0x22);
    p.stateRoot = makeH256(0x33);
    p.gasUsed = bcos::u256(12345);
    p.blobGasUsed = bcos::u256(54321);
    return p;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpMismatchedFieldSuite)

BOOST_AUTO_TEST_CASE(AllFieldsMatchReturnsNullopt)
{
    const C c = match();
    const C a = match();
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());
}

BOOST_AUTO_TEST_CASE(ReportsReceiptsRootFirst)
{
    C c = match();
    C a = match();
    a.receiptsRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "receiptsRoot");
}

BOOST_AUTO_TEST_CASE(ReportsLogsBloomSecond)
{
    C c = match();
    C a = match();
    a.logsBloom.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "logsBloom");
}

BOOST_AUTO_TEST_CASE(ReportsWithdrawalsRoot)
{
    C c = match();
    C a = match();
    a.withdrawalsRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "withdrawalsRoot");
}

BOOST_AUTO_TEST_CASE(ReportsStateRoot)
{
    C c = match();
    C a = match();
    a.stateRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "stateRoot");
}

BOOST_AUTO_TEST_CASE(ReportsGasUsed)
{
    C c = match();
    C a = match();
    a.gasUsed = bcos::u256(1);
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "gasUsed");
}

BOOST_AUTO_TEST_CASE(TxRootSlotReportsTransactionsRootLiteral)
{
    C c = match();
    C a = match();
    a.txRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "transactionsRoot");  // NOT "txRoot"
}

BOOST_AUTO_TEST_CASE(FirstMismatchWins)
{
    C c = match();
    C a = match();
    a.receiptsRoot.data()[0] = 0x01;
    a.stateRoot.data()[0] = 0x01;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "receiptsRoot");
}

BOOST_AUTO_TEST_CASE(FirstMismatchWinsMidField)
{
    C c = match();
    C a = match();
    a.gasUsed = bcos::u256(1);      // field 5 differs
    a.txRoot.data()[0] = 0x01;      // field 6 also differs
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c, a), "gasUsed");  // mid-field order pinned
}

BOOST_AUTO_TEST_CASE(BlobGasUsedComparedOnlyWhenComputedHasValue)
{
    // computed nullopt + announced value → SKIP (pre-Jovian real path: seal.blobGasUsed nullopt,
    // payload.blobGasUsed=0).
    C c = match();
    C a = match();
    a.blobGasUsed = 1;
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());

    // computed value + announced value different → compare
    C c2 = match();
    C a2 = match();
    c2.blobGasUsed = 1;
    a2.blobGasUsed = 2;
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c2, a2), "blobGasUsed");

    // computed value + announced value equal → match
    C c3 = match();
    C a3 = match();
    c3.blobGasUsed = 7;
    a3.blobGasUsed = 7;
    BOOST_CHECK(!mismatchedFieldOf(c3, a3).has_value());
}

BOOST_AUTO_TEST_CASE(RequestsHashComparedOnlyWhenComputedHasValue)
{
    C c = match();
    C a = match();
    a.requestsHash = bcos::h256{};
    BOOST_CHECK(!mismatchedFieldOf(c, a).has_value());  // computed nullopt → SKIP

    C c2 = match();
    C a2 = match();
    c2.requestsHash = makeH256(0x01);
    a2.requestsHash = makeH256(0x02);
    BOOST_CHECK_EQUAL(*mismatchedFieldOf(c2, a2), "requestsHash");

    // equal → match (4-element matrix completed)
    C c3 = match();
    C a3 = match();
    c3.requestsHash = makeH256(0x09);
    a3.requestsHash = makeH256(0x09);
    BOOST_CHECK(!mismatchedFieldOf(c3, a3).has_value());
}

BOOST_AUTO_TEST_CASE(AnnouncedProjectsAllEightFields)
{
    const auto payload = makePayload();
    const auto txRoot = makeH256(0x44);
    bcostars::protocol::BlockHeaderImpl header;
    header.setRequestsHash(makeH256(0x55));  // BlockHeaderImpl::setRequestsHash(h256), non-optional

    const auto announced = announcedCommitmentsOf(payload, txRoot, header);
    BOOST_CHECK_EQUAL(announced.receiptsRoot, payload.receiptsRoot);
    BOOST_CHECK_EQUAL(announced.logsBloom.data()[0], 0xaa);       // byte-faithful bloom
    BOOST_CHECK_EQUAL(announced.logsBloom.data()[255], 0xbb);
    BOOST_CHECK_EQUAL(announced.withdrawalsRoot, *payload.withdrawalsRoot);
    BOOST_CHECK_EQUAL(announced.stateRoot, payload.stateRoot);
    BOOST_CHECK_EQUAL(announced.gasUsed, payload.gasUsed);
    BOOST_CHECK_EQUAL(announced.txRoot, txRoot);
    BOOST_REQUIRE(announced.blobGasUsed.has_value());
    BOOST_CHECK_EQUAL(*announced.blobGasUsed, 54321u);            // u256 → uint64 narrow
    BOOST_REQUIRE(announced.requestsHash.has_value());
    BOOST_CHECK_EQUAL(*announced.requestsHash, makeH256(0x55));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::engine
