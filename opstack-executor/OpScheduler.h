// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpScheduler — the OP-specific scheduler. Derives from SchedulerSkeleton, implementing the OP's
// 5 CRTP hooks + 4 pure virtuals (call/getCode/getABI/getPendingStorageAt) + classifyException;
// locks/continuity/backpressure/view/queues/notifier/flow are inherited. A template header — the
// real GlobalStateStorage is instantiated by the composition root at slot 3, so it is not named
// here. OP block execution goes through the execute hook → runOpBlockInjection; the block's
// Transaction objects are consumed directly (opEnvelopeToTars conversion happened in buildOpBlock).

#include <opstack-executor/OpBlockExecute.h>  // runOpBlockInjection / OpBlockSeal
#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpTxDecode.h>  // detail::decodeOneRawTx (execute hook)
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>
#include <transaction-scheduler/bcos-transaction-scheduler/SchedulerSkeleton.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/engine/Errors.h>  // OpExecutionInternalError (commit-hook guard)
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
/// the commit hook: the execution result, the raw EIP-2718 envelopes, and the announced block hash.
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
        /// The announced header's opHeaderHash — stashed at execute time so the commit hook keys
        /// the tables by the authoritative block hash; the executed header carries only commitment
        /// fields, so computing opHeaderHash on it would throw std::bad_optional_access.
        bcos::crypto::HashType announcedBlockHash;
    };

