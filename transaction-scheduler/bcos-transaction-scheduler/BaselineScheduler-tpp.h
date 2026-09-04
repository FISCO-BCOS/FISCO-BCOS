#pragma once

// Implementation of BaselineScheduler.h — the member definitions and the heavy
// include set they need. Include this header ONLY where a BaselineScheduler
// specialization is actually instantiated (the explicit-instantiation TU in
// libinitializer, and unit tests that define their own mock template arguments).
// Everything else should include BaselineScheduler.h alone.
//
// Two notes on the out-of-line design:
//  - A TU that includes this header AND references a specialization that has an
//    extern template declaration (the production ones in BaselineSchedulerInitializer.h,
//    the test ones in SharedBaselineSchedulerMock.h / FullChainFixture.h) must make sure
//    the corresponding extern template declaration is visible, or it will instantiate
//    its own copy — wasteful but harmless; the reverse (extern template declared,
//    explicit instantiation TU missing from the binary) is a link error.
//  - Inlining: all 21 members are out-of-line now, but the production entry points
//    (executeBlock/commitBlock/call/callAtBlock) are SchedulerInterface virtuals whose
//    call sites already dispatch through the vtable, and the coroutine bodies
//    (coExecuteBlock/coCommitBlock) share the instantiation TU with their wrappers and
//    remain TU-inlineable — so this split costs no devirtualization/inlining in
//    practice. Revisit only if a direct (non-virtual) hot call site appears.

#include "BaselineScheduler.h"

#include "BaselineSchedulerMPTHelpers.h"
#include "HistoricalCallStorage.h"
#include "MPTNodeStorage.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/dispatcher/SchedulerTypeDef.h"
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/ITTAPI.h"
#include <fmt/format.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/task_arena.h>
#include <boost/atomic.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/iterator/operations.hpp>
#include <range/v3/view/enumerate.hpp>

