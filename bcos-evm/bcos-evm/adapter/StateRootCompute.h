// bcos-evm-ref/bcos-evm-ref/adapter/StateRootCompute.h
#pragma once

// TODO(eth-utils-removal): C 路线——建根引擎从 evmone mpt_hash(eth/utils)替换为自研 MPT;
// 形参 TestState 同步替换为自研内存账本(实现 StateView + apply_diff 契约)。
// 字节等价判定:33 向量 gate 的 stateRoot 单腿比对族必须保持全绿。
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/utils/mpt.hpp>
#include <bcos-evm/eth/utils/rlp.hpp>
#include <evmc/evmc.hpp>
#include <map>

namespace evmone::test
{
class TestState;
}

namespace bcos::evm
{
/// Full-state MPT root (correctness version: rebuilds the entire root every time; scalable
/// incremental building is TA-1d, a non-goal here).
/// Engine = evmone mpt_hash(TestState) — the same-source implementation t8n uses to compute
/// stateRoot (secure-trie account tree + per-account storage trie); this function is zero
/// custom logic, just wiring.
/// Empty state → EMPTY_MPT_HASH.
///
/// Timing contract (the block-header production call sequence, anchored to the same point as
/// OpBlockSeal.h's messagePasserStorage snapshot, aligned with op-geth consensus.go:416-427
/// IntermediateRoot ordering):
///   processOpBlock → applyDiff per-transaction write-back → block-tail finalize → this function
///   → sealOpBlock.
/// Anti-circularity invariant (OP spec Isthmus exec-engine): during execution this block's
/// stateRoot does not yet exist and is structurally impossible to expose to the EVM — a
/// documented invariant, no runtime defense.
///
/// [[deprecated]] (真账本桥 Task 5, design §6): superseded by the generic
/// `stateRootOf<Ledger>(const Ledger&)` template below, which works against any ledger backend
/// exposing `visitAccounts` (MemoryLedger, Storage2Ledger — and this TestState overload's own
/// engine is what that template's per-account leaf construction is aligned to, vendored
/// mpt_hash.cpp:27-36). Retained, not removed: it is still the TestState replay leg's engine
/// (spec §7 "三腿回放" — TestState gate "既有,不动"), so removing it would perturb a leg this
/// task must not touch. The sole remaining call site (T8nReplayHarness.h's TestStateBackend)
/// locally suppresses -Wdeprecated-declarations around its one call rather than migrating —
/// migrating would mean adding a visitAccounts-shaped view over evmone::test::TestState for no
/// behavioral gain, since the TestState leg's whole point is exercising this exact engine
/// unmodified.
[[deprecated(
    "use the generic stateRootOf<Ledger>(const Ledger&) template (design §6); retained only for "
    "the TestState replay leg's engine, see StateRootCompute.h")]] [[nodiscard]] evmone::hash256
stateRootOf(const evmone::test::TestState& state);

/// Per-account storage-trie root (design §6): secure-trie over one account's live slot map, key
/// = keccak256(slot), value = rlp(trim(value)) — the exact same construction as
/// bcos-evm/opstack/OpBlockSeal.cpp::opStorageRoot (itself aligned with the private helper in
/// vendored mpt_hash.cpp:13-24). The logic is duplicated here rather than called through
/// (`stateRootOf<Ledger>` reusing `bcos::evm::opstack::opStorageRoot` directly) for two reasons:
/// (1) layering — bcos-evm-opstack links bcos-evm-eth, not the reverse, so this adapter/ (eth
/// layer) header calling into opstack/ would be a backward dependency; (2) opStorageRoot's exact
/// line range in OpBlockSeal.cpp is tracked by the op_storage_root entry in
/// scripts/upstream-diff/manifest.tsv — turning it into a delegating wrapper would perturb that
/// golden without a genuine upstream-diff reason. Design §6's "可复用/提炼共用" is satisfied by
/// reusing the *logic* (verified identical construction), not the symbol.
[[nodiscard]] evmone::hash256 accountStorageRoot(
    const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Generic full-state MPT root (design §6): builds a secure-trie over any Ledger exposing
/// `bool visitAccounts(Visitor) const` (the AccountVisitor contract shared by MemoryLedger and
/// Storage2Ledger — payload nonce/balance/codeHash + a `.storage` slot map + a lazy `.code()`
/// getter, `.addr` for the trie key). Account key = keccak256(addr), leaf =
/// rlp(nonce, balance, storageRoot, codeHash) — field-for-field aligned with vendored
/// mpt_hash.cpp:27-36. Correctness-first (rebuilds the whole trie on every call; incremental
/// building is TA-1d, a non-goal, design §6 "性能边界"). The lazy code() getter is never invoked
/// here — state-root computation needs codeHash only, not code bytes (avoids an unconditional
/// SYS_CODE_BINARY read per account on the Storage2Ledger backend).
///
/// Poison contract (design §6/§4.3): this function does not itself inspect `ledger.poisoned()`
/// — a poisoned traversal's product is defined to be entirely void ("遍历产物全部作废"), and it
/// is the *caller's* responsibility (the block-seal driver) to check poisoned() after calling
/// this and discard the result wholesale if set, exactly as with every other Storage2Ledger read
/// method. MemoryLedger's poisoned() is always false (design §6: "抽象层统一提供该查询,后端不
/// 对称由此收敛"), so this contract is a no-op for that backend.
template <class Ledger>
[[nodiscard]] evmone::hash256 stateRootOf(const Ledger& ledger)
{
    evmone::state::MPT trie;
    ledger.visitAccounts([&](const auto& account) {
        trie.insert(evmone::keccak256(account.addr),
            evmone::rlp::encode_tuple(account.nonce, account.balance,
                accountStorageRoot(account.storage), account.codeHash));
        return true;
    });
    return trie.hash();
}
}  // namespace bcos::evm
