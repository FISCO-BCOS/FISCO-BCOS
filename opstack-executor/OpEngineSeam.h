// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpEngineSeam.h — the engine-facing publication surface of the OP seam.
//
// `engine/bcos-engine` must not gain a dependency on `bcos-evm` (library purity: its CMake target
// links only bcos-framework/bcos-task/bcos-utilities/ledger), so the newPayload OP branch reaches
// every OP-specific thing it needs (block-env struct, error taxonomy, tx-root derivation, header
// table name, comparison surface) as a **dependent name on the SchedulerType template parameter**,
// re-published by `OpSchedulerImpl`. This header holds the actual definitions (in bcos:: types);
// it deliberately does NOT include `OpSchedulerImpl.h` (that would be circular).

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <opstack-executor/OpBlockSeal.h>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm::engine
{

/// (REMOVED 2026-08-05) `SYS_ETH_BLOCK_HEADER`/"s_eth_block_header" was retired: OP headers now
/// land in the standard `bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER` ("s_number_2_header") as tars
/// `protocol::BlockHeader`.

/// Table holding each accepted OP block's transactions as their **raw EIP-2718 envelopes**, keyed
/// by `keccak(envelope)` — the Ethereum transaction hash, the same key `SYS_HASH_2_RECEIPT` uses,
/// so a transaction and its receipt are retrievable under one key. OP-only table; its name lives
/// here, not in bcos-framework's LedgerTypeDef.h.
///
/// Deliberately NOT the generic `SYS_HASH_2_TX`: that table holds tars-encoded
/// `bcos::protocol::Transaction` objects whose readers hand bytes straight to the transaction
/// factory — an Ethereum envelope would decode into an all-default, plausible-looking transaction
/// whose hash does not match the key it was stored under, reaching `eth_getTransactionByHash`
/// responses and txpool consensus-proposal verification. Mapping to a real `protocol::Transaction`
/// is also unavailable (type 0x04/0x7E rejected, no tars slots for sourceHash/mint/
/// authorizationList, and `Transaction::verify` would ecrecover a signature-less deposit into a
/// fabricated sender). Storing the faithful bytes under an OP-specific name keeps every option
/// open.
inline constexpr std::string_view SYS_ETH_HASH_2_RAWTX{"s_eth_hash_2_rawtx"};

namespace detail
{
/// evmc::bytes32 -> bcos::h256 (the `FixedBytes(byte const*, size_t)` constructor, same
/// conversion `Storage2State.h`'s `applyModifiedEntry` uses for codeHash). Defined here rather
/// than in `OpSchedulerImpl.h` (which includes this header and uses this definition) so there is
/// exactly one definition of this name in `bcos::evm::engine::detail`.
inline bcos::h256 toBcosH256(const evmc::bytes32& hash)
{
    return bcos::h256(reinterpret_cast<const bcos::byte*>(hash.bytes), sizeof(hash.bytes));
}

/// evmone::state::BloomFilter (256 raw bytes) -> bcos::h2048, the type
/// `bcos::codec::rlp::EthBlockHeader::logsBloom` is declared with (Task 3).
inline bcos::h2048 toBcosBloom(const evmone::state::BloomFilter& bloom)
{
    return bcos::h2048(reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes));
}
}  // namespace detail

/// The block-execution commitments the engine's newPayload OP branch compares against the
/// payload, restated in bcos:: types.
///
/// Two roles in one struct:
///   - the **six-way comparison surface** proper (exactly six, no more):
///     `receiptsRoot`/`logsBloom`/`withdrawalsRoot` (from `OpBlockSeal`) +
///     `stateRoot`/`gasUsed`/`txRoot` (from `OpExecuteBlockResult`'s own three members);
///   - two further seal outputs assigned to seal comparison and therefore NOT counted among the
///     six: `blobGasUsed` (engaged from Jovian on, where the header slot is repurposed as the DA
///     footprint — carried by seal comparison) and `requestsHash` (engaged Isthmus+; the engine
///     reconstructs the header with its own `OP_EMPTY_REQUESTS_HASH` copy of this protocol
///     constant and comparing catches any drift between the two copies).
///
/// Type choices mirror what the engine holds on the other side of each comparison:
/// `ExecutionPayload::gasUsed` is `u256`, `logsBloom` becomes `h2048` (the `EthBlockHeader` field
/// type), everything else is `h256`.
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

