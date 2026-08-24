// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h
#pragma once

// Root building via FISCO bcos-ledger/mpt computeTrieRoot (retired evmone mpt_hash) —
// byte-identical for the same key set. Anchored by StateRootComputeTest's golden vectors: the
// empty root is the canonical Ethereum emptyRootHash (keccak256(RLP(""))), and the single-
// account leaf is a regression anchor frozen from this implementation's output with a
// field-level cross-check against evmone's mpt_hash — the op-geth value-level differential
// gate arrives with the part-3 block-seal PR (33 vectors).
// Stateless helper — no per-block mutable state; safe to call from any execution context.
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-ledger/mpt/HashBuilder.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <cstring>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <map>
#include <stdexcept>
#include <unordered_map>

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

/// Per-account storage-trie BUILD (root + newNodes): secure trie over one account's live slot
/// map, key = keccak256(slot), value = rlp(trim(value)). Same construction as
/// opstack/OpBlockSeal.cpp::opStorageRoot; duplicated here rather than delegated to avoid a
/// backward eth→opstack dependency and to keep opStorageRoot's upstream-diff manifest entry
/// untouched. Exposes computeTrieRoot's newNodes so the OP finalize path can persist the
/// storage sub-trie's nodes (①b historical eth_call: without them the account trie's
/// storageRoot references dangle).
[[nodiscard]] bcos::ledger::mpt::TrieBuildResult accountStorageTrie(
    const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Per-account storage-trie root: the root half of accountStorageTrie (nodes discarded).
/// Note (round-2 NIT): production trie-node persistence no longer flows through
/// stateRootOfWithNodes — block heights persist via the ①a incremental buildAndCollect
/// (OpScheduler execute ⑤) and genesis via Ledger::buildGenesisBlock's l2EthereumCompat import;
/// the collecting variant below now serves tests and genesis tooling.
[[nodiscard]] evmone::hash256 accountStorageRoot(
    const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// stateRootOfWithNodes result: the root plus EVERY hash-keyed node the two-layer build
/// produced (each account's storage sub-trie + the outer account trie), merged into one
/// content-addressed map — duplicate keys are identical RLP by construction, ready for
/// bcos::ledger::mpt::flushTrieNodes.
struct StateRootResult
{
    evmone::hash256 root{};
    std::unordered_map<bcos::h256, bcos::bytes> newNodes;
};

/// Full-state MPT root over any Ledger exposing `visitAccounts` (the AccountVisitor contract shared
/// by MemoryState and Storage2State), PLUS every trie node produced along the way. Account key =
/// keccak256(addr), leaf = rlp(nonce, balance, storageRoot, codeHash) — field-for-field aligned
/// with the vendored mpt_hash.cpp:27-36. Rebuilds the whole trie per call (correctness-first). The
/// lazy code() getter is never invoked — state-root computation needs codeHash only. Empty state →
/// keccak256(RLP("")), newNodes empty.
///
/// The merged newNodes make the computed root resolvable for historical reads: flush them as
/// "/mpt/" state rows (ViewNodeStorage + flushTrieNodes) and HistoricalStateBackend can walk the
/// trie from this root. Same content-addressed format BaselineScheduler persists.
///
/// Poison contract: does not inspect ledger.poisoned(); a mid-traversal failure surfaces as an
/// exception instead — Storage2State poisons and returns false, which stateRootOf turns into an
/// explicit std::runtime_error (a partially-built trie must never be returned). The poison flag
/// stays set, so the caller (block-seal driver) can still classify via poisoned() (-32603) or
/// the exception type. A no-op for MemoryState (its poisoned() is always false). The visitor
/// must not throw: MemoryState::visitAccounts is noexcept and an exception would escape as
/// std::terminate.
///
/// Exception-binding caveat (wedprcrypto issue, Storage2State.h's catch-ladder comment): this
/// throw is runtime_error-family, which in affected binaries may escape a plain
/// catch(const std::exception&). The part-3 driver must classify on poisoned() with catch(...)
/// as the backstop — not on the exception type alone.
///
/// @p collectNodes=false skips RETAINING the nodes (per-account sub-trie nodes are dropped as each
/// account finishes, the outer trie's build result is discarded) — the root construction is
/// byte-identical, only the peak-memory footprint differs. Use it for root-only callers (the
/// verify=false probe / non-scenario-B paths discard the nodes anyway). collectNodes=true now
/// serves only tests and genesis tooling (round-2 NIT: production persistence moved to the ①a
/// incremental buildAndCollect + Ledger::buildGenesisBlock's genesis import).
template <class Ledger>
[[nodiscard]] StateRootResult stateRootOfWithNodes(const Ledger& ledger, bool collectNodes = true)
{
    std::map<bcos::h256, bcos::bytes> entries;
    StateRootResult out;
    // The visitor never aborts (always returns true), so false can only mean the traversal
    // failed mid-way — fail the root at that point instead of computing a partial state root
    // (the poison flag stays set, so the caller can still classify via poisoned()).
    if (!ledger.visitAccounts([&](const auto& account) {
            // Secure-trie leaf: rlp(nonce, balance-be-trimmed, storageRoot, codeHash); balance is
            // intx big-endian 32 bytes trimmed of leading zeros (evmone rlp::encode(intx)
            // semantics).
            evmone::hash256 storageRoot;
            if (collectNodes)
            {
                auto storageTrie = accountStorageTrie(account.storage);
                // Content-addressed: a duplicate key is the identical node RLP, so merge() dropping
                // it loses nothing.
                out.newNodes.merge(std::move(storageTrie.newNodes));
                std::memcpy(storageRoot.bytes, storageTrie.root.data(), sizeof(storageRoot.bytes));
            }
            else
            {
                storageRoot = accountStorageRoot(account.storage);  // nodes dropped per account
            }
            auto const balanceBe = intx::be::store<evmc::uint256be>(account.balance);
            bcos::bytes leaf;
            bcos::codec::rlp::encode(leaf, account.nonce,
                trimmedBigEndian(bcos::bytesConstRef{balanceBe.bytes, sizeof(balanceBe.bytes)}),
                bcos::bytesConstRef{storageRoot.bytes, sizeof(storageRoot)},
                bcos::bytesConstRef{account.codeHash.bytes, sizeof(evmc::bytes32)});
            entries[bcos::h256{evmone::keccak256(account.addr).bytes, 32}] = std::move(leaf);
            return true;
        }))
    {
        throw std::runtime_error("stateRootOf: account traversal incomplete");
    }
    auto result = bcos::ledger::mpt::computeTrieRoot(entries);
    if (collectNodes)
        out.newNodes.merge(std::move(result.newNodes));
    std::memcpy(out.root.bytes, result.root.data(), sizeof(out.root.bytes));
    return out;
}

/// Full-state MPT root: root-only delegate of stateRootOfWithNodes (nodes never retained).
/// The 33-vector stateRoot comparison gate therefore exercises the exact same root
/// construction as the node-collecting variant — only node retention differs.
template <class Ledger>
[[nodiscard]] evmone::hash256 stateRootOf(const Ledger& ledger)
{
    return stateRootOfWithNodes(ledger, /*collectNodes=*/false).root;
}
}  // namespace bcos::evm
