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
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <limits>
#include <optional>
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
}  // namespace bcos::evm::opstack

namespace bcos::evm::engine
{
/// Thrown for anything OP block execution classifies as a consensus-level rejection (error
/// table): malformed/undecodable raw tx bytes, processOpBlock's own semantic throws
/// (empty block, first tx not the L1 attributes deposit, gas-pool overrun, ...). Maps to INVALID
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

/// Build the OP block context from a FISCO header. `gasLimitOverride` injects the head block's
/// gasLimit as blockGasLeft (a minimal test header may leave gasLimit==0); `lenientOptionals`
/// tolerates unset optional header fields as 0 (eth_call path), while block execution uses
/// `.value()` and throws on an unset field.
inline evmone::state::BlockInfo toBlockInfo(const bcos::protocol::BlockHeader& env,
    std::optional<uint64_t> gasLimitOverride = std::nullopt, bool lenientOptionals = false)
{
    evmone::state::BlockInfo blk;
    blk.number = static_cast<int64_t>(env.number());
    // FISCO tars store milliseconds; evmone wants seconds.
    blk.timestamp = static_cast<uint64_t>(env.timestamp()) / 1000;
    blk.gas_limit = gasLimitOverride.has_value() ?
                        static_cast<int64_t>(*gasLimitOverride) :
                        narrowU256ToU64(env.gasLimit(), "BlockInfo::gasLimit");
    blk.base_fee = narrowU256ToU64(
        lenientOptionals ? env.baseFee().value_or(bcos::u256{0}) : env.baseFee().value(),
        "BlockInfo::baseFee");
    blk.coinbase = toEvmcAddress(env.coinbase());
    blk.prev_randao = toEvmcBytes32(env.prevRandao());
    blk.parent_beacon_block_root =
        toEvmcBytes32(lenientOptionals ? env.parentBeaconBlockRoot().value_or(bcos::h256{}) :
                                         env.parentBeaconBlockRoot().value());
    blk.extra_data = evmc::bytes(env.extraData().begin(), env.extraData().end());
    blk.blob_gas_used = narrowU256ToU64(
        lenientOptionals ? env.blobGasUsed().value_or(bcos::u256{0}) : env.blobGasUsed().value(),
        "BlockInfo::blobGasUsed");
    return blk;
}
}  // namespace detail
}  // namespace bcos::evm::engine
