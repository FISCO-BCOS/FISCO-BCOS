#pragma once

#include "MPTNodeStorage.h"  // ViewNodeStorage
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/Entry.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/CommitObserver.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-ledger/mpt/MPTDeltaLayer.h>
#include <bcos-task/TBBWait.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Exceptions.h>
#include <tbb/parallel_pipeline.h>
#include <tbb/task_arena.h>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <optional>

namespace bcos::scheduler_v1
{

DERIVE_BCOS_EXCEPTION(InvalidMPTFlagMatrix);

// Decide whether block @p blockNumber commits with an Ethereum MPT state root instead of
// the legacy XOR root (spec 5.6 / 5.10). The execute path (coExecuteBlock) consults it once
// per block when the MPT branch is wired in.
//
// Pure: no throw, no side effect. feature_raw_address is NOT part of the decision: the MPT
// delta scan classifies both the 40-hex and the 20-byte raw-address account table layouts
// (Classify.h parseAccountTable), so raw_address and the MPT state root combine freely.
inline bool shouldBuildMPT(
    bcos::ledger::Features const& features, bcos::protocol::BlockNumber blockNumber)
{
    bool buildMPT = false;
    // Scenario B: L2 Ethereum-compat chains build the MPT from genesis on. Checked FIRST:
    // at block 0 feature_mpt_state_root is not yet active, so consulting scenario A first
    // would send an L2 chain down the XOR path.
    if (features.get(ledger::Features::Flag::feature_l2_ethereum_compat))
    {
        buildMPT = true;
    }
    // Scenario A: MPT enabled mid-chain by feature_mpt_state_root. Strictly-greater keeps
    // the activation block N itself on XOR as the transition boundary (spec 5.10). The
    // >= 0 guard rejects activationBlockOf == -1 — a flag set() without a storage load —
    // which would otherwise silently enable MPT since blockNumber > -1 is always true.
    else if (features.get(ledger::Features::Flag::feature_mpt_state_root))
    {
        auto activationBlock =
            features.activationBlockOf(ledger::Features::Flag::feature_mpt_state_root);
        buildMPT = activationBlock >= 0 && blockNumber > activationBlock;
    }
    // Neither flag: legacy XOR state root (buildMPT stays false).
    return buildMPT;
}

// Startup-time guard for the flag matrix shouldBuildMPT relies on (spec 5.10, M7.3).
//
// Scenario B has no transition rule: shouldBuildMPT returns true for EVERY block once
// feature_l2_ethereum_compat is set, because the flag is assumed enabled at genesis
// (activation block 0) — the chain never has XOR history to transition from. A mid-chain
// enable would silently flip the state-root scheme with no boundary, forking any node
// that replays the pre-flag blocks. Refuse to start instead.
//
// Call this with a Features loaded via readFromStorage so activation blocks are
// populated; a bare set() leaves activationBlockOf at -1, which this guard rejects for
// the same reason (an unverifiable activation must not pass a consistency check).
//
// feature_raw_address combines freely with feature_mpt_state_root (scenario A, mainline):
// the MPT delta scan classifies both account-table layouts (Classify.h parseAccountTable
// accepts the 40-hex and the 20-byte raw-address forms), the first-touch flat back-fill
// reads through the chain's AddressTableMode (FlatToMPT.h readFlatAccountMeta), and the
// mainline executor is mode-aware. It does NOT combine with feature_l2_ethereum_compat:
// the OP lane's Storage2State bridge and the ethereum-executor still name account tables
// /apps/<40-hex> (Storage2StateHelpers.h accountTableName; EthereumState.h), while the
// mode-aware RPC read paths (OpScheduler getCode/getABI/getStorageAt, accountTableMode)
// would route to 20-byte binary tables once raw_address activates — reads and writes split
// onto disjoint tables and the RPC silently reads empty state. Refuse the combination;
// OpScheduler additionally re-checks raw_address per block, because feature_raw_address is
// not genesis-only and a mid-chain governance activation is invisible to this boot guard.
//
// @throws InvalidMPTFlagMatrix when feature_raw_address is set together with
//         feature_l2_ethereum_compat, or when feature_l2_ethereum_compat is set with a
//         non-zero (or unknown) activation block. A features object without the L2 flag
//         always passes (raw_address + feature_mpt_state_root included).
inline void validateMPTFlagMatrix(bcos::ledger::Features const& features)
{
    using Flag = bcos::ledger::Features::Flag;
    if (features.get(Flag::feature_raw_address) && features.get(Flag::feature_l2_ethereum_compat))
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "feature_raw_address cannot be combined with feature_l2_ethereum_compat: "
                "the OP lane's Storage2State bridge and the ethereum-executor still name "
                "account tables /apps/<40-hex>, but with raw_address active the mode-aware "
                "RPC read paths (OpScheduler getCode/getABI/getStorageAt via "
                "accountTableMode) route to 20-byte binary tables — the RPC read path and "
                "the executor write path would split onto disjoint tables and reads would "
                "silently come back empty. Keep feature_raw_address off on L2 chains until "
                "those lanes grow mode-aware naming"));
    }
    if (!features.get(Flag::feature_l2_ethereum_compat))
    {
        return;
    }
    auto activationBlock = features.activationBlockOf(Flag::feature_l2_ethereum_compat);
    if (activationBlock != 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "feature_l2_ethereum_compat must be enabled at genesis (activation block 0), "
                "but its activation block is " +
                std::to_string(activationBlock) +
                "; enabling it mid-chain would switch the state-root scheme with no "
                "transition rule (spec 5.10 scenario B)"));
    }
}

