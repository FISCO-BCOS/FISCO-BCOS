// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpBlockScheduler — OP-mode scheduler facade (spec 2026-08-12-op-block-scheduler-design.md v2).
// Role: the single OP scheduler object serving the RPC face (SchedulerInterface, slot 3 in
// MultiVersionScheduler) while inheriting OpSchedulerImpl for the engine-facing seam surface
// (BlockEnv/ExecuteResult/ConsensusError/StorageError/c_ethRawTxTable/commitmentsOf/computeTxRoot/
// isIsthmusActiveAt/isJovianActiveAt) and the block-execution machinery (executeOpBlock + RTTI
// dual-catch) — ready if the engine ever routes block execution through this class.
//
// What it serves:
//   - `call()` — OP eth_call: fork the latest committed state, build the real OP block context
//     (manual LedgerConfig — NOT getLedgerConfig, see coCallLatest; buildOpBlockInfo with ms->s /
//     header baseFee / full field set), load L1Block fee params, run
//     OpstackExecutor::executeTransaction with the injected chainId/blockGasLeft/block hashes,
//     discard the fork (dry-run). Invalid call -> JSON-RPC Error.
//   - `getPendingStorageAt` / `getCode` / `getABI` — pure storage reads (BaselineScheduler-mirror,
//     features only, no header/hash()).
//
// What it refuses loudly: `executeBlock` / `commitBlock` / `preExecuteBlock` — OP block execution
// and commit are engine-driven (newPayload -> SchedulerType::executeOpBlock; registerOpBlock),
// NOT scheduler-driven.
//
// Layering: a pure template header in opstack-executor, same shape as OpSchedulerImpl.h. ViewType
// is the engine's fork-view type (the OpSchedulerImpl base's Storage). OpenedStorage is the
// openable storage (has `.fork()`), injected by the composition root (GlobalStateStorage). Two
// parameters because the base-class Storage and the RPC fork source are different types.
//
// Threading: the inherited OpSchedulerImpl base owns one evmc::VM (block path); call() builds an
// OpstackExecutor per invocation (own VM) — acceptable for an RPC dry-run, not a hot path.

