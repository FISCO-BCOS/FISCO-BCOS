#pragma once

// Declaration-only header for BaselineScheduler: member definitions live in
// BaselineScheduler-tpp.h so that translation units which only use the class through
// the production (extern template) specializations do not pay for parsing the
// implementation and its heavy include set (TBB / fmt / boost / storage2 / MPT).
// Include BaselineScheduler-tpp.h instead wherever a NEW specialization is
// instantiated (unit tests with mock executors / schedulers).

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-framework/transaction-scheduler/TransactionScheduler.h"
#include "bcos-ledger/mpt/CommitObserver.h"
#include "bcos-ledger/mpt/MPTDeltaLayer.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <oneapi/tbb/task_group.h>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace bcos::protocol
{
class BlockFactory;
class TransactionSubmitResultFactory;
}  // namespace bcos::protocol
namespace bcos::txpool
{
class TxPoolInterface;
}
namespace bcos::crypto
{
class Hash;
}

namespace bcos::scheduler_v1
{
#define BASELINE_SCHEDULER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("BASELINE_SCHEDULER")

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

template <class MultiLayerStorage,
    executor_v1::TransactionExecutor<typename MultiLayerStorage::ViewType> Executor,
    scheduler_v1::TransactionScheduler<typename MultiLayerStorage::ViewType, Executor,
        std::vector<protocol::Transaction::ConstPtr>>
        SchedulerImpl,
    class Ledger>
class BaselineScheduler : public scheduler::SchedulerInterface
{
private:
    std::reference_wrapper<MultiLayerStorage> m_multiLayerStorage;
    std::reference_wrapper<std::remove_reference_t<SchedulerImpl>> m_schedulerImpl;
    std::reference_wrapper<Executor> m_executor;
    std::reference_wrapper<protocol::BlockFactory> m_blockFactory;
    std::reference_wrapper<Ledger> m_ledger;
    std::reference_wrapper<txpool::TxPoolInterface> m_txpool;
    std::reference_wrapper<protocol::TransactionSubmitResultFactory>
        m_transactionSubmitResultFactory;
    std::function<void(bcos::protocol::BlockNumber)> m_blockNumberNotifier;
    std::function<void(bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
        std::function<void(Error::Ptr)>)>
        m_transactionNotifier;
    std::reference_wrapper<crypto::Hash const> m_hashImpl;

    // FIB-101 / FIB-102: Each counter is owned by exactly one mutex and is read /
    // written only while that mutex is held. Plain integers are sufficient — no
    // atomics needed. m_lastExecutedBlockNumber is owned by m_executeMutex;
    // m_lastCommittedBlockNumber is owned by m_commitMutex.
    int64_t m_lastExecutedBlockNumber{-1};
    std::mutex m_executeMutex;
    int64_t m_lastCommittedBlockNumber{-1};
    std::mutex m_commitMutex;
    tbb::task_group m_asyncGroup;

    struct ExecuteResult
    {
        protocol::ConstTransactionsPtr m_transactions;
        std::vector<protocol::TransactionReceipt::Ptr> m_receipts;
        protocol::BlockHeader::Ptr m_executedBlockHeader;
        protocol::Block::Ptr m_block;
        bool m_sysBlock{};
        /// Engaged when the block was executed with an MPT state root (shouldBuildMPT).
        /// CommitObserver payload ONLY: coCommitBlock hands the whole delta to the observer
        /// after the merge lands. Nothing else reads it — node PERSISTENCE goes through the
        /// block's mutable layer into mergeBackStorage as ordinary state rows
        /// (MPTNodeStorage.h), and the NEXT block reads its parent root from the published
        /// header row, not from here (buildMPTStateRoot), so this field is not a channel any
        /// consensus-path data flows through.
        std::optional<ledger::mpt::MPTDeltaLayer> m_mptDelta;
    };
    std::deque<std::shared_ptr<ExecuteResult>> m_results;
    std::mutex m_resultsMutex;

    /// Post-commit hook over each MPT block's node delta — the pathdb pruning seam
    /// (CommitObserver.h). Defaults to the no-op observer; replaced via setMPTCommitObserver.
    /// Written only before block flow starts (wiring time), read on the commit path.
    std::shared_ptr<ledger::mpt::CommitObserver> m_mptCommitObserver =
        std::make_shared<ledger::mpt::NoopCommitObserver>();

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
        ledger::LedgerConfig const& ledgerConfig);


    /**
     * Executes a block and returns a tuple containing an error (if any), the block header, and
     * a boolean indicating success.
     *
     * @param block The block to execute.
     * @param verify Whether to verify the block before executing it.
     * @return A tuple containing an error (if any), the block header, and a boolean indicating
     * success.
     */
    task::Task<std::tuple<bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool>> coExecuteBlock(
        bcos::protocol::Block::Ptr block, bool verify);


    /**
     * Commits a block to the ledger and returns an error object and a ledger configuration
     * object.
     *
     * @param header A shared pointer to the block header to be committed.
     * @return A task that returns a tuple containing an error object and a ledger configuration
     * object.
     */
    task::Task<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> coCommitBlock(
        protocol::BlockHeader::Ptr header);

public:
    BaselineScheduler(MultiLayerStorage& multiLayerStorage, SchedulerImpl& schedulerImpl,
        Executor& executor, protocol::BlockFactory& blockFactory, Ledger& ledger,
        txpool::TxPoolInterface& txPool,
        protocol::TransactionSubmitResultFactory& transactionSubmitResultFactory,
        crypto::Hash const& hashImpl);

    BaselineScheduler(const BaselineScheduler&) = delete;
    BaselineScheduler(BaselineScheduler&&) = delete;
    BaselineScheduler& operator=(const BaselineScheduler&) = delete;
    BaselineScheduler& operator=(BaselineScheduler&&) = delete;
    ~BaselineScheduler() noexcept override;


    void executeBlock(bcos::protocol::Block::Ptr block, bool verify,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool sysBlock)>
            callback) override;


    void commitBlock(protocol::BlockHeader::Ptr header,
        std::function<void(Error::Ptr, ledger::LedgerConfig::Ptr)> callback) override;


    void status([[maybe_unused]] std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)>
            callback) override;


    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override;


    /// The pre-existing latest-state call, verbatim: fork the live view and execute on top
    /// of it. Shared by call() and by callAtBlock() when the requested height IS the latest.
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction);


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
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override;


    void reset([[maybe_unused]] std::function<void(Error::Ptr)> callback) override;


    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override;


    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override;


    task::Task<std::optional<bcos::storage::Entry>> getPendingStorageAt(std::string_view address,
        std::string_view key, bcos::protocol::BlockNumber number) override;


    void preExecuteBlock([[maybe_unused]] bcos::protocol::Block::Ptr block,
        [[maybe_unused]] bool verify,
        [[maybe_unused]] std::function<void(Error::Ptr)> callback) override;


    void stop() override;


    void registerTransactionNotifier(std::function<void(bcos::protocol::BlockNumber,
            bcos::protocol::TransactionSubmitResultsPtr, std::function<void(Error::Ptr)>)>
            txNotifier);


    void registerBlockNumberNotifier(
        std::function<void(bcos::protocol::BlockNumber)> blockNumberNotifier);


    /// Replace the MPT commit observer (CommitObserver.h). Call at wiring time, before block
    /// flow starts. A null pointer keeps the current observer — the commit path relies on the
    /// member never being empty.
    void setMPTCommitObserver(std::shared_ptr<ledger::mpt::CommitObserver> observer);


    void setVersion(int version, ledger::LedgerConfig::Ptr ledgerConfig) override;
};

}  // namespace bcos::scheduler_v1