public:
    // 5 CRTP hooks + 4 pure virtuals + classify are public (CRTP non-virtual dispatch via
    // derived()); status/reset/preExecuteBlock use the skeleton defaults.
    /// ① Transaction source: block.transactions() → Transaction::ConstPtr (OP blocks carry
    /// inline transactions, no txpool). extraTransactionBytes holds the full envelope.
    task::Task<std::vector<protocol::Transaction::ConstPtr>> getTransactions(
        protocol::Block& block, ViewType& /*view*/)
    {
        co_return ::ranges::views::transform(block.transactions(), [](auto tx) {
            return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
        }) | ::ranges::to<std::vector>();
    }

    /// ② Execution kernel: runOpBlockInjection. Assembly: rawTxBytes = each tx's
    /// extraTransactionBytes (the full envelope); deposits = decoded 0x7E envelopes; the block's
    /// Transaction objects are consumed as-is (buildOpBlock converted them via opEnvelopeToTars —
    /// no raw parse, no composition-root converter); cfg = configAt(timestamp/1000); executor = a
    /// per-block OpstackExecutor (one evmc::VM); ledgerConfig only needs evmcRevision.
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
            const auto& cfg =
                op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

            // Classify by type byte: deposits decoded; normal txs consumed as the block's
            // Transaction objects (buildOpBlock already converted them via opEnvelopeToTars) — no
            // raw parse.
            std::vector<op::DepositTx> deposits;
            deposits.reserve(rawTxBytes.size());
            for (auto const& raw : rawTxBytes)
            {
                if (raw.empty())  // 空 envelope：直接 raw[0] 会越界（旧 decodeOneRawTx 的 empty
                                  // 拒绝保留）
                    throw bcos::evm::engine::OpConsensusError("OpScheduler: empty envelope");
                auto const typeByte = raw[0];
                if (typeByte == static_cast<uint8_t>(op::kDepositTxType))
                    deposits.push_back(detail::decodeDepositTx(raw));  // OpConsensusError on
                                                                       // malformed
                else if (typeByte < 0xc0 && typeByte != 0x01 && typeByte != 0x02 &&
                         typeByte != 0x04)
                    throw bcos::evm::engine::OpConsensusError(
                        "OpScheduler: unsupported tx type byte 0x" + std::to_string(typeByte));
            }

            bcos::ledger::LedgerConfig ledgerConfig;
            ledgerConfig.setEVMCRevision(cfg.rev);

            OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

            result = bcos::evm::engine::runOpBlockInjection(executor, view, header, transactions,
                deposits, cfg, m_chainId, ledgerConfig, rawTxBytes, m_hashImpl);
        }
        catch (const bcos::evm::engine::OpConsensusError&)
        {
            // FISCO types bind reliably — rethrow as-is, preserving the message (describeException
            // restores it at the skeleton backstop). Do not fall to catch(std::exception&) (the
            // -fno-rtti evmone boundary's typeinfo is unreliable) or catch(...) (message lost).
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            // Keep the type and message — classifyException maps it to OpStorageFault (-32603).
            throw;
        }
        catch (const std::exception&)
        {
            throw;  // Bindable families → the skeleton classifies.
        }
        catch (...)
        {
            // RTTI bypass: -fno-rtti evmone types escape the typed catch with broken typeinfo and
            // would bypass the skeleton's catch(std::exception&). Normalize to a FISCO type so
            // classifyException receives a catchable one.
            throw bcos::evm::engine::OpConsensusError(
                "OpScheduler: runOpBlockInjection threw an unrecognized (RTTI-bypassed) exception; "
                "raw tx decode or block-level consensus fault");
        }

        bcos::scheduler_v1::SchedulerExecuteResult out;
        // Copy (cheap shared_ptr vector), not move: both SchedulerExecuteResult.receipts and
        // OpExecuteExtra.result.receipts must hold the full receipts.
        out.receipts = result.receipts;
        // No txpool submission on the OP path — a non-empty empty table keeps the skeleton's
        // notifyBlockNumber from dereferencing a null transactions vector.
        out.m_transactions = std::make_shared<protocol::ConstTransactions>();
        // Authoritative block hash (announced header's opHeaderHash), stashed for the commit hook;
        // the executed header's optional fields are incomplete, so it cannot be recomputed there.
        out.modeExtra = std::make_shared<OpExecuteExtra>(
            OpExecuteExtra{std::move(result), std::move(rawTxBytes),
                header.opHeaderHash(
                    bcos::protocol::BlockHeader::OpHeaderConst{.ommersHash = c_emptyOmmersHash,
                        .difficulty = bcos::u256(0),
                        .nonce = c_posNonce})});
        co_return out;
    }

    /// Override of the skeleton's fastPathHit (hard-contract guard). The base matches only on
    /// block number, which does not uniquely identify an OP block: if the previous block executed
    /// but commit failed and op-node resends a *different* block at the same height, the base would
    /// hit the stale executedHeader, skip execution + verify, and report the new payload VALID —
    /// forking the chain. This adds a comparison on a hit: the cached announcedBlockHash must equal
    /// the incoming header's opHeaderHash, else no hit (forcing re-execution). Range-matching is
    /// verbatim the base's, under a single lock (no TOCTOU).
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
                // Different block at this height: the cached executedHeader must not stand in.
                BASELINE_SCHEDULER_LOG(INFO) << "Fast-path cache holds a different block at height "
                                             << number << "; ignoring cache and re-executing";
                return std::nullopt;
            }
            BASELINE_SCHEDULER_LOG(INFO) << "Block has been executed, return result directly";
            return std::pair{result->m_executedBlockHeader, result->m_sysBlock};
        }
        return std::nullopt;
    }

    /// ③ finish: write the OP commitments into the executedHeader (skip MPT; never call
    /// BlockHeader::hash() — the header is rebuilt from the announced payload).
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

    /// ④ verify: unconditional six-way comparison; a mismatch throws OpConsensusError →
    /// OpConsensusRejected. Returns null = pass.
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

    /// ⑤ commit: reuse ledger::prewriteBlockToBuffer to persist the 7 SYS tables into a standalone
    /// MutableStorage (the skeleton's only mergeBackStorage). blockHash = the execute hook's
    /// announcedBlockHash (the announced header's opHeaderHash, validated by engine step 2), never
    /// recomputed on the executed header (its optional fields are incomplete → would throw).
    /// writeNonces=false (OP never writes SYS_BLOCK_NUMBER_2_NONCES).
    /// **The registered header is the announced one (result.m_block->blockHeader()), NOT the
    /// executed header** — the executed header's tars encode is incomplete (missing
    /// coinbase/gasLimit/baseFee/...), which a child block's parent-header read would reject.
    task::Task<std::shared_ptr<typename MultiLayerStorage::MutableStorage>> commit(
        ViewType& /*view*/, protocol::BlockHeader::Ptr /*header*/,
        bcos::scheduler_v1::SchedulerExecuteResult const& result)
    {
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(result.modeExtra);
        auto storage = std::make_shared<typename MultiLayerStorage::MutableStorage>();

        // 守卫：回执数必须等于交易数（保留旧 opstackRegisterBlock 不变量）。
        if (result.receipts.size() != result.m_block->transactionsSize())
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a receipt count differing "
                                      "from the transaction count"});

        auto block = result.m_block;
        // 幂等：commit 失败重试同一 pending result 会二次 append，先 clear 再 append。
        block->clearReceipts();
        for (auto const& r : result.receipts)
            block->appendReceipt(r);
        // setStoreToBackend 只应落在 toShared() 的 fresh 副本上；此处重置是防御性冗余。
        for (auto const& tx : block->transactions())
            tx->setStoreToBackend(false);

        // blockTxs：复用 getTransactions 钩子写法（AnyTransaction 无 ConstPtr 转换，toShared()
        // 限定）。
        auto blockTxs = std::make_shared<protocol::ConstTransactions>(
            block->transactions() | ::ranges::views::transform([](auto tx) {
                return protocol::Transaction::ConstPtr{std::move(tx).toShared()};
            }) |
            ::ranges::to<std::vector>());

        co_await bcos::ledger::prewriteBlockToBuffer(*this->m_ledger, blockTxs, block, *storage,
            extra.announcedBlockHash, /*writeNonces=*/false);
        co_return storage;
    }

    /// Test observation surface: returns the raw execution result of the latest pending block
    /// (after pushResult, m_results.front() is the newest). Not consumed by production.
    std::optional<bcos::evm::engine::OpExecuteBlockResult> peekExecuteResult()
    {
        std::unique_lock<std::mutex> lock(this->m_resultsMutex);
        if (this->m_results.empty())
            return std::nullopt;
        auto& extra = *std::static_pointer_cast<OpExecuteExtra>(this->m_results.front()->modeExtra);
        return extra.result;
    }

    // ---- Pure virtuals: call / storage reads (the skeleton does not implement these;
    // ---- each derived class does)

    /// eth_call: coCallLatest with injection + a hand-built LedgerConfig + double catch.
    /// Errors go back via the callback as a JSON-RPC Error, never a status-0 receipt.
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
                // Typed-catch RTTI bypass: catch(std::exception&) does not reliably bind across the
                // -fno-rtti evmone boundary — return an Error rather than dangling the coroutine.
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

    // Exception classification — OpConsensusError→OpConsensusRejected / OpStorageError→
    // OpStorageFault / other→UnknownError, via rethrow + catch (the RTTI trap across the -fno-rtti
    // evmone boundary is handled by the execute hook's catch(...) backstop).
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

    /// Error-message recovery at the skeleton's catch(...) backstop: rethrow + typed catch, so the
    /// engine barrier can emit a detailed validationError. FISCO types bind reliably and yield
    /// what(); catch(std::exception&) cannot reliably bind across the -fno-rtti evmone boundary.
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
        bcos::ledger::LedgerInterface::Ptr ledger = nullptr)
      : SchedulerBase(),
        m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps)
    {
        this->m_multiLayerStorage = &multiLayerStorage;
        this->m_blockFactory = blockFactory.get();
        // Reuse the skeleton's protected m_ledger (consumed by commit's prewriteBlockToBuffer).
        // A null ledger is tolerated by the execute path but committing would throw a null deref.
        if (ledger)
        {
            this->m_ledger = ledger.get();
        }
        // OP has no RPC push needs — register no-op notifiers (a default-empty std::function would
        // throw bad_function_call inside an async task → terminate), overridable by the
        // composition root.
        this->m_blockNumberNotifier = [](bcos::protocol::BlockNumber) {};
        this->m_transactionNotifier =
            [](bcos::protocol::BlockNumber, bcos::protocol::TransactionSubmitResultsPtr,
                std::function<void(bcos::Error::Ptr)> cb) { cb(nullptr); };
    }
    OpScheduler(const OpScheduler&) = delete;
    OpScheduler& operator=(const OpScheduler&) = delete;
    ~OpScheduler() noexcept override = default;