/// OP mode (executor_version >= OPSTACK_EXECUTOR_VERSION) is a genesis-only property: it is
/// decided when the chain is created and cannot change afterwards. It requires the
/// genesis-only feature_l2_ethereum_compat (the OP lane commits account state in MPT only),
/// and executor_version must be genesis-bound (activation block 0). A value above the newest
/// declared lane is not a lane of its own: MultiVersionScheduler::setVersion saturates it onto
/// the newest WIRED slot, so boot does not refuse it (and must not, or a chain that wrote such
/// a row before 3.18 could not start to fix it) — what remains here is the lane's own
/// preconditions, which apply to every value at or above OPSTACK.
/// The converse does NOT hold: feature_l2_ethereum_compat is the LEDGER's L2 state shape,
/// and the Ethereum lane (executor_version == ETHEREUM_EXECUTOR_VERSION) serves L2 chains
/// with it — the pure-Ethereum executor on an MPT root, sealing through the consensus
/// layer (the executor integration harness has covered that pairing since #5397). Such a
/// chain is Eth mode, not OP mode; only the OP lane needs engine-driven production.
///
inline void validateOpModeGenesisOnly(bcos::ledger::Features const& features, int executorVersion,
    bcos::protocol::BlockNumber executorVersionActivation)
{
    using Flag = bcos::ledger::Features::Flag;
    bool const flagOn = features.get(Flag::feature_l2_ethereum_compat);
    bool const opMode = (executorVersion >= bcos::ledger::OPSTACK_EXECUTOR_VERSION);
    // The activation check runs first so that any mid-chain row -- with or without the L2 flag
    // -- reaches the recovery sentence instead of only the flag message.
    if (opMode && executorVersionActivation != 0)
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "executor_version is genesis-only in OP mode (activation block " +
                std::to_string(executorVersionActivation) +
                " != 0); it cannot be changed on a running chain. Recovery on a chain that "
                "wrote this row before upgrading: run the previous binary and set "
                "executor_version back to the value that chain ran with (2 = Eth lane), then "
                "upgrade again. A new chain is only needed if that write is impossible"));
    }
    if (opMode && !flagOn)
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "OP mode must be decided at chain creation: executor_version=" +
                std::to_string(executorVersion) +
                " (the OPSTACK slot) requires feature_l2_ethereum_compat=on, but it is off; "
                "the OP lane commits account state in MPT only, so the flag is genesis-bound "
                "with the mode"));
    }
}

