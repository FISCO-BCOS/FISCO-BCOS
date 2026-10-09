// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — SchedulerInterface for OP. Linear only: blockGasLeft, state-diff
// visibility, and deposit order forbid a parallel scheduler.
//
// Unfinalized window (D1 方案 A, docs/superpowers/plans/op-stack-l2/2026-08-19-d1-reorg-design.md
// §8-§13). The backend holds exactly the FINALIZED chain; every executed-but-unfinalized block
// lives in memory as one BlockLayer {state, ledger} keyed by its CL-announced hash, in a tree
// rooted at the backend tip. Same-height siblings coexist; the Engine tracker decides which is
// canonical, this class never does.
//   executeBlock(verify=true): view = forkChain(ancestor layers of parent) + newMutable →
//     preBlockOpEthSteps → SchedulerSerialImpl(serial=true) → finalizeOpEthBlockResult →
//     commitment check → ledger rows (prewriteBlockToBuffer) → STAGED (m_staged).
//   commitBlock (= admit): staged → m_window. Nothing is written to the backend.
//   finalizeUpTo(F): merge the window chain tip..F oldest-first (mergeToBackends per block),
//     then prune every window block not descending from F. Only here does the backend advance
//     and only here do the block-number notifiers fire.
//   reset(): drops staged blocks and the retained probe; the window survives.
// Parent unknown (not in window, not the finalized tip) → InvalidBlockNumber; the engine
// resolves parents itself first and answers SYNCING, so this is defense in depth.

#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/dispatcher/SchedulerTypeDef.h>
#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/storage2/MultiLayerStorage.h>
#include <bcos-framework/storage2/Storage.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-ledger/mpt/Errors.h>
#include <bcos-ledger/mpt/MPTBuilder.h>
#include <bcos-rlp-protocol/BlockHeaderHash.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/BaselineSchedulerMPTHelpers.h>
#include <bcos-transaction-scheduler/HistoricalCallStorage.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/IOServicePool.h>
#include <fmt/format.h>
#include <opstack-executor/OpCommon.h>  // OpConsensusError / OpStorageError / detail conversions
#include <opstack-executor/OpEthBlockSteps.h>   // preBlockOpEthSteps / finalizeOpEthBlockResult
#include <opstack-executor/OpEthCommitments.h>  // OpEthExecuteBlockResult / opEthMismatchedFieldOf
#include <opstack-executor/OpEthDeposit.h>      // decodeOpDepositEnvelope / OP_DEPOSIT_TX_TYPE
#include <opstack-executor/OpEthExecutor.h>     // OpEthExecutor / OpEthBlockContext
#include <opstack-executor/OpForkSpec.h>        // opForkSpecAt / opForkTimestampSec
#include <opstack-executor/OpRecentBlockHashes.h>  // per-block BLOCKHASH source
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/algorithm/hex.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/view/zip.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
#define OP_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("OP_SCHEDULER")

/// executeBlock → commitBlock(admit) → finalizeUpTo. The window key is the CL-announced hash
/// (canonicalBlockHash of the announced header); do not recompute it from executedHeader
/// (optional fields are incomplete).
template <class MultiLayerStorage>
class OpScheduler : public scheduler::SchedulerInterface
{
public:
    using ViewType = typename MultiLayerStorage::ViewType;
    using MutableStorage = typename MultiLayerStorage::MutableStorage;
    using LayerPtr = std::shared_ptr<MutableStorage>;
    using Ptr = std::shared_ptr<OpScheduler>;

    /// One executed block held in memory (D1 §8.2). `state` is the execution view's mutable
    /// layer (account KV + MPT nodes), `ledger` this block's own ledger rows
    /// (prewriteBlockToBuffer output, D1 §9). Neither is written after the block is staged.
    struct BlockLayer
    {
        protocol::BlockNumber number = 0;
        bcos::crypto::HashType hash;        // CL-announced hash (window key)
        bcos::crypto::HashType parentHash;  // window block or the finalized tip
        LayerPtr state;
        LayerPtr ledger;  // null on execute-only construction (no ledger); admit refuses it
        protocol::BlockHeader::Ptr executedHeader;  // commitment-filled header
        protocol::Block::Ptr block;                 // receipts attached
    };

    /// execute() result before it is wrapped as a BlockLayer.
    struct ExecuteOutcome
    {
        OpEthExecuteBlockResult result;
        bcos::crypto::HashType announcedBlockHash;
    };

    /// verify=false probe retained for adoptProbeAsPending.
    struct ProbeSlot
    {
        ViewType view;                   // forkChain(parent chain)+newMutable execution view
        OpEthExecuteBlockResult result;  // commitments + receipts
        protocol::BlockHeader::Ptr executedHeader;  // commitment-filled header
        std::vector<LayerPtr> parentLayers;         // the chain the probe ran on
    };

    /// Ancestor layers of a parent hash, oldest first (state, ledger per window block).
    /// `error` non-empty = the parent is neither in the window nor the finalized tip.
    struct ParentChain
    {
        std::vector<LayerPtr> layers;
        std::string error;
    };

    // ---- SchedulerInterface overrides ----

