// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpSchedulerImpl — dual-signature OP scheduler component. `executeBlock` satisfies the
// scheduler_v1::TransactionScheduler concept check only (the concept is unconditional; OP mode
// never calls it — throws immediately); `executeOpBlock` is the real OP-mode entry point, called
// from handleNewPayload's OP branch.
//
// Layering: a pure template header under bcos-evm/bcos-evm/engine/, same shape as Storage2State.h
// — depends on bcos-framework (Storage template parameter is instantiated against
// storage2/MultiLayerStorage::ViewType, protocol:: types) but is not itself part of the
// bcos-evm-opstack static library (header-only, no .cpp).
//
// vm ownership: one evmc::VM (evmone) per scheduler, constructed once (evmc_create_evmone()) and
// reused across blocks; thread-safety rests on the engine execution segment being serialized under
// x_state, not on any locking inside this class.

#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <evmone/evmone.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpBlockSeal.h>
#include <opstack-executor/OpDecodePrimitives.h>
#include <opstack-executor/OpEngineSeam.h>
#include <opstack-executor/OpSchedulerErrors.h>
#include <opstack-executor/OpTxDecode.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <opstack-executor/Storage2State.h>
#include <bcos-evm/eth/state/block.hpp>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <bcos-evm/eth/state/state_diff.hpp>
#include <bcos-evm/eth/state/state_view.hpp>
#include <bcos-evm/eth/state/system_contracts.hpp>
#include <bcos-evm/eth/state/transaction.hpp>
#include <cstdint>
#include <cstring>
#include <evmc/evmc.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <map>
#include <optional>
#include <range/v3/range/concepts.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/rlp_encode.hpp>
#include <utility>
#include <variant>
#include <vector>