/// Projects an executed block's seal + the three standalone result members into
/// `OpBlockCommitments`. Takes the pieces (not `OpExecuteBlockResult`) so this header stays
/// independent of `OpSchedulerImpl.h`; that class supplies the field projection.
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

/// Compares the executed block's commitments against the payload's announced commitments.
/// Returns the mismatching field name (first in comparison order), or nullopt if all match.
/// Contract (verbatim port of the engine's comparison block, zero judgment change):
///   - fields compared in order receiptsRoot → logsBloom → withdrawalsRoot → stateRoot →
///     gasUsed → txRoot → blobGasUsed → requestsHash; first mismatch wins;
///   - the txRoot slot reports the literal "transactionsRoot" (not "txRoot");
///   - blobGasUsed / requestsHash are compared only when the COMPUTED side has a value, and the
///     announced side is dereferenced (guaranteed present by the engine validation — see design
///     doc §4); computed-side nullopt skips regardless of announced.
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
    if (computed.requestsHash.has_value() && *computed.requestsHash != announced.requestsHash.value())
        return "requestsHash";
    return std::nullopt;
}

/// transactionsRoot over the block's raw EIP-2718 envelopes: trie key = canonical RLP of the
/// index, trie value = **the raw wire bytes as received**.
///
/// NOTE: this is NOT op-geth's `DeriveSha` convention — `types.DeriveSha` RE-ENCODES each
/// transaction canonically from the parsed struct, while this hashes the bytes as they arrived.
/// The two agree for canonical input and disagree for non-canonical input (two different block
/// hashes for one payload); they coincide only because `OpSchedulerImpl`'s raw-tx decoders reject
/// every non-canonical encoding Go's `rlp` rejects — per-field strictness, the shared length-prefix
/// fix, and the `assertCanonicalRoundTrip` whole-envelope backstop. If that decoder strictness is
/// ever relaxed, this equivalence lapses with it, which is why the round-trip invariant exists:
/// to fail closed on a future change.
///
/// Factored out of `OpSchedulerImpl::executeOpBlock`'s step 6 because the engine needs the *same*
/// value **before** execution: `ExecutionPayload` carries no `transactionsRoot` field, so the
/// header reconstruction the blockHash check depends on (a static check that must precede
/// parentKnown/execution) has to derive it — exactly as op-geth's `ExecutableDataToBlock` does
/// before comparing block hashes. One function, two call sites: the six-way surface's `txRoot`
/// comparison then verifies that execution's own derivation did not diverge from this one.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    // txRoot trie: key = rlp(index) (ascending, non-prefix-of-each-other), leaf = raw envelope
    // bytes as-is. Built with FISCO computeTrieRootVarKey (same construction as the retired
    // evmone list-trie mpt_hash.cpp:38-46).
    std::vector<std::pair<bcos::bytes, bcos::bytes>> entries;
    entries.reserve(rawTxBytes.size());
    uint64_t index = 0;
    for (auto const& rawItem : rawTxBytes)
    {
        bcos::bytes key;
        bcos::codec::rlp::encode(key, index);
        entries.emplace_back(std::move(key), bcos::bytes(std::begin(rawItem), std::end(rawItem)));
        ++index;
    }
    auto result = bcos::ledger::mpt::computeTrieRootVarKey(entries);
    return result.root;  // already bcos::h256 (computeTrieRootVarKey returns bcos::h256)
}

}  // namespace bcos::evm::engine