/// Per-block half of the raw_address guard for the lanes whose executors are still
/// hex-only — the OP lane (opstack-executor's Storage2State bridge derives
/// /apps/<40-hex> names itself) and the Eth engine lane (ethereum-executor's
/// EthereumState hard-codes AddressTableMode::Hex). The boot guard
/// (validateMPTFlagMatrix) rejects raw_address + feature_l2_ethereum_compat, but
/// feature_raw_address is NOT genesis-only: a governance activation after boot never
/// re-runs the boot check, so the lanes re-check at the point where they read the block's
/// features (OpScheduler::loadLedgerConfig, EthEngineService::buildPayload). The mainline
/// v1 baseline lane is mode-aware and deliberately NOT covered here — raw_address +
/// feature_mpt_state_root stays legal there (this PR's purpose).
///
/// @throws InvalidMPTFlagMatrix when feature_raw_address is set. Halting block production
///         loudly beats the alternative: with raw_address active the mode-aware RPC read
///         paths route to 20-byte binary tables while the hex-only executor keeps writing
///         /apps/<40-hex>, and the two split silently. @p blockNumber is diagnostic only.
///
/// OPERATIONAL CONSEQUENCE, deliberate (same trade-off as the base's
/// rejectRawAddressWithMPT): turning feature_raw_address on mid-chain halts these lanes'
/// block production from the activation block, and there is no in-protocol recovery — the
/// flag cannot be turned off again through consensus once every node refuses to execute.
inline void rejectRawAddressOnEngineLanes(
    bcos::ledger::Features const& features, bcos::protocol::BlockNumber blockNumber)
{
    if (features.get(ledger::Features::Flag::feature_raw_address))
    {
        BOOST_THROW_EXCEPTION(
            InvalidMPTFlagMatrix{} << bcos::errinfo_comment(
                "feature_raw_address is active on a lane whose executor is still hex-only "
                "(block " +
                std::to_string(blockNumber) +
                "): the OP/Eth engine lanes name account tables /apps/<40-hex> "
                "(opstack-executor Storage2State, ethereum-executor EthereumState), but "
                "raw_address routes the mode-aware read paths to 20-byte binary tables — "
                "reads and writes would split onto disjoint tables and RPC reads would "
                "silently come back empty. Halting block production; do not activate "
                "feature_raw_address on these lanes until their executors grow mode-aware "
                "naming"));
    }
}

/// XOR fold over flat storage — the legacy (non-MPT) state-root path, shared by the PBFT
/// scheduler and the engine service. BOTH callers must pass @p features to Entry::hash: the
/// v3.17 bugfix flag (bugfix_statestorage_hash_v3_17) changes the digest, so an
/// implementation that omits it commits a different root than the other for the same state.
template <class StorageType>
task::Task<h256> xorStateRoot(StorageType& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    auto range = co_await storage2::range(storage);
    storage::Entry deletedEntry;
    deletedEntry.setStatus(storage::Entry::DELETED);

    // Wrap once outside the parallel pipeline so the optional copy is paid only once,
    // not per entry.
    std::optional<ledger::Features> const featuresOpt(features);

    h256 totalHash;
    using KeyValueType = task::AwaitableReturnType<decltype(range.next())>;
    tbb::parallel_pipeline(tbb::this_task_arena::max_concurrency(),
        tbb::make_filter<void, KeyValueType>(tbb::filter_mode::serial_in_order,
            [&](tbb::flow_control& control) -> KeyValueType {
                if (auto keyValue = task::tbb::syncWait(range.next()))
                {
                    return keyValue;
                }
                control.stop();
                return {};
            }) &
            tbb::make_filter<KeyValueType, h256>(tbb::filter_mode::parallel,
                [&](KeyValueType keyValue) -> h256 {
                    auto& [key, value] = *keyValue;
                    executor_v1::StateKeyView view(key);
                    auto [tableName, keyName] = view.get();

                    const storage::Entry* entry = nullptr;
                    if (entry = std::get_if<storage::Entry>(std::addressof(value)); !entry)
                    {
                        entry = std::addressof(deletedEntry);
                    }
                    return entry->hash(tableName, keyName, hashImpl, blockVersion, featuresOpt);
                }) &
            tbb::make_filter<h256, void>(
                tbb::filter_mode::serial_out_of_order, [&](h256 hash) { totalHash ^= hash; }));
    co_return totalHash;
}

/// Backwards-compatible name for xorStateRoot, kept for the FIB-99/FIB-105 state-root tests.
template <class StorageType>
task::Task<h256> calculateStateRoot(StorageType& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    co_return co_await xorStateRoot(storage, blockVersion, hashImpl, features);
}

