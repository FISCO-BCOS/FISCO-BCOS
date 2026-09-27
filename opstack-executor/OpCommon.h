// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OP error types and bcos::<->evmc conversion helpers. The block-seal/result types and the
// block-context builder retired with the legacy bcos-evm execution layer (step 3.5); their
// bcos-evm-free counterparts live in OpEthCommitments.h / OpEthBlockExecute.h.

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace bcos::evm
{
/// Thrown for anything OP block execution classifies as a consensus-level rejection (error
/// table): malformed/undecodable raw tx bytes, block-execution semantic throws
/// (empty block, first tx not the L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
/// on the caller side, never -32603. Lives in bcos::evm so both the opstack and engine
/// namespaces (and the code that references it from either) resolve it by outer-scope lookup.
struct OpConsensusError : std::runtime_error
{
    std::optional<bcos::h256> txHash;
    /// True when the block gas pool cannot fit this tx (skip this build, do not evict).
    bool capacity = false;
    /// The opValidate table's std::error_code when the reject came from the
    /// validate step. Typed classification surface next to txHash:
    /// consumers classify on this field + capacity, never on what() substrings.
    /// Empty for every other reject class (chain-id/mirror/field faults, capacity,
    /// wiring).
    std::error_code validateErrorCode;

    explicit OpConsensusError(std::string const& what_arg) : std::runtime_error(what_arg) {}

    /// Per-tx reject. `txHash` is structured; do not encode it into `what()`.
    OpConsensusError(std::string what_arg, bcos::h256 hash, bool _capacity = false)
      : std::runtime_error(std::move(what_arg)), txHash(hash), capacity(_capacity)
    {}
};
}  // namespace bcos::evm

namespace bcos::evm::engine
{
/// Thrown when the storage error slot's poison flag is set (a storage2-layer failure, not a
/// consensus violation — OpStorageErrorGuard.h's poison-flag error channel contract). Maps to
/// JSON-RPC -32603 internal error on the caller side, never INVALID.
struct OpStorageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Table holding each accepted OP block's transactions as their raw EIP-2718 envelopes, keyed by
/// keccak(envelope). Deliberately not the generic SYS_HASH_2_TX (which holds tars Transaction
/// objects — an Ethereum envelope would decode into a plausible-looking but hash-mismatched tx).
inline constexpr std::string_view SYS_ETH_HASH_2_RAWTX{"s_eth_hash_2_rawtx"};

namespace detail
{
// ---- bcos:: <-> evmc:: fixed-size conversions ----

inline evmc::address toEvmcAddress(const bcos::Address& a) noexcept
{
    evmc::address out{};
    std::memcpy(out.bytes, a.data(), sizeof(out.bytes));
    return out;
}

inline evmc::bytes32 toEvmcBytes32(const bcos::h256& h) noexcept
{
    evmc::bytes32 out{};
    std::memcpy(out.bytes, h.data(), sizeof(out.bytes));
    return out;
}

/// Bounds-checked u256→u64 narrowing — explicit > max check, never raw static_cast
/// (silent-truncation guard).
inline uint64_t narrowU256ToU64(const bcos::u256& v, const char* fieldName)
{
    if (!bcos::u256FitsUint64(v))
        throw OpConsensusError(std::string("field exceeds uint64_t range: ") + fieldName);
    return static_cast<uint64_t>(v);
}

/// Bounds-checked u256→int64 narrowing — BlockInfo::gas_limit is int64_t, so narrowU256ToU64's
/// uint64_t ceiling is NOT sufficient: a value in (INT64_MAX, UINT64_MAX] would wrap negative
/// through the uint64_t→int64_t conversion, silently defeating the guard on the signed field.
inline int64_t narrowU256ToI64(const bcos::u256& v, const char* fieldName)
{
    static const bcos::u256 kMaxI64(std::numeric_limits<int64_t>::max());
    if (v > kMaxI64)
        throw OpConsensusError(std::string("field exceeds int64_t range: ") + fieldName);
    return static_cast<int64_t>(v);
}

/// Bounds-checked u256→int64 narrowing of a receipt's gasUsed (a corrupt receipt must not wrap
/// the gas pool). Kept here rather than OpEthBlockExecute.h's narrowOpEthGasUsed because that
/// one throws the new layer's OpEthBlockError (a bare runtime_error), which would escape the
/// INVALID/-32603 classification — this one throws OpConsensusError (INVALID), as legacy.
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        // Classified as OpConsensusError (INVALID), never a bare runtime_error escaping the
        // INVALID/-32603 boundary (test: NarrowGasUsedRejectsAboveInt64).
        throw OpConsensusError("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}

/// Decimal string for the tars receipt field. eth_getTransactionReceipt reads this via
/// `safeCastToU256` (`boost::lexical_cast<u256>`, decimal — not `safeFromQuantity`).
[[nodiscard]] inline std::string decimalCumulative(uint64_t cumulative)
{
    return std::to_string(cumulative);
}

/// Strict-path optional header-field unwrap. `.value()` would throw std::bad_optional_access,
/// which is neither OpConsensusError nor OpStorageError and would escape the INVALID/-32603
/// classification; a missing header field is an input error and must classify as INVALID.
template <class T>
[[nodiscard]] T requireHeaderField(const std::optional<T>& opt, const char* fieldName)
{
    if (!opt.has_value())
        throw OpConsensusError(std::string("missing required header field: ") + fieldName);
    return *opt;
}
}  // namespace detail
}  // namespace bcos::evm::engine
