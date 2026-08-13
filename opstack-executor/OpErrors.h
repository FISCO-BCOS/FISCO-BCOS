// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared error types + the block-execution result for the OP scheduler. Split out of
// OpSchedulerImpl.h so the decode layers (OpRlpDecode.h / OpTxDecode.h) can throw without
// depending on the class template — and so neither side needs an include-order guarantee.
// OpBlockSeal lives here too (not in OpBlockExecute.h): OpExecuteBlockResult carries it by
// value, and this header must stay independent of OpBlockExecute.h so the merged block-execution
// header can include OpErrors.h without a cycle.

#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <array>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
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
    /// Jovian block-header BlobGasUsed reuse slot = DA footprint (CalcDAFootprint,
    /// rollup_cost.go:563-591: only non-deposit txs accumulate, each tx = EstimatedDASize ×
    /// scalar). This implementation = Σ of the meta.da_footprint over non-deposit receipts; a
    /// deposits-only block sums no terms and is always 0 ≡ op-geth's first-Jovian-block special
    /// case (the equivalence reason being that there are no terms). op-geth's validation-side
    /// "reject footprint > gasLimit" (block_validator.go:131) is a validation responsibility; this
    /// function only produces the value. When has_da_footprint is false there is always no value.
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
/// evmc::bytes32 -> bcos::h256.
inline bcos::h256 toBcosH256(const evmc::bytes32& hash)
{
    return bcos::h256(reinterpret_cast<const bcos::byte*>(hash.bytes), sizeof(hash.bytes));
}

/// evmone::state::BloomFilter (256 raw bytes) -> bcos::h2048.
inline bcos::h2048 toBcosBloom(const evmone::state::BloomFilter& bloom)
{
    return bcos::h2048(reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes));
}
}  // namespace detail

/// The block-execution commitments the engine's newPayload OP branch compares against the
/// payload, restated in bcos:: types: the six-way comparison surface (receiptsRoot/logsBloom/
/// withdrawalsRoot from OpBlockSeal + stateRoot/gasUsed/txRoot) plus two seal-only outputs
/// (blobGasUsed, engaged from Jovian on; requestsHash, engaged Isthmus+).
struct OpBlockCommitments
{
    bcos::h256 receiptsRoot;
    bcos::h2048 logsBloom;
    bcos::h256 withdrawalsRoot;
    bcos::h256 stateRoot;
    bcos::u256 gasUsed;
    bcos::h256 txRoot;
    std::optional<uint64_t> blobGasUsed;
    std::optional<bcos::h256> requestsHash;
};

/// Project an executed block's seal + the three standalone result members into OpBlockCommitments.
inline OpBlockCommitments commitmentsOf(const bcos::evm::opstack::OpBlockSeal& seal,
    const bcos::h256& stateRoot, uint64_t gasUsed, const bcos::h256& txRoot)
{
    OpBlockCommitments out{
        .receiptsRoot = detail::toBcosH256(seal.receiptsRoot),
        .logsBloom = detail::toBcosBloom(seal.logsBloom),
        .withdrawalsRoot = detail::toBcosH256(seal.withdrawalsRoot),
        .stateRoot = stateRoot,
        .gasUsed = bcos::u256(gasUsed),
        .txRoot = txRoot,
        .blobGasUsed = seal.blobGasUsed,
        .requestsHash = std::nullopt,
    };
    if (seal.requestsHash.has_value())
    {
        out.requestsHash = detail::toBcosH256(*seal.requestsHash);
    }
    return out;
}

/// Payload bloom (std::array<byte,256>) → bcos::h2048, byte-faithful.
inline bcos::h2048 payloadBloomToH2048(const std::array<bcos::byte, 256>& bloom)
{
    bcos::h2048 out;
    std::memcpy(out.data(), bloom.data(), bloom.size());
    return out;
}

/// Compare the executed block's commitments against the payload's announced commitments; returns
/// the first mismatching field name (txRoot slot reports "transactionsRoot"), or nullopt. blobGas
/// Used / requestsHash are compared only when the computed side has a value.
inline std::optional<std::string> mismatchedFieldOf(
    const OpBlockCommitments& computed, const OpBlockCommitments& announced)
{
    if (computed.receiptsRoot != announced.receiptsRoot)
        return "receiptsRoot";
    if (computed.logsBloom != announced.logsBloom)
        return "logsBloom";
    if (computed.withdrawalsRoot != announced.withdrawalsRoot)
        return "withdrawalsRoot";
    if (computed.stateRoot != announced.stateRoot)
        return "stateRoot";
    if (computed.gasUsed != announced.gasUsed)
        return "gasUsed";
    if (computed.txRoot != announced.txRoot)
        return "transactionsRoot";
    if (computed.blobGasUsed.has_value() && *computed.blobGasUsed != *announced.blobGasUsed)
        return "blobGasUsed";
    if (computed.requestsHash.has_value() &&
        *computed.requestsHash != announced.requestsHash.value())
        return "requestsHash";
    return std::nullopt;
}
}  // namespace bcos::evm::engine