namespace bcos::evm::engine
{

/// OP block execution environment was folded into `protocol::BlockHeader` when
/// PR #5385 gave the FISCO header tars slots for all 8 former OpBlockEnv fields (prevRandao/baseFee
/// -> coinbase/baseFee/prevRandao/parentBeaconBlockRoot/gasLimit/extraData/blobGasUsed/parentHash
/// -> parentInfo). The engine now fills one header object and `executeOpBlock`/`toBlockInfo` read
/// the accessors directly.

/// Six-way comparison surface: `seal`'s receiptsRoot/logsBloom/withdrawalsRoot
/// (bcos::evm::opstack::OpBlockSeal, unchanged structure) plus three members below
/// (stateRoot/gasUsed/txRoot) that are deliberately NOT folded into OpBlockSeal.
struct OpExecuteBlockResult
{
    std::vector<bcos::protocol::TransactionReceipt::Ptr> receipts;
    bcos::evm::opstack::OpBlockSeal seal;
    bcos::h256 stateRoot;
    uint64_t gasUsed;
    bcos::h256 txRoot;
};

namespace detail
{
// Decode primitives (conversions / RLP scalars / composite decoders) live in
// OpDecodePrimitives.h; tx-type decoders + canonical round-trip in OpTxDecode.h.
// (Group 4-5 moved out — the class template below references detail::decodeOneRawTx etc.)
}  // namespace detail
/// OP scheduler component: dual signature, constructed once per [receiptFactory, chainId,
/// fork-timestamps] combination (composition-root-owned; this class never reads chainId/fork
/// thresholds from SystemConfigs itself).
template <class Storage>
class OpSchedulerImpl
{
public:
    OpSchedulerImpl(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps)
      : m_receiptFactory(std::move(receiptFactory)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_vm(evmc_create_evmone())
    {}

    // ---- engine-facing seam surface ----
    //
    // The engine's newPayload OP branch reaches every name below as a **dependent name on its
    // `SchedulerType` template parameter** (`typename SchedulerType::BlockEnv`,
    // `SchedulerType::computeTxRoot(...)`, ...) — the only channel available: engine must not
    // `#include` anything from bcos-evm (library purity, see the `c_opMode` comment in
    // EngineServiceImpl.h), and dependent names are looked up at instantiation, inside
    // `if constexpr (c_opMode)`, in a TU that has already included this header. The definitions
    // live in `OpEngineSeam.h`; this block only re-publishes them under the class scope the
    // engine can reach.

    /// The block-execution environment the engine fills in from the payload — the FISCO
    /// `protocol::BlockHeader` itself (PR #5385 gave every former OpBlockEnv field a tars slot).
    using BlockEnv = bcos::protocol::BlockHeader;
    /// What `executeOpBlock` returns.
    using ExecuteResult = OpExecuteBlockResult;
    /// Consensus-level rejection -> engine maps to INVALID.
    using ConsensusError = OpConsensusError;
    /// Storage-layer failure -> engine maps to JSON-RPC -32603, never INVALID.
    using StorageError = OpStorageError;
    /// c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX (s_eth_hash_2_rawtx). No longer written since
    /// plan B (2026-08-10): registerOpBlock writes SYS_HASH_2_TX via opEnvelopeToTars instead.
    /// The constant is kept only for read-side test assertions that the rawtx table is absent.
    static constexpr std::string_view c_ethRawTxTable = SYS_ETH_HASH_2_RAWTX;

    /// The six-way comparison surface (plus the two seal-only outputs) in bcos:: types.
    static OpBlockCommitments commitmentsOf(const OpExecuteBlockResult& result)
    {
        return bcos::evm::engine::commitmentsOf(
            result.seal, result.stateRoot, result.gasUsed, result.txRoot);
    }

    /// transactionsRoot over raw EIP-2718 envelopes — the engine needs it *before* execution to
    /// reconstruct the header for the blockHash check (`ExecutionPayload` carries no
    /// transactionsRoot field); `executeOpBlock`'s step 6 calls the same function.
    static bcos::h256 computeTxRoot(::ranges::input_range auto const& rawTxBytes)
    {
        return computeOpTxRoot(rawTxBytes);
    }

    /// Isthmus activation predicate for the engine's -38005 timestamp x version gate. The
    /// threshold comparison deliberately lives on this side of the seam, next to `configAt`,
    /// rather than being reimplemented in the engine.
    ///
    /// It is a *separate* function from `configAt` on purpose: `configAt` cannot answer this
    /// question — it resolves sub-`isthmusTime` timestamps to the Isthmus config as well
    /// (documented in OpForkSchedule.h: "Timestamps below isthmusTime also resolve to Isthmus"),
    /// because the minimal loop has no pre-Isthmus config to fall back to. The version gate, by
    /// contrast, must reject pre-Isthmus timestamps outright (-38005 on any version), so it needs
    /// the raw threshold. Both read the same injected `m_forkTimestamps.isthmusTime`, so there is
    /// still exactly one source of truth for the value.
    [[nodiscard]] bool isIsthmusActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.isthmusTime;
    }

    /// Jovian activation predicate. The engine needs it for one fork-dependent static check: the
    /// header's `blobGasUsed` slot must be 0 under Isthmus, but from Jovian on the same slot is
    /// repurposed as the DA footprint and is validated by seal comparison instead (OpBlockSeal.h).
    /// Same "threshold comparison stays on this side" reasoning as `isIsthmusActiveAt`.
    [[nodiscard]] bool isJovianActiveAt(uint64_t timestamp) const noexcept
    {
        return timestamp >= m_forkTimestamps.jovianTime;
    }

