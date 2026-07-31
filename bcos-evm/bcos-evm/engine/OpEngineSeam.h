// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpEngineSeam.h — the engine-facing publication surface of the OP seam (op-validator-minimal-loop
// design §6.1, task-5b).
//
// Why this header exists at all: `engine/bcos-engine` must not gain a dependency on `bcos-evm`
// (the "库纯净" constraint recorded at length on `EngineServiceImpl::c_opMode`, task-5a) — the
// `engine` CMake target links only bcos-framework/bcos-task/bcos-utilities/ledger, and adding
// bcos-evm would invert the layering and drag evmone into every engine consumer. Yet the
// newPayload OP branch legitimately needs OP-specific things: the block-env struct, the error
// taxonomy, the tx-root derivation, the header-registration table name, and a comparison surface
// expressed in bcos:: types rather than evmone::/evmc:: ones.
//
// The resolution (design §2 "组件形态" — SchedulerType *is* the OP seam): every one of those is
// reached from the engine as a **dependent name on SchedulerType** — an associated type alias, a
// static member function, or a static constexpr member re-published by `OpSchedulerImpl` (see the
// "engine-facing seam surface" block there). A dependent name needs no `#include` on the engine
// side; it is looked up only at instantiation, inside `if constexpr (c_opMode)`, in a translation
// unit that has already included `OpSchedulerImpl.h` itself. This header is where the actual
// definitions live so `OpSchedulerImpl.h` only re-publishes them.
//
// It deliberately does NOT include `OpSchedulerImpl.h` (that would be circular): the two things
// it computes take `OpBlockSeal`/raw-bytes ranges as parameters, not `OpExecuteBlockResult`;
// `OpSchedulerImpl` supplies the field-projection.

#include <bcos-evm/opstack/OpBlockSeal.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/utils/mpt.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <iterator>
#include <optional>
#include <string_view>

