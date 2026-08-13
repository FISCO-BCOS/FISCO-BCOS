// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the extracted decode-primitive layer (OpRlpDecode.h). These are pure
// functions over RLP wire bytes — no class template instantiation, no storage.

#include <opstack-executor/OpRlpDecode.h>

#include <bcos-evm/eth/RlpEncodeTuple.h>
#include <boost/test/unit_test.hpp>
#include <intx/intx.hpp>

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

using bcos::evm::eth::detail::encodeTuple;

// 0x02 envelope: [chainId, nonce, maxPriority, maxFee, gas, to, value, data, accessList,
// yParity, r, s]
bcos::bytes makeEip1559(uint64_t chainId, intx::uint256 yParity, intx::uint256 r, intx::uint256 s)
{
    auto body = encodeTuple(chainId, uint64_t{0}, intx::uint256{0}, intx::uint256{0},
        uint64_t{21000}, evmc::bytes_view{}, intx::uint256{0}, evmc::bytes_view{},
        evmone::state::AccessList{}, yParity, r, s);
    bcos::bytes out{0x02};
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// legacy envelope: [nonce, gasPrice, gas, to, value, data, v, r, s]
bcos::bytes makeLegacy(intx::uint256 v)
{
    auto body = encodeTuple(uint64_t{0}, intx::uint256{1}, uint64_t{21000}, evmc::bytes_view{},
        intx::uint256{0}, evmc::bytes_view{}, v, intx::uint256{1}, intx::uint256{1});
    return {body.begin(), body.end()};
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

BOOST_AUTO_TEST_CASE(ValidateEnvelopeSignature)
{
    constexpr uint64_t kChainId = 0x2105;
    // Valid eip1559 envelope (r=1, s=1 low-s).
    BOOST_CHECK_NO_THROW(validateEnvelopeSignature(makeEip1559(kChainId, 0, 1, 1), kChainId));
    // chain-id mismatch.
    BOOST_CHECK_THROW(
        validateEnvelopeSignature(makeEip1559(kChainId, 0, 1, 1), kChainId + 1), OpConsensusError);
    // yParity=2 rejected.
    BOOST_CHECK_THROW(
        validateEnvelopeSignature(makeEip1559(kChainId, 2, 1, 1), kChainId), OpConsensusError);
    // s > secp256k1n/2 rejected (EIP-2 high-s malleable signature).
    BOOST_CHECK_THROW(validateEnvelopeSignature(
                          makeEip1559(kChainId, 0, 1, intx::uint256{1} << 255), kChainId),
        OpConsensusError);
    // legacy v=27 (pre-EIP-155) accepted; v = chainId*2+35+0 accepted (chain matches).
    BOOST_CHECK_NO_THROW(validateEnvelopeSignature(makeLegacy(27), kChainId));
    BOOST_CHECK_NO_THROW(validateEnvelopeSignature(makeLegacy(0x2105 * 2 + 35), kChainId));
    // legacy v = chainId*2+35+2: parity 2 pollutes the division -> derived chainId mismatch.
    BOOST_CHECK_THROW(validateEnvelopeSignature(makeLegacy(0x2105 * 2 + 35 + 2), kChainId),
        OpConsensusError);
    // deposit 0x7e: unsigned, returns before any signature check.
    BOOST_CHECK_NO_THROW(validateEnvelopeSignature(bcos::bytes{0x7e}, kChainId));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::engine::detail
