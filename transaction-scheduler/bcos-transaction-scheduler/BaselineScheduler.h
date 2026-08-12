#pragma once

#include "BaselineSchedulerMPTHelpers.h"
#include "HistoricalCallStorage.h"
#include "MPTNodeStorage.h"
#include "SchedulerSkeleton.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/merkle/Merkle.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
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
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionSubmitResultFactory.h"
#include "bcos-framework/storage2/MultiLayerStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-framework/txpool/TxPoolInterface.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-ledger/mpt/MPTBuilder.h"
#include "bcos-task/TBBWait.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/Bloom.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/ITTAPI.h"
#include <fmt/format.h>
#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/parallel_invoke.h>
#include <oneapi/tbb/parallel_pipeline.h>
#include <oneapi/tbb/task_arena.h>
#include <boost/atomic.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <chrono>
#include <exception>
#include <memory>
#include <range/v3/algorithm/any_of.hpp>
#include <range/v3/iterator/operations.hpp>
#include <range/v3/view/enumerate.hpp>
#include <type_traits>

namespace bcos::scheduler_v1
{
DERIVE_BCOS_EXCEPTION(NotFoundTransactionError);

/**
 * Retrieves a vector of transactions from the provided transaction pool and block.
 *
 * @param txpool The transaction pool to retrieve transactions from.
 * @param block The block to retrieve transactions for.
 * @return A task that resolves to a vector of transactions.
 */
task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
    txpool::TxPoolInterface& txpool, protocol::Block& block);

/**
 * Calculates the transaction root hash for a given block using the specified hash
 * implementation.
 *
 * @param block The block for which to calculate the transaction root hash.
 * @param hashImpl The hash implementation to use for the calculation.
 * @return The calculated transaction root hash.
 */
bcos::h256 calculateTransactionRoot(protocol::Block const& block, crypto::Hash const& hashImpl);

/**
 * Returns the current time in milliseconds since the epoch.
 *
 * @return the current time in milliseconds
 */
std::chrono::milliseconds::rep current();

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

template <class MultiLayerStorage,
    executor_v1::TransactionExecutor<typename MultiLayerStorage::ViewType> Executor,
    scheduler_v1::TransactionScheduler<typename MultiLayerStorage::ViewType, Executor,
        std::vector<protocol::Transaction::ConstPtr>>
        SchedulerImpl,
    class Ledger>