    /// Engine newPayload drives this; PBFT/sync do not.
    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) override
    {
        // Parameter-form task so a genuine suspend does not read a destroyed capturing
        // lambda (BaselineScheduler-tpp.h). syncWait: EngineServiceImpl treats execute
        // as finished when this returns (task::wait is fire-and-forget).
        task::syncWait([](decltype(this) self, bcos::protocol::Block::Ptr block, bool verify,
                           std::function<void(
                               bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
                               callback) -> task::Task<void> {
            std::apply(callback, co_await self->coExecuteBlock(std::move(block), verify));
        }(this, std::move(block), verify, std::move(callback)));
    }

    /// Adopt the retained verify=false probe as the pending block without re-executing.
    void adoptProbeAsPending(bcos::protocol::Block::Ptr block,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
            callback) override
    {
        task::syncWait([](decltype(this) self, bcos::protocol::Block::Ptr block,
                           std::function<void(
                               bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool _sysBlock)>
                               callback) -> task::Task<void> {
            std::apply(callback, co_await self->coAdoptProbe(std::move(block)));
        }(this, std::move(block), std::move(callback)));
    }

    /// ADMIT: move the staged block whose executed header is @p header into the window.
    /// Nothing reaches the backend here (see finalizeUpTo). The callback's LedgerConfig is the
    /// number+timestamp stub (loadCommitLedgerConfig); nobody may publish it.
    void commitBlock(bcos::protocol::BlockHeader::Ptr header,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        task::syncWait(
            [](decltype(this) self, bcos::protocol::BlockHeader::Ptr header,
                std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback)
                -> task::Task<void> {
                std::apply(callback, co_await self->coAdmit(std::move(header)));
            }(this, std::move(header), std::move(callback)));
    }

    /// FINALIZE: merge the window chain (finalized tip, @p blockHash] into the backend oldest
    /// first, one mergeToBackends(state, ledger) per block, then prune every window block not
    /// descending from @p blockHash (D1 §12.2). Fires the block-number notifiers per merged
    /// block. Idempotent on the current finalized tip.
    void finalizeUpTo(
        bcos::crypto::HashType const& blockHash, std::function<void(Error::Ptr)> callback) override
    {
        task::syncWait([](decltype(this) self, bcos::crypto::HashType blockHash,
                           std::function<void(Error::Ptr)> callback) -> task::Task<void> {
            callback(co_await self->coFinalizeUpTo(blockHash));
        }(this, blockHash, std::move(callback)));
    }

    std::optional<UnfinalizedBlock> unfinalizedBlock(
        bcos::crypto::HashType const& blockHash) const override
    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        auto it = m_window.find(blockHash);
        if (it == m_window.end())
        {
            return std::nullopt;
        }
        return UnfinalizedBlock{.number = it->second.number,
            .hash = it->second.hash,
            .parentHash = it->second.parentHash,
            .header = it->second.executedHeader};
    }

    // ---- Read plane for the RPC side (D1 §10.2) ----
    // Rule 1: views come from here. viewAt(hash) is the chain view of one window block (or the
    // finalized tip); hashAtHeightOnChain maps a height on one chain to its block. Rule 2:
    // heights come from the Engine tracker — this scheduler never decides which branch is
    // canonical, it is TOLD the head through setCanonicalHeadProvider, and call()/
    // callAtBlock()/getCode()/getABI()/getPendingStorageAt() read that head's chain
    // (headView). The type-erased facade EthEndpoint consumes is
    // bcos::engine::OpCanonicalReader (engine/bcos-engine/OpCanonicalReaderImpl.h).

    /// The read view of @p blockHash's chain: forkChain(ancestors incl. the block itself) for
    /// a window block, forkCommitted() for the finalized tip, nullopt for anything else
    /// (finalized blocks below the tip are the ledger's, read them through forkCommitted()).
    task::Task<std::optional<ViewType>> viewAt(bcos::crypto::HashType blockHash)
    {
        co_await hydrateFinalized();
        std::lock_guard<std::mutex> lock(m_windowMutex);
        if (blockHash != bcos::crypto::HashType{} && blockHash == m_finalizedHash)
        {
            co_return m_multiLayerStorage->forkCommitted();
        }
        auto it = m_window.find(blockHash);
        if (it == m_window.end())
        {
            co_return std::nullopt;
        }
        co_return m_multiLayerStorage->forkChain(chainLayersLocked(it->second));
    }

    /// The hash at height @p number on the chain whose tip is @p tipHash: walks the window
    /// from the tip, then the ledger's SYS_NUMBER_2_HASH once the height is finalized.
    /// nullopt when @p tipHash is unknown or @p number is above it.
    task::Task<std::optional<bcos::crypto::HashType>> hashAtHeightOnChain(
        bcos::crypto::HashType tipHash, protocol::BlockNumber number)
    {
        co_await hydrateFinalized();
        {
            std::lock_guard<std::mutex> lock(m_windowMutex);
            auto cursor = tipHash;
            for (auto it = m_window.find(cursor); it != m_window.end(); it = m_window.find(cursor))
            {
                if (it->second.number == number)
                {
                    co_return cursor;
                }
                if (it->second.number < number)
                {
                    co_return std::nullopt;
                }
                cursor = it->second.parentHash;
            }
            // Left the window: the cursor must be the finalized tip (or the tip was asked for).
            if (cursor != m_finalizedHash || cursor == bcos::crypto::HashType{})
            {
                co_return std::nullopt;
            }
            if (number > m_finalizedNumber.load())
            {
                co_return std::nullopt;
            }
        }
        auto view = m_multiLayerStorage->forkCommitted();
        co_return co_await ledger::getBlockHash(view, number, ledger::fromStorage);
    }

    /// Height of the finalized (backend) tip; -1 before any block is on disk.
    protocol::BlockNumber finalizedNumber() const { return m_finalizedNumber.load(); }
    /// Hash of the finalized tip; zero when the ledger has no SYS_NUMBER_2_HASH row for it.
    bcos::crypto::HashType finalizedHash() const
    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        return m_finalizedHash;
    }
    /// Number of blocks in the unfinalized window (all branches).
    std::size_t windowSize() const
    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        return m_window.size();
    }

    /// The finalized tip as (number, hash), hydrated from the backend on first use (restart,
    /// D1 §13.2). number == -1 and a zero hash when the ledger is empty.
    task::Task<std::pair<protocol::BlockNumber, bcos::crypto::HashType>> finalizedTip()
    {
        co_await hydrateFinalized();
        std::lock_guard<std::mutex> lock(m_windowMutex);
        co_return std::pair{
            static_cast<protocol::BlockNumber>(m_finalizedNumber.load()), m_finalizedHash};
    }

    /// The Engine tracker's head as seen by this scheduler's own reads (D1 §10.2 rule 2):
    /// call()/callAtBlock()/getCode()/getABI()/getPendingStorageAt() evaluate against the
    /// chain of the hash this returns. nullopt (or unset) = the finalized plane. The
    /// composition root wires OpEngineService::trackedHead(); the provider must be cheap and
    /// non-blocking (it is called on every RPC read).
    using CanonicalHeadProvider = std::function<std::optional<bcos::crypto::HashType>()>;
    void setCanonicalHeadProvider(CanonicalHeadProvider provider)
    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        m_headProvider = std::move(provider);
    }

    /// The read view of the canonical head's chain: viewAt(head) when the provider names a
    /// window block or the finalized tip, else the finalized plane — no provider, or no
    /// tracker head yet after a restart. The unknown-head fallback is defensive: the tracker
    /// only records heads resolveBlock found, and finalize prunes nothing on the head chain.
    task::Task<ViewType> headView()
    {
        CanonicalHeadProvider provider;
        {
            std::lock_guard<std::mutex> lock(m_windowMutex);
            provider = m_headProvider;
        }
        if (provider)
        {
            if (auto head = provider())
            {
                if (auto view = co_await viewAt(*head))
                {
                    co_return std::move(*view);
                }
            }
        }
        co_await hydrateFinalized();
        co_return m_multiLayerStorage->forkCommitted();
    }

    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }

    /// Drop staged (executed, not admitted) blocks and the retained probe. The window is NOT
    /// touched: the engine calls reset before every payload build, and admitted blocks must
    /// survive a build.
    void reset(std::function<void(Error::Ptr)> callback) override
    {
        std::scoped_lock lock(m_executeMutex, m_commitMutex, m_windowMutex);
        if (!m_staged.empty())
        {
            OP_SCHEDULER_LOG(INFO) << "reset: dropping " << m_staged.size()
                                   << " staged (executed, unadmitted) block(s)";
            m_staged.clear();
        }
        m_lastProbe.reset();
        callback(nullptr);
    }

    void preExecuteBlock(
        bcos::protocol::Block::Ptr, bool, std::function<void(Error::Ptr)> callback) override
    {
        callback(nullptr);
    }

    /// eth_call on the canonical head's state (headView). Failures return an RPC Error, not a
    /// status-0 receipt.
    void call(protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait(
            [](decltype(this) self, protocol::Transaction::Ptr transaction,
                std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
                -> task::Task<void> {
                try
                {
                    callback(nullptr, co_await self->coCallLatest(std::move(transaction)));
                }
                catch (const std::exception& e)
                {
                    // Map storage faults to OpStorageFault, same as historical eth_call.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING) << LOG_DESC("eth_call failed")
                                              << LOG_KV("detail", boost::diagnostic_information(e));
                    callback(BCOS_ERROR_PTR(code, fmt::format("eth_call failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        nullptr);
                }
                catch (...)
                {
                    // Same shape as callAtBlock: classify the unrecognized object and log a trace —
                    // an unlogged "unknown exception" would leave the operator nothing to
                    // correlate.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("eth_call failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        nullptr);
                }
            }(this, std::move(transaction), std::move(callback)));
    }

    /// eth_call against the MPT at @p blockNumber on the canonical head's chain.
    void callAtBlock(protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait(
            [](decltype(this) self, protocol::Transaction::Ptr transaction,
                protocol::BlockNumber blockNumber,
                std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
                -> task::Task<void> {
                try
                {
                    auto [error, receipt] =
                        co_await self->coCallAtBlock(std::move(transaction), blockNumber);
                    callback(std::move(error), std::move(receipt));
                }
                catch (const std::exception& e)
                {
                    // Log the detail; return a generic RPC reason.
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                        << LOG_KV("detail", boost::diagnostic_information(e));
                    callback(BCOS_ERROR_PTR(
                                 code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                           blockNumber, rpcSafeReason(code))),
                        nullptr);
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("eth_call at block failed") << LOG_KV("block", blockNumber)
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(
                                 code, fmt::format("eth_call at block {} failed: {} (see node log)",
                                           blockNumber, rpcSafeReason(code))),
                        nullptr);
                }
            }(this, std::move(transaction), blockNumber, std::move(callback)));
    }

    /// Contract code at the canonical head (headView). Do not use getLedgerConfig
    /// (header.hash() throws).
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait(
            [](decltype(this) self, std::string contract,
                std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) -> task::Task<void> {
                try
                {
                    auto view = co_await self->headView();
                    // The OP lane's naming rule (no /sys/ routing, re-encoded to the node
                    // layout), NOT the v1-rule constructor — the bridge writes every
                    // address, system-tx ones included, under its /apps/ logical name.
                    bcos::ledger::account::EVMAccount account(view,
                        bcos::ledger::account::FromTableName{},
                        bcos::ledger::account::ethLaneAccountTableName(parseAddress(contract)));
                    auto code = co_await account.code();
                    if (!code)
                    {
                        callback(nullptr, {});
                        co_return;
                    }
                    auto bytesView = code->get();
                    callback(nullptr, bcos::bytes(bytesView.begin(), bytesView.end()));
                }
                catch (const std::exception& e)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getCode failed") << LOG_KV("detail", e.what());
                    callback(BCOS_ERROR_PTR(code, fmt::format("getCode failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getCode failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("getCode failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
            }(this, std::string(contract), std::move(callback)));
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        task::wait(
            [](decltype(this) self, std::string contract,
                std::function<void(bcos::Error::Ptr, std::string)> callback) -> task::Task<void> {
                try
                {
                    auto view = co_await self->headView();
                    // Lane rule, as in getCode above.
                    bcos::ledger::account::EVMAccount account(view,
                        bcos::ledger::account::FromTableName{},
                        bcos::ledger::account::ethLaneAccountTableName(parseAddress(contract)));
                    auto abi = co_await account.abi();
                    if (!abi)
                    {
                        callback(nullptr, {});
                        co_return;
                    }
                    callback(nullptr, std::string(abi->get()));
                }
                catch (const std::exception& e)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getABI failed") << LOG_KV("detail", e.what());
                    callback(BCOS_ERROR_PTR(code, fmt::format("getABI failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
                catch (...)
                {
                    auto const code = self->classifyException(std::current_exception());
                    OP_SCHEDULER_LOG(WARNING)
                        << LOG_DESC("getABI failed")
                        << LOG_KV("detail", self->describeException(std::current_exception()));
                    callback(BCOS_ERROR_PTR(code, fmt::format("getABI failed: {} (see node log)",
                                                      rpcSafeReason(code))),
                        {});
                }
            }(this, std::string(contract), std::move(callback)));
    }

    // `number` discarded: pending has no historical block context. See
    // SchedulerInterface::getPendingStorageAt. The "pending plane" on the OP lane is the
    // canonical head's chain (D1 §10.2: same as `latest`, headView) — a block that is
    // executed or admitted but not yet the tracker's head is not visible, exactly as op-geth
    // keeps the pending state at the last forkchoice head. Nonce checks for tx admission read
    // this, so a sender whose tx sits in an unfinalized head-chain block sees the advanced
    // nonce, while a side branch's inclusion does not count.
    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(std::string_view address,
        std::string_view key, bcos::protocol::BlockNumber /*number*/) override
    {
        auto const addressOwned = std::string(address);
        auto const keyOwned = std::string(key);
        auto view = co_await headView();
        // The OP lane is scenario B by construction (executor_version >= OPSTACK_EXECUTOR_VERSION
        // is genesis-fixed), so no feature read decides the routing below. The tip number is
        // still needed for the committed-MPT fallback. (The account-table mode itself needs no
        // read — it is node-local, nodeAddressTableMode().)
        auto const tipNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        if (keyOwned == bcos::ledger::ACCOUNT_TABLE_FIELDS::NONCE)
        {
            // Pending first: the caller uses this value as the transaction nonce, and an
            // unfinalized head-chain block may already have advanced it in its own window
            // layer. Scenario B keeps account fields in the committed
            // MPT, so when the pending/flat plane has no row, fall back to the committed tip's
            // MPT state. The OP lane's naming rule (verbatim /apps/ logical name, no /sys/
            // routing, re-encoded to the node layout) is legacyAppsAccountTableName — the
            // v1-rule constructor would route system-tx addresses to /sys/, where the bridge
            // never writes.
            bcos::ledger::account::EVMAccount pendingAccount(view,
                bcos::ledger::account::FromTableName{},
                bcos::ledger::account::legacyAppsAccountTableName(addressOwned));
            if (auto pending = co_await pendingAccount.storageEntry(keyOwned))
            {
                co_return pending;
            }
            auto block = co_await bcos::ledger::getBlockData(
                view, tipNumber, bcos::ledger::HEADER, *m_blockFactory);
            // getBlockData can answer nullptr for a number with no committed header; a null
            // header here would be a deref crash, so fall through to the flat path instead.
            auto const stateRoot = (block != nullptr && block->blockHeader() != nullptr) ?
                                       block->blockHeader()->stateRoot() :
                                       bcos::crypto::HashType{};
            if (stateRoot != bcos::crypto::HashType{})
            {
                using HistoricalBackend = bcos::scheduler_v1::HistoricalStateBackend<ViewType>;
                HistoricalBackend historicalBackend(view, stateRoot,
                    bcos::ledger::account::nodeAddressTableMode(),
                    /*ethLaneNaming=*/true);
                storage2::View<typename MultiLayerStorage::MutableStorage, void, HistoricalBackend>
                    historicalView(std::addressof(historicalBackend));
                bcos::ledger::account::EVMAccount<decltype(historicalView)> account(historicalView,
                    bcos::ledger::account::FromTableName{},
                    bcos::ledger::account::legacyAppsAccountTableName(addressOwned));
                if (auto nonce = co_await account.nonce())
                {
                    storage::Entry entry;
                    entry.set(*nonce);
                    co_return entry;
                }
                co_return std::nullopt;
            }
        }
        bcos::ledger::account::EVMAccount account(view, bcos::ledger::account::FromTableName{},
            bcos::ledger::account::legacyAppsAccountTableName(addressOwned));
        co_return co_await account.storageEntry(keyOwned);
    }

    /// ledger may be null (execute only). ioServicePool is required (SchedulerSerialImpl GC).
    OpScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::ledger::OpForkSchedule forkSchedule, bcos::protocol::BlockFactory::Ptr blockFactory,
        MultiLayerStorage& multiLayerStorage, bcos::ledger::LedgerInterface::Ptr ledger,
        bcos::IOServicePool::Ptr ioServicePool)
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkSchedule(forkSchedule),
        m_multiLayerStorage(&multiLayerStorage),
        m_blockFactory(std::move(blockFactory)),
        m_ledger(std::move(ledger)),
        m_ioServicePool(std::move(ioServicePool))
    {
        // execute() tolerates a null ledger; admit does not (see coAdmit).
        // Default no-op notifiers. An empty std::function would throw inside the async task.
        m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        m_transactionNotifier = [](bcos::protocol::BlockNumber,
                                    bcos::protocol::TransactionSubmitResultsPtr,
                                    std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

    /// Optional RPC block-number callback; finalizeUpTo invokes it once per block merged into
    /// the backend (admit fires nothing: the ledger has not advanced). Head changes are the
    /// Engine tracker's (OpEngineService::trackedHead()).
    void setBlockNumberNotifier(std::function<void(bcos::protocol::BlockNumber)> notifier)
    {
        if (notifier)  // symmetric with setTransactionNotifier: an empty std::function would
        {              // throw bad_function_call inside the finalize task's try block.
            m_blockNumberNotifier = std::move(notifier);
        }
    }

    /// Optional txpool eviction callback; finalizeUpTo invokes it per merged block.
    void setTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(bcos::Error::Ptr)>)>
            notifier)
    {
        if (notifier)
        {
            m_transactionNotifier = std::move(notifier);
        }
    }

    /// When true, execute() compares buildAndCollect against a full rebuild from the
    /// empty root over a scratch copy of the whole visible flat state.
    /// Defaults off: the equality contract lives in IncrementalMPTRootMatchesFullRebuild.
    void setCrossCheckIncrementalRoot(bool enable) { m_crossCheckIncrementalRoot = enable; }

private:
    // ---- execute / commit ----

    task::Task<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        protocol::Block::Ptr block, bool verify)
    {
        try
        {
            auto blockHeader = block->blockHeader();
            OP_SCHEDULER_LOG(INFO)
                << "Execute block: " << blockHeader->number() << " | " << verify << " | "
                << block->transactionsMetaDataSize() << " | " << block->transactionsSize();
            auto number = blockHeader->number();

            // Resend of a block already executed (staged or in the window): reuse its header.
            auto const announcedBlockHash = bcos::protocol::canonicalBlockHash(*blockHeader);
            if (auto cached = knownExecutedHeader(number, announcedBlockHash))
            {
                co_return {nullptr, std::move(cached), false};
            }

            // One execute at a time. Also take m_commitMutex so pushView / popFrontStorage
            // cannot race mergeBackStorage (reset() already takes all three). NOTE: these
            // std::unique_lock objects span the co_awaits below. executeBlock/commitBlock
            // now use task::syncWait, so the EngineServiceImpl caller does not proceed
            // until this task finishes. If a future awaitable resumes on another thread,
            // these locks must be narrowed or replaced with a coroutine-aware lock.
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = std::string{"Another block is executing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }
            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }

            // Clear any stale slot before executing a probe: a failed probe must not leave
            // the previous build's retained view behind (covers the ledgerGas re-probe too).
            if (!verify)
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                m_lastProbe.reset();
            }

            co_await hydrateFinalized();

            // A block at the finalized height: the finalized tip itself is served without
            // re-execution; anything else at or below that height is a rewind of finalized
            // state, which the OP lane never does (D7).
            auto const finalized = m_finalizedNumber.load();
            if (number > 0 && number <= finalized)
            {
                auto tipView = m_multiLayerStorage->forkCommitted();
                auto const canonicalAtHeight =
                    co_await ledger::getBlockHash(tipView, number, ledger::fromStorage);
                if (number == finalized && canonicalAtHeight.has_value() &&
                    *canonicalAtHeight == announcedBlockHash)
                {
                    OP_SCHEDULER_LOG(INFO)
                        << "Block " << number
                        << " is already the finalized tip; serving without re-execution";
                    auto served = m_blockFactory->blockHeaderFactory()->populateBlockHeader(
                        protocol::BlockHeader::ConstPtr{
                            blockHeader.get(), [](protocol::BlockHeader const*) {}});
                    co_return {nullptr, std::move(served), false};
                }
                auto message = fmt::format(
                    "Block {} is at or below the finalized height {} with a different hash; "
                    "the finalized chain does not rewind",
                    number, finalized);
                OP_SCHEDULER_LOG(WARNING) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }

            // The execution view is the parent's ancestor chain (window layers, oldest first)
            // over the finalized backend, plus a fresh mutable for this block. A sibling
            // branch's layers are not in the chain, so they are invisible here (D1 §8.3).
            auto chain = resolveParentChain(blockHeader->parentInfo().blockHash, number);
            if (!chain.error.empty())
            {
                OP_SCHEDULER_LOG(INFO) << chain.error;
                co_return {BCOS_ERROR_UNIQUE_PTR(
                               scheduler::SchedulerError::InvalidBlockNumber, chain.error),
                    nullptr, false};
            }
            auto view = m_multiLayerStorage->forkChain(chain.layers);
            view.newMutable();

            auto transactions = co_await getTransactions(*block, view);
            if (std::any_of(transactions.begin(), transactions.end(),
                    [](auto const& tx) { return tx == nullptr; }))
            {
                auto message =
                    fmt::format("Not found transactions in txpool for block: {}", number);
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                    nullptr, false};
            }

            auto ledgerConfig = co_await loadLedgerConfig(view, number);

            // Persist trie nodes even for verify=false; adopt reuses this view. The OP lane
            // is scenario B by construction, so this is unconditional.
            bool const persistTrieNodes = true;
            auto outcome =
                co_await execute(view, *blockHeader, transactions, *ledgerConfig, persistTrieNodes);

            // Copy execution commitments onto a header cloned from the announced payload.
            bool sysBlock = false;
            auto executedHeader = co_await finishExecute(
                view, outcome, *blockHeader, *block, transactions, *ledgerConfig, sysBlock);

            // When verify=true, announced header fields must match execution.
            if (verify)
            {
                if (executedHeader->withdrawalsRoot().has_value() !=
                    blockHeader->withdrawalsRoot().has_value())
                {
                    throw bcos::evm::OpConsensusError(
                        "OpScheduler: commitment mismatch on field withdrawalsRoot");
                }
                if (auto mismatch = opEthMismatchedFieldOf(
                        headerCommitments(*executedHeader), headerCommitments(*blockHeader)))
                {
                    throw bcos::evm::OpConsensusError(
                        "OpScheduler: commitment mismatch on field " + *mismatch);
                }
            }

            // Stage only when verify is true. Probe results are returned, not staged.
            if (verify)
            {
                // Ledger rows are generated now, not at admit: a child executing on this
                // block reads its parent header / NUMBER_2_HASH through the chain (D1 §9.3).
                LayerPtr ledgerLayer;
                if (m_ledger)
                {
                    ledgerLayer = co_await buildLedgerLayer(chain.layers, view.m_mutableStorage,
                        block, outcome.result, announcedBlockHash);
                }
                std::lock_guard<std::mutex> lock(m_windowMutex);
                m_staged[announcedBlockHash] = BlockLayer{.number = number,
                    .hash = announcedBlockHash,
                    .parentHash = blockHeader->parentInfo().blockHash,
                    .state = view.m_mutableStorage,
                    .ledger = std::move(ledgerLayer),
                    .executedHeader = executedHeader,
                    .block = std::move(block)};
                m_lastProbe.reset();
            }
            else
            {
                // Keep the probe so adoptProbeAsPending can stage this view.
                std::lock_guard<std::mutex> lock(m_windowMutex);
                m_lastProbe = ProbeSlot{std::move(view), std::move(outcome.result), executedHeader,
                    std::move(chain.layers)};
            }

            co_return {nullptr, std::move(executedHeader), sysBlock};
        }
        catch (std::exception& e)
        {
            auto message =
                fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            auto error = BCOS_ERROR_PTR(classifyException(std::current_exception()), message);
            attachOpRejectInfo(*error, std::current_exception());
            co_return {std::move(error), nullptr, false};
        }
        catch (...)
        {
            auto message = std::string{"Execute block failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            auto error =
                BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message);
            attachOpRejectInfo(*error, std::current_exception());
            co_return {std::move(error), nullptr, false};
        }
    }

    task::Task<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> coAdoptProbe(
        protocol::Block::Ptr block)
    {
        try
        {
            auto blockHeader = block->blockHeader();
            auto const number = blockHeader->number();
            OP_SCHEDULER_LOG(INFO) << "Adopt probe: " << number;

            // Same double-lock as coExecuteBlock: pushView must not race mergeBackStorage.
            std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
            if (!executeLock.owns_lock())
            {
                auto message = std::string{"Another block is executing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }
            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }
            // Take the probe out of the slot; every exit below either stages it or drops it.
            std::optional<ProbeSlot> probe;
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                probe.swap(m_lastProbe);
            }
            if (!probe)
            {
                auto message = std::string{"adoptProbeAsPending: no retained probe"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr, false};
            }
            if (probe->executedHeader->number() != number)
            {
                auto message = fmt::format(
                    "adoptProbeAsPending: retained probe is at height {}, "
                    "adopt input is at height {}",
                    probe->executedHeader->number(), number);
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }
            co_await hydrateFinalized();
            if (number > 0 && number <= m_finalizedNumber.load())
            {
                auto message =
                    std::string{"adoptProbeAsPending: block is at or below the finalized tip"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr, false};
            }

            if (auto mismatch = opEthMismatchedFieldOf(
                    headerCommitments(*probe->executedHeader), headerCommitments(*blockHeader)))
            {
                auto message =
                    fmt::format("adoptProbeAsPending: commitment mismatch on field {}", *mismatch);
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr, false};
            }
            auto const announcedBlockHash = bcos::protocol::canonicalBlockHash(*blockHeader);
            auto const probeHash = bcos::protocol::canonicalBlockHash(*probe->executedHeader);
            if (probeHash != announcedBlockHash)
            {
                auto message = std::string{
                    "adoptProbeAsPending: executed header hash does not match the announced hash"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr, false};
            }
            // The probe ran with a provisional header; the ledger rows must carry the final
            // block (announced header + receipts), so they are generated here, on the same
            // parent chain the probe executed against.
            auto executedHeader = probe->executedHeader;
            LayerPtr ledgerLayer;
            if (m_ledger)
            {
                ledgerLayer = co_await buildLedgerLayer(probe->parentLayers,
                    probe->view.m_mutableStorage, block, probe->result, announcedBlockHash);
            }
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                m_staged[announcedBlockHash] = BlockLayer{.number = number,
                    .hash = announcedBlockHash,
                    .parentHash = blockHeader->parentInfo().blockHash,
                    .state = probe->view.m_mutableStorage,
                    .ledger = std::move(ledgerLayer),
                    .executedHeader = executedHeader,
                    .block = std::move(block)};
            }
            OP_SCHEDULER_LOG(INFO) << "Adopted probe as staged block: " << number;
            co_return {nullptr, std::move(executedHeader), false};
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Adopt probe failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
        catch (...)
        {
            auto message = std::string{"Adopt probe failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr, false};
        }
    }

    /// ADMIT (SchedulerInterface::commitBlock). Moves the staged block whose executed header
    /// is @p header into the window. Nothing is written to the backend.
    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coAdmit(
        protocol::BlockHeader::Ptr header)
    {
        try
        {
            auto const number = header->number();
            OP_SCHEDULER_LOG(INFO) << "Admit block: " << number;

            std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
            if (!commitLock.owns_lock())
            {
                auto message = std::string{"Another block is committing!"};
                OP_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }
            if (!m_ledger)
            {
                auto message = std::string{
                    "OpScheduler: commit requires a ledger (execute-only construction)"};
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }

            // Bind the admit to the exact block that was executed. announcedBlockHash is the
            // CL hash and cannot be recomputed from the executed header (the engine passes the
            // executed header back), so match on the executed header's canonical hash.
            auto const executedHash = bcos::protocol::canonicalBlockHash(*header);
            std::optional<BlockLayer> staged;
            bool alreadyAdmitted = false;
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                for (auto const& [hash, layer] : m_staged)
                {
                    if (layer.number == number &&
                        bcos::protocol::canonicalBlockHash(*layer.executedHeader) == executedHash)
                    {
                        staged = layer;
                        break;
                    }
                }
                if (!staged)
                {
                    for (auto const& [hash, layer] : m_window)
                    {
                        if (layer.number == number && bcos::protocol::canonicalBlockHash(
                                                          *layer.executedHeader) == executedHash)
                        {
                            alreadyAdmitted = true;
                            break;
                        }
                    }
                }
            }
            if (alreadyAdmitted)
            {
                OP_SCHEDULER_LOG(INFO) << "Block " << number << " is already in the window";
                co_return {nullptr, co_await loadCommitLedgerConfig(header)};
            }
            if (!staged)
            {
                // Carries the OpPendingDropped tag on top of the code: the engine may
                // re-execute a payload whose staged block was dropped (reset), and the code
                // alone cannot say so (classifyException's catch-all also reports
                // UnknownError — bcos-framework/engine/Errors.h).
                auto pendingDropped = BCOS_ERROR_UNIQUE_PTR(
                    scheduler::SchedulerError::UnknownError, "Unexpected empty results!");
                *pendingDropped << bcos::engine::OpPendingDropped{true};
                co_return {std::move(pendingDropped), nullptr};
            }
            if (!staged->ledger)
            {
                auto message = fmt::format(
                    "Admit block {}: no ledger rows were generated (executed without a ledger)",
                    number);
                OP_SCHEDULER_LOG(ERROR) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr};
            }

            // The parent must still resolve: a finalize in between may have pruned it.
            co_await hydrateFinalized();
            if (auto chain = resolveParentChain(staged->parentHash, number); !chain.error.empty())
            {
                auto message = fmt::format("Admit block {}: {}", number, chain.error);
                OP_SCHEDULER_LOG(WARNING) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }

            std::size_t depth = 0;
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                m_staged.erase(staged->hash);
                m_window.insert_or_assign(staged->hash, std::move(*staged));
                depth = m_window.size();
            }
            auto ledgerConfig = co_await loadCommitLedgerConfig(header);
            OP_SCHEDULER_LOG(INFO)
                << "Admit block finished: " << number << LOG_KV("windowBlocks", depth)
                << LOG_KV("finalized", m_finalizedNumber.load());
            co_return {nullptr, ledgerConfig};
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Admit block failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
        catch (...)
        {
            auto message = std::string{"Admit block failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message),
                nullptr};
        }
    }

    /// FINALIZE (D1 §12.2). Block-atomic, not chain-atomic: a failure between two merges
    /// leaves the backend at the last merged block with the window still consistent, and the
    /// next FCU(finalized=F) resumes from there.
    task::Task<Error::Ptr> coFinalizeUpTo(bcos::crypto::HashType target)
    {
        try
        {
            // Blocking: an FCU waits for an in-flight admit rather than failing; an execute
            // arriving meanwhile fails closed on its try-lock ("Another block is committing").
            std::unique_lock commitLock(m_commitMutex);
            if (!m_ledger)
            {
                co_return BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                    "OpScheduler: finalize requires a ledger (execute-only construction)");
            }
            co_await hydrateFinalized();

            std::vector<BlockLayer> chain;  // oldest first after the reverse below
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                if (target != bcos::crypto::HashType{} && target == m_finalizedHash)
                {
                    // Already the finalized tip. Prune anyway: a previous run may have merged
                    // the last block and then failed in the notifier before pruning.
                    pruneWindowLocked(target);
                    co_return nullptr;
                }
                auto cursor = target;
                for (auto it = m_window.find(cursor); it != m_window.end();
                    it = m_window.find(cursor))
                {
                    chain.push_back(it->second);
                    cursor = it->second.parentHash;
                }
                if (chain.empty())
                {
                    co_return BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                        fmt::format("finalizeUpTo: block {} is not in the unfinalized window",
                            target.abridged()));
                }
                if (!isFinalizedTipLocked(cursor, chain.back().number))
                {
                    co_return BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                        fmt::format("finalizeUpTo: block {} does not descend from the finalized "
                                    "tip {} (chain breaks at parent {})",
                            target.abridged(), m_finalizedHash.abridged(), cursor.abridged()));
                }
                std::reverse(chain.begin(), chain.end());
            }

            for (auto& block : chain)
            {
                // One merge per block: state + ledger rows land together.
                co_await m_multiLayerStorage->mergeToBackends(*block.state, *block.ledger);
                {
                    std::lock_guard<std::mutex> lock(m_windowMutex);
                    m_window.erase(block.hash);
                    m_finalizedNumber.store(block.number);
                    m_finalizedHash = block.hash;
                }
                OP_SCHEDULER_LOG(INFO)
                    << "Finalized block " << block.number << " " << block.hash.abridged();
                notifyBlockNumber(block.number);
            }

            std::size_t pruned = 0;
            std::size_t remaining = 0;
            {
                std::lock_guard<std::mutex> lock(m_windowMutex);
                pruned = pruneWindowLocked(target);
                remaining = m_window.size();
            }
            OP_SCHEDULER_LOG(INFO)
                << "Finalize finished" << LOG_KV("finalized", chain.back().number)
                << LOG_KV("merged", chain.size()) << LOG_KV("pruned", pruned)
                << LOG_KV("windowBlocks", remaining);
            co_return nullptr;
        }
        catch (std::exception& e)
        {
            auto message = fmt::format("Finalize failed! {}", boost::diagnostic_information(e));
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message);
        }
        catch (...)
        {
            auto message = std::string{"Finalize failed! ("} +
                           describeException(std::current_exception()) + ")";
            OP_SCHEDULER_LOG(ERROR) << message;
            co_return BCOS_ERROR_UNIQUE_PTR(classifyException(std::current_exception()), message);
        }
    }

    /// Drop every window block that does not descend from @p root, and every staged block
    /// whose parent is no longer resolvable. Caller holds m_windowMutex.
    std::size_t pruneWindowLocked(bcos::crypto::HashType const& root)
    {
        std::unordered_multimap<bcos::crypto::HashType, bcos::crypto::HashType> children;
        for (auto const& [hash, layer] : m_window)
        {
            children.emplace(layer.parentHash, hash);
        }
        std::unordered_set<bcos::crypto::HashType> keep;
        std::vector<bcos::crypto::HashType> stack{root};
        while (!stack.empty())
        {
            auto parent = stack.back();
            stack.pop_back();
            auto [begin, end] = children.equal_range(parent);
            for (auto it = begin; it != end; ++it)
            {
                if (keep.insert(it->second).second)
                {
                    stack.push_back(it->second);
                }
            }
        }
        std::size_t pruned = 0;
        for (auto it = m_window.begin(); it != m_window.end();)
        {
            if (keep.contains(it->first))
            {
                ++it;
                continue;
            }
            OP_SCHEDULER_LOG(INFO)
                << "Pruned side-branch block " << it->second.number << " " << it->first.abridged();
            it = m_window.erase(it);
            ++pruned;
        }
        for (auto it = m_staged.begin(); it != m_staged.end();)
        {
            bool const parentOk =
                m_window.contains(it->second.parentHash) || it->second.parentHash == root;
            it = parentOk ? std::next(it) : m_staged.erase(it);
        }
        return pruned;
    }

    /// True when @p hash is the finalized tip a chain may sit on. When the ledger has no
    /// SYS_NUMBER_2_HASH row for the tip (m_finalizedHash zero) or nothing is on disk yet, the
    /// hash cannot be checked and continuity falls back to the height (@p oldestNumber ==
    /// finalized + 1) — the same leniency the pre-window scheduler had. Caller holds
    /// m_windowMutex.
    bool isFinalizedTipLocked(
        bcos::crypto::HashType const& hash, protocol::BlockNumber oldestNumber) const
    {
        auto const finalized = m_finalizedNumber.load();
        if (finalized == -1)
        {
            return true;
        }
        if (m_finalizedHash != bcos::crypto::HashType{})
        {
            return hash == m_finalizedHash;
        }
        return oldestNumber == finalized + 1;
    }

    /// Ancestor layers of @p parentHash, oldest first, walking the window down to the
    /// finalized tip. Requires hydrateFinalized() to have run.
    ParentChain resolveParentChain(
        bcos::crypto::HashType const& parentHash, protocol::BlockNumber number)
    {
        ParentChain out;
        std::lock_guard<std::mutex> lock(m_windowMutex);
        std::vector<BlockLayer const*> ancestors;  // newest first
        auto cursor = parentHash;
        auto oldestNumber = number;
        for (auto it = m_window.find(cursor); it != m_window.end(); it = m_window.find(cursor))
        {
            ancestors.push_back(&it->second);
            oldestNumber = it->second.number;
            cursor = it->second.parentHash;
        }
        if (!isFinalizedTipLocked(cursor, oldestNumber))
        {
            out.error = fmt::format(
                "Block {}: parent {} is neither in the unfinalized window nor the finalized tip "
                "{} (finalized height {})",
                number, parentHash.abridged(), m_finalizedHash.abridged(),
                m_finalizedNumber.load());
            return out;
        }
        out.layers.reserve(ancestors.size() * 2);
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
        {
            out.layers.push_back((*it)->state);
            out.layers.push_back((*it)->ledger);
        }
        return out;
    }

    /// Layers of @p layer's chain INCLUDING the block itself, oldest first. Caller holds
    /// m_windowMutex.
    std::vector<LayerPtr> chainLayersLocked(BlockLayer const& layer) const
    {
        std::vector<BlockLayer const*> ancestors{&layer};
        auto cursor = layer.parentHash;
        for (auto it = m_window.find(cursor); it != m_window.end(); it = m_window.find(cursor))
        {
            ancestors.push_back(&it->second);
            cursor = it->second.parentHash;
        }
        std::vector<LayerPtr> layers;
        layers.reserve(ancestors.size() * 2);
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
        {
            layers.push_back((*it)->state);
            layers.push_back((*it)->ledger);
        }
        return layers;
    }

    /// The executed header of a staged or admitted block with this height and announced hash.
    protocol::BlockHeader::Ptr knownExecutedHeader(
        protocol::BlockNumber number, bcos::crypto::HashType const& announcedBlockHash)
    {
        std::lock_guard<std::mutex> lock(m_windowMutex);
        for (auto const* table : {&m_staged, &m_window})
        {
            if (auto it = table->find(announcedBlockHash);
                it != table->end() && it->second.number == number)
            {
                OP_SCHEDULER_LOG(INFO) << "Block " << number << " has been executed, return "
                                       << "result directly";
                return it->second.executedHeader;
            }
        }
        return nullptr;
    }

    /// OP blocks carry txs inline.
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// preBlockOpEthSteps → serial per-tx → finalizeOpEthBlockResult.
    /// persistTrieNodes: persist incremental MPT nodes when number > 0.
    task::Task<ExecuteOutcome> execute(ViewType& view, protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig, bool persistTrieNodes)
    {
        namespace detail = bcos::evm::engine::detail;

        // Views into each tx envelope; transactions outlive this vector.
        std::vector<bcos::bytesConstRef> rawTxBytes;
        rawTxBytes.reserve(transactions.size());
        for (auto const& tx : transactions)
        {
            rawTxBytes.emplace_back(tx->extraTransactionBytes());
        }

        OpEthExecuteBlockResult result;

        // Assigned inside the try; the catch ladder below reclassifies a poisoned slot as a
        // storage fault even when the escaping exception is not std::exception-matching
        // (wedprcrypto's corrupted typed-catch; the OpStorageErrorGuard.h contract).
        std::shared_ptr<OpStorageErrorSlot> sharedError;
        auto rethrowStorageFaultIfPoisoned = [&sharedError]() {
            if (sharedError && sharedError->poisoned())
                throw bcos::evm::engine::OpStorageError(
                    "OpScheduler: block state read fault (poisoned): " +
                    sharedError->firstErrorMessage());
        };
        try
        {
            // The block being executed decides its own fork (op-node keys IsJovian/IsKarst on
            // the L2 block's own timestamp); opForkTimestampSec is the single ms->s conversion.
            const auto spec = opForkSpecAt(m_forkSchedule, opForkTimestampSec(header.timestamp()));

            // Split deposits from other typed envelopes.
            std::vector<DepositTx> deposits;
            deposits.reserve(rawTxBytes.size());
            for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            {
                auto const& raw = rawTxBytes[i];
                if (raw.empty())  // empty envelope: raw[0] would be out of bounds
                {
                    throw bcos::evm::OpConsensusError(
                        "OpScheduler: empty envelope", transactions[i]->hash());
                }
                auto const typeByte = raw[0];
                if (opEthClassifyTxType(typeByte) == OP_DEPOSIT_TX_TYPE)
                {
                    try
                    {
                        deposits.push_back(decodeOpDepositEnvelope(raw));
                    }
                    catch (const OpEthDepositValidationFailed& e)
                    {
                        throw bcos::evm::OpConsensusError(
                            std::string("OpScheduler: malformed deposit: ") + e.what(),
                            transactions[i]->hash());
                    }
                }
                // Reject blob (0x03) and 0x7d type bytes.
                else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 &&
                         typeByte != 0x04)
                {
                    throw bcos::evm::OpConsensusError(
                        fmt::format("OpScheduler: unsupported tx type byte 0x{:02x}",
                            static_cast<unsigned>(typeByte)),
                        transactions[i]->hash());
                }
            }

            bcos::ledger::LedgerConfig execLedgerConfig;
            execLedgerConfig.setEVMCRevision(spec.rev);

            sharedError = std::make_shared<OpStorageErrorSlot>();
            OpEthExecutor executor(m_receiptFactory, spec, sharedError);

            // Block-start system call, deposit-first check, Jovian shape, DA scalar.
            std::optional<std::string> hashErr;
            std::optional<uint16_t> daFootprintGasScalar;
            std::optional<OpRecentBlockHashes<ViewType>> hashes;
            co_await preBlockOpEthSteps(view, header, spec, rawTxBytes, deposits, executor.vm(),
                sharedError, hashes, hashErr, daFootprintGasScalar);

            // Fee params load on the first normal tx. blockGasLeft is narrowed from gasLimit.
            // BLOCKHASH answers come from the per-block OpRecentBlockHashes (op-geth GetHashFn
            // semantics), wrapped into the new layer's BlockHashLookup function.
            OpEthBlockContext ctx{.fee = {},
                .blockGasLeft =
                    detail::narrowU256ToI64(header.gasLimit(), "OpScheduler blockGasLeft"),
                .blockHashLookup = opEthBlockHashLookup(*hashes),
                .chainId = m_chainId,
                .daFootprintGasScalar = daFootprintGasScalar};

            // Linear per-tx loop (serial=true, chunk size 1).
            bcos::scheduler_v1::SchedulerSerialImpl serialScheduler(
                m_ioServicePool, /*chunkSize=*/1, /*serial=*/true);
            auto transactionsRefs =
                transactions |
                ::ranges::views::transform(
                    [](protocol::Transaction::ConstPtr const& ptr) -> protocol::Transaction const& {
                        return *ptr;
                    });
            auto receipts = co_await serialScheduler.executeBlock(
                view, executor, header, transactionsRefs, execLedgerConfig, ctx);

            // Finalize receipts/seal. incrementalRoot skips finalize's own full rebuild.
            // The incremental vs full equality contract is IncrementalMPTRootMatchesFullRebuild;
            // an optional debug cross-check (default off) can still run a full rebuild here.
            bool const incrementalRoot = persistTrieNodes && header.number() > 0;
            result = co_await finalizeOpEthBlockResult(view, header, execLedgerConfig, spec,
                sharedError, receipts, rawTxBytes, ctx.cumulativeGasUsed, hashErr, incrementalRoot);

            // Persist this block's trie nodes. Parent nodes must already exist; otherwise
            // MPTInvariantViolation (do not rebuild from an empty trie).
            if (incrementalRoot)
            {
                bcos::scheduler_v1::ViewNodeStorage<ViewType> nodeStorage(view);
                auto parentBlock = co_await ledger::getBlockData(
                    view, header.number() - 1, ledger::HEADER, *m_blockFactory);
                auto const parentRoot = parentBlock->blockHeader()->stateRoot();
                try
                {
                    auto delta = co_await ledger::mpt::buildAndCollect(nodeStorage, parentRoot,
                        view, /*l2Mode=*/true, bcos::ledger::account::nodeAddressTableMode());
                    if (m_crossCheckIncrementalRoot)
                    {
                        // Full-rebuild cross-check on the bcos-evm-free layer (the retired
                        // adapter stateRootOf/Storage2State whole-state traversal's successor):
                        // re-materialize every flat row visible through the execution view into
                        // a scratch mutable layer, then rebuild the MPT from the EMPTY root over
                        // it. computeMptStateRoot cannot substitute: it is incremental-from-
                        // parent, and from emptyRootHash over the block view it treats every
                        // account as first-touch — cold flat slots of pre-existing accounts
                        // never reach the trie. The copy skips DELETED tombstones (a deleted
                        // row IS absent state for an empty-parent rebuild); /mpt/ and /sys/
                        // rows land in the scratch layer but the builder's account-table
                        // classification skips them, and the new nodes go to a throwaway
                        // in-memory node storage. Fail-loud: any fault leaves as
                        // OpStorageError, same classification as the legacy poison check.
                        bcos::h256 fullRoot;
                        try
                        {
                            auto scratch = m_multiLayerStorage->fork();
                            scratch.newMutable();
                            auto it = co_await storage2::range(view);
                            while (auto keyValue = co_await it.next())
                            {
                                auto const& [k, v] = *keyValue;
                                if (auto const* entry =
                                        std::get_if<bcos::storage::Entry>(std::addressof(v)))
                                {
                                    co_await storage2::writeOne(scratch, k, *entry);
                                }
                            }
                            bcos::storage2::memory_storage::MemoryStorage<bcos::h256, bcos::bytes>
                                fullNodeStorage;
                            auto fullDelta = co_await ledger::mpt::buildAndCollect(fullNodeStorage,
                                ledger::mpt::emptyRootHash(), scratch,
                                /*l2Mode=*/true, bcos::ledger::account::nodeAddressTableMode());
                            fullRoot = fullDelta.stateRoot;
                        }
                        catch (const std::exception& e)
                        {
                            throw bcos::evm::engine::OpStorageError(
                                fmt::format("OpScheduler: full rebuild failed at block {}: {}",
                                    header.number(), e.what()));
                        }
                        if (delta.stateRoot != fullRoot)
                        {
                            throw bcos::evm::engine::OpStorageError(fmt::format(
                                "OpScheduler: incremental MPT root diverged from the full rebuild "
                                "at block {} (incremental={}, full={}, parent={})",
                                header.number(), delta.stateRoot.hexPrefixed(),
                                fullRoot.hexPrefixed(), parentRoot.hexPrefixed()));
                        }
                    }
                    result.stateRoot = delta.stateRoot;
                }
                catch (const bcos::ledger::mpt::MPTInvariantViolation& e)
                {
                    // Missing parent trie nodes: fail rather than rebuild from an empty trie.
                    throw bcos::evm::engine::OpStorageError(fmt::format(
                        "OpScheduler: incremental MPT build at block {} failed — parent block "
                        "{}'s state root {} has no persisted trie nodes (the OP lane builds the "
                        "complete MPT from genesis): {}",
                        header.number(), header.number() - 1, parentRoot.hex(), e.what()));
                }
            }
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            // A poisoned slot is a storage fault even when validation wrapped it as consensus:
            // EthereumState reads are noexcept and swallow the fault into the shared slot while
            // returning defaults, so a missing/corrupt trie row under the tx sender surfaces as
            // an insufficient-funds-style OpConsensusError (ExecuteContext::prepare wraps
            // validate failures with no slot check). Same check coCallOnView runs for every
            // exception type on the eth_call path.
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            throw;
        }
        catch (const std::exception&)
        {
            rethrowStorageFaultIfPoisoned();
            throw;
        }
        catch (...)
        {
            rethrowStorageFaultIfPoisoned();
            throw bcos::evm::OpConsensusError(
                "OpScheduler: execute threw an unrecognized (non-std::exception) object; "
                "raw tx decode or block-level consensus fault");
        }

        // Commit keys on the announced payload hash; finishExecute() mirrors announced
        // metadata + execution commitments onto executedHeader, which stays out of this identity.
        bcos::crypto::HashType announcedBlockHash = bcos::protocol::canonicalBlockHash(header);
        co_return ExecuteOutcome{std::move(result), announcedBlockHash};
    }

    /// Copy execution commitments onto a clone of the announced header.
    task::Task<protocol::BlockHeader::Ptr> finishExecute(ViewType& /*view*/,
        ExecuteOutcome const& outcome, protocol::BlockHeader const& blockHeader,
        protocol::Block& /*block*/,
        std::vector<protocol::Transaction::ConstPtr> const&
        /*transactions*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool& sysBlock)
    {
        sysBlock = false;
        auto const& opResult = outcome.result;

        auto executedBlockHeader = m_blockFactory->blockHeaderFactory()->populateBlockHeader(
            protocol::BlockHeader::ConstPtr{&blockHeader, [](protocol::BlockHeader const*) {}});
        // populateBlockHeader mirrors the 13 framework fields only; the six Ethereum metadata
        // fields are not in its copy set, so mirror them here (identity, not just commitments).
        // Optional fields follow announced presence — never invent a value the payload lacks.
        executedBlockHeader->setCoinbase(blockHeader.coinbase());
        executedBlockHeader->setGasLimit(blockHeader.gasLimit());
        executedBlockHeader->setPrevRandao(blockHeader.prevRandao());
        if (blockHeader.baseFee())
            executedBlockHeader->setBaseFee(*blockHeader.baseFee());
        if (blockHeader.excessBlobGas())
            executedBlockHeader->setExcessBlobGas(*blockHeader.excessBlobGas());
        if (blockHeader.parentBeaconBlockRoot())
            executedBlockHeader->setParentBeaconBlockRoot(*blockHeader.parentBeaconBlockRoot());
        executedBlockHeader->setStateRoot(opResult.stateRoot);
        executedBlockHeader->setTxsRoot(opResult.txRoot);
        executedBlockHeader->setReceiptsRoot(opResult.seal.receiptsRoot);
        executedBlockHeader->setGasUsed(bcos::u256(opResult.gasUsed));
        auto const& bloom = opResult.seal.logsBloom;
        executedBlockHeader->setLogsBloom(bcos::bytesConstRef(bloom.data(), bloom.size()));
        // Optional seal fields follow seal presence (never invent a value the fork's header
        // shape lacks): pre-Canyon seals carry no withdrawalsRoot, so the executed header
        // — cloned from the announced header, itself field-less pre-Canyon — keeps it unset.
        if (opResult.seal.withdrawalsRoot.has_value())
            executedBlockHeader->setWithdrawalsRoot(*opResult.seal.withdrawalsRoot);
        if (opResult.seal.requestsHash.has_value())
            executedBlockHeader->setRequestsHash(*opResult.seal.requestsHash);
        // blobGasUsed is engaged from Ecotone on (0 through Isthmus), so the announced-value
        // fallback below is only reachable for a pre-Ecotone announcement — never valid, but
        // kept as the fail-closed arm.
        if (opResult.seal.blobGasUsed.has_value())
            executedBlockHeader->setBlobGasUsed(bcos::u256(*opResult.seal.blobGasUsed));
        else if (auto const announced = blockHeader.blobGasUsed())
        {
            // Seal omitted blobGasUsed (pre-Jovian): the OP spec fixes the field at 0 here, so a
            // non-zero announcement is an invalid payload. Reject it instead of copying — the
            // copy feeds headerCommitments, and comparing the announced value against its own
            // copy makes that verify arm self-referential (always pass).
            if (*announced != bcos::u256{0})
                throw bcos::evm::OpConsensusError(
                    "OpScheduler: pre-Jovian payload must announce blobGasUsed=0");
            // Spec-valid zero: keep the identity back-fill so the header's shape stays faithful.
            executedBlockHeader->setBlobGasUsed(*announced);
        }
        co_return executedBlockHeader;
    }

    /// This block's ledger rows as its own layer (D1 §9.3): receipts attached to the block,
    /// prewriteBlockToBuffer(announcedHash, writeNonces=false) written on top of the parent
    /// chain + @p stateLayer so number-keyed rows of a sibling never collide, then the two
    /// SYS_CURRENT_STATE totals recomputed from the PARENT CHAIN. Ledger::asyncPrewriteBlock
    /// derives them from the ledger's own state storage, i.e. the finalized backend, which
    /// under a window is stale by every unfinalized ancestor; reading the parent's rows through
    /// the chain view keeps each branch's running totals correct by induction.
    task::Task<LayerPtr> buildLedgerLayer(std::vector<LayerPtr> parentLayers,
        LayerPtr const& stateLayer, protocol::Block::Ptr const& block,
        OpEthExecuteBlockResult const& result, bcos::crypto::HashType const& announcedBlockHash)
    {
        // Receipt count must equal tx count.
        if (result.receipts.size() != block->transactionsSize())
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a receipt count differing "
                                      "from the transaction count"});

        parentLayers.push_back(stateLayer);
        auto view = m_multiLayerStorage->forkChain(std::move(parentLayers));
        auto readTotal = [&view](std::string_view key) -> task::Task<int64_t> {
            auto entry = co_await storage2::readOne(
                view, executor_v1::StateKeyView{ledger::SYS_CURRENT_STATE, key});
            if (!entry)
            {
                co_return 0;
            }
            co_return boost::lexical_cast<int64_t>(std::string(entry->get()));
        };
        auto const parentTotal = co_await readTotal(ledger::SYS_KEY_TOTAL_TRANSACTION_COUNT);
        auto const parentFailed = co_await readTotal(ledger::SYS_KEY_TOTAL_FAILED_TRANSACTION);
        view.newMutable();

        // Idempotency: a re-run re-appends the same result; clear first.
        block->clearReceipts();
        int64_t failedCount = 0;
        for (auto const& receipt : result.receipts)
        {
            block->appendReceipt(receipt);
            if (receipt->status() != 0)
            {
                ++failedCount;
            }
        }
        // Only toShared() copies should carry setStoreToBackend; a side-branch tx must never
        // be marked as persisted (D1 §9.3).
        for (auto const& tx : block->transactions())
            tx->setStoreToBackend(false);

        // toShared() is required for ConstPtr.
        auto blockTxs = std::make_shared<protocol::ConstTransactions>(
            block->transactions() | ::ranges::views::transform([](auto tx) {
                return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
            }) |
            ::ranges::to<std::vector>());

        co_await bcos::ledger::prewriteBlockToBuffer(
            *m_ledger, blockTxs, block, view, announcedBlockHash, /*writeNonces=*/false);

        auto writeTotal = [&view](std::string_view key, int64_t value) -> task::Task<void> {
            storage::Entry entry;
            entry.set(boost::lexical_cast<std::string>(value));
            co_await storage2::writeOne(view,
                executor_v1::StateKey{ledger::SYS_CURRENT_STATE, std::string(key)},
                std::move(entry));
        };
        co_await writeTotal(ledger::SYS_KEY_TOTAL_TRANSACTION_COUNT,
            parentTotal + static_cast<int64_t>(result.receipts.size()));
        if (failedCount != 0)
        {
            co_await writeTotal(
                ledger::SYS_KEY_TOTAL_FAILED_TRANSACTION, parentFailed + failedCount);
        }
        co_return view.m_mutableStorage;
    }

    /// Load the finalized tip (height + hash) from the backend once (restart, D1 §13.2). The
    /// hash stays zero when the ledger has no SYS_NUMBER_2_HASH row for the tip; the chain
    /// checks then fall back to height continuity (isFinalizedTipLocked).
    task::Task<void> hydrateFinalized()
    {
        if (m_finalizedNumber.load() != -1)
        {
            co_return;
        }
        auto view = m_multiLayerStorage->forkCommitted();
        auto const tip =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        if (tip == -1)
        {
            co_return;
        }
        auto const hash = co_await ledger::getBlockHash(view, tip, ledger::fromStorage);
        std::lock_guard<std::mutex> lock(m_windowMutex);
        if (m_finalizedNumber.load() == -1)
        {
            m_finalizedNumber.store(tip);
            m_finalizedHash = hash.value_or(bcos::crypto::HashType{});
        }
    }

    /// Invoke the installed notifiers (ctor defaults are no-ops).
    void notifyBlockNumber(protocol::BlockNumber number)
    {
        m_blockNumberNotifier(number);
        m_transactionNotifier(number, std::make_shared<bcos::protocol::TransactionSubmitResults>(),
            [](const Error::Ptr&) {});
    }

    /// Build LedgerConfig without header.hash() (throws on an OP header). The executor_version
    /// is pinned to the OP lane: this scheduler only runs on executor_version >=
    /// OPSTACK_EXECUTOR_VERSION chains, and the consumers that branch on the lane
    /// (computeMptStateDelta's l2Mode, shouldBuildMPT) must see it.
    task::Task<ledger::LedgerConfig::Ptr> loadLedgerConfig(
        ViewType& view, protocol::BlockNumber number)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(number);
        ledgerConfig->setExecutorVersion(ledger::OPSTACK_EXECUTOR_VERSION);
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        ledgerConfig->setFeatures(features);
        co_return ledgerConfig;
    }

    /// Commit-path LedgerConfig: number + timestamp only, and never published anywhere.
    /// The admission holder is republished from the LEDGER by the notifier this scheduler fires
    /// after every commit (engine/bcos-engine/OpLedgerConfigRepublish.h) — publishing THIS object
    /// instead would refuse every EIP-155 envelope from the first committed block on (-32602),
    /// because chainId()/features()/executorVersion() are empty here.
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

    /// Recover culprit / capacity tags via exception_ptr. The surrounding catch sites
    /// only see std::exception (catch (std::exception&) / catch (...)); rethrow from the
    /// exception_ptr to recover OpConsensusError and its structured fields.
    static void attachOpRejectInfo(Error& error, std::exception_ptr eptr)
    {
        if (!eptr)
        {
            return;
        }
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::OpConsensusError& opErr)
        {
            if (opErr.txHash.has_value())
            {
                error << bcos::engine::OpCulpritTxHash(*opErr.txHash);
            }
            if (opErr.capacity)
            {
                error << bcos::engine::OpRejectIsCapacity{true};
            }
            if (opErr.validateErrorCode)
            {
                error << bcos::engine::OpValidateErrorCode{opErr.validateErrorCode};
            }
        }
        catch (...)
        {}
    }

