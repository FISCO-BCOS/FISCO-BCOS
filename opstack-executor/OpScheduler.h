// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — the OP-specific scheduler (spec 2026-08-12-op-baseline-scheduler-wiring,
// wiring Task 4 + P4). Derives from SchedulerSkeleton (the shared orchestration skeleton),
// implementing the OP's 5 CRTP hooks + 4 pure virtuals (call/getCode/getABI/getPendingStorageAt)
// + classifyException; locks/continuity/backpressure/view/queues/notifier/flow are all inherited.
// status/reset/preExecuteBlock use the skeleton defaults (v3 P1-7).
//
// Layering: a template header (template<class MultiLayerStorage>) — the real GlobalStateStorage is
// instantiated by the composition root (Initializer) at slot 3; opstack-executor does not link
// libinitializer, so GlobalStateStorage is not named here (homomorphic to OpBlockScheduler's
// `<ViewType, OpenedStorage>` template precedent).
//
// Engine seam: the engine's SchedulerType stays OpSchedulerImpl (computeTxRoot/… dependent names
// unchanged); OP block execution goes through this class's execute hook → runOpBlockInjection
// (route B, OpBlockInjector.h:31, the per-tx injection loop). Route A (executeOpBlock) is retired.
//
// EnvelopeToTarsConverter is injected by the composition root (v4 P0-2: opstack-executor does not
// link engine — opEnvelopeToTars lives in the engine lib; reuses the using alias from Task 1,
// OpBlockRegister.h:24).

