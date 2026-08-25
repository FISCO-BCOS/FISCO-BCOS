// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared error types + the block-execution result for the OP scheduler, plus the block-context
// conversion helpers (bcos<->evmc fixed-size conversions / bounds-checked narrowing / FISCO-header
// -> evmone BlockInfo build). Split out of OpSchedulerSeam.h so dependent layers can throw without
// depending on the class template. OpBlockSeal lives here too (not in OpBlockExecute.h):
// OpExecuteBlockResult carries it by value.
//
// The six-way commitment comparison surface (OpBlockCommitments / commitmentsOf /
// payloadBloomToH2048 / mismatchedFieldOf / detail::toBcosH256 / toBcosBloom) lives in
// OpCommitments.h — this header is deliberately types + conversions, no commitment logic.

#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>  // bcos::toQuantity (hexCumulative reuse)
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::opstack
{
/// Block-header commitment fields. Jovian BlobGasUsed (the DA footprint header field) was
/// reclaimed into this struct.
struct OpBlockSeal
{
    evmone::hash256 receiptsRoot;
    evmone::state::BloomFilter logsBloom;
    evmone::hash256 withdrawalsRoot;
    std::optional<evmone::hash256> requestsHash;  // Isthmus+ has a value; CANCUN-family fork
                                                  // headers lack this field
    /// Jovian block-header BlobGasUsed reuse slot = DA footprint (only non-deposit txs accumulate,
    /// each tx = EstimatedDASize × scalar). Implemented as Σ of the meta.da_footprint over
    /// receipts; a deposits-only block sums no terms and is always 0 ≡ op-geth's first-Jovian-block
    /// special case. When has_da_footprint is false there is always no value.
    std::optional<uint64_t> blobGasUsed;
};

/// "0x" + lowercase hex (op-geth hexutil.Uint64); the value later feeds the receipts-root leaf
/// encoding that returns with the block-seal wiring (part 5).
[[nodiscard]] inline std::string hexCumulative(uint64_t cumulative)
{
    return bcos::toQuantity(cumulative);  // reuse the library quantity formatter
}

/// EIP-2718 tx-type classification, single home for the block-execution sites (the OpScheduler
/// deposit-classification loop and the part-5 seal wiring's txTypes rebuild) so the mapping can't
/// drift and silently emit a wrong receiptsRoot leaf. Maps a raw type byte to the per-receipt
/// type byte:
/// OP deposit 0x7e (kDepositTxType, OpTransition.h) → itself; legacy (>= 0xc0 RLP list prefix)
/// → 0; typed (0x01/0x02/0x04) → its own type byte. Unknown bytes (< 0xc0, not deposit) pass
/// through unchanged — callers that must reject them (the deposit loop) keep their own guard.
[[nodiscard]] constexpr uint8_t classifyTxType(uint8_t typeByte) noexcept
{
    constexpr uint8_t kDepositTypeByte = 0x7e;  // kDepositTxType (OpTransition.h)
    constexpr uint8_t kRlpListBase = 0xc0;      // legacy RLP list prefix
    if (typeByte == kDepositTypeByte)
    {
        return typeByte;  // deposit stored as its own type byte
    }
    if (typeByte >= kRlpListBase)
    {
        return 0;  // legacy
    }
    return typeByte;  // typed 0x01/0x02/0x04 — stored as the type byte itself
}
}  // namespace bcos::evm::opstack

