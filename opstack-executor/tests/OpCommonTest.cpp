// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpCommonTest — runtime coverage for OpCommon.h's pure decision functions: the bounds-checked
// narrowings (narrowGasUsed / narrowU256ToU64 / narrowU256ToI64), requireHeaderField's
// OpConsensusError classification (never std::bad_optional_access — the INVALID/-32603 boundary
// depends on it), and classifyTxType's EIP-2718 type-byte mapping.

#include <opstack-executor/OpCommon.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

using bcos::evm::engine::OpConsensusError;
namespace detail = bcos::evm::engine::detail;
namespace op = bcos::evm::opstack;

namespace
{
/// All-ones 256-bit value (fixed-precision cpp_int: complement of zero wraps to the max).
const bcos::u256 kU256Max = ~bcos::u256{0};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpCommonTest)

BOOST_AUTO_TEST_CASE(NarrowGasUsedAcceptsInt64Range)
{
    BOOST_CHECK_EQUAL(op::narrowGasUsed(bcos::u256{0}), 0);
    BOOST_CHECK_EQUAL(op::narrowGasUsed(bcos::u256{1}), 1);
    const bcos::u256 kMaxI64(std::numeric_limits<int64_t>::max());
    BOOST_CHECK_EQUAL(op::narrowGasUsed(kMaxI64), std::numeric_limits<int64_t>::max());
}

BOOST_AUTO_TEST_CASE(NarrowGasUsedRejectsAboveInt64)
{
    const bcos::u256 kMaxI64PlusOne = bcos::u256(std::numeric_limits<int64_t>::max()) + 1;
    // Exact-type anchor: a corrupt receipt's gasUsed must classify as OpConsensusError
    // (INVALID), never a bare std::runtime_error escaping the INVALID/-32603 boundary.
    BOOST_CHECK_THROW((void)op::narrowGasUsed(kMaxI64PlusOne), OpConsensusError);
    BOOST_CHECK_THROW((void)op::narrowGasUsed(kU256Max), OpConsensusError);
    // Message names the failure for the operator log.
    try
    {
        (void)op::narrowGasUsed(kMaxI64PlusOne);
        BOOST_FAIL("narrowGasUsed must throw above INT64_MAX");
    }
    catch (const OpConsensusError& e)
    {
        BOOST_CHECK(std::string(e.what()).find("int64_t") != std::string::npos);
    }
}

BOOST_AUTO_TEST_CASE(NarrowU256ToU64Boundary)
{
    BOOST_CHECK_EQUAL(detail::narrowU256ToU64(bcos::u256{0}, "f"), 0u);
    BOOST_CHECK_EQUAL(detail::narrowU256ToU64(bcos::u256{1}, "f"), 1u);
    const bcos::u256 kMaxU64(std::numeric_limits<uint64_t>::max());
    BOOST_CHECK_EQUAL(detail::narrowU256ToU64(kMaxU64, "f"), std::numeric_limits<uint64_t>::max());
    BOOST_CHECK_THROW(detail::narrowU256ToU64(kMaxU64 + 1, "f"), OpConsensusError);
    BOOST_CHECK_THROW(detail::narrowU256ToU64(kU256Max, "f"), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(NarrowU256ToI64Boundary)
{
    BOOST_CHECK_EQUAL(detail::narrowU256ToI64(bcos::u256{0}, "f"), 0);
    BOOST_CHECK_EQUAL(detail::narrowU256ToI64(bcos::u256{1}, "f"), 1);
    const bcos::u256 kMaxI64(std::numeric_limits<int64_t>::max());
    BOOST_CHECK_EQUAL(detail::narrowU256ToI64(kMaxI64, "f"), std::numeric_limits<int64_t>::max());
    BOOST_CHECK_THROW(detail::narrowU256ToI64(kMaxI64 + 1, "f"), OpConsensusError);
    // The (INT64_MAX, UINT64_MAX] band the signed narrowing exists for: a uint64_t ceiling
    // would let this wrap negative through the u64->i64 conversion.
    const bcos::u256 kMaxU64(std::numeric_limits<uint64_t>::max());
    BOOST_CHECK_THROW(detail::narrowU256ToI64(kMaxU64, "f"), OpConsensusError);
}

BOOST_AUTO_TEST_CASE(RequireHeaderFieldEngagedReturnsValue)
{
    const std::optional<bcos::u256> engaged{bcos::u256{42}};
    BOOST_CHECK(detail::requireHeaderField(engaged, "f") == bcos::u256{42});
}

BOOST_AUTO_TEST_CASE(RequireHeaderFieldDisengagedThrowsConsensusError)
{
    const std::optional<bcos::u256> empty;
    // Exact-type anchor: `.value()` would throw std::bad_optional_access, which is neither
    // OpConsensusError nor OpStorageError and would escape INVALID classification.
    BOOST_CHECK_THROW(
        (void)detail::requireHeaderField(empty, "BlockInfo::baseFee"), OpConsensusError);
    try
    {
        (void)detail::requireHeaderField(empty, "BlockInfo::baseFee");
        BOOST_FAIL("requireHeaderField must throw on a disengaged optional");
    }
    catch (const OpConsensusError& e)
    {
        BOOST_CHECK(std::string(e.what()).find("BlockInfo::baseFee") != std::string::npos);
    }
    catch (...)
    {
        BOOST_FAIL("must throw OpConsensusError, not bad_optional_access or anything else");
    }
}

BOOST_AUTO_TEST_CASE(ClassifyTxTypeMapping)
{
    // deposit byte passes through as itself
    BOOST_CHECK_EQUAL(op::classifyTxType(0x7e), 0x7e);
    // legacy RLP list prefix range folds to 0
    BOOST_CHECK_EQUAL(op::classifyTxType(0xc0), 0);
    BOOST_CHECK_EQUAL(op::classifyTxType(0xff), 0);
    // typed transactions keep their own type byte
    BOOST_CHECK_EQUAL(op::classifyTxType(0x01), 0x01);
    BOOST_CHECK_EQUAL(op::classifyTxType(0x02), 0x02);
    BOOST_CHECK_EQUAL(op::classifyTxType(0x04), 0x04);
    // unknown bytes below the RLP list base pass through unchanged (callers keep their own guard)
    BOOST_CHECK_EQUAL(op::classifyTxType(0x05), 0x05);
}

BOOST_AUTO_TEST_SUITE_END()