    OpSchedulerImpl(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl(OpSchedulerImpl&&) = delete;
    OpSchedulerImpl& operator=(const OpSchedulerImpl&) = delete;
    OpSchedulerImpl& operator=(OpSchedulerImpl&&) = delete;
    ~OpSchedulerImpl() = default;

    /// Dummy signature satisfying scheduler_v1::TransactionScheduler (concept-check purpose only
    /// — the concept is an unconditional compile-time constraint on EngineServiceImpl's
    /// SchedulerType template parameter, independent of runtime reachability; OP mode never calls
    /// this). Throws immediately, before any co_await/co_return: safe because bcos::task::Task's
    /// promise_type uses std::suspend_always at initial_suspend (libtask/bcos-task/Task.h:55), so
    /// the coroutine body does not run until the coroutine is actually resumed (syncWait/co_await);
    /// unhandled_exception() (Task.h:63-71) is the standard propagation path for exceptions raised
    /// inside a Task<T> coroutine body.
    task::Task<std::vector<bcos::protocol::TransactionReceipt::Ptr>> executeBlock(
        Storage& /*storage*/, auto& /*executor*/,
        bcos::protocol::BlockHeader const& /*blockHeader*/,
        ::ranges::input_range auto const& /*transactions*/,
        bcos::ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        throw std::logic_error(
            "OpSchedulerImpl::executeBlock: not supported in OP mode; use executeOpBlock");
        co_return {};  // unreachable; satisfies the coroutine's declared return type
    }

    /// OP-mode entry point (called from handleNewPayload's OP branch). Steps:
    ///   1. sort/decode rawTxBytes into OpBlockTx (deposit/eip1559/set_code dispatch,
    ///      detail::decodeOneRawTx);
    ///   2. one Storage2State<Storage> bridge instance for this block ("one per block");
    ///   3. processOpBlock (bridge doubles as StateView and applyDiff sink);
    ///   4. poison-flag check (bridge.poisoned() -> OpStorageError; any other throw from
    ///      processOpBlock -> OpConsensusError — error-classification table; poisoned() is
    ///      checked *first* because Storage2State's read methods are noexcept and swallow
    ///      storage failures into the poison flag rather than propagating them, so it is
    ///      authoritative over whatever processOpBlock did or threw — Storage2State.h's
    ///      poison-flag error channel contract);
    ///   5. sealOpBlock (needs the post-finalize MessagePasser storage snapshot) + stateRootOf,
    ///      both while the bridge is still alive ("compute before the bridge is destroyed");
    ///   6. txRoot over the caller-supplied rawTxBytes (independent of step 1's parsed
    ///      interpretation — txRoot commits to the exact wire bytes) + gasUsed, folded into the
    ///      six-way comparison surface (OpExecuteBlockResult).
    task::Task<OpExecuteBlockResult> executeOpBlock(
        Storage& storage, BlockEnv const& env, ::ranges::input_range auto const& rawTxBytes)
    {
        // Step 1: sort/decode. m_chainId is passed as a plain uint64_t argument (not a new
        // parameter on this method, and not an OP-specific type) — see decodeOneRawTx's comment
        // on why the OP dependent-name-in-signature hazard does not apply here.
        std::vector<bcos::evm::opstack::OpBlockTx> txs;
        for (auto const& rawItem : rawTxBytes)
            txs.push_back(detail::decodeOneRawTx(
                bcos::bytes(std::begin(rawItem), std::end(rawItem)), m_chainId));

        // Step 2: one bridge instance for this block.
        bcos::evm::evmstate::Storage2State<Storage> bridge(storage);

        const auto blk = detail::toBlockInfo(env);
        // RecentBlockHashes lazily loads ancestor hashes; the seed {N-1: parentHash} is set in
        // the constructor. hashErr is this block's poison channel (storage fault ->
        // OpStorageError, not INVALID).
        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<Storage> hashes(
            storage, blk.number, detail::toEvmcBytes32(env.parentInfo().blockHash), &hashErr);

        // tars stores milliseconds; fork configs consume seconds (blockHash/execution surface is
        // always in seconds).
        const auto& cfg = bcos::evm::opstack::configAt(
            static_cast<uint64_t>(env.timestamp()) / 1000, m_forkTimestamps);

        const auto applyDiff = [&bridge](const evmone::state::StateDiff& diff) {
            bridge.applyDiff(diff);
        };

        // Step 3+4.
        bcos::evm::opstack::OpBlockResult result;
        try
        {
            result = bcos::evm::opstack::processOpBlock(
                bridge, blk, hashes, txs, cfg, m_vm, m_chainId, m_receiptFactory, applyDiff);
        }
        catch (const std::exception& e)
        {
            // This typed catch binds only the NON-runtime_error families — std::bad_alloc
            // (direct std::exception child) and the std::logic_error family (which
            // system_contracts.cpp throws for a fatal system-call failure). Every
            // std::runtime_error and its subclasses — including all of processOpBlock's
            // block-level consensus rejections — escape this catch via the RTTI bypass explained
            // in the catch(...) clause below and are handled THERE as OpConsensusError → INVALID.
            // So whatever binds here is by construction a LOCAL fault (allocation failure /
            // internal invariant), which must never vote against the block → OpStorageError
            // (-32603), not OpConsensusError.
            if (bridge.poisoned() || hashErr.has_value())
                throw OpStorageError(
                    hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));
            throw OpStorageError(e.what());
        }
        catch (...)
        {
            // Typed-catch RTTI bypass (same phenomenon documented and worked around in
            // T8nReplayHarness.h): the -fno-rtti libevmone.a brings a hidden non-unique typeinfo
            // for std::exception, so `catch (const std::exception&)` above does NOT reliably bind
            // std::runtime_error thrown by evmone/opstack-linked code. Without this fallback those
            // throws would propagate out of executeOpBlock as raw, unclassified exceptions,
            // silently breaking the INVALID vs -32603 dispatch. Re-applies the *same*
            // poisoned()-first classification without relying on typeid matching; the original
            // message is unrecoverable here (no typed handle on the caught object).
            if (bridge.poisoned() || hashErr.has_value())
                throw OpStorageError(
                    hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));
            throw OpConsensusError(
                "OpSchedulerImpl: processOpBlock threw a block-level error (typed catch bypassed "
                "by a known RTTI issue across the -fno-rtti evmone library boundary; original "
                "exception message unavailable, see this catch(...) clause's comment)");
        }
        if (bridge.poisoned() || hashErr.has_value())
            throw OpStorageError(hashErr.has_value() ? *hashErr : std::string(bridge.firstError()));