namespace bcos::evm::engine
{
/// Thrown for anything OP block execution classifies as a consensus-level rejection (error
/// table): malformed/undecodable raw tx bytes, the block-shape semantic throws
/// (empty block, missing L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
/// on the caller side, never -32603.
struct OpConsensusError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Thrown when the ledger bridge's poison flag is set (a storage2-layer failure, not a consensus
/// violation — Storage2State.h's poison-flag error channel contract). Maps to JSON-RPC -32603
/// internal error on the caller side, never INVALID.
struct OpStorageError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// Six-way comparison surface for an executed OP block: `seal`'s
/// receiptsRoot/logsBloom/withdrawalsRoot (bcos::evm::opstack::OpBlockSeal, unchanged structure)
/// plus three members below (stateRoot/gasUsed/txRoot) that are deliberately NOT folded into
/// OpBlockSeal.
struct OpExecuteBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;
    bcos::h256 stateRoot;
    uint64_t gasUsed;
    bcos::h256 txRoot;
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

// `toBcosH256` (evmc::bytes32 -> bcos::h256) lives in OpCommitments.h (two identical inline
// definitions of one name in the same namespace would be a redefinition error).

/// Bounds-checked u256→u64 narrowing — explicit > max check, never raw static_cast
/// (silent-truncation guard).
inline uint64_t narrowU256ToU64(const bcos::u256& v, const char* fieldName)
{
    static const bcos::u256 kMaxU64(std::numeric_limits<uint64_t>::max());
    if (v > kMaxU64)
        throw OpConsensusError(
            std::string("OpSchedulerSeam: field exceeds uint64_t range: ") + fieldName);
    return static_cast<uint64_t>(v);
}

/// Bounds-checked u256→int64 narrowing — BlockInfo::gas_limit is int64_t, so narrowU256ToU64's
/// uint64_t ceiling is NOT sufficient: a value in (INT64_MAX, UINT64_MAX] would wrap negative
/// through the uint64_t→int64_t conversion, silently defeating the guard on the signed field.
inline int64_t narrowU256ToI64(const bcos::u256& v, const char* fieldName)
{
    static const bcos::u256 kMaxI64(std::numeric_limits<int64_t>::max());
    if (v > kMaxI64)
        throw OpConsensusError(
            std::string("OpSchedulerSeam: field exceeds int64_t range: ") + fieldName);
    return static_cast<int64_t>(v);
}

/// Strict-path optional header-field unwrap. `.value()` would throw std::bad_optional_access,
/// which is neither OpConsensusError nor OpStorageError and would escape the INVALID/-32603
/// classification; a missing header field is an input error and must classify as INVALID.
template <class T>
[[nodiscard]] T requireHeaderField(const std::optional<T>& opt, const char* fieldName)
{
    if (!opt.has_value())
        throw OpConsensusError(
            std::string("OpSchedulerSeam: missing required header field: ") + fieldName);
    return *opt;
}

/// Build the OP block context from a FISCO header. `gasLimitOverride` injects the head block's
/// gasLimit as blockGasLeft (a minimal test header may leave gasLimit==0); `lenientOptionals`
/// tolerates unset optional header fields as 0 (eth_call path), while block execution uses
/// `.value()` and throws on an unset field.
inline evmone::state::BlockInfo toBlockInfo(const bcos::protocol::BlockHeader& env,
    std::optional<uint64_t> gasLimitOverride = std::nullopt, bool lenientOptionals = false)
{
    evmone::state::BlockInfo blk;
    blk.number = static_cast<int64_t>(env.number());
    // TIMESTAMP UNIT CONVENTION (do not "fix" — see below):
    // FISCO tars store MILLISECONDS; evmone wants SECONDS, so this /1000 is REQUIRED and correct.
    // The RPC boundary converts seconds→milliseconds on the way in (EngineHelper.cpp
    // engineSecondsToInternalMillis / EngineTimestampBoundaryTest), so a header built by the
    // engine already carries ms; feeding it to the EVM un-divided would make every timestamp
    // 1000× too large and diverge from op-geth (which stores seconds). Fork SELECTION is
    // feature-driven (feature_op_jovian) since the feature-flag refactor — the timestamp is no
    // longer a fork selector — but the EVM still receives seconds (op-geth semantics), so the
    // division stays. If a future header source writes seconds directly, convert at THAT boundary
    // — never remove this division.
    blk.timestamp = static_cast<uint64_t>(env.timestamp()) / 1000;
    blk.gas_limit = gasLimitOverride.has_value() ?
                        narrowU256ToI64(bcos::u256(*gasLimitOverride), "BlockInfo::gasLimit") :
                        narrowU256ToI64(env.gasLimit(), "BlockInfo::gasLimit");
    blk.base_fee =
        narrowU256ToU64(lenientOptionals ? env.baseFee().value_or(bcos::u256{0}) :
                                           requireHeaderField(env.baseFee(), "BlockInfo::baseFee"),
            "BlockInfo::baseFee");
    blk.coinbase = toEvmcAddress(env.coinbase());
    blk.prev_randao = toEvmcBytes32(env.prevRandao());
    blk.parent_beacon_block_root = toEvmcBytes32(
        lenientOptionals ?
            env.parentBeaconBlockRoot().value_or(bcos::h256{}) :
            requireHeaderField(env.parentBeaconBlockRoot(), "BlockInfo::parentBeaconBlockRoot"));
    blk.extra_data = evmc::bytes(env.extraData().begin(), env.extraData().end());
    blk.blob_gas_used = narrowU256ToU64(
        lenientOptionals ? env.blobGasUsed().value_or(bcos::u256{0}) :
                           requireHeaderField(env.blobGasUsed(), "BlockInfo::blobGasUsed"),
        "BlockInfo::blobGasUsed");
    return blk;
}
}  // namespace detail
}  // namespace bcos::evm::engine

namespace bcos::evm::opstack
{
/// Bounds-checked u256→int64 narrowing (a corrupt receipt must not wrap the gas pool). Throws
/// engine::OpConsensusError — a bare std::runtime_error would escape the INVALID/-32603
/// classification this header establishes (see requireHeaderField's comment). Defined after the
/// engine block because the error types live there.
[[nodiscard]] inline int64_t narrowGasUsed(const bcos::u256& gasUsed)
{
    static const bcos::u256 kMaxInt64(std::numeric_limits<int64_t>::max());
    if (gasUsed > kMaxInt64)
        throw engine::OpConsensusError("op block: receipt gasUsed exceeds int64_t range");
    return static_cast<int64_t>(gasUsed);
}
}  // namespace bcos::evm::opstack