class BaselineScheduler
  : public SchedulerSkeleton<MultiLayerStorage, Executor, SchedulerImpl, Ledger,
        BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>>
{
    using SchedulerBase = SchedulerSkeleton<MultiLayerStorage, Executor, SchedulerImpl, Ledger,
        BaselineScheduler<MultiLayerStorage, Executor, SchedulerImpl, Ledger>>;

private:
    // ethereum-specific state kept in the derived class (used only by the CRTP hooks
    // and the call/storage-read methods below); the shared orchestration state
    // (m_multiLayerStorage / m_blockFactory / m_ledger / m_results / mutexes / notifier /
    // m_asyncGroup / m_mptCommitObserver / counters) lives in SchedulerSkeleton.
    std::reference_wrapper<std::remove_reference_t<SchedulerImpl>> m_schedulerImpl;
    std::reference_wrapper<Executor> m_executor;
    std::reference_wrapper<txpool::TxPoolInterface> m_txpool;
    std::reference_wrapper<crypto::Hash const> m_hashImpl;

    /**
     * Build the block's Ethereum MPT state root over the execute view — the view whose top
     * mutable layer is exactly this block's delta and whose immutable layers are the pending
     * blocks' not-yet-committed deltas, node rows included — and return the trie-node delta.
     *
     * The parent state root comes from the parent block's HEADER, read through the view.
     * finishExecute publishes every non-genesis MPT block's executed header into its own
     * mutable layer, so ONE getBlockData arm covers every case: a pending parent resolves
     * from the view's immutable chain (pipeline), a committed parent — the genesis block
     * included — from the backend's signed header (restart / sequential; genesis is the
     * ledger's own genesis header, which is why finishExecute must not publish one). A
     * missing header under an MPT parent throws NotFoundBlockHeader (getBlockData,
     * LedgerMethods.h) — never a silent empty-trie rebuild.
     *
     * An XOR parent (the scenario-A activation boundary) starts from the EMPTY trie, and
     * never has its header read here. That is the scenario-A semantics, not an oversight:
     * activating feature_mpt_state_root mid-chain commits only to state written AFTER
     * activation — accounts dormant since the flip stay outside the trie and answer
     * AccountNotInMPT, and the root is deliberately NOT a full-state Ethereum commitment
     * (spec design3 §1.1.2 / §1.1.4 / §1.1.6; the 2026-07-09 revision removed first-touch
     * bootstrap and the preheat tooling on purpose — no stop-the-world scan, no migration).
     * Sync completeness on such a chain comes from replaying the block sequence, as it did
     * before MPT. Scenario B (L2 from genesis) is the full-state case: every account enters
     * the trie when it is created.
     *
     * Scenario-B genesis nodes: Ledger::buildGenesisBlock persists every genesis trie node
     * (account trie + storage sub-tries, computeGenesisStateTrie) as "/mpt/" state rows on
     * first init of an L2 chain, so block 1's incremental build reads its parents here.
     */
    task::Task<ledger::mpt::MPTDeltaLayer> buildMPTStateRoot(
        typename MultiLayerStorage::ViewType& view, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig)
    {
        auto const blockNumber = blockHeader.number();
        h256 parentStateRoot = ledger::mpt::emptyRootHash();
        if (blockNumber > 0 && shouldBuildMPT(ledgerConfig.features(), blockNumber - 1))
        {
            auto parentBlock = co_await ledger::getBlockData(
                view, blockNumber - 1, ledger::HEADER, *this->m_blockFactory);
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

    // ================================================================
    // 5 CRTP hooks（PUBLIC：CRTP 非虚分派需要基类（SchedulerSkeleton）经 derived() 访问
    // 派生定义——派生私有成员基类不可访问，故 hook 与 classifyException 置 public）。
    // ================================================================
public:
    /// ① 交易来源：现有自由函数 getTransactions(txpool, block)（BaselineScheduler.cpp:3-17，
    ///    协程）。适配器忽略 view，传 m_txpool.get()；cfg 在 getTransactions 之后加载
    ///    （骨架 coExecuteBlock 保持现状顺序）。
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, typename MultiLayerStorage::ViewType&)
    {
        return scheduler_v1::getTransactions(m_txpool.get(), block);
    }

    /// ② 执行内核：m_schedulerImpl.executeBlock → 包 SchedulerExecuteResult 富结果
    ///    （v3 P1-2：m_transactions 供 commit 的 prewriteBlockToBuffer + notifier 用）。
    task::Task<SchedulerExecuteResult> execute(typename MultiLayerStorage::ViewType& view,
        protocol::BlockHeader const& header,
        std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig)
    {
        auto receipts = co_await m_schedulerImpl.get().executeBlock(
            view, m_executor.get(), header, ::ranges::views::indirect(transactions), ledgerConfig);
        SchedulerExecuteResult result;
        result.receipts = std::move(receipts);
        result.m_transactions = std::make_shared<protocol::ConstTransactions>(transactions);
        co_return result;
    }

    /// ③ finish：MPT 前置（buildMPTStateRoot，随 hook 迁自 coExecuteBlock:485-527）+
    ///    populateBlockHeader + 自由函数 finishExecute（calculateHash 在内）。
    task::Task<protocol::BlockHeader::Ptr> finishExecute(typename MultiLayerStorage::ViewType& view,
        SchedulerExecuteResult& result, protocol::BlockHeader const& blockHeader,
        protocol::Block& block, std::vector<protocol::Transaction::ConstPtr> const& transactions,
        ledger::LedgerConfig const& ledgerConfig, bool& sysBlock)
    {
        // MPT state root (spec §5.6): built at EXECUTE time, not commit time — the block
        // header's stateRoot is set and hashed in the free finishExecute below and that hash
        // is what PBFT signs, so a commit-time build could never reach the header. The
        // trie-node rows land in this view's mutable layer (persisted by commit's
        // mergeBackStorage like any state row); the delta rides in result.m_mptDelta for
        // the CommitObserver and the next block's parent root.
        std::optional<ledger::mpt::MPTDeltaLayer> mptDelta;
        std::optional<h256> mptStateRoot;
        if (shouldBuildMPT(ledgerConfig.features(), blockHeader.number()))
        {
            // Reaching here means an MPT IS being built, so the flag-matrix rule the build
            // depends on has to hold. Re-checked per block rather than at startup only: a
            // mid-chain activation of either flag is invisible to the boot-time guard
            // (LedgerInitializer). Inside the branch, so shouldBuildMPT stays a pure
            // predicate AND is evaluated once.
            rejectRawAddressWithMPT(ledgerConfig.features(), blockHeader.number());
            try
            {
                mptDelta.emplace(co_await buildMPTStateRoot(view, blockHeader, ledgerConfig));
                mptStateRoot = mptDelta->stateRoot;
            }
            catch (std::exception& e)
            {
                // No fallback to the XOR root: a silent state-root scheme switch is a fork —
                // a halted chain is diagnosable, a forked one is not. The rethrow surfaces
                // through coExecuteBlock's catch as an execute error.
                BASELINE_SCHEDULER_LOG(ERROR)
                    << "MPT state root build failed, refusing XOR fallback, block="
                    << blockHeader.number() << " | " << boost::diagnostic_information(e);
                if (blockHeader.number() == 1)
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
        result.m_mptDelta = std::move(mptDelta);

        auto executedBlockHeader = this->m_blockFactory->blockHeaderFactory()->populateBlockHeader(
            protocol::BlockHeader::ConstPtr{&blockHeader, [](protocol::BlockHeader const*) {}});
        co_await scheduler_v1::finishExecute(mutableStorage(view),
            ::ranges::views::all(result.receipts), *executedBlockHeader, block,
            ::ranges::views::all(transactions), sysBlock, m_hashImpl.get(), ledgerConfig.features(),
            mptStateRoot);
        co_return executedBlockHeader;
    }

    /// ④ verify（v3 P1-1 + Task 3b I-2）：verify 标志穿入，返回 Error::Ptr 保真
    ///    InvalidBlocks（不经 classify 变 UnknownError）。
    task::Task<Error::Ptr> verifyResult(protocol::BlockHeader::Ptr executedHeader,
        protocol::BlockHeader const& blockHeader, bool verify)
    {
        if (verify && (executedHeader->hash() != blockHeader.hash()))
        {
            auto message = fmt::format("Sync block error, mismatch block hash: {} | {}",
                executedHeader->hash().hex(), blockHeader.hash().hex());
            if (executedHeader->stateRoot() != blockHeader.stateRoot())
            {
                message.append(fmt::format(", state root: {} | {}",
                    executedHeader->stateRoot().hex(), blockHeader.stateRoot().hex()));
            }
            if (executedHeader->txsRoot() != blockHeader.txsRoot())
            {
                message.append(fmt::format(", tx root: {} | {}", executedHeader->txsRoot().hex(),
                    blockHeader.txsRoot().hex()));
            }
            if (executedHeader->receiptsRoot() != blockHeader.receiptsRoot())
            {
                message.append(fmt::format(", receipt root: {} | {}",
                    executedHeader->receiptsRoot().hex(), blockHeader.receiptsRoot().hex()));
            }
            BASELINE_SCHEDULER_LOG(ERROR) << message;
            co_return BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidBlocks, message);
        }
        co_return nullptr;
    }

    /// ⑤ commit：prewriteBlockToBuffer → 返回 prewriteStorage（v3 P1-3：非 execute view——
    ///    execute view 已在 coExecuteBlock pushView；骨架唯一一次 mergeBackStorage）。
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        typename MultiLayerStorage::ViewType&, protocol::BlockHeader::Ptr header,
        SchedulerExecuteResult const& result)
    {
        Bloom logsBloom;
        for (auto& receipt : result.receipts)
        {
            orBloom(logsBloom, receipt->logsBloom());
        }
        result.m_block->setBlockHeader(header);
        result.m_block->setLogsBloom({logsBloom.data(), logsBloom.size()});

        // FIB-104: Single unified prewrite — header, hash mappings, nonces, current block
        // number, tx metadata, transactions, and receipts all flow into prewriteStorage.
        // The subsequent (skeleton) mergeBackStorage commit is therefore atomic at the
        // storage layer: either every piece of data is visible after the merge, or none is.
        auto prewriteStorage = std::make_shared<typename MultiLayerStorage::MutableStorage>();
        if (result.m_block->blockHeader()->number() != 0)
        {
            ittapi::Report report(ittapi::ITT_DOMAINS::instance().BASE_SCHEDULER,
                ittapi::ITT_DOMAINS::instance().SET_BLOCK);
            co_await ledger::prewriteBlockToBuffer(
                *this->m_ledger, result.m_transactions, result.m_block, *prewriteStorage);
        }

        co_return prewriteStorage;
    }

    // A3：异常分类——ethereum 现状恒 UnknownError（P0-4：SchedulerError 是普通 enum）。
    scheduler::SchedulerError classifyException(std::exception_ptr) const override
    {
        return scheduler::SchedulerError::UnknownError;
    }

public:
    BaselineScheduler(MultiLayerStorage& multiLayerStorage, SchedulerImpl& schedulerImpl,
        Executor& executor, protocol::BlockFactory& blockFactory, Ledger& ledger,
        txpool::TxPoolInterface& txPool,
        protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory,
        crypto::Hash const& hashImpl)
      : SchedulerBase(multiLayerStorage, blockFactory, ledger, transactionSubmitResultFactory),
        m_schedulerImpl(schedulerImpl),
        m_executor(executor),
        m_txpool(txPool),
        m_hashImpl(hashImpl)
    {}
    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) = delete;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) = delete;
    // m_asyncGroup 排空由 SchedulerSkeleton::~SchedulerSkeleton 承担。
    ~BaselineScheduler() noexcept override = default;

    /// The pre-existing latest-state call, verbatim: fork the live view and execute on top
    /// of it. Shared by call() and by callAtBlock() when the requested height IS the latest.
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        auto view = this->m_multiLayerStorage->fork();
        view.newMutable();
        auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
        auto ledgerConfig =
            co_await ledger::getLedgerConfig(view, blockNumber, *this->m_blockFactory);
        auto block =
            co_await ledger::getBlockData(view, blockNumber, ledger::HEADER, *this->m_blockFactory);
        co_return co_await m_executor.get().executeTransaction(
            view, *block->blockHeader(), *transaction, 0, *ledgerConfig, true);
    }

    /// eth_call pinned at @p blockNumber (M13.2, spec §5.13): execute against the state
    /// block N committed — the MPT at block N's header stateRoot — with the call's own
    /// writes landing in a fresh mutable layer stacked on a HistoricalStateBackend
    /// (read-your-writes inside the call, nothing persisted; HistoricalCallStorage.h).
    ///
    /// Gated to scenario B (feature_l2_ethereum_compat): only there is the trie the
    /// COMPLETE state at the root. A scenario-A trie excludes every account dormant since
    /// activation, so a historical call could silently execute against a state where such
    /// accounts read as absent — refused loudly instead (OQ6 resolution, MPTAccount.h:83-85).
    /// Blocks above the latest height and blocks whose header records no state root
    /// (a scenario-B genesis) are refused with explicit errors. Trie-read failures
    /// (MPTMissingNode / MPTDecodeError / MPTInvariantViolation) surface as Error results,
    /// never as swallowed wrong answers.
    void callAtBlock(protocol::Transaction::Ptr transaction, protocol::BlockNumber blockNumber,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                       protocol::BlockNumber blockNumber,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto latestView = self->m_multiLayerStorage->fork();
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
                    latestView, blockNumber, *self->m_blockFactory);
                if (!ledgerConfig->features().get(
                        ledger::Features::Flag::feature_l2_ethereum_compat))
                {
                    callback(BCOS_ERROR_UNIQUE_PTR(scheduler::SchedulerError::InvalidStatus,
                                 fmt::format(
                                     "eth_call: historical call at block {} requires the "
                                     "full-fidelity MPT of an L2 Ethereum-compat chain "
                                     "(feature_l2_ethereum_compat, scenario B); this chain's state "
                                     "at that block is not completely committed to an MPT",
                                     blockNumber)),
                        nullptr);
                    co_return;
                }
                auto block = co_await ledger::getBlockData(
                    latestView, blockNumber, ledger::HEADER, *self->m_blockFactory);
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

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
    {
        task::wait([](decltype(this) self, protocol::Transaction::Ptr transaction,
                       decltype(callback) callback) -> task::Task<void> {
            callback(nullptr, co_await self->coCallLatest(std::move(transaction)));
        }(this, std::move(transaction), std::move(callback)));
    }

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            auto view = self->m_multiLayerStorage->fork();
            auto contractAddress = unhexAddress(contract);
            auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            auto ledgerConfig =
                co_await ledger::getLedgerConfig(view, blockNumber, *self->m_blockFactory);

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

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            auto view = self->m_multiLayerStorage->fork();
            auto contractAddress = unhexAddress(contract);
            auto blockNumber = co_await ledger::getCurrentBlockNumber(view, ledger::fromStorage);
            auto ledgerConfig =
                co_await ledger::getLedgerConfig(view, blockNumber, *self->m_blockFactory);

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

    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) override
    {
        auto view = this->m_multiLayerStorage->fork();
        auto ledgerConfig = co_await ledger::getLedgerConfig(view, number, *this->m_blockFactory);

        ledger::account::EVMAccount account(view, address,
            ledgerConfig->features().get(ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    void stop() override{};

    void registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier)
    {
        this->m_transactionNotifier = std::move(txNotifier);
    }

    void registerBlockNumberNotifier(
        std::function<void(bcos::protocol::BlockNumber)> blockNumberNotifier)
    {
        this->m_blockNumberNotifier = std::move(blockNumberNotifier);
    }

    /// Replace the MPT commit observer (CommitObserver.h). Call at wiring time, before block
    /// flow starts. A null pointer keeps the current observer — the commit path relies on the
    /// member never being empty.
    void setMPTCommitObserver(std::shared_ptr<ledger::mpt::CommitObserver> observer)
    {
        if (observer)
        {
            this->m_mptCommitObserver = std::move(observer);
        }
    }

    void setVersion(int version, ledger::LedgerConfig::Ptr ledgerConfig) override {}
};

}  // namespace bcos::scheduler_v1