namespace bcos::evm::engine
{

/// Table holding the ETH/OP block header RLP written by the newPayload OP branch on VALID
/// (design §6.1 step 6 "块登记", 裁定 B5): the constant lives HERE, in `bcos-evm/bcos-evm/engine/`,
/// and explicitly NOT in `bcos-framework/.../LedgerTypeDef.h` — this is an OP-validator-mode-only
/// table, not part of the FISCO ledger schema every node ships.
///
/// Key/value encoding follows the two ledger tables it is written alongside (encoding copied from
/// the `BaselineScheduler.h:207-220` production precedent): key = block number as a *decimal
/// string* (identical to `SYS_NUMBER_2_HASH`'s key), value = `EthBlockHeader::encode()` — the
/// 21-field RLP whose keccak is the block hash, stored as raw bytes.
inline constexpr std::string_view SYS_ETH_BLOCK_HEADER{"s_eth_block_header"};

/// Table holding each accepted OP block's transactions as their **raw EIP-2718 envelopes**, keyed
/// by `keccak(envelope)` — i.e. the Ethereum transaction hash, and the same key
/// `SYS_HASH_2_RECEIPT` uses, so a transaction and its receipt are retrievable under one key
/// (final review batch 6, decision (B)). Same placement rule as above (裁定 B5): OP-only table,
/// its name lives here and not in bcos-framework's LedgerTypeDef.h.
///
/// **Why a separate table instead of the generic `SYS_HASH_2_TX`** — this is a deliberate refusal,
/// not an omission. `SYS_HASH_2_TX` holds *tars-encoded* `bcos::protocol::Transaction` objects, and
/// its readers (`Ledger.cpp:1440-1443` and friends) hand whatever bytes they find straight to
/// `TransactionFactoryImpl::createTransaction(..., checkSig=false, checkHash=false)`. Feeding an
/// Ethereum envelope into that path does not fail loudly: every field of `bcostars::Transaction`
/// is `optional` and tars' tag scanner swallows decode errors, so the value decodes into an
/// all-default object, which the factory then hands a freshly computed, self-consistent hash. The
/// consumer receives a **non-null, plausible-looking transaction whose hash does not match the key
/// it was stored under**, and nothing checks that — it would reach `eth_getTransactionByHash`
/// responses and txpool's `requestMissedTxs` (i.e. consensus proposal verification). Mapping the
/// envelope into a real `protocol::Transaction` instead is not available either: the only such
/// mapper rejects type `0x04`/`0x7E` outright, the tars IDL has nowhere to put
/// `sourceHash`/`mint`/`authorizationList`, and `Transaction::verify` would ecrecover a
/// **signature-less** deposit into a fabricated sender. Storing the faithful bytes under an
/// OP-specific name keeps every option open and invents nothing. Full argument: spec §6.4 (f).
inline constexpr std::string_view SYS_ETH_HASH_2_RAWTX{"s_eth_hash_2_rawtx"};

namespace detail
{
/// evmc::bytes32 -> bcos::h256 (the `FixedBytes(byte const*, size_t)` constructor, same
/// conversion `Storage2Ledger.h`'s `applyModifiedEntry` uses for codeHash). Defined here rather
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
/// payload, restated in bcos:: types (design §6.1 step 4 "六项比对面").
///
/// Two roles in one struct:
///   - the **six-way comparison surface** proper (design §4.1/§6.1, exactly six, no more):
///     `receiptsRoot`/`logsBloom`/`withdrawalsRoot` (from `OpBlockSeal`) +
///     `stateRoot`/`gasUsed`/`txRoot` (from `OpExecuteBlockResult`'s own three members);
///   - two further seal outputs that design **§5.1** (not §6.1) assigns to seal comparison and
///     which are therefore NOT counted among the six: `blobGasUsed` (engaged from Jovian on,
///     where the header slot is repurposed as the DA footprint — "由 seal 比对承接") and
///     `requestsHash` (engaged Isthmus+; the engine reconstructs the header with its own
///     `OP_EMPTY_REQUESTS_HASH` copy of this protocol constant and comparing catches any drift
///     between the two copies).
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

/// transactionsRoot over the block's raw EIP-2718 envelopes: trie key = canonical RLP of the
/// index, trie value = **the raw wire bytes as received**.
///
/// Relationship to op-geth (corrected by final review B4-2 — the earlier "op-geth `DeriveSha`
/// convention" phrasing was not accurate): `types.DeriveSha` builds its leaves from
/// `EncodeIndex`, which RE-ENCODES each transaction canonically from the parsed struct, whereas
/// this function hashes the bytes exactly as they arrived. The two agree for canonical input and
/// **disagree for non-canonical input** — the same payload would then yield two different
/// transactionsRoots and therefore two different block hashes.
///
/// What makes the two equivalent in practice is not this function but the decoder:
/// `OpSchedulerImpl`'s raw-tx decoders reject every non-canonical encoding Go's `rlp` rejects —
/// leading-zero scalars, over-wide integers, wrong-width address/hash fields, non-`0x01` booleans
/// (B4-2), a leading-zero long-form LENGTH PREFIX (C1, final review batch B, fixed in the shared
/// bcos-codec/rlp decoder), and — as a whole-envelope backstop — any residual non-canonicality
/// caught by `assertCanonicalRoundTrip` (decode → re-encode → byte-compare, run per transaction).
/// So no block containing a non-canonically encoded transaction survives step 1. Every input that
/// reaches this function is canonical, and on canonical input "hash the wire bytes" and "re-encode
/// then hash" are the same computation. If that decoder strictness is ever relaxed, this
/// equivalence lapses with it — which is exactly why the round-trip invariant exists: to fail
/// closed if a future change does.
///
/// Factored out of `OpSchedulerImpl::executeOpBlock`'s step 6 (which now calls this) because the
/// engine needs the *same* value **before** execution: `ExecutionPayload` carries no
/// `transactionsRoot` field, so the header reconstruction that the blockHash check depends on
/// (design §6.1 step 2, a *static* check that must precede parentKnown/execution) has to derive
/// it — exactly as op-geth's `ExecutableDataToBlock` derives it from the transaction list before
/// comparing block hashes. One function, two call sites: the six-way surface's `txRoot`
/// comparison then verifies that execution's own derivation did not diverge from this one.
template <class RawTxRange>
[[nodiscard]] bcos::h256 computeOpTxRoot(RawTxRange const& rawTxBytes)
{
    evmone::state::MPT txTrie;
    uint64_t index = 0;
    for (auto const& rawItem : rawTxBytes)
    {
        const auto key = evmone::rlp::encode(index);
        txTrie.insert(evmc::bytes_view{key.data(), key.size()},
            evmc::bytes(std::begin(rawItem), std::end(rawItem)));
        ++index;
    }
    return detail::toBcosH256(txTrie.hash());
}

}  // namespace bcos::evm::engine