public:
    /// Stable RPC reason text for a classified scheduler code.
    static constexpr std::string_view rpcSafeReason(scheduler::SchedulerError code) noexcept
    {
        return code == scheduler::SchedulerError::OpStorageFault      ? "storage fault" :
               code == scheduler::SchedulerError::OpConsensusRejected ? "consensus rejection" :
                                                                        "internal error";
    }

    /// Map OP / MPT exceptions to SchedulerError. Raw MPT faults are storage faults.
    scheduler::SchedulerError classifyException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            return scheduler::SchedulerError::OpConsensusRejected;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (const bcos::ledger::mpt::MPTInvariantViolation&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (const bcos::ledger::mpt::MPTDecodeError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (...)
        {
            return scheduler::SchedulerError::UnknownError;
        }
    }

    /// Recover what() from known exception types; unknown throws stay generic.
    std::string describeException(std::exception_ptr eptr) const
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::OpConsensusError& e)
        {
            return e.what();
        }
        catch (const bcos::evm::engine::OpStorageError& e)
        {
            return e.what();
        }
        catch (const bcos::ledger::mpt::MPTInvariantViolation& e)
        {
            return e.what();
        }
        catch (const bcos::ledger::mpt::MPTDecodeError& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unclassified exception (not derived from std::exception)";
        }
    }

