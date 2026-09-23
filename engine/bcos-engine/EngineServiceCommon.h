/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EngineServiceCommon.h
 * @brief Shared validators and helpers for the Engine API services
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/engine/Constants.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/RawTransactionDispatch.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/BlockHeaderFactory.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/EthereumBlockRoots.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-task/Task.h>
#include <evmc/evmc.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace bcos::engine
{

/// One shared definition (was duplicated in EngineTracker.h and EngineServiceImpl.h).
struct TrackedHeadBlock
{
    h256 hash;
    bcos::protocol::BlockNumber blockNumber = 0;
};

struct BuiltPayload
{
    std::uint32_t version = 0;
    ExecutionPayload executionPayload;
    u256 blockValue = 0;
    std::optional<BlobsBundleV1> blobsBundle;
    bool shouldOverrideBuilder = false;
    std::optional<h256> parentBeaconBlockRoot;
    /// Prague+ execution requests (engine-API wire form) for built-here L1 payloads;
    /// std::nullopt means "not computed" (pre-Prague or imported payloads), which
    /// getPayloadV4+ reports as the empty list.
    std::optional<std::vector<bytes>> executionRequests = std::nullopt;
};

using BuiltPayloadPtr = std::shared_ptr<const BuiltPayload>;

namespace detail
{
/// Holocene/Jovian extraData from CL attributes. Attribute 0,0 becomes Canyon 250/6
/// (op-core EncodeHoloceneExtraData / EncodeJovianExtraData).
bcos::bytes encodeOptimismExtraData(const PayloadAttributes& payloadAttributes);

std::optional<std::string> validateExecutionPayload(
    const ExecutionPayload& executionPayload, std::uint32_t version, bool allowBlob = false);
/// Compare a submitted payload against the locally built copy. Required fields
/// must match. Omitting withdrawalsRoot is equivalent to the empty trie;
/// omitting blobGasUsed / excessBlobGas is a mismatch. blockAccessList and
/// slotNumber are compared only when both sides carry them.
std::optional<std::string> compareWithBuiltPayload(
    const ExecutionPayload& submitted, const ExecutionPayload& built);
/// Evm revision -> Eth header fork. Total: nullopt covers both "above this binary's
/// knowledge" (EVMC_EXPERIMENTAL and newer) and nothing else; revisions below LONDON
/// fold to LONDON. Callers that must not guess pick an answer for nullopt.
std::optional<bcos::protocol::EthBlockVersion> tryEthBlockVersionFor(evmc_revision rev);
/// Throwing adapter over tryEthBlockVersionFor, for the build path: hashing a header
/// under an era the chain never configured is the silent-divergence failure the
/// chain-derived selection exists to prevent, so it fails loudly (-38005).
bcos::protocol::EthBlockVersion ethBlockVersionFor(evmc_revision rev);
/// Header fork for payload @p blockNumber from the chain's per-block schedule.
/// Either way of not resolving the era — no revision at this block, or a revision
/// this binary cannot map — is a node-local fact: callers answer SYNCING, never
/// InvalidBlockHash (that would blame the submitted block). Never throws.
std::optional<bcos::protocol::EthBlockVersion> ethBlockVersionForBlock(
    ledger::LedgerConfig const& ledgerConfig, bcos::protocol::BlockNumber blockNumber);
/// Rebuild the Eth header from submitted fields and require hash == payload.blockHash.
/// Fork-gated fields come from @p forkVersion via finalizeEthBlockHeader.
/// Returns a message (callers answer InvalidBlockHash) for a genuine hash mismatch or for a
/// header the submitted fields cannot reconstruct into a valid Ethereum header. A node-local
/// fault — a null @p factory, the transactionsRoot MPT build — throws instead, so the caller
/// answers an internal error.
std::optional<std::string> matchReconstructedEthBlockHash(
    const bcos::protocol::BlockHeaderFactory::Ptr& factory, const ExecutionPayload& payload,
    const std::optional<bcos::h256>& parentBeaconBlockRoot,
    bcos::protocol::EthBlockVersion forkVersion,
    std::optional<bcos::h256> requestsHash = {});
/// Stamp Eth constants and fork-gated fields, then compute the RLP hash.
/// @p withdrawalsRoot, when set, overrides withdrawalsRootFor (cache-miss
/// reconstruction prefers the submitted header field). @p requestsHash, when set,
/// overrides the empty-requests constant stamped on Prague+ headers (L1 payloads
/// with real EIP-7685 requests hash to their computed value).
void finalizeEthBlockHeader(bcos::protocol::BlockHeader& header, const ExecutionPayload& payload,
    std::optional<bcos::h256> parentBeaconBlockRoot, bcos::protocol::EthBlockVersion forkVersion,
    std::optional<bcos::h256> withdrawalsRoot = {}, std::optional<bcos::h256> requestsHash = {});

inline bcos::h256 withdrawalsRootFor(const ExecutionPayload& /*payload*/)
{
    return bcos::ledger::mpt::emptyRootHash();
}
}  // namespace detail

namespace engine_common
{
/// A CL-pushed (external) payload mapped into the RLP domain: the reconstructed Ethereum
/// header, the committed parent header (filled by the caller from the ledger), and the raw
/// sidecars (EIP-2718 envelopes, per-item EIP-4895 withdrawal RLP) the EL verifier consumes.
struct ExternalPayloadBlock
{
    bcos::protocol::EthBlockHeaderData ethHeader;
    bcos::protocol::EthBlockHeaderData parentHeader;
    std::vector<bcos::bytes> rawTransactions;
    std::optional<std::vector<bcos::bytes>> rawWithdrawals;
};

enum class ExternalPayloadOutcome : std::uint8_t
{
    Valid,
    Invalid,
    StaleOrOutOfOrder,
};

struct ExternalPayloadResult
{
    ExternalPayloadOutcome outcome = ExternalPayloadOutcome::Invalid;
    std::string error;
};

/// Outcome of a chain-rewind request (EL shallow reorg, Phase 3): rolledBack=false carries
/// the refusal reason (target beyond the reorg window, or a journal row missing) — the
/// caller turns it into a resync, never a retry.
struct ExternalRollbackResult
{
    bool rolledBack = false;
    std::string error;
};

/// Derivation context for the L1 block the EL builder is about to produce (Phase 4):
/// the EIP-1559 base fee and EIP-4844 excess blob gas of the NEXT block, computed from
/// the committed parent header under the chain's fork schedule, plus the header fork
/// era. The era is TIMESTAMP-derived (the number-keyed SYS_CONFIG evmc revision cannot
/// express the EL mode's timestamp forks — the verifier derives revisions from
/// EvmcForkTimestamps too), so the builder finalizes the header under the same era the
/// verifier would check it against.
struct ExternalL1Context
{
    u256 baseFee = 0;
    u256 excessBlobGas = 0;
    bcos::protocol::EthBlockVersion forkVersion = bcos::protocol::EthBlockVersion::LONDON;
};

/// The block the EL builder assembles for buildL1Block: the Ethereum header with every
/// CONTEXT field set (number, timestamp, gasLimit, coinbase, prevRandao, baseFee,
/// blobGasUsed/excessBlobGas, parentBeaconRoot, withdrawalsHash) and the ROOT fields
/// (stateRoot/txsRoot/receiptsRoot/requestsHash) left zero — execution stamps them —
/// plus the raw sidecars (EIP-2718 envelopes, per-item EIP-4895 withdrawal RLP).
struct ExternalBuildBlock
{
    bcos::protocol::EthBlockHeaderData ethHeader;
    bcos::protocol::EthBlockHeaderData parentHeader;
    std::vector<bcos::bytes> rawTransactions;
    std::optional<std::vector<bcos::bytes>> rawWithdrawals;
};

/// Outcome of an EL block-build execution (Phase 4): the deterministic outputs the
/// builder stamps into the payload/header, plus the commit-time artifacts (decoded
/// transactions, receipts, MPT delta) the newPayload commit path persists. ok=false
/// carries the execution failure (a decode/execution/system-call fault — the block
/// cannot be built, which the FCU caller surfaces as an internal error).
struct ExternalBuildResult
{
    bool ok = false;
    std::string error;
    std::vector<protocol::Transaction::Ptr> transactions;
    std::vector<protocol::TransactionReceipt::Ptr> receipts;
    ledger::mpt::EthereumBlockComputation computation;
    crypto::HashType stateRoot;
    std::shared_ptr<const ledger::mpt::MPTDeltaLayer> mptDelta;
    /// Prague+: the computed EIP-7685 requestsHash, stamped into the built header.
    std::optional<crypto::HashType> requestsHash;
    /// The EIP-7685 execution requests in engine-API form (getPayloadV4+), hashing to
    /// requestsHash under engine_common::calculateRequestsHash.
    std::vector<bcos::bytes> executionRequests;
};

/// Type-erased seam over scheduler_v1::EthereumBlockVerifier for the EL-mode external
/// newPayload path (the verifier header lives in transaction-scheduler, above this module).
/// libinitializer injects the instance shared with the devp2p sync loop so both commit
/// lanes serialize on the verifier's m_commitMutex; null on the OP/single-node wiring,
/// where the external path stays unreachable.
template <class GlobalStateStorageType>
class IExternalPayloadVerifier
{
public:
    virtual ~IExternalPayloadVerifier() = default;
    virtual task::Task<ExternalPayloadResult> verifyAndCommit(
        GlobalStateStorageType& storage, ExternalPayloadBlock const& block) = 0;
    /// Rewind the committed chain to @p targetNumber (EL shallow reorg). Runs under the
    /// verifier's commit mutex, serialized against both commit lanes.
    virtual task::Task<ExternalRollbackResult> rollbackToCommitted(
        GlobalStateStorageType& storage, bcos::protocol::BlockNumber targetNumber) = 0;
    /// Phase 4 (EL block building): derive the next block's fee-market context (base
    /// fee, excess blob gas, header fork era) from the committed parent header at the
    /// new block's timestamp (SECONDS, the Ethereum wire unit).
    virtual task::Task<ExternalL1Context> deriveL1Context(
        bcos::protocol::EthBlockHeaderData const& parentHeader,
        int64_t timestampSeconds) = 0;
    /// Phase 4 (EL block building): execute the assembled block into the caller's view
    /// — block-start system calls, transactions, withdrawals, block-end system calls,
    /// the deterministic roots and the MPT delta — without verifying or committing
    /// (the builder stamps the outputs into the header; the view is staged into the
    /// payload artifact and pushed at newPayload commit time). Runs the same
    /// executeEthereumBlock implementation verifyAndCommit verifies with.
    virtual task::Task<ExternalBuildResult> buildL1Block(
        typename GlobalStateStorageType::ViewType& view, ExternalBuildBlock const& block) = 0;
};
}  // namespace engine_common

namespace detail
{
/// EIP-7685 requestsHash: sha256 over the concatenated per-request sha256 digests (each
/// engine-API executionRequests entry is request_type ++ rlp(request_data)); the empty
/// list hashes to sha256("") — the constant c_emptyRequestsHashHex.
bcos::h256 calculateRequestsHash(std::vector<bcos::bytes> const& executionRequests);

/// L1 (EL-mode) execution payload shape validation: the vanilla execution-apis rules,
/// without the OP/L2-only rejections validateExecutionPayload carries (blob transactions,
/// non-empty withdrawals, the Holocene extraData shape, the V4 withdrawalsRoot dialect).
std::optional<std::string> validateExecutionPayloadL1(
    const ExecutionPayload& executionPayload, std::uint32_t version);

/// Map a CL-submitted NewPayloadRequest into the RLP-domain block the EL verifier consumes,
/// then require ethHeaderHash(ethHeader) == payload.blockHash. Any mismatch or
/// unrepresentable field returns a message (the caller answers InvalidBlockHash, matching
/// matchReconstructedEthBlockHash's contract).
std::variant<engine_common::ExternalPayloadBlock, std::string> executionPayloadToEthBlock(
    const NewPayloadRequest& request);
}  // namespace detail

/// Shared Engine-API service surface, distinct from implementation-internal detail
/// helpers: these validators/status/shape helpers are consumed across the engine-split
/// stack (the live EngineServiceImpl here, EngineTracker, and the Eth/Op services in
/// #5548/#5549), so they get a named home instead of the private detail namespace.
namespace engine_common
{
/// The envelope → executable-transaction carrier step both build paths share: stamp the
/// raw EIP-2718 envelope onto extraTransactionBytes (the executor must see the exact wire
/// form a pool transaction would carry) and wrap the Tars transaction in its lazy
/// self-pointer. One home, because the carrier shape decides what the executor hashes and
/// executes — a drift between the lanes would fork the payload composition.
inline std::shared_ptr<bcostars::protocol::TransactionImpl> decodedTransactionFromEnvelope(
    bcostars::Transaction tars, bcos::bytes const& raw)
{
    tars.extraTransactionBytes.assign(raw.begin(), raw.end());
    return std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
}


/// Engine API behavior follows op-geth.
/// op-geth d401af16f2dd94b010a72eaef10e07ac10b31931
/// (eth/catalyst/api.go, miner/payload_building.go).
std::vector<std::string> supportedCapabilities();
bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);
/// op-geth ForkchoiceUpdatedV3/V4 both store PayloadV3; GetPayloadV4 requires PayloadV3.
std::uint32_t payloadShapeVersion(std::uint32_t methodVersion);
/// @param allowBlob  the executor_version==2 (pure-Ethereum) lane admits type-3
///        envelopes; the OP lane keeps the refusal.
std::optional<std::string> validateRawTransactionKind(
    bcos::engine::RawTransactionKind kind, std::size_t index, bool allowBlob = false);
/// EIP-1559 attribute pairing rule: the pair must be both-zero or both
/// non-zero. (0,0) is legal attribute input — encodeOptimismExtraData translates it to
/// the Canyon constants 250/6 — but a mixed pair such as (d>0,e==0) would be encoded
/// verbatim as a zero-elasticity header that calcOpBaseFee can never extend, bricking
/// the chain on top of it. Committed headers are validated separately with a strict
/// non-zero rule (validateOpExtraDataShape) since encode never produces a zero header.
inline std::optional<std::string> validateHolocene1559Params(
    std::uint32_t denominator, std::uint32_t elasticity)
{
    if ((denominator == 0) != (elasticity == 0))
    {
        return std::string(
            "holocene eip-1559 params denominator and elasticity must be both zero or "
            "both non-zero");
    }
    return std::nullopt;
}
/// op-geth ReadCanonicalHash(number) != submitted hash → not canonical.
/// Missing NUMBER_2_HASH is also not canonical (fail closed). Fixtures must write
/// both HASH_2_NUMBER and NUMBER_2_HASH (`registerVerifiedBlock` already does).
inline bool forkchoiceHashIsCanonical(
    const h256& submitted, const std::optional<h256>& canonicalAtNumber)
{
    return canonicalAtNumber.has_value() && *canonicalAtNumber == submitted;
}
/// High semantic ceiling for FCU forced txs. Not a ~256 miner
/// limit — deposit blocks can exceed that. It is stricter than HTTP's default
/// 10MiB request body, so it fires on the RPC path too, and it is the only bound
/// for callers that bypass HTTP; either way it rejects before keccak. Forced DA
/// overflow is still not INVALID (OP deposits are undroppable).
inline constexpr std::size_t c_maxForcedTxCount = 16384;
inline constexpr std::size_t c_maxForcedTxBytes = 8 * 1024 * 1024;

/// Consensus header constants shared by the Eth and OP header builders
/// (finalizeEthBlockHeader / rebuildOpEthHeader + applyOpHeaderConstants). Both stamp
/// keccak256(rlp(header))-critical values, so the literals must live in exactly one place.
/// The empty-requests hash is single-sourced cross-layer too: its hex lives in the
/// framework (c_emptyRequestsHashHex) because the OP block seal stamps the same value.
/// The empty-ommers hash is single-sourced one layer down, in bcos-rlp-protocol
/// (bcos::protocol::c_emptyOmmersHash, EthBlockHeader.h) — call sites use it directly.
inline const bcos::h64 c_posNonce{std::string{"0x0000000000000000"}};
inline const bcos::h256 c_emptyRequestsHash{std::string{c_emptyRequestsHashHex}};

/// Decoded byte count of a hex string, matching `fromHex` (optional 0x, odd nibble pads).
/// Used to reject over-ceiling forced txs before allocating the decoded buffer.
inline std::size_t decodedHexByteCount(std::string_view hex)
{
    if (hex.size() >= 2 && (hex[0] == '0') && (hex[1] == 'x' || hex[1] == 'X'))
    {
        hex.remove_prefix(2);
    }
    return (hex.size() + 1) / 2;
}

std::optional<std::string> validatePayloadAttributes(const PayloadAttributes& payloadAttributes,
    std::uint32_t version, std::vector<bcos::bytes>* decodedForcedTxs = nullptr,
    bool l1Mode = false);
/// `decodedForcedTxs` reuses bytes from validate. Hex fallback is
/// gone: if attributes carry transactions, pass the validated decoded bodies.
/// An empty span with a non-empty transactions list returns nullopt.
std::optional<PayloadID> derivePayloadId(const PayloadAttributes& payloadAttributes,
    const h256& parentHash, std::uint32_t version,
    std::span<const bcos::bytes> decodedForcedTxs = {});
PayloadStatus makeStatus(PayloadValidationStatus status,
    std::optional<h256> latestValidHash = std::nullopt,
    std::optional<std::string> validationError = std::nullopt);
/// Shared getPayload shape gate. Throws IncompatiblePayloadVersion when the request
/// version cannot render the stored body.
void requireGetPayloadShape(std::uint32_t builtVersion, const ExecutionPayload& payload,
    std::optional<h256> const& parentBeaconBlockRoot, std::uint32_t requestVersion);
/// Supported getPayload API version window.
inline bool isGetPayloadVersionSupported(std::uint32_t version)
{
    return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
           version <= static_cast<std::uint32_t>(ApiVersion::V5);
}
/// Shared getPayload response assembly so V4+ executionRequests semantics stay aligned.
template <class EntryT>
GetPayloadResult assembleGetPayloadData(const EntryT& entry, std::uint32_t version)
{
    return std::make_unique<GetPayloadData>(GetPayloadData{
        .executionPayload = entry.executionPayload,
        .blockValue = entry.blockValue,
        .blobsBundle = entry.blobsBundle,
        .shouldOverrideBuilder = entry.shouldOverrideBuilder,
        // getPayloadV4/V5 responses must carry executionRequests. Built-here L1
        // payloads carry the requests computed during assembly; everything else
        // reports a present-but-empty list (serialized as []).
        .executionRequests = version >= static_cast<std::uint32_t>(ApiVersion::V4) ?
                                 (entry.executionRequests.has_value() ?
                                         entry.executionRequests :
                                         std::optional<std::vector<bytes>>{std::in_place}) :
                                 std::nullopt,
        .parentBeaconBlockRoot = entry.parentBeaconBlockRoot,
    });
}

namespace op
{
/// Decode one EIP-2718 (typed/legacy) Web3 raw envelope into its Tars transaction form,
/// tagged with @p txHash (keccak256 of the same raw bytes). Returns nullopt when the
/// envelope is not a decodable Web3 signing payload. Single home for both the Eth and OP
/// build paths: a raw/forced envelope needs the same executable `decoded` form a sealed
/// pool transaction already carries, or the scheduler skips it and receiptsRoot ends up
/// covering fewer transactions than transactionsRoot.
/// @param allowDeposit  deposit (0x7e) envelopes are an OP-Stack payloadAttributes
///        extension: the OP build path passes true (deposits are the only OP-sanctioned
///        forced-tx lane); the Eth build path passes false — a 0x7e type is invalid on
///        an Eth/L1 chain, so no Eth client would re-execute such a block. No default:
///        this is a consensus-shape decision, so every caller states its lane's policy.
std::optional<bcostars::Transaction> opEnvelopeToTars(
    bcos::bytes const& env, bcos::crypto::HashType const& txHash, bool allowDeposit);
}  // namespace op
}  // namespace engine_common

namespace detail
{
/// True when the OpExecutionInternalError carries the OpPayloadUndecodable tag:
/// a payload-content fault (an envelope the CL submitted cannot be decoded),
/// not a node-internal fault. Single predicate for both answer shapes on BOTH
/// lanes — the FCU path maps it to an Invalid FCU status, the newPayload path
/// to an Invalid PayloadStatus; any OTHER OpExecutionInternalError must keep
/// propagating as -32603, never be flattened into a consensus INVALID.
inline bool isUndecodablePayloadFault(OpExecutionInternalError const& error)
{
    return boost::get_error_info<OpPayloadUndecodable>(error) != nullptr;
}

inline std::optional<ForkchoiceUpdatedResult> fcuInvalidIfUndecodable(
    OpExecutionInternalError const& error)
{
    if (!isUndecodablePayloadFault(error))
    {
        return std::nullopt;
    }
    return ForkchoiceUpdatedResult{
        .payloadStatus = engine_common::makeStatus(PayloadValidationStatus::Invalid, std::nullopt,
            std::string("undecodable payload transaction envelope")),
        .payloadId = std::nullopt,
    };
}
}  // namespace detail

}  // namespace bcos::engine
