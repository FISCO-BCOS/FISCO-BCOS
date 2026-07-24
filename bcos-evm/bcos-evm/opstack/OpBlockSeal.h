#pragma once

#include <bcos-evm/opstack/OpBlockExecute.h>
#include <bcos-evm/eth/state/bloom_filter.hpp>
#include <bcos-evm/eth/utils/mpt_hash.hpp>
#include <map>
#include <optional>

namespace bcos::evmref::opstack
{
using evmc::literals::operator""_bytes32;

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
/// upstream does not export it, so it is reproduced here as an exported piece and registered in the
/// upstream-diff manifest to watch for drift).
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
}  // namespace bcos::evmref::opstack
