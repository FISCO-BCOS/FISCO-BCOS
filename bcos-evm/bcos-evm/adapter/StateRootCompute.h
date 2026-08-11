// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h
#pragma once

// Root building uses FISCO bcos-ledger/mpt's computeTrieRoot (replacing the retired evmone
// mpt_hash/MPT). Byte-equivalence guarantee: the 33-vector gate's stateRoot comparison family
// must stay all-green (computeTrieRoot and evmone mpt_hash produce byte-identical roots for the
// same key set).
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <map>

namespace bcos::evm
{
// Trim leading zero bytes off a fixed-width scalar's big-endian form (op-geth rlp "nil" and
// evmone rlp::trim semantics: canonical RLP has no leading zeros, 0 → empty).
inline bcos::bytes trimmedBigEndian(bcos::bytesConstRef v)
{
    size_t first = 0;
    while (first < v.size() && v[first] == 0)
    {
        ++first;
    }
    return bcos::bytes(v.data() + first, v.data() + v.size());
}

/// Full-state MPT root (correctness version: rebuilds the entire root every time; scalable
/// incremental building is a non-goal here).
/// Engine = FISCO bcos-ledger/mpt computeTrieRoot — builds a secure-trie over any Ledger exposing
/// `visitAccounts`, the same construction the retired evmone mpt_hash used (account tree +
/// per-account storage trie); byte-identical root.
/// Empty state → emptyRootHash() (== keccak256(RLP(""))).
///
/// Timing contract (the block-header production call sequence, anchored to the same point as
/// OpBlockSeal.h's messagePasserStorage snapshot, aligned with op-geth consensus.go:416-427
/// IntermediateRoot ordering):
///   processOpBlock → applyDiff per-transaction write-back → block-tail finalize → this function
///   → sealOpBlock.
/// Anti-circularity invariant: during execution this block's stateRoot does not yet exist and is
/// structurally impossible to expose to the EVM — a documented invariant, no runtime defense.

/// Per-account storage-trie root: secure-trie over one account's live slot map, key
/// = keccak256(slot), value = rlp(trim(value)) — the exact same construction as
/// bcos-evm/opstack/OpBlockSeal.cpp::opStorageRoot (itself aligned with the private helper in
/// vendored mpt_hash.cpp:13-24). The logic is duplicated here rather than called through
/// (`stateRootOf<Ledger>` reusing `bcos::evm::opstack::opStorageRoot` directly) for two reasons:
/// (1) layering — bcos-evm-opstack links bcos-evm-eth, not the reverse, so this adapter/ (eth
/// layer) header calling into opstack/ would be a backward dependency; (2) opStorageRoot's exact
/// line range in OpBlockSeal.cpp is tracked by the op_storage_root entry in
/// scripts/upstream-diff/manifest.tsv — turning it into a delegating wrapper would perturb that
/// golden without a genuine upstream-diff reason. Reusability is satisfied by reusing the *logic*
/// (verified identical construction), not the symbol.
[[nodiscard]] evmone::hash256 accountStorageRoot(
    const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Generic full-state MPT root: builds a secure-trie over any Ledger exposing
/// `bool visitAccounts(Visitor) const` (the AccountVisitor contract shared by MemoryLedger and
/// Storage2Ledger — payload nonce/balance/codeHash + a `.storage` slot map + a lazy `.code()`
/// getter, `.addr` for the trie key). Account key = keccak256(addr), leaf =
/// rlp(nonce, balance, storageRoot, codeHash) — field-for-field aligned with vendored
/// mpt_hash.cpp:27-36. Correctness-first (rebuilds the whole trie on every call; incremental
/// building is a non-goal). The lazy code() getter is never invoked here — state-root computation
/// needs codeHash only, not code bytes (avoids an unconditional SYS_CODE_BINARY read per account
/// on the Storage2Ledger backend).
///
/// Poison contract: this function does not itself inspect `ledger.poisoned()` — a poisoned
/// traversal's product is defined to be entirely void, and it is the *caller's* responsibility
/// (the block-seal driver) to check poisoned() after calling this and discard the result
/// wholesale if set, exactly as with every other Storage2Ledger read method. MemoryLedger's
/// poisoned() is always false (the abstraction uniformly provides the query; backend asymmetry
/// converges here), so this contract is a no-op for that backend.
template <class Ledger>
[[nodiscard]] evmone::hash256 stateRootOf(const Ledger& ledger)
{
    std::map<bcos::h256, bcos::bytes> entries;
    ledger.visitAccounts([&](const auto& account) {
        // Account key = keccak256(addr) (secure trie); leaf = rlp(nonce, balance, storageRoot,
        // codeHash) — same construction as the retired evmone mpt_hash.cpp:27-36, now built with
        // bcos-ledger/mpt's computeTrieRoot (byte-identical root). `balance` is intx::uint256:
        // encode its big-endian 32-byte form trimmed of leading zeros (evmone rlp::encode(intx)
        // semantics).
        auto const balanceBe = intx::be::store<evmc::uint256be>(account.balance);
        bcos::bytes leaf;
        bcos::codec::rlp::encode(leaf, account.nonce,
            trimmedBigEndian(bcos::bytesConstRef{balanceBe.bytes, sizeof(balanceBe.bytes)}),
            bcos::bytesConstRef{accountStorageRoot(account.storage).bytes,
                sizeof(evmone::hash256::bytes)},
            bcos::bytesConstRef{account.codeHash.bytes, sizeof(evmc::bytes32)});
        entries[bcos::h256{evmone::keccak256(account.addr).bytes, 32}] = std::move(leaf);
        return true;
    });
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    evmone::hash256 root{};
    std::memcpy(root.bytes, result.root.data(), sizeof(root.bytes));
    return root;
}
}  // namespace bcos::evm
