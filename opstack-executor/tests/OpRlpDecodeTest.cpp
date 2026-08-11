// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the extracted decode-primitive layer (OpRlpDecode.h). These are pure
// functions over RLP wire bytes — no class template instantiation, no storage.

#include <opstack-executor/OpRlpDecode.h>

#include <boost/test/unit_test.hpp>

namespace bcos::evm::engine::detail
{
BOOST_AUTO_TEST_SUITE(OpRlpDecodeTest)

namespace
{
/// Mutable bytesRef over the given bytes (the decoders advance the ref, so it must be a
/// modifiable lvalue at the call site).
bcos::bytesRef makeRef(bcos::bytes& b)
{
    return bcos::bytesRef(b.data(), b.size());
}
}  // namespace

BOOST_AUTO_TEST_CASE(decodeU64ScalarTest)
{
    {
        bcos::bytes b{0x01};
        auto r = makeRef(b);
        BOOST_CHECK_EQUAL(decodeU64Scalar(r), 1u);
    }
    {
        bcos::bytes b{0x80};  // empty string == 0
        auto r = makeRef(b);
        BOOST_CHECK_EQUAL(decodeU64Scalar(r), 0u);
    }
    {
        bcos::bytes b{0x82, 0x01, 0x00};  // 256 (two-byte, no leading zero)
        auto r = makeRef(b);
        BOOST_CHECK_EQUAL(decodeU64Scalar(r), 256u);
    }
    {
        // Non-canonical leading zero rejected.
        bcos::bytes b{0x82, 0x00, 0x01};
        auto r = makeRef(b);
        BOOST_CHECK_THROW(decodeU64Scalar(r), OpConsensusError);
    }
    {
        // Over-wide for uint64 rejected.
        bcos::bytes b{0x89, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        auto r = makeRef(b);
        BOOST_CHECK_THROW(decodeU64Scalar(r), OpConsensusError);
    }
}

BOOST_AUTO_TEST_CASE(decodeU256ScalarTest)
{
    {
        bcos::bytes b{0x80};
        auto r = makeRef(b);
        BOOST_CHECK(decodeU256Scalar(r) == intx::uint256{0});
    }
    {
        bcos::bytes b{0x05};
        auto r = makeRef(b);
        BOOST_CHECK(decodeU256Scalar(r) == intx::uint256{5});
    }
    {
        // 32-byte max-width scalar accepted (first byte non-zero, so it is canonical; value is
        // 0xff shifted up by 248 bits).
        bcos::bytes b(33);
        b[0] = 0xa0;  // length header: 0x80 + 32
        b[1] = 0xff;
        for (size_t i = 2; i < 33; ++i)
            b[i] = 0x00;
        auto r = makeRef(b);
        BOOST_CHECK(decodeU256Scalar(r) == (intx::uint256{0xff} << 248));
    }
}

BOOST_AUTO_TEST_CASE(decodeBoolFieldTest)
{
    {
        bcos::bytes b{0x80};  // empty == false (Go rlp native bool)
        auto r = makeRef(b);
        BOOST_CHECK(!decodeBoolField(r));
    }
    {
        bcos::bytes b{0x01};
        auto r = makeRef(b);
        BOOST_CHECK(decodeBoolField(r));
    }
    {
        bcos::bytes b{0x00};  // single 0x00 is not a valid Go bool
        auto r = makeRef(b);
        BOOST_CHECK_THROW(decodeBoolField(r), OpConsensusError);
    }
}

BOOST_AUTO_TEST_CASE(decodeAuthYParityScalarTest)
{
    {
        bcos::bytes b{0x01};
        auto r = makeRef(b);
        BOOST_CHECK(decodeAuthYParityScalar(r) == intx::uint256{1});
    }
    {
        bcos::bytes b{0x02};  // 1-byte value 2 is width-valid (skipped at execution, not decode)
        auto r = makeRef(b);
        BOOST_CHECK(decodeAuthYParityScalar(r) == intx::uint256{2});
    }
    {
        // Two-byte encoding (256) overflows the EIP-7702 uint8 → decode-time rejection.
        bcos::bytes b{0x82, 0x01, 0x00};
        auto r = makeRef(b);
        BOOST_CHECK_THROW(decodeAuthYParityScalar(r), OpConsensusError);
    }
}

BOOST_AUTO_TEST_CASE(narrowGasLimitTest)
{
    BOOST_CHECK_EQUAL(narrowGasLimit(0, "test"), 0);
    BOOST_CHECK_EQUAL(
        narrowGasLimit(std::numeric_limits<int64_t>::max(), "test"), std::numeric_limits<int64_t>::max());
    // Exceeds int64_t range → rejected (a canonical 8-byte scalar would silently go negative).
    BOOST_CHECK_THROW(
        narrowGasLimit(static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1, "test"),
        OpConsensusError);
}

BOOST_AUTO_TEST_CASE(fixedSizeConversions)
{
    // toEvmcAddress / toEvmcBytes32 round-trip through the 20/32-byte buffers.
    bcos::Address a;
    a.data()[0] = 0xab;
    a.data()[19] = 0x12;
    auto evmcA = toEvmcAddress(a);
    BOOST_CHECK_EQUAL(std::memcmp(evmcA.bytes, a.data(), 20), 0);

    bcos::h256 h;
    h.data()[0] = 0xcd;
    h.data()[31] = 0xef;
    auto evmcH = toEvmcBytes32(h);
    BOOST_CHECK_EQUAL(std::memcmp(evmcH.bytes, h.data(), 32), 0);
}

BOOST_AUTO_TEST_CASE(decodeAddressFieldTest)
{
    {
        // 20-byte address decodes.
        bcos::bytes b(21);
        b[0] = 0x94;  // 0x80 + 20
        for (size_t i = 1; i < 21; ++i)
            b[i] = static_cast<uint8_t>(i);
        auto r = makeRef(b);
        auto addr = decodeAddressField(r);
        BOOST_CHECK_EQUAL(std::memcmp(addr.bytes, b.data() + 1, 20), 0);
    }
    {
        // Wrong width (19 bytes) rejected.
        bcos::bytes b{0x93, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
        auto r = makeRef(b);
        BOOST_CHECK_THROW(decodeAddressField(r), OpConsensusError);
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::engine::detail