#include <opstack-executor/OpBlockInjector.h>  // runOpBlockInjection (route B, Task 6 P4 M3)
#include <opstack-executor/OpBlockRegister.h>
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>  // detail::decodeOneRawTx (execute hook)
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <transaction-scheduler/bcos-transaction-scheduler/SchedulerSkeleton.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>  // readFromStorage
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/Block.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-ledger/LedgerMethods.h>  // getCurrentBlockNumber / getBlockData CPO tag_invoke
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <ethereum-executor/StorageStateView.h>
#include <boost/algorithm/hex.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::executor_v1::opstack
{
/// OP block execution payload carried through the shared `SchedulerExecuteResult.modeExtra` to
/// the commit hook (v4 P1-4): the raw EIP-2718 envelopes plus the execution result — exactly what
/// `opstackRegisterBlock` needs (rawTxBytes + result + blockFactory + envelopeToTars).
template <class MultiLayerStorage>
class OpScheduler : public bcos::scheduler_v1::SchedulerSkeleton<MultiLayerStorage,
                        bcos::executor_v1::opstack::OpstackExecutor,
                        bcos::evm::engine::OpSchedulerImpl<typename MultiLayerStorage::ViewType>,
                        bcos::ledger::LedgerInterface, OpScheduler<MultiLayerStorage>>
{
    using ViewType = typename MultiLayerStorage::ViewType;
    using SchedulerBase = bcos::scheduler_v1::SchedulerSkeleton<MultiLayerStorage,
        bcos::executor_v1::opstack::OpstackExecutor, bcos::evm::engine::OpSchedulerImpl<ViewType>,
        bcos::ledger::LedgerInterface, OpScheduler<MultiLayerStorage>>;

    /// What the execute hook stashes in `SchedulerExecuteResult::modeExtra`.
    struct OpExecuteExtra
    {
        bcos::evm::engine::OpExecuteBlockResult result;
        std::vector<bcos::bytes> rawTxBytes;
        /// The block's OP hash (announced header's `opHeaderHash`) — stashed at execute time so
        /// the commit hook's opstackRegisterBlock keys the tables by the authoritative block
        /// hash without recomputing it from the (commitment-only) executed header. finishExecute
        /// deliberately fills only the commitment fields (A8); the executed header's optional
        /// header fields (baseFee/excessBlobGas/parentBeaconBlockRoot/...) stay empty, so
        /// `opHeaderHash` on it throws std::bad_optional_access (Task 5b wiring finding — the
        /// commit hook was never driven until the delegate path).
        bcos::crypto::HashType announcedBlockHash;
    };

public:
    // 5 CRTP hooks + 4 pure virtuals + classify are public (CRTP non-virtual dispatch: the base
    // reaches derived definitions via derived(); same as BaselineScheduler, decided in Task 3b).
    // status/reset/preExecuteBlock use the skeleton defaults.
    /// ① Transaction source: block.transactions() → Transaction::ConstPtr (skips the txpool — OP
    /// blocks carry inline transactions). v3 (SEV-8): extraTransactionBytes is the signing
    /// preimage — P3 block assembly overwrote it with the full envelope; the execute hook extracts
    /// the raw envelope from it.
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// ② Execution kernel: route B — runOpBlockInjection (the per-tx injection loop,
    /// OpBlockInjector.h:31, Task 6 P4 M3 core swap). Assembly: rawTxBytes = each tx's
    /// extraTransactionBytes (P3 block assembly overwrote it with the full envelope, SEV-8);
    /// txs = detail::decodeOneRawTx(chainId); normalTxs = EnvelopeToTarsConverter (a
    /// composition-root-injected lambda, passed in the ctor — its result must overwrite
    /// extraTransactionBytes with the full envelope, since opEnvelopeToTars does not set that
    /// field; precedents EngineServiceImpl.h:1192 / OpDualPathEquivalenceTest.cpp:566-568);
    /// cfg = configAt(timestamp/1000, forkTimestamps); executor = a per-block OpstackExecutor
    /// (one evmc::VM; reverts to the pre-SEV-9 wiring after the cache removal); ledgerConfig only
    /// needs evmcRevision (the injector's executeDeposit/executeTransaction/finalizeBlock validate
    /// it).
    /// → Produces SchedulerExecuteResult{receipts, modeExtra=OpExecuteExtra{result, rawTxBytes}}.
    task::Task<bcos::scheduler_v1::SchedulerExecuteResult> execute(ViewType& view,
        protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& /*ledgerConfig*/)
    {
        namespace op = bcos::evm::opstack;
        namespace detail = bcos::evm::engine::detail;

        std::vector<bcos::bytes> rawTxBytes;
        rawTxBytes.reserve(transactions.size());
        for (auto const& tx : transactions)
        {
            auto const& env = tx->extraTransactionBytes();
            rawTxBytes.emplace_back(env.begin(), env.end());
        }

        bcos::evm::engine::OpExecuteBlockResult result;
        try
        {
            // cfg: forkTimestamps resolution (same configAt source as executeOpBlock; tars ms →
            // OP s).
            const auto& cfg =
                op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

            // txs: sort/decode (decodeOneRawTx embeds the whole-envelope canonical round-trip,
            // the P4 backstop).
            std::vector<op::OpBlockTx> txs;
            txs.reserve(rawTxBytes.size());
            for (auto const& raw : rawTxBytes)
                txs.push_back(detail::decodeOneRawTx(raw, m_chainId));
            // normalTxs: converter per tx (skipping deposits, in block order) — aligned with the
            // injector's normalIdx advancing only on the non-deposit branch (OpBlockInjector.h:71).
            // Whether the conversion succeeds decides extraTransactionBytes overwrite (SEV-8
            // above). A conversion failure: the envelope reaching execution already passed the
            // engine's step-2 static validation (canonical + enumerated), so it is a local fault —
            // the same semantics as the engine's buildOpBlock OpExecutionInternalError
            // (EngineServiceImpl.h:1186), not a verdict on the block.
            std::vector<protocol::Transaction::Ptr> normalTxs;
            normalTxs.reserve(rawTxBytes.size());
            for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            {
                if (std::holds_alternative<op::DepositTx>(txs[i].tx))
                    continue;
                const auto txHash = m_hashImpl->hash(rawTxBytes[i]);
                auto tarsTx = m_envelopeToTars(rawTxBytes[i], txHash);
                if (!tarsTx)
                {
                    BOOST_THROW_EXCEPTION(
                        bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                            "OpScheduler: envelope failed opEnvelopeToTars conversion"});
                }
                tarsTx->extraTransactionBytes.assign(rawTxBytes[i].begin(), rawTxBytes[i].end());
                normalTxs.push_back(std::make_shared<bcostars::protocol::TransactionImpl>(
                    [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; }));
            }

            bcos::ledger::LedgerConfig ledgerConfig;
            ledgerConfig.setEVMCRevision(cfg.rev);

            // Construct the executor per block (one evmc::VM); reverts to the pre-SEV-9 wiring
            // after the cache removal.
            OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

            result = bcos::evm::engine::runOpBlockInjection(executor, view, header, txs, normalTxs,
                cfg, m_chainId, ledgerConfig, rawTxBytes, m_hashImpl);
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            // FISCO types have stable typeinfo and bind reliably — rethrow as-is, preserving the
            // original message (describeException restores it at the skeleton's catch(...)
            // backstop). Do not fall to catch(std::exception&) (the -fno-rtti evmone boundary's
            // std::exception typeinfo is non-unique, unreliable) or catch(...) (message lost).
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            // Same: keep the type and message for storage faults — the skeleton's classifyException
            // maps them to OpStorageFault (-32603, not INVALID).
            throw;
        }
        catch (const std::exception&)
        {
            throw;  // Bindable families (logic_error / normally-typed runtime_error) → the skeleton
                    // classifies.
        }
        catch (...)
        {
            // RTTI bypass (measured in Storage2State.h:195-199: runtime_error subclasses escape
            // the typed catch into catch(...)) — decodeOneRawTx's OpConsensusError etc. escape
            // this hook with a broken typeinfo and would bypass the skeleton coExecuteBlock's
            // catch(std::exception&) (measured: thrown straight to the test). Normalize to a FISCO
            // type (consensus — storage faults were already normalized to OpStorageError on the
            // bindable path by runOpBlockInjection's poison-first classification) so
            // classifyException receives a catchable type.
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: runOpBlockInjection threw an unrecognized (RTTI-bypassed) exception; "
                "raw tx decode or block-level consensus fault");
        }

        bcos::scheduler_v1::SchedulerExecuteResult out;
        // Copy (cheap shared_ptr vector) rather than move: both `SchedulerExecuteResult.receipts`
        // (the skeleton's pushResult / callback surface) and `OpExecuteExtra.result.receipts` (the
        // commit hook's opstackRegisterBlock checks rawTxBytes.size() != result.receipts.size(),
        // OpBlockRegister.h:113) must hold the full receipts.
        out.receipts = result.receipts;
        // No txpool submission on the OP path — set a non-empty empty table so the skeleton
        // coCommitBlock's notifyBlockNumber does not dereference the null `*result->m_transactions`
        // (SchedulerSkeleton.h:355). The empty table makes the tx-submit zip a no-op loop while the
        // blockNumber/transaction notifiers still run.
        out.m_transactions = std::make_shared<protocol::ConstTransactions>();
        // announcedBlockHash: the authoritative block hash (the announced header's opHeaderHash,
        // validated by engine step 2), stashed so the commit hook keys the tables with it instead
        // of recomputing on the executed header (whose optional fields are incomplete).
        out.modeExtra = std::make_shared<OpExecuteExtra>(
            OpExecuteExtra{std::move(result), std::move(rawTxBytes),
                header.opHeaderHash(
                    bcos::protocol::BlockHeader::OpHeaderConst{.ommersHash = c_emptyOmmersHash,
                        .difficulty = bcos::u256(0),
                        .nonce = c_posNonce})});
        co_return out;
    }

    /// Override of the skeleton's fastPathHit (I-2 hard-contract guard). The base only matches on
    /// block number (SchedulerSkeleton.h); on an OP chain a number does not uniquely identify a
    /// block — if the previous block executed but commit failed (opstackRegisterBlock /
    /// mergeBackStorage threw), the result stays in m_results, the view stays on the layer stack,
    /// and SYS_HASH_2_NUMBER / SYS_NUMBER_2_HASH are unwritten (neither engine step 3b nor 3c stops
    /// the same-height resend). If op-node then sends a different block at that height, the base
    /// would hit the stale block's executedHeader, skip the new block's execution + verify, commit
    /// the old block's state yet report the new payload VALID — forking the chain from op-node.
    /// This override adds a comparison on a hit: the cached block
    /// (OpExecuteExtra::announcedBlockHash stashed by the execute hook) must equal the incoming
    /// announced header's opHeaderHash, else report no hit (forcing re-execution). Range-matching
    /// logic is verbatim the base's, done under a single lock (eliminating the TOCTOU between
    /// base-hit and the check). On no-hit the skeleton falls through to the continuity recheck and
    /// rejects the same-height duplicate
    /// (-32603 — an honest rejection, not a false VALID).
    std::optional<std::pair<protocol::BlockHeader::Ptr, bool>> fastPathHit(
        protocol::BlockNumber number, protocol::BlockHeader const& announcedHeader)
    {
        std::unique_lock resultsLock(this->m_resultsMutex);
        if (this->m_results.empty())
        {
            return std::nullopt;
        }
        auto frontNumber = this->m_results.front()->m_executedBlockHeader->number();
        auto backNumber = this->m_results.back()->m_executedBlockHeader->number();
        if (number <= frontNumber && number >= backNumber)
        {
            auto& result = this->m_results.at(frontNumber - number);
            auto extra = std::static_pointer_cast<OpExecuteExtra>(result->modeExtra);
            if (!extra ||
                extra->announcedBlockHash !=
                    announcedHeader.opHeaderHash(
                        bcos::protocol::BlockHeader::OpHeaderConst{.ommersHash = c_emptyOmmersHash,
                            .difficulty = bcos::u256(0),
                            .nonce = c_posNonce}))
            {
                // Different block (or a result lacking the OP extra) at this height: the cached
                // executedHeader must not stand in for the new block.
                BASELINE_SCHEDULER_LOG(INFO) << "Fast-path cache holds a different block at height "
                                             << number << "; ignoring cache and re-executing";
                return std::nullopt;
            }
            BASELINE_SCHEDULER_LOG(INFO) << "Block has been executed, return result directly";
            return std::pair{result->m_executedBlockHeader, result->m_sysBlock};
        }
        return std::nullopt;
    }

    /// ③ finish: write the OP commitments into the executedHeader (setStateRoot/TxsRoot/
    /// ReceiptsRoot/GasUsed/LogsBloom/WithdrawalsRoot/RequestsHash/BlobGasUsed; skip MPT; never
    /// call BlockHeader::hash()). (v3 A8: a new mechanism — OP has no "write computed values back
    /// to the header" code; the header is rebuilt from the announced payload +
    /// comparison-verified.)
    task::Task<protocol::BlockHeader::Ptr> finishExecute(ViewType& /*view*/,
        bcos::scheduler_v1::SchedulerExecuteResult& result,
        protocol::BlockHeader const& blockHeader, protocol::Block& /*block*/,
        std::vector<protocol::Transaction::ConstPtr> const& /*transactions*/,
        ledger::LedgerConfig const& /*ledgerConfig*/, bool& sysBlock)
    {
        namespace detail = bcos::evm::engine::detail;
        sysBlock = false;
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto const& opResult = extra.result;

        auto executedBlockHeader = this->m_blockFactory->blockHeaderFactory()->populateBlockHeader(
            protocol::BlockHeader::ConstPtr{&blockHeader, [](protocol::BlockHeader const*) {}});
        executedBlockHeader->setStateRoot(opResult.stateRoot);
        executedBlockHeader->setTxsRoot(opResult.txRoot);
        executedBlockHeader->setReceiptsRoot(detail::toBcosH256(opResult.seal.receiptsRoot));
        executedBlockHeader->setGasUsed(bcos::u256(opResult.gasUsed));
        auto const& bloom = opResult.seal.logsBloom;
        executedBlockHeader->setLogsBloom(bcos::bytesConstRef(
            reinterpret_cast<const bcos::byte*>(bloom.bytes), sizeof(bloom.bytes)));
        executedBlockHeader->setWithdrawalsRoot(detail::toBcosH256(opResult.seal.withdrawalsRoot));
        if (opResult.seal.requestsHash.has_value())
            executedBlockHeader->setRequestsHash(detail::toBcosH256(*opResult.seal.requestsHash));
        if (opResult.seal.blobGasUsed.has_value())
            executedBlockHeader->setBlobGasUsed(bcos::u256(*opResult.seal.blobGasUsed));
        co_return executedBlockHeader;
    }

    /// ④ verify: six-way comparison (mismatchedFieldOf, Task 1 seam; unconditional — the verify
    /// gate lives in the hook, OP always compares); a mismatch throws OpConsensusError(
    /// mismatchedField), classified by the skeleton → OpConsensusRejected. Returns null = pass.
    task::Task<Error::Ptr> verifyResult(protocol::BlockHeader::Ptr executed,
        protocol::BlockHeader const& announced, bool /*verify*/)
    {
        namespace engine = bcos::evm::engine;
        if (auto mismatch = engine::mismatchedFieldOf(
                headerCommitments(*executed), headerCommitments(announced)))
        {
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: six-way commitment mismatch on field " + *mismatch);
        }
        co_return nullptr;
    }

    /// ⑤ commit: opstackRegisterBlock (Task 1 mechanism) writes a standalone MutableStorage and
    /// returns it (the skeleton's only mergeBackStorage, FIB-104). blockHash uses the execute
    /// hook's extra.announcedBlockHash (the announced header's opHeaderHash, validated by engine
    /// step 2) — not recomputed on the executed header (finishExecute fills only the commitment
    /// fields; an incomplete optional would throw bad_optional_access, found in Task 5b).
    /// **The registered header is the announced one (result.m_block->blockHeader()), NOT the
    /// executed header** — finishExecute fills only commitments, so the executed header's tars
    /// encode is incomplete (missing coinbase/gasLimit/baseFee/prevRandao/excessBlobGas/
    /// parentBeaconBlockRoot/...); a child block's step-3a parent-header read re-parses it via
    /// createBlockHeader, and the incomplete header would trigger a dataHash-recompute throw
    /// (Task 5b wiring finding). The OP header hash lives in the codec; BlockHeader::hash() is
    /// never called (A8).
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        ViewType& /*view*/, protocol::BlockHeader::Ptr /*header*/,
        bcos::scheduler_v1::SchedulerExecuteResult const& result)
    {
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        // The block's header is the announced one (the engine's buildOpBlock set it); finishExecute
        // built a separate executed header without touching the block's. Keep the Ptr alive:
        // blockHeader() returns a fresh shared_ptr BY VALUE, and binding a reference to
        // `*block->blockHeader()` would dangle a temporary (OpBlockScheduler's documented
        // segfault-on-timestamp() trap).
        auto announcedHeader = result.m_block->blockHeader();
        co_await bcos::evm::engine::opstackRegisterBlock(*storage, *announcedHeader,
            extra.announcedBlockHash, extra.rawTxBytes, extra.result, *this->m_blockFactory,
            m_envelopeToTars);
        co_return storage;
    }

    /// Test observation surface (Task 6 P1-8 harness surgery): the dual-path harness drives the
    /// execute hook via executeBlock and needs the OpExecuteBlockResult back for the A-vs-B
    /// comparison. Returns the raw execution result of the latest pending block (after the
    /// skeleton's pushResult, m_results.front() is the newest). Not consumed by production — the
    /// commit hook uses result.modeExtra's OpExecuteExtra (richer: announcedBlockHash/rawTxBytes).
    std::optional<bcos::evm::engine::OpExecuteBlockResult> peekExecuteResult()
    {
        std::unique_lock<std::mutex> lock(this->m_resultsMutex);
        if (this->m_results.empty())
            return std::nullopt;
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(this->m_results.front()->modeExtra);
        return extra.result;
    }

    // ---- Pure virtuals: call / storage reads (v3 B3: the skeleton does not implement these;
    // ---- each derived class does)

    /// eth_call: replicates OpBlockScheduler::call (coCallLatest, 10-param injection + a
    /// hand-built LedgerConfig + double catch + RTTI-bypass). Errors go back via the callback as a
    /// JSON-RPC Error, never a status-0 receipt.
    void call(protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([this, tx = std::move(transaction),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            try
            {
                cb(nullptr, co_await coCallLatest(std::move(tx)));
            }
            catch (const std::exception& e)
            {
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
            catch (...)
            {
                // Typed-catch RTTI bypass (same as the OpBlockScheduler.h:104-118 note): the
                // -fno-rtti libevmone.a carries hidden typeinfo, so catch(const std::exception&)
                // does not reliably bind runtime_error — fall back to returning an Error rather
                // than dangling the RPC coroutine.
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "OpScheduler::call: unknown (RTTI-bypassed) exception"),
                    nullptr);
            }
        }());
    }

    /// getCode: storage read via the readFromStorage pattern (not getLedgerConfig — header.hash()
    /// throws EmptyBlockHeaderHash for OP headers).
    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                bcos::ledger::Features features;
                co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    features.get(bcos::ledger::Features::Flag::feature_raw_address));
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
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    void getABI(std::string_view contract,
        std::function<void(bcos::Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_multiLayerStorage->fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                bcos::ledger::Features features;
                co_await bcos::ledger::readFromStorage(features, view, blockNumber);

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    features.get(bcos::ledger::Features::Flag::feature_raw_address));
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
                callback(
                    BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()), {});
            }
        }(this, contract, std::move(callback)));
    }

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) override
    {
        auto view = this->m_multiLayerStorage->fork();
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::account::EVMAccount account(
            view, address, features.get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    // A3: exception classification — OpConsensusError→OpConsensusRejected /
    // OpStorageError→OpStorageFault / other→UnknownError. Type recognition via rethrow + catch
    // (precondition: executeOpBlock already normalized to FISCO types; the RTTI trap across the
    // -fno-rtti evmone boundary is handled by executeOpBlock's catch(...) backstop, v3 P1-6).
    scheduler::SchedulerError classifyException(std::exception_ptr eptr) const override
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            return scheduler::SchedulerError::OpConsensusRejected;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            return scheduler::SchedulerError::OpStorageFault;
        }
        catch (...)
        {
            return scheduler::SchedulerError::UnknownError;
        }
    }

    /// Error-message recovery at the skeleton's catch(...) backstop (wiring Task 5b): rethrow +
    /// typed catch — FISCO types have stable typeinfo, so they bind reliably and yield what()
    /// (catch(std::exception&) cannot reliably bind across the -fno-rtti evmone boundary and the
    /// original message would be lost). Both the six-way mismatch (verifyResult's OpConsensusError)
    /// and executeOpBlock's consensus/storage rejection messages are recovered here so the engine
    /// barrier can emit a detailed validationError (the -fno-rtti issue is detailed in
    /// SchedulerSkeleton.h's describeException comment).
    std::string describeException(std::exception_ptr eptr) const override
    {
        try
        {
            std::rethrow_exception(std::move(eptr));
        }
        catch (const bcos::evm::engine::OpConsensusError& e)
        {
            return e.what();
        }
        catch (const bcos::evm::engine::OpStorageError& e)
        {
            return e.what();
        }
        catch (...)
        {
            return "unclassified exception, RTTI typed-catch bypassed";
        }
    }

    OpScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps,
        bcos::protocol::BlockFactory::Ptr blockFactory, MultiLayerStorage& multiLayerStorage,
        bcos::evm::engine::EnvelopeToTarsConverter envelopeToTars)
      : SchedulerBase(),
        m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_envelopeToTars(std::move(envelopeToTars))
    {
        this->m_multiLayerStorage = &multiLayerStorage;
        this->m_blockFactory = blockFactory.get();
        // v3 (end of M4): OP commit's notifyBlockNumber goes through the skeleton (m_asyncGroup);
        // OP has no RPC push needs — register no-op notifiers by default, overridable by the
        // composition root (Initializer) via setBlockNumberNotifier/setTransactionNotifier. A
        // default-empty std::function would throw std::bad_function_call (inside an async task →
        // rethrown by the dtor's m_asyncGroup.wait() → terminate). OP's m_transactions is always a
        // non-empty empty table so the tx-submit zip is a no-op loop;
        // m_transactionSubmitResultFactory being null is also safe (createTxSubmitResult is never
        // called).
        this->m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        this->m_transactionNotifier =
            [](bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
                std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

public:
    /// The OP path cannot use the skeleton's default getLedgerConfig (LedgerMethods.h:347 calls
    /// header.hash(), which throws EmptyBlockHeaderHash for an OP header with empty dataHash) —
    /// build the LedgerConfig by hand (features only; the execute hook's executeOpBlock does not
    /// go through LedgerConfig — it consumes the header + fork schedule).
    /// Public: the skeleton's coExecuteBlock reaches derived definitions via derived() (CRTP);
    /// protected would trigger an access error (same reason as the 5 hooks, Task 3b).
    task::Task<ledger::LedgerConfig::Ptr> loadLedgerConfig(
        ViewType& view, protocol::BlockNumber number)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(number);
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        ledgerConfig->setFeatures(features);
        co_return ledgerConfig;
    }

    /// Same: the skeleton default goes through header->hash() + getLedgerConfig(*m_ledger) — the
    /// OP commit path never calls header.hash() (M4). Public for the same reason as
    /// loadLedgerConfig (coCommitBlock reaches it via derived()).
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

    /// Commit continuity (v3 P1-6 head-advance guard): opstackRegisterBlock's write of
    /// SYS_CURRENT_STATE is unconditional (MAIN OpBlockRegister.h:75-80) — the engine's original
    /// guarded write (blockNumber > currentHead, EngineServiceImpl inline path) moved to
    /// OpScheduler's commit and is carried by this override, preserving the monotonic-guard
    /// semantics (rejecting already-committed / discontinuous commits). The skeleton base reads
    /// *m_ledger (getCurrentBlockNumber(*m_ledger)); the OP composition root does not wire
    /// LedgerInterface — here the guard reads the storage view instead (getCurrentBlockNumber(view,
    /// fromStorage), the same source as engine step 3). Public for the same reason as
    /// loadCommitLedgerConfig (coCommitBlock reaches it via derived()). The isSysContractDeploy
    /// special case is retained (block-0 system-contract deployment, PrecompiledTypeDef.h:31).
    task::Task<bool> commitContinuityCheck(protocol::BlockNumber number)
    {
        if (!isSysContractDeploy(number))
        {
            if (this->m_lastCommittedBlockNumber == -1)
            {
                auto view = this->m_multiLayerStorage->fork();
                this->m_lastCommittedBlockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
            }
            if (this->m_lastCommittedBlockNumber != -1 &&
                number <= this->m_lastCommittedBlockNumber)
            {
                BASELINE_SCHEDULER_LOG(INFO) << "Block already committed: " << number
                                             << "! latest: " << this->m_lastCommittedBlockNumber;
                co_return false;
            }
            if (this->m_lastCommittedBlockNumber != -1 &&
                number - this->m_lastCommittedBlockNumber != 1)
            {
                BASELINE_SCHEDULER_LOG(INFO)
                    << "Discontinuous commit block number: " << number
                    << "! expect: " << (this->m_lastCommittedBlockNumber + 1);
                co_return false;
            }
        }
        co_return true;
    }

private:
    /// Project an executed/announced OP header's commitment fields into the six-way comparison
    /// surface. Both sides read the same header accessors so `mismatchedFieldOf` is comparing the
    /// values finishExecute wrote (executed) against what the payload announced.
    static bcos::evm::engine::OpBlockCommitments headerCommitments(protocol::BlockHeader const& h)
    {
        namespace detail = bcos::evm::engine::detail;
        auto bloom = h.logsBloom();
        bcos::h2048 logsBloom(reinterpret_cast<const bcos::byte*>(bloom.data()), bloom.size());
        std::optional<uint64_t> blobGasUsed;
        if (auto bg = h.blobGasUsed())
            blobGasUsed = detail::narrowU256ToU64(*bg, "headerCommitments blobGasUsed");
        return bcos::evm::engine::OpBlockCommitments{
            .receiptsRoot = h.receiptsRoot(),
            .logsBloom = logsBloom,
            .withdrawalsRoot = h.withdrawalsRoot().value_or(bcos::h256{}),
            .stateRoot = h.stateRoot(),
            .gasUsed = h.gasUsed(),
            .txRoot = h.txsRoot(),
            .blobGasUsed = blobGasUsed,
            .requestsHash = h.requestsHash(),
        };
    }

    /// Strict hex-address parse for getCode/getABI (same as OpBlockScheduler::parseAddress).
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

    /// OP eth_call: fork the latest committed state, build a real OP block context (hand-built
    /// LedgerConfig, not getLedgerConfig), load the L1Block fee params, run
    /// OpstackExecutor::executeTransaction (injecting chainId/blockGasLeft/block hashes), then
    /// discard the fork (dry-run). Replicates OpBlockScheduler::coCallLatest
    /// (OpBlockScheduler.h:259-306).
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

        auto view = this->m_multiLayerStorage->fork();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, *this->m_blockFactory);
        // Keep the header Ptr alive: blockHeader() returns a fresh shared_ptr BY VALUE; binding a
        // reference to it would dangle a temporary.
        auto blockHeader = block->blockHeader();
        auto const& header = *blockHeader;

        const auto& cfg =
            op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

        auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(blockNumber);
        ledgerConfig->setTimestamp(header.timestamp());
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, blockNumber);
        ledgerConfig->setFeatures(features);
        ledgerConfig->setEVMCRevision(cfg.rev);

        eth::StorageStateView<ViewType> stateView(view);
        auto fee = op::loadOpFeeParams(stateView);
        const auto blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "OpScheduler blockGasLeft"));

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<ViewType> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // Construct the executor per call (one evmc::VM) — reverts to the pre-cache wiring of the
        // OpBlockScheduler.h:298 precedent.
        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
    }

    // 3 post-merge OP header constants (same values as EngineServiceImpl.cpp:81-83, used by the
    // commit's opHeaderHash — the engine injects them via detail::opHeaderConst(); opstack-executor
    // does not link engine).
    static const bcos::h256 c_emptyOmmersHash;
    static const bcos::h64 c_posNonce;

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    bcos::evm::engine::EnvelopeToTarsConverter m_envelopeToTars;
};

template <class MultiLayerStorage>
const bcos::h256 OpScheduler<MultiLayerStorage>::c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
template <class MultiLayerStorage>
const bcos::h64 OpScheduler<MultiLayerStorage>::c_posNonce{std::string{"0x0000000000000000"}};

}  // namespace bcos::executor_v1::opstack
