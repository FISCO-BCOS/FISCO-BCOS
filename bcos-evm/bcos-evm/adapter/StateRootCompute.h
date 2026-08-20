// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h
#pragma once

// Root building via FISCO bcos-ledger/mpt computeTrieRoot (retired evmone mpt_hash) —
// byte-identical for the same key set. Anchored by StateRootComputeTest's golden vectors
// (empty-root canonical value + single-account leaf, cross-verified against go-ethereum);
// the part-3 block-seal PR adds the 33-vector differential gate against op-geth state roots.
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <map>

namespace bcos::evm
{
// Trim leading zero bytes of a big-endian scalar (canonical RLP: no leading zeros, 0 → empty).
inline bcos::bytes trimmedBigEndian(bcos::bytesConstRef v)
{
    size_t first = 0;
    while (first < v.size() && v[first] == 0)
    {
        ++first;
    }
    return bcos::bytes(v.data() + first, v.data() + v.size());
}

/// Per-account storage-trie root: secure trie over one account's live slot map, key =
/// keccak256(slot), value = rlp(trim(value)). Same construction as
/// opstack/OpBlockSeal.cpp::opStorageRoot; duplicated here rather than delegated to avoid a
/// backward eth→opstack dependency and to keep opStorageRoot's upstream-diff manifest entry
/// untouched.
[[nodiscard]] evmone::hash256 accountStorageRoot(
    const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Full-state MPT root over any Ledger exposing `visitAccounts` (the AccountVisitor contract shared
/// by MemoryState and Storage2State). Account key = keccak256(addr), leaf = rlp(nonce, balance,
/// storageRoot, codeHash) — field-for-field aligned with the vendored mpt_hash.cpp:27-36. Rebuilds
/// the whole trie per call (correctness-first). The lazy code() getter is never invoked —
/// state-root computation needs codeHash only. Empty state → keccak256(RLP("")).
///
/// Poison contract: does not inspect ledger.poisoned(); the caller (block-seal driver) must check
/// poisoned() after this returns and discard the result wholesale if set. A no-op for MemoryState
/// (its poisoned() is always false).
template <class Ledger>
[[nodiscard]] evmone::hash256 stateRootOf(const Ledger& ledger)
{
    std::map<bcos::h256, bcos::bytes> entries;
    ledger.visitAccounts([&](const auto& account) {
        // Secure-trie leaf: rlp(nonce, balance-be-trimmed, storageRoot, codeHash); balance is intx
        // big-endian 32 bytes trimmed of leading zeros (evmone rlp::encode(intx) semantics).
        auto const balanceBe = intx::be::store<evmc::uint256be>(account.balance);
        bcos::bytes leaf;
        bcos::codec::rlp::encode(leaf, account.nonce,
            trimmedBigEndian(bcos::bytesConstRef{balanceBe.bytes, sizeof(balanceBe.bytes)}),
            bcos::bytesConstRef{
                accountStorageRoot(account.storage).bytes, sizeof(evmone::hash256::bytes)},
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
