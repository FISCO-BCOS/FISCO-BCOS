// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Shared error types + the block-execution result for the OP scheduler. Split out of
// OpSchedulerImpl.h so the decode layers (OpRlpDecode.h) can throw without depending on the class
// template — and so neither side needs an include-order guarantee. OpBlockSeal lives here too
// (not in OpBlockExecute.h): OpExecuteBlockResult carries it by value, and this header must stay
// independent of OpBlockExecute.h so the merged block-execution header can include OpErrors.h
// without a cycle.
//
// The six-way commitment comparison surface (OpBlockCommitments / commitmentsOf /
// payloadBloomToH2048 / mismatchedFieldOf / detail::toBcosH256 / toBcosBloom) lives in
// OpCommitments.h — this header is deliberately pure types.

#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
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
}  // namespace bcos::evm::engine
