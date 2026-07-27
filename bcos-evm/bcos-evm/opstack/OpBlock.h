#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpReceipt.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
// TODO(eth-utils-removal): 三根建根(state/tx/receiptsRoot)从 evmone mpt_hash 迁自研 MPT。
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <test/utils/mpt_hash.hpp>
#include <variant>
#include <vector>

namespace bcos::evm::opstack
{
using evmc::literals::operator""_bytes32;

// ---- block execution (formerly OpBlockExecute.h) ----

/// One transaction within a block: deposit or normal tx (a normal tx must carry a signed envelope
/// for L1 fee calculation).
struct OpBlockTx
{
    std::variant<DepositTx, evmone::state::Transaction> tx;
    evmc::bytes signedEnvelope;  // empty for deposit
};

/// Block execution result. receipts keep their original in-block order (M-B2's receipts-root /
/// block-level bloom depend on this order; cumulative_gas_used is already filled in interleaved
/// order).
struct OpBlockResult
{
    std::vector<std::variant<OpDepositReceipt, OpTxReceipt>> receipts;
    int64_t gasUsed = 0;                    // = last tx's cumulative
    evmone::state::StateDiff finalizeDiff;  // end-of-block finalize output (already delivered via
                                            // applyDiff)
};

/// Execute a whole block (spec §4.1 ordering): system_call_block_start → first L1 attributes
/// deposit → loadOpFeeParams → per-transaction (gas pool / cumulative / per-transaction write-back)
/// → finalizeOpBlock. Write-back callback: invoked immediately after each diff segment is produced;
/// the view read by the next step must already reflect it.
/// **discard-writes contract**: after any throw from this function, the caller must discard the
/// entire write set already applied via applyDiff within this block (same semantics as op-geth
/// Process discarding the whole statedb on error, state_processor.go:109-113). Throws
/// std::runtime_error (block-level error): txs empty or first tx is not the L1 attributes deposit
/// (to==OP_L1_BLOCK && from==OP_DEPOSITOR, stricter-than-spec); a deposit appears after a
/// non-deposit (stricter-than-spec); any tx gasLimit exceeds the remaining block gas; is_system_tx;
/// any validate error on a normal tx (op-geth has no failed-receipt mechanism for normal txs,
/// state_processor.go:109-113).
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);

// ---- block finalize (formerly OpBlockFinalize.h) ----

/// OP block finalize: withdrawals are always empty, no ommers / block reward; EIP-6110/7002/7251
/// requests are suppressed per cfg.disable_prague_requests — op-geth explicitly disables them for
/// OP Isthmus (state_processor.go:140-156), and this switch is always true for every OP fork; false
/// throws std::invalid_argument (configuration guardrail). Scope note: on OP Isthmus op-geth still
/// runs the EIP-4788/2935 **pre-execution** system call (state_processor.go:90-95) — that is a
/// precondition step wired in during block-level orchestration (§4.4), not part of this finalize
/// function.
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);

// ---- block seal (formerly OpBlockSeal.h) ----

/// OP Isthmus+ block-header requestsHash is a fixed value = sha256("") (op-geth EmptyRequestsHash,
/// hashes.go:43-44; on the build side worker.go:283-290 calls CalcRequestsHash on an empty list, on
/// the validation side block_validator.go:177-184 always matches Process's nil requests — pinned by
/// spec §4.2 rev.2).
inline constexpr auto OP_EMPTY_REQUESTS_HASH =
    0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855_bytes32;

/// Block-header commitment fields (M-B2 scope). Jovian BlobGasUsed (the DA footprint header field)
/// was reclaimed into this struct in M-B3+M6 Task 1 (the give-back of the original M-B2 decision
/// record 2).
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

/// Single-account storage root (secure-trie: key = keccak256(slot), value = rlp(trim(value)),
/// zero-value slots skipped — aligned with the private helper in evmone mpt_hash.cpp:13-24; the
/// upstream does not export it, so it is reproduced here as an exported piece).
[[nodiscard]] evmone::hash256 opStorageRoot(const std::map<evmc::bytes32, evmc::bytes32>& storage);

/// Compute the block-header commitment fields from the block execution result.
/// messagePasserStorage: the storage snapshot of OP_L2_TO_L1_MESSAGE_PASSER (0x4200…0016,
/// protocol_params.go:31) **after end-of-block finalize** (on the op-geth build side
/// consensus.go:416-427 takes it after IntermediateRoot — the timing point is a documented
/// contract; decision point 3 ruling (a): the caller provides it, keeping the StateView narrow
/// interface). pre-Isthmus forks ignore it (withdrawalsRoot = empty-list root EMPTY_MPT_HASH,
/// Canyon+ withdrawals list is always empty). Precondition: result.receipts is non-empty
/// (guaranteed by processOpBlock's first-attributes invariant; an empty sequence produces
/// EMPTY_MPT_HASH rather than an error).
[[nodiscard]] OpBlockSeal sealOpBlock(const OpBlockResult& result, const OpForkConfig& cfg,
    const std::map<evmc::bytes32, evmc::bytes32>& messagePasserStorage);
}  // namespace bcos::evm::opstack