/// Build an Ethereum MPT state root over @p view. Single source for the parent-root rule:
/// the parent's committed state root is read only when the parent itself built an MPT, so an
/// activation-boundary parent (XOR root) starts from the empty trie.
///
/// @param trackRefCounts  forwarded to buildAndCollect: false skips the per-hash
///                        refCountDeltas tally for callers whose commit path never reads it.
///                        Pass the commit observer's needsRefCountDeltas() (the PBFT scheduler
///                        and the engine services both do). Defaults to false so a producer
///                        that forgets to wire its observer through fails LOUD — the pruner's
///                        empty-refCountDeltas check (MPTPruner::coPreparePruneRows) throws on
///                        the first pruned block — instead of silently tallying with no
///                        consumer.
template <class ViewType>
task::Task<ledger::mpt::MPTDeltaLayer> buildMPTStateRootForView(ViewType& view,
    protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
    protocol::BlockFactory& blockFactory, bool trackRefCounts = false)
{
    auto const blockNumber = blockHeader.number();
    h256 parentStateRoot = ledger::mpt::emptyRootHash();
    if (blockNumber > 0 && shouldBuildMPT(ledgerConfig.features(), blockNumber - 1))
    {
        auto parentBlock =
            co_await ledger::getBlockData(view, blockNumber - 1, ledger::HEADER, blockFactory);
        parentStateRoot = parentBlock->blockHeader()->stateRoot();
    }
    // Node reads resolve through the full view (parent nodes live in the pending layers /
    // backend); node writes land in this block's own mutable layer (MPTNodeStorage.h).
    ViewNodeStorage<ViewType> nodeStorage(view);
    bool const l2Mode =
        ledgerConfig.features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
    co_return co_await ledger::mpt::buildAndCollect(nodeStorage, parentStateRoot, view, l2Mode,
        ledger::account::accountTableMode(ledgerConfig.features()), trackRefCounts);
}

/// Publish the header under SYS_NUMBER_2_BLOCK_HEADER so the next block's MPT build can read
/// the parent's committed state root through the view.
///
/// NOT byte-equivalent to a fully signed header when called at execute time: the engine calls
/// it before receiptsRoot / txsRoot / gasUsed are set and before the hash is computed, so the
/// row carries defaults for those fields. Two facts make that safe, and any new reader must
/// re-check them: (1) the only reader before the commit overwrites the row is the next
/// block's parent-stateRoot lookup, which needs only stateRoot; (2) the commit's prewrite
/// layer merges after the block layer, so the signed header wins in the backend.
template <class ViewType>
task::Task<void> publishPendingBlockHeaderForMPT(
    ViewType& view, protocol::BlockHeader const& header)
{
    if (header.number() == 0)
    {
        co_return;
    }
    auto blockNumberStr = boost::lexical_cast<std::string>(header.number());
    bytes headerBuffer;
    header.encode(headerBuffer);
    storage::Entry headerEntry;
    headerEntry.set(std::move(headerBuffer));
    co_await storage2::writeOne(view,
        executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));
}

/// The pre-commit pruning hook, shared by every producer that lands an MPT delta (the PBFT
/// BaselineScheduler::coCommitBlock and the engine services' newPayload commit): the observer
/// turns the block's delta into the deletion keys of expired "/mpt/" node rows, applied to
/// @p prewriteStorage so the deletions land in the SAME WriteBatch as the block data
/// (CommitObserver.h explains the crash-atomicity contract). The NoopCommitObserver default returns
/// an empty batch, so a node without pruning configured pays nothing.
///
/// SERIALIZATION CONTRACT: MPTPruner stages the block's counting work on a single shared
/// overlay between this call and the matching CommitObserver::onCommit, so the caller must
/// hold its commit mutex across [prepareMPTPruneRows -> merge -> onCommit] and thereby
/// serialize the triple against every other commit (BaselineScheduler::m_commitMutex, the
/// engine services' m_commitMutex). A commit that fails before onCommit simply re-runs this
/// helper on retry — the staged overlay is discarded and re-derived (idempotent).
template <class MutableStorageType>
task::Task<void> prepareMPTPruneRows(ledger::mpt::CommitObserver& commitObserver,
    protocol::BlockNumber blockNumber, ledger::mpt::MPTDeltaLayer const& mptDelta,
    MutableStorageType& prewriteStorage)
{
    auto pruneRows = co_await commitObserver.coPreparePruneRows(blockNumber, mptDelta);
    if (!pruneRows.deletions.empty())
    {
        // The mutable layer is LOGICAL_DELETION: removeSome writes tombstones that the
        // merge turns into physical deletes in the backend's WriteBatch (and removals
        // in the cache fan-out).
        co_await storage2::removeSome(prewriteStorage, std::move(pruneRows.deletions));
    }
}

}  // namespace bcos::scheduler_v1
