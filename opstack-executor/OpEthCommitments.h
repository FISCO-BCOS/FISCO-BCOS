/// @file OpEthCommitments.h
/// @brief The six-way commitment comparison surface on the bcos-evm-free OP
///        layer — the counterpart of the pre-cutover OpCommitments.h (commitmentsOf /
///        mismatchedFieldOf, since renamed into this file),
///        OpBlockExecute.h (announcedCommitmentsOf /
///        computeOpTxRoot) and OpSchedulerSeam.h's isJovianActive /
///        isKarstActive, restated over OpEthBlockSeal (framework types; no
///        evmone BloomFilter/hash256).
///
/// Built from the new-layer primitives: the seal comes from sealOpEthBlock
/// (OpEthBlockExecute.h), the fork predicates from opForkSpecAt
/// (OpForkSpec.h). Signatures mirror OpSchedulerSeam's shape so the 3.2/3.4
/// seam switch is a near-mechanical swap.
///
/// Fork-field presence matrix (all three fork-gated fields compare presence
/// AND value):
///   withdrawalsRoot: nullopt pre-Canyon; empty-trie root Canyon–Holocene;
///                    MessagePasser storage root Isthmus+.
///   blobGasUsed:     nullopt pre-Ecotone; 0 Ecotone–Isthmus; DA footprint
///                    Jovian+.
///   requestsHash:    nullopt pre-Isthmus; sha256("") Isthmus+.

#pragma once

#include <bcos-framework/engine/Types.h>  // bcos::engine::ExecutionPayload
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-ledger/mpt/EthTrieRoots.h>  // calculateTransactionsRoot
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpEthBlockExecute.h>  // OpEthBlockSeal
#include <opstack-executor/OpForkSpec.h>         // opForkSpecAt / opForkTimestampSec
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bcos::executor_v1::opstack
{
/// The block-execution commitments the engine's newPayload OP branch compares
/// against the payload, restated in bcos:: types: the six-way comparison
/// surface (receiptsRoot/logsBloom/withdrawalsRoot from the seal +
/// stateRoot/gasUsed/txRoot) plus two seal-only outputs (blobGasUsed, engaged
/// Ecotone on — 0 through Isthmus, DA footprint from Jovian; requestsHash,
/// engaged Isthmus+). Same field set as the legacy
/// bcos::evm::engine::OpBlockCommitments.
struct OpEthBlockCommitments
{
    bcos::h256 receiptsRoot;
    bcos::h2048 logsBloom;
    std::optional<bcos::h256> withdrawalsRoot;
    bcos::h256 stateRoot;
    bcos::u256 gasUsed;
    bcos::h256 txRoot;
    std::optional<uint64_t> blobGasUsed;
    std::optional<bcos::h256> requestsHash;
};

/// Project an executed block's seal + the three standalone result members
/// into OpEthBlockCommitments.
[[nodiscard]] OpEthBlockCommitments opEthCommitmentsOf(const OpEthBlockSeal& seal,
    const bcos::h256& stateRoot, uint64_t gasUsed, const bcos::h256& txRoot);

/// Announced-side projection: the payload/header announced commitments as
/// OpEthBlockCommitments (the "announced" side of opEthMismatchedFieldOf).
/// Throws bcos::evm::OpConsensusError when an Isthmus-invariant field is
/// missing from the payload (a clean consensus-level rejection naming the
/// field, never bad_optional_access).
[[nodiscard]] OpEthBlockCommitments announcedOpEthCommitmentsOf(
    const bcos::engine::ExecutionPayload& payload, const bcos::h256& transactionsRoot,
    const bcos::protocol::BlockHeader& ethHeader);

/// First mismatching field name (txRoot slot reports "transactionsRoot"), or
/// nullopt. The three fork-gated fields compare presence AND value
/// bidirectionally (optional != optional): an announced-only field (peer
/// ahead of the local fork config) is rejected just as a computed-only one is
/// — op-geth's engine API rejects fork-field asymmetry in both directions.
[[nodiscard]] std::optional<std::string> opEthMismatchedFieldOf(
    const OpEthBlockCommitments& computed, const OpEthBlockCommitments& announced);

/// Executed-block result on the new layer — the counterpart of OpCommon.h's
/// bcos::evm::engine::OpExecuteBlockResult with the evmone-typed seal replaced
/// by OpEthBlockSeal (framework types; the projections above consume it
/// directly, no toBcosH256/toBcosBloom conversion step).
struct OpEthExecuteBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    OpEthBlockSeal seal;
    bcos::h256 stateRoot;
    uint64_t gasUsed = 0;
    bcos::h256 txRoot;
};

/// Jovian semantics or later for a block whose internal (millisecond)
/// timestamp is @p internalTimestampMs — blobGasUsed is the DA footprint, the
/// operator fee uses the ×100 formula and extraData is the 17-byte Jovian
/// shape; Isthmus keeps blobGasUsed 0. Derived from the fork the schedule
/// resolves (Karst is a superset of Jovian, and OpFork is declared in fork
/// order, so `>= Jovian` is the predicate). The CALLER picks which block's
/// timestamp to pass: op-geth keys base fee on the parent, op-node keys the
/// L1-attributes layout and the payload attributes on the child.
[[nodiscard]] inline bool isOpEthJovianActive(
    const OpForkSchedule& schedule, int64_t internalTimestampMs) noexcept
{
    return bcos::ledger::resolveOpFork(schedule, opForkTimestampSec(internalTimestampMs)) >=
           OpFork::Jovian;
}

/// Karst semantics for a block whose internal (millisecond) timestamp is
/// @p internalTimestampMs: Jovian's fee and receipt rules on an Osaka EVM
/// base. Used by the engine's getPayload method-version gate (V5 is
/// Karst-only, V4 is pre-Karst).
[[nodiscard]] inline bool isOpEthKarstActive(
    const OpForkSchedule& schedule, int64_t internalTimestampMs) noexcept
{
    return bcos::ledger::resolveOpFork(schedule, opForkTimestampSec(internalTimestampMs)) >=
           OpFork::Karst;
}

/// transactionsRoot over raw EIP-2718 envelopes (trie key = rlp(index), value
/// = raw wire bytes) — the bcos-evm-free counterpart of OpBlockExecute.h's
/// computeOpTxRoot, sharing the framework helper (calculateTransactionsRoot)
/// with it.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpEthTransactionsRoot(RawTxRange const& rawTxBytes)
{
    std::vector<bcos::bytesConstRef> rawEnvelopes;
    rawEnvelopes.reserve(rawTxBytes.size());
    for (auto const& rawItem : rawTxBytes)
    {
        // .data()/.size() rather than bcos::ref: the range's element is `bytes` at one
        // call site and already a RefDataContainer at another, and bcos::ref would
        // double-wrap the latter.
        rawEnvelopes.emplace_back(rawItem.data(), rawItem.size());
    }
    return bcos::ledger::mpt::calculateTransactionsRoot(rawEnvelopes);
}
}  // namespace bcos::executor_v1::opstack
