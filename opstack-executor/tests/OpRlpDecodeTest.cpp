// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// Unit tests for the OP block-context + conversion helpers (OpCommon.h). The RPC display-grade
// raw-envelope RLP decode primitives were retired (block execution consumes the block's tars
// Transaction objects); the CONSENSUS-grade deposit-envelope decoder is
// OpstackExecutor.h's decodeDepositEnvelope, covered by OpDepositEnvelopeTest.

#include <opstack-executor/OpCommon.h>

#include <boost/test/unit_test.hpp>
#include <cstring>
#include <limits>

namespace bcos::evm::engine::detail
{
BOOST_AUTO_TEST_SUITE(OpRlpDecodeTest)

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

BOOST_AUTO_TEST_CASE(narrowU256ToU64Bounds)
{
    // Bounds-checked narrowing: in-range passes, > uint64_max throws OpConsensusError.
    BOOST_CHECK_EQUAL(narrowU256ToU64(bcos::u256(42), "test"), 42U);
    BOOST_CHECK_EQUAL(narrowU256ToU64(bcos::u256(std::numeric_limits<uint64_t>::max()), "test"),
        std::numeric_limits<uint64_t>::max());
    BOOST_CHECK_THROW(narrowU256ToU64(bcos::u256(std::numeric_limits<uint64_t>::max()) + 1, "test"),
        OpConsensusError);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::engine::detail