namespace bcos::scheduler_v1
{

/**
 * Calculates the state root of the given storage using the specified hash implementation.
 *
 * @param storage The storage to calculate the state root for.
 * @param hashImpl The hash implementation to use for the calculation.
 * @return A task that will eventually resolve to the calculated state root.
 */
task::Task<h256> calculateStateRoot(auto& storage, uint32_t blockVersion,
    crypto::Hash const& hashImpl, ledger::Features const& features)
{
    auto range = co_await storage2::range(storage);
    storage::Entry deletedEntry;
    deletedEntry.setStatus(storage::Entry::DELETED);

    // Wrap once outside the parallel pipeline so the optional copy is paid only once,
    // not per entry.
    const std::optional<ledger::Features> featuresOpt(features);

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

h256 calculateReceiptRoot(
    ::ranges::range auto const& receipts, protocol::Block& block, crypto::Hash const& hashImpl)
{
    h256 receiptRoot;

    bcos::crypto::merkle::Merkle merkle(hashImpl.hasher());
    auto hashesRange =
        receipts | ::ranges::views::transform([](const auto& receipt) { return receipt->hash(); });

    if (!::ranges::empty(hashesRange))
    {
        std::vector<bcos::h256> merkleTrie;
        merkle.generateMerkle(hashesRange, merkleTrie);
        receiptRoot = *::ranges::rbegin(merkleTrie);
    }

    return receiptRoot;
}

/**
 * @brief Finishes the execution of a transaction and updates the block header and block.
 *
 * @param storage The storage object used to store the transaction receipts.
 * @param receipts The range of transaction receipts to be stored.
 * @param blockHeader The original block header.
 * @param newBlockHeader The updated block header.
 * @param newBlock The updated block.
 * @param hashImpl The hash implementation used to calculate the block hash.
 */
task::Task<void> finishExecute(auto& storage, ::ranges::range auto receipts,
    protocol::BlockHeader& newBlockHeader, protocol::Block& block,
    ::ranges::input_range auto transactions, bool& sysBlock, crypto::Hash const& hashImpl,
    ledger::Features const& features, std::optional<h256> mptStateRoot = {})
{
    ittapi::Report finishReport(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().FINISH_EXECUTE);
    u256 totalGasUsed;
    h256 transactionRoot;
    h256 stateRoot;
    h256 receiptRoot;

    tbb::parallel_invoke([&]() { transactionRoot = calculateTransactionRoot(block, hashImpl); },
        [&]() {
            // When the block was built with an Ethereum MPT root (shouldBuildMPT), the header
            // commits to it verbatim and the legacy XOR fold is skipped; with no MPT root the
            // XOR arm below is byte-identical to the pre-MPT behavior.
            if (mptStateRoot)
            {
                stateRoot = *mptStateRoot;
                return;
            }
            stateRoot = task::tbb::syncWait(
                calculateStateRoot(storage, block.blockHeader()->version(), hashImpl, features));
        },
        [&]() { receiptRoot = calculateReceiptRoot(receipts, block, hashImpl); },
        [&]() {
            size_t logIndex = 0;
            block.clearReceipts();
            for (auto&& [index, receipt] : ::ranges::views::enumerate(receipts))
            {
                receipt->setTransactionIndex(index);
                receipt->setLogIndex(logIndex);
                auto logBloom = getLogsBloom(receipt->logEntries());
                receipt->setLogsBloom({logBloom.data(), logBloom.size()});
                logIndex += receipt->logEntries().size();
                totalGasUsed += receipt->gasUsed();
                receipt->setCumulativeGasUsed(totalGasUsed.str());

                block.appendReceipt(receipt);
            }
        },
        [&]() {
            sysBlock = ::ranges::any_of(transactions, [](auto const& transaction) {
                return precompiled::contains(
                    bcos::precompiled::c_systemTxsAddress, transaction->to());
            });
        });

    newBlockHeader.setGasUsed(totalGasUsed);
    newBlockHeader.setTxsRoot(transactionRoot);
    newBlockHeader.setStateRoot(stateRoot);
    newBlockHeader.setReceiptsRoot(receiptRoot);
    newBlockHeader.calculateHash(hashImpl);

    // 写入blocknumber和blockhash供getBlockHash()使用
    // Write the blocknumber and blockhash for getBlockHash() to use
    auto blockNumberStr = boost::lexical_cast<std::string>(newBlockHeader.number());
    auto blockHash = newBlockHeader.hash();

    storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    co_await storage2::writeOne(storage,
        executor_v1::StateKey{ledger::SYS_NUMBER_2_HASH, blockNumberStr}, std::move(hashEntry));

    storage::Entry hash2NumberEntry;
    hash2NumberEntry.set(blockNumberStr);
    co_await storage2::writeOne(storage,
        executor_v1::StateKey{
            ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        hash2NumberEntry);

    // The executed header itself, under the same key commit's prewrite uses — written at
    // execute time for the same reason as the two hash mappings above: the NEXT block's
    // execution reads its parent's header (stateRoot for the MPT parent root) through the
    // view while the parent is still pending. Commit overwrites this row with the
    // consensus-SIGNED encoding in the same WriteBatch — mergeBackStorage merges the
    // block's layer before prewriteStorage, so the signed version wins in the backend.
    //
    // Both conditions are load-bearing:
    //  - MPT blocks only. buildMPTStateRoot reads a parent header exactly when the parent
    //    itself built an MPT root, so publishing headers for XOR blocks would buy nothing
    //    and leave every legacy chain's execute path carrying an extra row;
    //  - never block 0. Unlike the two hash mappings above, the genesis header row has an
    //    owner: Ledger::buildGenesisBlock writes it at chain init, and coCommitBlock skips
    //    prewriteBlockToBuffer for block 0 (isSysContractDeploy), so nothing would overwrite
    //    an execute-time row — the sys-contract-deploy header (its own gasUsed, hash, empty
    //    sealer/parentInfo) would replace the chain's genesis header on disk.
    if (mptStateRoot && newBlockHeader.number() != 0)
    {
        bytes headerBuffer;
        newBlockHeader.encode(headerBuffer);
        storage::Entry headerEntry;
        headerEntry.set(std::move(headerBuffer));
        co_await storage2::writeOne(storage,
            executor_v1::StateKey{ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
            std::move(headerEntry));
    }
}

template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
task::Task<ledger::mpt::MPTDeltaLayer>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::buildMPTStateRoot(
    typename MultiLayerStorage::ViewType& view, protocol::BlockHeader const& blockHeader,
    ledger::LedgerConfig const& ledgerConfig)
{
    auto const blockNumber = blockHeader.number();
    h256 parentStateRoot = ledger::mpt::emptyRootHash();
    if (blockNumber > 0 && shouldBuildMPT(ledgerConfig.features(), blockNumber - 1))
    {
        auto parentBlock = co_await ledger::getBlockData(
            view, blockNumber - 1, ledger::HEADER, m_blockFactory.get());
        parentStateRoot = parentBlock->blockHeader()->stateRoot();
    }
    // else: scenario-A activation boundary (parent committed an XOR root) — empty trie.

    // Node reads resolve through the full view (parent nodes live in the pending layers /
    // backend); node writes land in this block's own mutable layer (MPTNodeStorage.h).
    ViewNodeStorage<typename MultiLayerStorage::ViewType> nodeStorage(view);
    bool const l2Mode =
        ledgerConfig.features().get(ledger::Features::Flag::feature_l2_ethereum_compat);
    co_return co_await ledger::mpt::buildAndCollect(nodeStorage, parentStateRoot, view, l2Mode);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
task::Task<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool>>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::coExecuteBlock(
    bcos::protocol::Block::Ptr block, bool verify)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().EXECUTE_BLOCK);
    try
    {
        auto blockHeader = block->blockHeader();
        BASELINE_SCHEDULER_LOG(INFO)
            << "Execute block: " << blockHeader->number() << " | " << verify << " | "
            << block->transactionsMetaDataSize() << " | " << block->transactionsSize();

        // Fast path: if the block has already been executed (consensus and sync
        // may both drive the same height), serve the cached result without
        // taking m_executeMutex. m_results is owned by m_resultsMutex.
        {
            std::unique_lock resultsLock(m_resultsMutex);
            if (!m_results.empty())
            {
                auto number = blockHeader->number();
                auto frontNumber = m_results.front()->m_executedBlockHeader->number();
                auto backNumber = m_results.back()->m_executedBlockHeader->number();
                if (number <= frontNumber && number >= backNumber)
                {
                    BASELINE_SCHEDULER_LOG(INFO)
                        << "Block has been executed, return result directly";
                    auto& result = m_results.at(frontNumber - number);
                    co_return {nullptr, result->m_executedBlockHeader, result->m_sysBlock};
                }
            }
        }

        // FIB-102: m_lastExecutedBlockNumber is owned by m_executeMutex; the
        // continuity check below must run while this lock is held to avoid
        // the write-here / read-elsewhere race that the original code had.
        std::unique_lock executeLock(m_executeMutex, std::try_to_lock);
        if (!executeLock.owns_lock())
        {
            auto message = std::string{"Another block is executing!"};
            BASELINE_SCHEDULER_LOG(INFO) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                nullptr, false};
        }

        // FIB-102: TOCTOU re-check of the discontinuity condition under
        // m_executeMutex. Reads of m_lastExecutedBlockNumber happen here only.
        // Cache hits are already handled by the no-lock fast path above; this
        // branch only logs the cache window for diagnostics before returning
        // the discontinuity error.
        if (m_lastExecutedBlockNumber != -1 &&
            blockHeader->number() - m_lastExecutedBlockNumber != 1)
        {
            auto message = fmt::format("Discontinuous execute block number! expect: {} input: {}",
                m_lastExecutedBlockNumber + 1, blockHeader->number());
            BASELINE_SCHEDULER_LOG(INFO) << message;
            co_return {
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                nullptr, false};
        }

        // FIB-103: Backpressure — refuse new executions when the pending-result
        // queue is at capacity, preventing unbounded growth of m_results /
        // view stack when commits stall or never arrive.
        static constexpr size_t MAX_PENDING_RESULTS = 16;
        {
            std::unique_lock resultsLock(m_resultsMutex);
            if (m_results.size() >= MAX_PENDING_RESULTS)
            {
                auto message =
                    fmt::format("Too many pending execution results: {}", m_results.size());
                BASELINE_SCHEDULER_LOG(WARNING) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message),
                    nullptr, false};
            }
        }

        auto now = current();
        auto view = m_multiLayerStorage.get().fork();
        view.newMutable();
        auto transactions = co_await getTransactions(m_txpool.get(), *block);
        if (::ranges::any_of(transactions, [](auto const& tx) { return tx == nullptr; }))
        {
            auto message = fmt::format(
                "Not found transactions in txpool for block: {}", blockHeader->number());
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                nullptr, false};
        }
        auto ledgerConfig =
            co_await ledger::getLedgerConfig(view, blockHeader->number(), m_blockFactory.get());
        // Execute writes must land in the view's mutable layer: finishExecute
        // computes the state root from it and commit merges it into the backend.
        auto receipts = co_await m_schedulerImpl.get().executeBlock(view, m_executor.get(),
            *blockHeader, ::ranges::views::indirect(transactions), *ledgerConfig);