        // Step 5: MessagePasser post-finalize storage snapshot (OpBlockSeal.h contract) + seal +
        // stateRoot, bridge still alive throughout.
        std::map<evmc::bytes32, evmc::bytes32> messagePasserStorage;
        bridge.visitAccounts([&](const auto& accountView) {
            if (accountView.addr == bcos::evm::opstack::OP_L2_TO_L1_MESSAGE_PASSER)
            {
                messagePasserStorage = accountView.storage;
                return false;  // found it; not a poison condition (Storage2State.h contract)
            }
            return true;
        });
        if (bridge.poisoned())
            throw OpStorageError(std::string(bridge.firstError()));

        const auto seal = bcos::evm::opstack::sealOpBlock(result, cfg, messagePasserStorage);
        const auto stateRootHash = bcos::evm::stateRootOf(bridge);
        if (bridge.poisoned())
            throw OpStorageError(std::string(bridge.firstError()));

        // Step 6: txRoot (trie key = canonical RLP encoding of the index, trie value = the raw
        // tx bytes as-is) + gasUsed. NOTE: this is NOT op-geth's `DeriveSha` convention —
        // `DeriveSha` re-encodes each transaction canonically from the parsed struct while this
        // hashes the wire bytes; the two coincide only because the decoders above reject every
        // non-canonical encoding (per-field strictness, the shared length-prefix fix, and the
        // whole-envelope `assertCanonicalRoundTrip` invariant — see `computeOpTxRoot`'s comment in
        // OpEngineSeam.h). The trie construction lives in `OpEngineSeam.h`'s `computeOpTxRoot` so
        // the engine's newPayload OP branch can derive the same value *before* execution, for the
        // header reconstruction the blockHash check depends on — same function, two call sites, no
        // second implementation.
        const auto txRoot = computeOpTxRoot(rawTxBytes);

        // Plan A phase 2: the execution layer already produced bcos::protocol::TransactionReceipt
        // objects (OP metadata in opStackMeta, effective gas price on the top-level field), so no
        // mapOpReceipt projection happens here — the result receipts ARE the framework receipts.
        co_return OpExecuteBlockResult{
            .receipts = std::move(result.receipts),
            .seal = seal,
            .stateRoot = detail::toBcosH256(stateRootHash),
            .gasUsed = static_cast<uint64_t>(result.gasUsed),
            .txRoot = txRoot,
        };
    }

private:
    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    evmc::VM m_vm;  // evmc_create_evmone(), one instance per scheduler
};

}  // namespace bcos::evm::engine
