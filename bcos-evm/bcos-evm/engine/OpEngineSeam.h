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

/// transactionsRoot over the block's raw EIP-2718 envelopes (op-geth `DeriveSha` convention:
/// trie key = canonical RLP of the index, trie value = the raw tx bytes as-is).
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