        // MPT state root (spec §5.6): built at EXECUTE time, not commit time — the block
        // header's stateRoot is set and hashed in finishExecute below and that hash is what
        // PBFT signs, so a commit-time build could never reach the header. The trie-node
        // rows land in this view's mutable layer (persisted by commit's mergeBackStorage
        // like any state row); the delta rides in ExecuteResult for the CommitObserver and
        // the next block's parent root.
        std::optional<ledger::mpt::MPTDeltaLayer> mptDelta;
        std::optional<h256> mptStateRoot;
        if (shouldBuildMPT(ledgerConfig->features(), blockHeader->number()))
        {
            // Reaching here means an MPT IS being built, so the flag-matrix rule the
            // build depends on has to hold. Re-checked per block rather than at startup
            // only: a mid-chain activation of either flag is invisible to the boot-time
            // guard (LedgerInitializer). Inside the branch, so shouldBuildMPT stays a
            // pure predicate AND is evaluated once.
            rejectRawAddressWithMPT(ledgerConfig->features(), blockHeader->number());
            try
            {
                mptDelta.emplace(co_await buildMPTStateRoot(view, *blockHeader, *ledgerConfig));
                mptStateRoot = mptDelta->stateRoot;
            }
            catch (std::exception& e)
            {
                // No fallback to the XOR root: a silent state-root scheme switch is a
                // fork — a halted chain is diagnosable, a forked one is not. The
                // rethrow surfaces through coExecuteBlock's catch as an execute error.
                BASELINE_SCHEDULER_LOG(ERROR)
                    << "MPT state root build failed, refusing XOR fallback, block="
                    << blockHeader->number() << " | " << boost::diagnostic_information(e);
                if (blockHeader->number() == 1)
                {
                    // The known scenario-B gap has exactly this shape, and a bare
                    // missing-node error from the trie core cannot say so: an L2 genesis
                    // built from a NON-EMPTY alloc writes the genesis stateRoot but not
                    // the trie nodes behind it, so block 1's incremental build cannot
                    // resolve the parent trie. Name it instead of leaving operators to
                    // guess.
                    BASELINE_SCHEDULER_LOG(ERROR)
                        << "Block 1 build failure on an L2 chain: if genesis was created "
                           "with a non-empty alloc, its trie nodes were never persisted "
                           "(known limitation) — only empty-alloc genesis chains block 1 "
                           "today";
                }
                throw;
            }
        }