#include <opstack-executor/OpSchedulerImpl.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/FeaturesStorage.h>  // readFromStorage
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
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
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace bcos::executor_v1::opstack
{
template <class ViewType, class OpenedStorage>
class OpBlockScheduler : public bcos::evm::engine::OpSchedulerImpl<ViewType>,
                         public bcos::scheduler::SchedulerInterface
{
public:
    OpBlockScheduler(bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory,
        bcos::crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps,
        bcos::protocol::BlockFactory::Ptr blockFactory, OpenedStorage& storage)
      : bcos::evm::engine::OpSchedulerImpl<ViewType>(receiptFactory, chainId, forkTimestamps),
        m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_blockFactory(std::move(blockFactory)),
        m_storage(storage)
    {}

    // ---- eth_call (the RPC execution path this scheduler serves) ----

    void call(bcos::protocol::Transaction::Ptr transaction,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr)> callback)
        override
    {
        task::wait([this, tx = std::move(transaction),
                       cb = std::move(callback)]() mutable -> task::Task<void> {
            try
            {
                cb(nullptr, co_await coCallLatest(std::move(tx)));
            }
            catch (const std::exception& e)
            {
                // OpstackExecutor throws OpTxValidationFailed for an invalid call and the OP
                // consensus/storage errors for block-context faults; the RPC expects an Error
                // (JSON-RPC error), not a status-0 failure receipt — matching op-geth.
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
            catch (...)
            {
                // Typed-catch RTTI bypass (same phenomenon documented in the block path's
                // catch(...) clause, OpSchedulerImpl.h:256-273): the -fno-rtti libevmone.a brings
                // a hidden non-unique typeinfo for std::exception, so the catch(const
                // std::exception&) above does NOT reliably bind std::runtime_error thrown by
                // evmone/opstack-linked code inside coCallLatest -> OpstackExecutor::
                // executeTransaction. Without this fallback such throws escape call() -> task::wait
                // into the EthEndpoint awaitable's unguarded await_suspend, crashing/hanging the
                // RPC coroutine instead of returning a JSON-RPC Error. The original message is
                // unrecoverable here (no typed handle on the caught object).
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                       "OpBlockScheduler::call: unknown (RTTI-bypassed) exception"),
                    nullptr);
            }
        }());
    }

    // ---- storage reads (BaselineScheduler-mirror; features only — NO getLedgerConfig) ----

    void getCode(std::string_view contract,
        std::function<void(bcos::Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_storage.fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                // No getLedgerConfig: its header.hash() throws EmptyBlockHeaderHash on OP headers.
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
                auto view = self->m_storage.fork();
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
        auto view = m_storage.fork();
        bcos::ledger::Features features;
        co_await bcos::ledger::readFromStorage(features, view, number);
        bcos::ledger::account::EVMAccount account(
            view, address, features.get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    // ---- block execution / commit are engine-driven; refuse loudly ----

    void executeBlock(bcos::protocol::Block::Ptr /*block*/, bool /*verify*/,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                     "OpBlockScheduler: executeBlock is not supported in OP mode; block execution "
                     "is engine-driven (newPayload -> executeOpBlock)"),
            nullptr, false);
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr /*header*/,
        std::function<void(bcos::Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                     "OpBlockScheduler: commitBlock is not supported in OP mode; block commit is "
                     "engine-driven (runOpNewPayloadSteps -> registerOpBlock)"),
            nullptr);
    }

    void preExecuteBlock(bcos::protocol::Block::Ptr /*block*/, bool /*verify*/,
        std::function<void(bcos::Error::Ptr)> callback) override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
            "OpBlockScheduler: preExecuteBlock is not supported in OP mode"));
    }

    // ---- lifecycle: no pipeline to manage, no state to report ----

    void status(
        std::function<void(bcos::Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }

    void reset(std::function<void(bcos::Error::Ptr)> callback) override { callback({}); }

private:
    /// Strict hex-address parse for getCode/getABI.
    static evmc_address parseAddress(std::string_view view)
    {
        evmc_address out{};
        if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
            view.remove_prefix(2);
        if (view.size() != sizeof(out.bytes) * 2)
            throw std::invalid_argument("OpBlockScheduler: invalid address (need 40 hex chars)");
        boost::algorithm::unhex(view.begin(), view.end(), out.bytes);
        return out;
    }

    /// OP eth_call: fork the latest committed state, build the real OP block context, run the
    /// transaction through OpstackExecutor's injection-style executeTransaction, discard the fork.
    ///
    /// LedgerConfig is built MANUALLY — NOT via getLedgerConfig: that CPO calls header.hash()
    /// (LedgerMethods.h:347) which throws EmptyBlockHeaderHash on OP headers (registerOpBlock
    /// persists them with an empty dataHash; the OP hash lives in the codec, see
    /// EngineServiceImpl.h:1253-1255). Production-path defect. evmcRevision = cfg.rev — the fork
    /// schedule IS the OP chain's revision source (same config the block path executes with),
    /// sidestepping the ledger's executor_version gate on evmc_revision entirely.
    task::Task<bcos::protocol::TransactionReceipt::Ptr> coCallLatest(
        bcos::protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

        auto view = m_storage.fork();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, (*m_blockFactory));
        // Keep the header Ptr alive: blockHeader() returns a fresh shared_ptr BY VALUE, so
        // `auto const& header = *block->blockHeader();` would bind a reference to a temporary that
        // is destroyed at the end of that statement (dangling ref -> segfault on timestamp()).
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
            detail::narrowU256ToU64(header.gasLimit(), "OpBlockScheduler blockGasLeft"));

        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<ViewType> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        bcos::executor_v1::opstack::OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpBlockScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
    }

    bcos::protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    bcos::crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    OpenedStorage& m_storage;
};
}  // namespace bcos::executor_v1::opstack