private:
    /// Commitment fields used to compare executed vs announced headers.
    static OpEthBlockCommitments headerCommitments(protocol::BlockHeader const& h)
    {
        namespace detail = bcos::evm::engine::detail;
        auto bloom = h.logsBloom();
        bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
        std::optional<uint64_t> blobGasUsed;
        if (auto bg = h.blobGasUsed())
            blobGasUsed = detail::narrowU256ToU64(*bg, "headerCommitments blobGasUsed");
        return OpEthBlockCommitments{
            .receiptsRoot = h.receiptsRoot(),
            .logsBloom = logsBloom,
            // Pass the header's optional through: pre-Canyon headers carry no withdrawalsRoot,
            // and presence asymmetry is a first-class mismatch in mismatchedFieldOf.
            .withdrawalsRoot = h.withdrawalsRoot(),
            .stateRoot = h.stateRoot(),
            .gasUsed = h.gasUsed(),
            .txRoot = h.txsRoot(),
            .blobGasUsed = blobGasUsed,
            .requestsHash = h.requestsHash(),
        };
    }

    /// Strict hex-address parse for getCode/getABI.
    static evmc_address parseAddress(std::string_view view)
    {
        evmc_address out{};
        if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
            view.remove_prefix(2);
        if (view.size() != sizeof(out.bytes) * 2)
            throw std::invalid_argument("OpScheduler: invalid address (need 40 hex chars)");
        boost::algorithm::unhex(view.begin(), view.end(), out.bytes);
        return out;
    }

    /// Dry-run one eth_call on @p view. errTag prefixes errors.
    template <class AnyView>
    task::Task<protocol::TransactionReceipt::Ptr> coCallOnView(AnyView& view,
        protocol::BlockHeader const& header, protocol::Transaction const& transaction,
        bcos::ledger::LedgerConfig const& ledgerConfig, std::string_view errTag)
    {
        namespace detail = bcos::evm::engine::detail;

        // The block the call is evaluated AGAINST decides the fork.
        const auto spec = opForkSpecAt(m_forkSchedule, opForkTimestampSec(header.timestamp()));
        // Fee params from the L1Block slots; a read fault here is a storage
        // fault (-32603), never a consensus reject (the try/catch below — the
        // retired adapter layer's poison check on loadOpFeeParams).
        OpFeeParams fee;
        try
        {
            fee = co_await loadOpFeeParamsAsync(view);
        }
        catch (const std::exception& e)
        {
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} fee-param read fault: {}", errTag, e.what()));
        }
        catch (...)
        {
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} fee-param read fault: unknown exception", errTag));
        }
        const auto blockGasLeft =
            detail::narrowU256ToI64(header.gasLimit(), "OpScheduler blockGasLeft");

        std::optional<std::string> hashErr;
        OpRecentBlockHashes<AnyView> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // One executor (and one evmc::VM) per call.
        OpEthExecutor executor(m_receiptFactory, spec);

        protocol::TransactionReceipt::Ptr receipt;
        try
        {
            receipt = co_await executor.executeTransaction(view, header, transaction,
                /*contextID=*/0, ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId,
                opEthBlockHashLookup(hashes));
        }
        catch (...)
        {
            // A poisoned slot is a storage fault even if validation wrapped it as consensus
            // (missing inner node → get_account returns nullopt → insufficient funds).
            if (executor.opErrorSlot()->poisoned())
                throw bcos::evm::engine::OpStorageError(
                    fmt::format("OpScheduler: {} state read fault: {}", errTag,
                        executor.opErrorSlot()->firstErrorMessage()));
            throw;
        }

        // Fail if the executor reported a storage read fault.
        if (executor.opErrorSlot()->poisoned())
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} state read fault: {}", errTag,
                    executor.opErrorSlot()->firstErrorMessage()));
        if (hashErr.has_value())
            throw bcos::evm::engine::OpStorageError(
                fmt::format("OpScheduler: {} block-hash lookup failed: {}", errTag, *hashErr));
        co_return receipt;
    }

    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        // The head chain's current number: its window layers carry SYS_CURRENT_STATE
        // (prewriteBlockToBuffer), so this is the head height, the backend tip when no head
        // is tracked.
        auto view = co_await headView();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);

        // Scenario B by construction (the OP lane builds the complete MPT from genesis):
        // balances live in MPT only. The flat plane has no ACCOUNT_BALANCE rows, so route
        // latest eth_call / estimateGas through the same MPT view as historical calls.
        auto [err, receipt] = co_await coCallAtBlock(std::move(transaction), blockNumber);
        if (err)
        {
            BOOST_THROW_EXCEPTION(*err);
        }
        co_return receipt;
    }

    /// eth_call against the MPT at @p blockNumber on the canonical head's chain (headView: the
    /// window layers stack the head chain's trie nodes and ledger rows over the backend, so a
    /// finalized height reads exactly the committed plane and an unfinalized one its own
    /// branch's). Refusals return Error, not throw.
    task::Task<std::tuple<Error::Ptr, protocol::TransactionReceipt::Ptr>> coCallAtBlock(
        protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber)
    {
        auto latestView = co_await headView();
        auto latestNumber =
            co_await bcos::ledger::getCurrentBlockNumber(latestView, bcos::ledger::fromStorage);
        // Negative or beyond-latest: InvalidBlockNumber.
        if (blockNumber < 0 || blockNumber > latestNumber)
        {
            co_return std::tuple{
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                    fmt::format("eth_call: block {} does not exist (latest: {})", blockNumber,
                        latestNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }

        auto block = co_await bcos::ledger::getBlockData(
            latestView, blockNumber, bcos::ledger::HEADER, *m_blockFactory);
        // blockHeader() returns a shared_ptr by value; keep it alive.
        auto blockHeader = block->blockHeader();
        auto const& header = *blockHeader;
        auto const stateRoot = header.stateRoot();
        if (stateRoot == bcos::crypto::HashType{})
        {
            co_return std::tuple{BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                                     fmt::format("eth_call: no MPT state root recorded in block "
                                                 "{}'s header",
                                         blockNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }
        // Empty root needs no nodes; any other missing root is an error.
        if (stateRoot != bcos::ledger::mpt::emptyRootHash() &&
            !co_await storage2::existsOne(latestView, storage2::mptNodeStateKey(stateRoot)))
        {
            co_return std::tuple{
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                    fmt::format("eth_call: block {}'s state root has no persisted MPT nodes "
                                "(trie-node persistence was not yet active at that height)",
                        blockNumber)),
                protocol::TransactionReceipt::Ptr{nullptr}};
        }

        // The block the call is evaluated AGAINST decides the fork.
        const auto spec = opForkSpecAt(m_forkSchedule, opForkTimestampSec(header.timestamp()));

        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(blockNumber);
        ledgerConfig->setTimestamp(header.timestamp());
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, latestView, blockNumber);
        ledgerConfig->setFeatures(features);
        ledgerConfig->setEVMCRevision(spec.rev);

        // Fresh mutable layer over the historical MPT; call writes are not persisted.
        using HistoricalBackend = bcos::scheduler_v1::HistoricalStateBackend<ViewType>;
        HistoricalBackend historicalBackend(latestView, stateRoot,
            bcos::ledger::account::nodeAddressTableMode(), /*ethLaneNaming=*/true);
        storage2::View<typename MultiLayerStorage::MutableStorage, void, HistoricalBackend>
            historicalView(std::addressof(historicalBackend));
        historicalView.newMutable();
        co_return std::tuple{Error::Ptr{nullptr}, co_await coCallOnView(historicalView, header,
                                                      *transaction, *ledgerConfig, "historical")};
    }

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::ledger::OpForkSchedule m_forkSchedule;

    MultiLayerStorage* m_multiLayerStorage = nullptr;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::ledger::LedgerInterface::Ptr m_ledger;
    bcos::IOServicePool::Ptr m_ioServicePool;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(bcos::Error::Ptr)>)>
        m_transactionNotifier;
    bool m_crossCheckIncrementalRoot = false;
    // Lock order: m_executeMutex → m_commitMutex → m_windowMutex. execute/admit try-lock the
    // first two (fail closed on contention); finalize blocks on m_commitMutex. m_windowMutex is
    // held only for map access, never across a co_await, so the RPC read plane (viewAt) is
    // never blocked by a merge.
    std::mutex m_executeMutex;
    std::mutex m_commitMutex;
    mutable std::mutex m_windowMutex;
    /// Finalized (backend) tip: height, monotonic, -1 before hydration/first block.
    std::atomic<int64_t> m_finalizedNumber{-1};
    /// Hash of the finalized tip; zero when the ledger has no SYS_NUMBER_2_HASH row for it.
    bcos::crypto::HashType m_finalizedHash;
    /// Admitted blocks by announced hash; parentHash chains to the window or the finalized tip.
    std::unordered_map<bcos::crypto::HashType, BlockLayer> m_window;
    /// Executed (verify=true) but not yet admitted; dropped by reset().
    std::unordered_map<bcos::crypto::HashType, BlockLayer> m_staged;
    /// The tracker's head for this scheduler's own reads (setCanonicalHeadProvider); guarded
    /// by m_windowMutex, copied out before it is invoked.
    CanonicalHeadProvider m_headProvider;
    std::optional<ProbeSlot> m_lastProbe;
};


}  // namespace bcos::executor_v1::opstack