        auto executedBlockHeader =
            m_blockFactory.get().blockHeaderFactory()->populateBlockHeader(blockHeader);
        bool sysBlock = false;
        co_await finishExecute(mutableStorage(view), ::ranges::views::all(receipts),
            *executedBlockHeader, *block, ::ranges::views::all(transactions), sysBlock,
            m_hashImpl.get(), ledgerConfig->features(), mptStateRoot);

        if (verify && (executedBlockHeader->hash() != blockHeader->hash()))
        {
            auto message = fmt::format("Sync block error, mismatch block hash: {} | {}",
                executedBlockHeader->hash().hex(), blockHeader->hash().hex());
            if (executedBlockHeader->stateRoot() != blockHeader->stateRoot())
            {
                message.append(fmt::format(", state root: {} | {}",
                    executedBlockHeader->stateRoot().hex(), blockHeader->stateRoot().hex()));
            }
            if (executedBlockHeader->txsRoot() != blockHeader->txsRoot())
            {
                message.append(fmt::format(", tx root: {} | {}",
                    executedBlockHeader->txsRoot().hex(), blockHeader->txsRoot().hex()));
            }
            if (executedBlockHeader->receiptsRoot() != blockHeader->receiptsRoot())
            {
                message.append(fmt::format(", receipt root: {} | {}",
                    executedBlockHeader->receiptsRoot().hex(), blockHeader->receiptsRoot().hex()));
            }
            BASELINE_SCHEDULER_LOG(ERROR) << message;

            co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message),
                nullptr, false};
        }

        auto executeResult = std::make_shared<ExecuteResult>(
            ExecuteResult{.m_transactions = std::make_shared<protocol::ConstTransactions>(
                              std::move(transactions)),
                .m_receipts = std::move(receipts),
                .m_executedBlockHeader = executedBlockHeader,
                .m_block = std::move(block),
                .m_sysBlock = sysBlock,
                .m_mptDelta = std::move(mptDelta)});

        // FIB-103 / FIB-104: Commit the execution result in a strict order:
        // push the view first (so we have a rollback target), then push the
        // result; if push_front throws pop the view back. The view stack and
        // m_results both belong to m_resultsMutex's invariant, so they share
        // this critical section. The earlier backpressure check under
        // m_resultsMutex is sufficient — m_executeMutex prevents any other
        // execute from pushing in between, and commit can only shrink the
        // queue, so the size cannot exceed MAX_PENDING_RESULTS here.
        {
            std::unique_lock resultsLock(m_resultsMutex);
            assert(m_results.size() < MAX_PENDING_RESULTS);

            m_multiLayerStorage.get().pushView(std::move(view));
            try
            {
                m_results.push_front(std::move(executeResult));
            }
            catch (...)
            {
                m_multiLayerStorage.get().popFrontStorage();
                throw;
            }
        }
        // FIB-102: Update m_lastExecutedBlockNumber only after the queue write
        // succeeds. Owned by m_executeMutex (still held via executeLock).
        m_lastExecutedBlockNumber = blockHeader->number();

        BASELINE_SCHEDULER_LOG(INFO)
            << "Execute block finished: " << executedBlockHeader->number() << " | "
            << static_cast<protocol::BlockVersion>(executedBlockHeader->version())
            << " | blockHash: " << executedBlockHeader->hash()
            << " | stateRoot: " << executedBlockHeader->stateRoot()
            << " | txRoot: " << executedBlockHeader->txsRoot()
            << " | receiptRoot: " << executedBlockHeader->receiptsRoot()
            << " | gasUsed: " << executedBlockHeader->gasUsed() << " | sysBlock: " << sysBlock
            << " | elapsed: " << (current() - now) << "ms";

        co_return {nullptr, std::move(executedBlockHeader), sysBlock};
    }
    catch (std::exception& e)
    {
        auto message = fmt::format("Execute block failed! {}", boost::diagnostic_information(e));
        BASELINE_SCHEDULER_LOG(ERROR) << message;

        co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message), nullptr,
            false};
    }
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::coCommitBlock(
    protocol::BlockHeader::Ptr header)
{
    ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
        ittapi::ITT_DOMAINS::instance().COMMIT_BLOCK);
    try
    {
        BASELINE_SCHEDULER_LOG(INFO) << "Commit block: " << header->number();

        // FIB-101: m_lastCommittedBlockNumber is owned by m_commitMutex; all
        // reads and writes happen below while we hold this lock.
        std::unique_lock commitLock(m_commitMutex, std::try_to_lock);
        if (!commitLock.owns_lock())
        {
            auto message = std::string{"Another block is committing!"};
            BASELINE_SCHEDULER_LOG(INFO) << message;

            co_return {
                BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus, message), nullptr};
        }

        // FIB-101: The genesis system-contract deploy block (number 0) is
        // committed exactly once, during initSysContract() at node startup.
        // buildGenesisBlock() has already written current-number=0, so the
        // ledger bootstrap below reads 0 and the already-committed / continuity
        // checks would wrongly reject this legitimate genesis commit (0 <= 0,
        // and 0 - 0 != 1). Block 0 only ever reaches here at init, so skip the
        // bootstrap-anchored validation for it; m_lastCommittedBlockNumber is
        // still advanced to 0 after the merge succeeds below.
        if (!isSysContractDeploy(header->number()))
        {
            // FIB-101: Bootstrap from the ledger on the very first commit so
            // that continuity / already-committed checks are anchored correctly.
            if (m_lastCommittedBlockNumber == -1)
            {
                m_lastCommittedBlockNumber = co_await ledger::getCurrentBlockNumber(m_ledger.get());
            }

            // FIB-101: Reject blocks that have already been committed (out-of-order
            // or duplicate commits from concurrent consensus / sync paths).
            if (m_lastCommittedBlockNumber != -1 && header->number() <= m_lastCommittedBlockNumber)
            {
                auto message = fmt::format("Block already committed: {}! latest: {}",
                    header->number(), m_lastCommittedBlockNumber);

                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }
            if (m_lastCommittedBlockNumber != -1 &&
                header->number() - m_lastCommittedBlockNumber != 1)
            {
                auto message = fmt::format("Discontinuous commit block number: {}! expect: {}",
                    header->number(), m_lastCommittedBlockNumber + 1);

                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }
        }

        {
            std::unique_lock resultsLock(m_resultsMutex);
            if (m_results.empty())
            {
                auto message = std::string{"Unexpected empty results!"};
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message),
                    nullptr};
            }

            auto resultBlockNumber = m_results.back()->m_executedBlockHeader->number();
            if (resultBlockNumber != header->number())
            {
                auto message = fmt::format(
                    "Commit block does not match pending execution result: input: {} pending: "
                    "{}",
                    header->number(), resultBlockNumber);
                BASELINE_SCHEDULER_LOG(INFO) << message;
                co_return {
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber, message),
                    nullptr};
            }
        }

        auto now = current();
        std::shared_ptr<ExecuteResult> result;
        {
            std::unique_lock resultsLock(m_resultsMutex);
            result = m_results.back();
        }

        Bloom logsBloom;
        for (auto& receipt : result->m_receipts)
        {
            orBloom(logsBloom, receipt->logsBloom());
        }
        result->m_block->setBlockHeader(header);
        result->m_block->setLogsBloom({logsBloom.data(), logsBloom.size()});

        // FIB-104: Single unified prewrite — header, hash mappings, nonces,
        // current block number, tx metadata, transactions, and receipts all
        // flow into prewriteStorage. The subsequent mergeBackStorage commit is
        // therefore atomic at the storage layer: either every piece of data
        // is visible after the merge, or none is. Replaces the previous
        // prewriteBlock + storeTransactionsAndReceipts pair (FIB-104.1).
        typename MultiLayerStorage::MutableStorage prewriteStorage;
        if (result->m_block->blockHeader()->number() != 0)
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SET_BLOCK);
            co_await ledger::prewriteBlockToBuffer(
                m_ledger.get(), result->m_transactions, result->m_block, prewriteStorage);
        }

        // FIB-104: mergeBackStorage is a heavy IO step (RocksDB / cache
        // write). Holding m_resultsMutex across it would needlessly serialize
        // coExecuteBlock against commit IO. The pop_back must follow a
        // successful merge so that on failure the pending entry is retained
        // for retry, but it does not have to share the same critical section.
        //
        // During the brief gap between mergeBackStorage's internal
        // m_storages.pop_back and our m_results.pop_back, coExecuteBlock may
        // see m_results.size() one larger than m_storages.size(); this is
        // harmless because (a) fork() snapshots m_storages directly under its
        // own mutex, (b) backpressure is only over-conservative by one, and
        // (c) m_results.back() cannot change here — m_commitMutex guarantees
        // a single committer.
        // MPT node persistence needs no code here: the block's trie-node rows were written
        // into its mutable layer at execute time as ordinary "/mpt/" state rows
        // (MPTNodeStorage.h), so the single mergeBackStorage below lands flat state and
        // trie nodes in one backend merge — one WriteBatch, one Write
        // (RocksDBStorage2::merge).
        //
        // MPT pruning (CommitObserver::coPreparePruneRows): the observer turns the block's
        // delta into its pruning metadata rows (refcount / delete-queue / watermark), which
        // are written into prewriteStorage so they land in the SAME WriteBatch as the block
        // data — metadata and data can never diverge across a crash. No node deletes are
        // issued on this path: deletion is posted asynchronously by onCommit below
        // (MPTPruner). The NoopCommitObserver default returns no rows, so a node without
        // pruning configured pays nothing here.
        if (result->m_mptDelta)
        {
            auto pruneRows = co_await m_mptCommitObserver->coPreparePruneRows(
                header->number(), *result->m_mptDelta);
            if (!pruneRows.empty())
            {
                co_await storage2::writeSome(prewriteStorage, std::move(pruneRows));
            }
        }
        {
            ittapi::Report mergeReport(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().MERGE_STATE);
            co_await m_multiLayerStorage.get().mergeBackStorage(prewriteStorage);
        }

        // CommitObserver timing contract (CommitObserver.h): AFTER the block's WriteBatch
        // has landed and BEFORE m_lastCommittedBlockNumber advances, so the delta the
        // observer sees is exactly the persisted state. Only MPT blocks fire it. The
        // contract requires implementations not to throw and not to block; deliberately
        // no try/catch here — swallowing an observer bug would hide it forever.
        if (result->m_mptDelta)
        {
            m_mptCommitObserver->onCommit(header->number(), *result->m_mptDelta);
        }

        {
            std::unique_lock resultsLock(m_resultsMutex);
            if (m_results.empty() ||
                m_results.back()->m_executedBlockHeader->number() != header->number())
            {
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("Pending execution result changed during commit!"));
            }
            m_results.pop_back();
        }

        auto ledgerConfig = co_await ledger::getLedgerConfig(m_ledger.get());
        ledgerConfig->setHash(header->hash());

        // FIB-101: Advance the committed counter only after the merge succeeds.
        m_lastCommittedBlockNumber = header->number();

        BASELINE_SCHEDULER_LOG(INFO) << "Commit block finished: " << header->number()
                                     << " | elapsed: " << (current() - now) << "ms";
        commitLock.unlock();

        m_asyncGroup.run([&, result = std::move(result), blockHash = ledgerConfig->hash(),
                             blockNumber = ledgerConfig->blockNumber()]() {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASELINE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().NOTIFY_RESULTS);

            auto submitResults =
                ::ranges::views::zip(
                    ::ranges::views::iota(0), *result->m_transactions, result->m_receipts) |
                ::ranges::views::transform(
                    [&](auto input) -> protocol::TransactionSubmitResult::Ptr {
                        auto&& [index, transaction, receipt] = input;

                        auto submitResult =
                            m_transactionSubmitResultFactory.get().createTxSubmitResult();
                        submitResult->setStatus(receipt->status());
                        submitResult->setTxHash(transaction->hash());
                        submitResult->setBlockHash(blockHash);
                        submitResult->setTransactionIndex(static_cast<int64_t>(index));
                        submitResult->setNonce(std::string(transaction->nonce()));
                        submitResult->setTransactionReceipt(receipt);
                        submitResult->setSender(std::string(transaction->sender()));
                        submitResult->setTo(std::string(transaction->to()));
                        submitResult->setType(transaction->type());

                        return submitResult;
                    }) |
                ::ranges::to<std::vector>();

            auto submitResultsPtr = std::make_shared<bcos::protocol::TransactionSubmitResults>(
                std::move(submitResults));
            m_blockNumberNotifier(blockNumber);
            m_transactionNotifier(
                blockNumber, std::move(submitResultsPtr), [](const Error::Ptr& error) {
                    if (error)
                    {
                        BASELINE_SCHEDULER_LOG(WARNING)
                            << "Push block notify error!" << boost::diagnostic_information(*error);
                    }
                });
        });

        co_return {Error::Ptr{}, std::move(ledgerConfig)};
    }
    catch (std::exception& e)
    {
        auto message = fmt::format("Commit block failed! {}", boost::diagnostic_information(e));
        BASELINE_SCHEDULER_LOG(ERROR) << message;

        co_return {
            BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError, message), nullptr};
    }
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::BaselineScheduler(
    MultiLayerStorage& multiLayerStorage, SchedulerImpl& schedulerImpl, Executor& executor,
    protocol::BlockFactory& blockFactory, Ledger& ledger, txpool::TxPoolInterface& txPool,
    protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory,
    crypto::Hash const& hashImpl)
  : m_multiLayerStorage(multiLayerStorage),
    m_schedulerImpl(schedulerImpl),
    m_executor(executor),
    m_blockFactory(blockFactory),
    m_ledger(ledger),
    m_txpool(txPool),
    m_transactionSubmitResultFactory(transactionSubmitResultFactory),
    m_hashImpl(hashImpl)
{}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::~BaselineScheduler() noexcept
{
    m_asyncGroup.wait();
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::executeBlock(
    bcos::protocol::Block::Ptr block, bool verify,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool sysBlock)> callback)
{
    task::wait([](decltype(this) self, bcos::protocol::Block::Ptr block, bool verify,
                   decltype(callback) callback) -> task::Task<void> {
        std::apply(callback, co_await self->coExecuteBlock(std::move(block), verify));
    }(this, std::move(block), verify, std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::commitBlock(
    protocol::BlockHeader::Ptr header,
    std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr)> callback)
{
    task::wait([](decltype(this) self, protocol::BlockHeader::Ptr blockHeader,
                   decltype(callback) callback) -> task::Task<void> {
        std::apply(callback, co_await self->coCommitBlock(std::move(blockHeader)));
    }(this, std::move(header), std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::status(
    [[maybe_unused]] std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback)
{
    callback({}, {});
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::call(
    protocol::Transaction::Ptr transaction,
    std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
{
    task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                   decltype(callback) callback) -> task::Task<void> {
        callback(nullptr, co_await self->coCallLatest(std::move(transaction)));
    }(this, std::move(transaction), std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
task::Task<protocol::TransactionReceipt::Ptr>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::coCallLatest(
    protocol::Transaction::Ptr transaction)
{
    auto view = m_multiLayerStorage.get().fork();
    view.newMutable();
    auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
    auto ledgerConfig = co_await ledger::getLedgerConfig(view, blockNumber, m_blockFactory.get());
    auto block =
        co_await ledger::getBlockData(view, blockNumber, ledger::HEADER, m_blockFactory.get());
    co_return co_await m_executor.get().executeTransaction(
        view, *block->blockHeader(), *transaction, 0, *ledgerConfig, true);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::callAtBlock(
    protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
    std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback)
{
    task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                   protocol::BlockNumber blockNumber,
                   decltype(callback) callback) -> task::Task<void> {
        try
        {
            auto latestView = self->m_multiLayerStorage.get().fork();
            auto latestNumber =
                co_await ledger::getCurrentBlockNumber(latestView, ledger::fromStorage);
            if (blockNumber > latestNumber)
            {
                callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlockNumber,
                             fmt::format("eth_call: block {} does not exist (latest: {})",
                                 blockNumber, latestNumber)),
                    nullptr);
                co_return;
            }
            if (blockNumber == latestNumber)
            {
                callback(nullptr, co_await self->coCallLatest(std::move(transaction)));
                co_return;
            }
            auto ledgerConfig = co_await ledger::getLedgerConfig(
                latestView, blockNumber, self->m_blockFactory.get());
            if (!ledgerConfig->features().get(ledger::Features::Flag::feature_l2_ethereum_compat))
            {
                callback(
                    BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                        fmt::format("eth_call: historical call at block {} requires the "
                                    "full-fidelity MPT of an L2 Ethereum-compat chain "
                                    "(feature_l2_ethereum_compat, scenario B); this chain's state "
                                    "at that block is not completely committed to an MPT",
                            blockNumber)),
                    nullptr);
                co_return;
            }
            auto block = co_await ledger::getBlockData(
                latestView, blockNumber, ledger::HEADER, self->m_blockFactory.get());
            auto stateRoot = block->blockHeader()->stateRoot();
            if (stateRoot == crypto::HashType{})
            {
                callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                             fmt::format("eth_call: no MPT state root recorded in block {}'s "
                                         "header",
                                 blockNumber)),
                    nullptr);
                co_return;
            }

            HistoricalStateBackend<typename MultiLayerStorage::ViewType> historicalBackend(
                latestView, stateRoot);
            storage2::View<typename MultiLayerStorage::MutableStorage, void,
                HistoricalStateBackend<typename MultiLayerStorage::ViewType>>
                historicalView(std::addressof(historicalBackend));
            historicalView.newMutable();
            auto receipt = co_await self->m_executor.get().executeTransaction(
                historicalView, *block->blockHeader(), *transaction, 0, *ledgerConfig, true);
            callback(nullptr, std::move(receipt));
        }
        catch (std::exception const& e)
        {
            callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::UnknownError,
                         fmt::format("eth_call at block {} failed: {}", blockNumber,
                             boost::diagnostic_information(e))),
                nullptr);
        }
    }(this, std::move(transaction), blockNumber, std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::reset(
    [[maybe_unused]] std::function<void(Error::Ptr)> callback)
{
    callback(nullptr);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::getCode(
    std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback)
{
    task::wait([](decltype(this) self, std::string_view contract,
                   decltype(callback) callback) -> task::Task<void> {
        auto view = self->m_multiLayerStorage.get().fork();
        auto contractAddress = unhexAddress(contract);
        auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
        auto ledgerConfig =
            co_await ledger::getLedgerConfig(view, blockNumber, self->m_blockFactory.get());

        ledger::account::EVMAccount account(view, contractAddress,
            ledgerConfig->features().get(ledger::Features::Flag::feature_raw_address));
        auto code = co_await account.code();

        if (!code)
        {
            callback(nullptr, {});
            co_return;
        }
        auto bytesView = code->get();
        callback(nullptr, bcos::bytes(bytesView.begin(), bytesView.end()));
    }(this, contract, std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::getABI(
    std::string_view contract, std::function<void(Error::Ptr, std::string)> callback)
{
    task::wait([](decltype(this) self, std::string_view contract,
                   decltype(callback) callback) -> task::Task<void> {
        auto view = self->m_multiLayerStorage.get().fork();
        auto contractAddress = unhexAddress(contract);
        auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
        auto ledgerConfig =
            co_await ledger::getLedgerConfig(view, blockNumber, self->m_blockFactory.get());

        ledger::account::EVMAccount account(view, contractAddress,
            ledgerConfig->features().get(ledger::Features::Flag::feature_raw_address));
        auto abi = co_await account.abi();

        if (!abi)
        {
            callback(nullptr, {});
            co_return;
        }
        callback(nullptr, std::string(abi->get()));
    }(this, contract, std::move(callback)));
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
task::Task<std::optional<bcos::storage::Entry>>
BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::getPendingStorageAt(
    std::string_view address, std::string_view key, bcos::protocol::BlockNumber number)
{
    auto view = m_multiLayerStorage.get().fork();
    auto ledgerConfig = co_await ledger::getLedgerConfig(view, number, m_blockFactory.get());

    ledger::account::EVMAccount account(
        view, address, ledgerConfig->features().get(ledger::Features::Flag::feature_raw_address));
    co_return co_await account.storageEntry(key);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::preExecuteBlock(
    [[maybe_unused]] bcos::protocol::Block::Ptr block, [[maybe_unused]] bool verify,
    [[maybe_unused]] std::function<void(Error::Ptr)> callback)
{
    callback(nullptr);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::stop() {};
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl,
    Ledger>::registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
        bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
        txNotifier)
{
    m_transactionNotifier = std::move(txNotifier);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl,
    Ledger>::registerBlockNumberNotifier(std::function<void(bcos::protocol::BlockNumber)>
        blockNumberNotifier)
{
    m_blockNumberNotifier = std::move(blockNumberNotifier);
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::setMPTCommitObserver(
    std::shared_ptr<ledger::mpt::CommitObserver> observer)
{
    if (observer)
    {
        m_mptCommitObserver = std::move(observer);
    }
}
template <class MultiLayerStorage, class Executor, class SchedulerImpl, class Ledger>
    requires BaselineSchedulerParams<MultiLayerStorage, Executor, SchedulerImpl, Ledger>
void BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>::setVersion(
    int version, ledger::LedgerConfig::Ptr ledgerConfig)
{}

}  // namespace bcos::scheduler_v1