public:
    /// The skeleton default calls header.hash(), which throws for an OP header — build the
    /// LedgerConfig by hand (features only). Public: coExecuteBlock reaches it via derived().
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

    /// Same: the skeleton default calls header.hash(); the OP commit path never does. Public for
    /// the same reason as loadLedgerConfig.
    task::Task<ledger::LedgerConfig::Ptr> loadCommitLedgerConfig(protocol::BlockHeader::Ptr header)
    {
        auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
        ledgerConfig->setBlockNumber(header->number());
        ledgerConfig->setTimestamp(header->timestamp());
        co_return ledgerConfig;
    }

    /// Commit continuity (head-advance guard): prewriteBlockToBuffer writes SYS_CURRENT_STATE
    /// unconditionally, so this override restores the monotonic guard (rejecting already-committed
    /// / discontinuous commits). Reads the storage view (where the OP commit writes), not the
    /// ledger's own m_stateStorage. Public for the same reason as loadCommitLedgerConfig. The
    /// isSysContractDeploy special case (block-0 system-contract deployment) is retained.
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
    /// Project an executed/announced header's commitment fields into the six-way comparison
    /// surface; both sides read the same accessors.
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

    /// OP eth_call: fork the latest committed state, build a real OP block context (hand-built
    /// LedgerConfig), load the L1Block fee params, run OpstackExecutor::executeTransaction
    /// (injecting chainId/blockGasLeft/block hashes), then discard the fork (dry-run).
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

        // Construct the executor per call (one evmc::VM).
        OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
    }

    // 3 post-merge OP header constants for the commit's opHeaderHash (the engine injects them via
    // detail::opHeaderConst(); opstack-executor does not link engine).
    static const bcos::h256 c_emptyOmmersHash;
    static const bcos::h64 c_posNonce;

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
};

template <class MultiLayerStorage>
const bcos::h256 OpScheduler<MultiLayerStorage>::c_emptyOmmersHash{
    std::string{"0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"}};
template <class MultiLayerStorage>
const bcos::h64 OpScheduler<MultiLayerStorage>::c_posNonce{std::string{"0x0000000000000000"}};

}  // namespace bcos::executor_v1::opstack
