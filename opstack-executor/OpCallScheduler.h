// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// OpCallScheduler — OP-mode RPC-facing scheduler adapter (eth_call with OP semantics).
//
// Role: implements `bcos::scheduler::SchedulerInterface` so MultiVersionScheduler version 3
// routes the RPC `call()` (eth_call) through OP execution instead of silently saturating to the
// v2 ethereum scheduler. Before this class, version 3 ran eth_call on the pure-Ethereum
// EthereumExecutor — wrong for an OP chain: no L1Block fee state, no OP fee vaults, no deposit
// handling (see docs/opstack-scheduler-adapter-design.md §0).
//
// What it serves:
//   - `call()` — OP eth_call: fork the latest committed state, then run the transaction through
//     OpstackExecutor's injection-style executeTransaction with the real OP block context
//     (chainId, block.gasLimit, L1Block fee params, storage-backed BLOCKHASH provider) — the same
//     context processOpBlock executes a block with. The fork is discarded (dry-run).
//   - `getPendingStorageAt` / `getCode` / `getABI` — pure storage reads, byte-for-byte the
//     BaselineScheduler implementations (executor-version independent; the txpool validation path
//     uses getPendingStorageAt).
//
// What it refuses loudly: OP block execution is engine-driven (EngineServiceImpl's newPayload ->
// SchedulerType::executeOpBlock), NOT scheduler-driven. `executeBlock` / `commitBlock` /
// `preExecuteBlock` fail with an explicit error instead of running any executor silently — the
// fix for the old version-3 saturation, where these forwarded to the ethereum scheduler.
//
// Layering: a pure template header in opstack-executor, same shape as OpSchedulerImpl.h — Storage
// is the opened-storage type (has `.fork()`); the instantiation in libinitializer is
// `GlobalStateStorage` (a MultiLayerStorage). Non-template RPC plumbing (BlockFactory, storage)
// is injected by the composition root.
//
// Scope note: deposits (0x7E) are delivered by op-node via engine API, never via RPC eth_call
// (the RPC builds EIP-1559/2930/legacy web3 transactions), so this adapter serves normal
// transactions only. A 0x7E-typed call would be rejected by opValidate as a malformed normal tx —
// a loud failure, acceptable for an unreachable path.

#include "bcos-ledger/LedgerMethods.h"

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-framework/dispatcher/SchedulerInterface.h>
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/Features.h>
#include <bcos-framework/ledger/Ledger.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <boost/algorithm/hex.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <ethereum-executor/StorageStateView.h>
#include <opstack-executor/OpRlpDecode.h>
#include <opstack-executor/OpstackExecutor.h>
#include <opstack-executor/RecentBlockHashes.h>

namespace bcos::executor_v1::opstack
{

/// OP-mode RPC-facing scheduler adapter. Storage is the opened-storage type (e.g.
/// `initializer::GlobalStateStorage`); `call()` forks it per invocation.
template <class Storage>
class OpCallScheduler : public bcos::scheduler::SchedulerInterface
{
public:
    /// The forked per-call view type (what `Storage::fork()` returns).
    using ViewType = decltype(std::declval<Storage&>().fork());

    OpCallScheduler(protocol::TransactionReceiptFactory::Ptr receiptFactory,
        crypto::Hash::Ptr hashImpl, uint64_t chainId,
        bcos::evm::opstack::OpForkTimestamps forkTimestamps,
        protocol::BlockFactory::Ptr blockFactory, Storage& storage)
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_chainId(chainId),
        m_forkTimestamps(forkTimestamps),
        m_blockFactory(std::move(blockFactory)),
        m_storage(storage)
    {}

    // ---- eth_call (the only execution path this scheduler serves) ----

    void call(protocol::Transaction::Ptr transaction,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::Ptr)> callback) override
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
                // (JSON-RPC error), not a status-0 failure receipt — matching op-geth, where an
                // invalid eth_call is an error.
                cb(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError, e.what()),
                    nullptr);
            }
        }());
    }

    // ---- storage reads (mirror BaselineScheduler.h:1018-1076, executor-independent) ----

    void getCode(
        std::string_view contract, std::function<void(Error::Ptr, bcos::bytes)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_storage.fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                auto ledgerConfig = co_await bcos::ledger::getLedgerConfig(
                    view, blockNumber, (*self->m_blockFactory));

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    ledgerConfig->features().get(
                        bcos::ledger::Features::Flag::feature_raw_address));
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

    void getABI(
        std::string_view contract, std::function<void(Error::Ptr, std::string)> callback) override
    {
        task::wait([](decltype(this) self, std::string_view contract,
                       decltype(callback) callback) -> task::Task<void> {
            try
            {
                auto view = self->m_storage.fork();
                auto blockNumber =
                    co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
                auto ledgerConfig = co_await bcos::ledger::getLedgerConfig(
                    view, blockNumber, (*self->m_blockFactory));

                bcos::ledger::account::EVMAccount account(view, parseAddress(contract),
                    ledgerConfig->features().get(
                        bcos::ledger::Features::Flag::feature_raw_address));
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
        auto ledgerConfig = co_await bcos::ledger::getLedgerConfig(view, number, (*m_blockFactory));

        bcos::ledger::account::EVMAccount account(view, address,
            ledgerConfig->features().get(bcos::ledger::Features::Flag::feature_raw_address));
        co_return co_await account.storageEntry(key);
    }

    // ---- block execution is engine-driven; refuse loudly ----

    void executeBlock(bcos::protocol::Block::Ptr /*block*/, bool /*verify*/,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockHeader::Ptr, bool)> callback)
        override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                     "OpCallScheduler: executeBlock is not supported in OP mode; block execution "
                     "is engine-driven (newPayload -> executeOpBlock)"),
            nullptr, false);
    }

    void commitBlock(bcos::protocol::BlockHeader::Ptr /*header*/,
        std::function<void(Error::Ptr, bcos::ledger::LedgerConfig::Ptr)> callback) override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
                     "OpCallScheduler: commitBlock is not supported in OP mode; block commit is "
                     "engine-driven (commitBlock -> opstackRegisterBlock)"),
            nullptr);
    }

    void preExecuteBlock(bcos::protocol::Block::Ptr /*block*/, bool /*verify*/,
        std::function<void(Error::Ptr)> callback) override
    {
        callback(BCOS_ERROR_PTR(bcos::scheduler::SchedulerError::UnknownError,
            "OpCallScheduler: preExecuteBlock is not supported in OP mode"));
    }

    // ---- lifecycle: no pipeline to manage, no state to report ----

    void status(
        std::function<void(Error::Ptr, bcos::protocol::Session::ConstPtr)> callback) override
    {
        callback({}, {});
    }

    void reset(std::function<void(Error::Ptr)> callback) override { callback({}); }

private:
    /// Strict hex-address parse for getCode/getABI: optional 0x prefix, exactly 40 hex chars
    /// (mirrors the guards the ethereum path's unhexAddress applies; FixedBytes::FromHex is
    /// deliberately not used — it left-fills a short input instead of rejecting it).
    static evmc_address parseAddress(std::string_view view)
    {
        evmc_address out{};
        if (view.size() >= 2 && view[0] == '0' && (view[1] == 'x' || view[1] == 'X'))
            view.remove_prefix(2);
        if (view.size() != sizeof(out.bytes) * 2)
            throw std::invalid_argument("OpCallScheduler: invalid address (need 40 hex chars)");
        boost::algorithm::unhex(view.begin(), view.end(), out.bytes);
        return out;
    }

    /// OP eth_call: fork the latest committed state, build the real OP block context, run the
    /// transaction through OpstackExecutor's injection-style executeTransaction, discard the fork.
    task::Task<protocol::TransactionReceipt::Ptr> coCallLatest(
        protocol::Transaction::Ptr transaction)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

        // Same acquisition sequence as BaselineScheduler::coCallLatest (BaselineScheduler.h:914)
        // — the fork is the latest committed view; the block header / ledger config come from it.
        auto view = m_storage.fork();
        view.newMutable();
        auto blockNumber =
            co_await bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage);
        auto ledgerConfig =
            co_await bcos::ledger::getLedgerConfig(view, blockNumber, (*m_blockFactory));
        auto block = co_await bcos::ledger::getBlockData(
            view, blockNumber, bcos::ledger::HEADER, (*m_blockFactory));
        auto const& header = *block->blockHeader();

        // Active OP fork at the head block (tars ms -> seconds), same configAt executeOpBlock uses.
        const auto& cfg =
            op::configAt(static_cast<uint64_t>(header.timestamp()) / 1000, m_forkTimestamps);

        // Real OP injection params, mirroring processOpBlock:
        //  - fee: L1Block slot values read from the forked state (no attributes-tx calldata
        //    override — a call has no deposit; the latest committed L1Block slots are the values
        //    the next block would inherit);
        //  - blockGasLeft: the head block's gas limit (op-geth eth_call defaults the call gas to
        //    the block limit);
        //  - chainId: the injected OP chain id, the same source block execution decodes with.
        eth::StorageStateView<ViewType> stateView(view);
        auto fee = op::loadOpFeeParams(stateView);
        const auto blockGasLeft = static_cast<int64_t>(
            detail::narrowU256ToU64(header.gasLimit(), "OpCallScheduler blockGasLeft"));

        // BLOCKHASH: lazy-loading provider over the head block's ancestors (SYS_NUMBER_2_HASH),
        // seeded {N-1: parentHash} — the same provider executeOpBlock uses.
        std::optional<std::string> hashErr;
        detail::RecentBlockHashes<ViewType> hashes(
            view, header.number(), detail::toEvmcBytes32(header.parentInfo().blockHash), &hashErr);

        // Fork-aware: the executor is built per call so its OpForkConfig matches the head block's
        // timestamp (a chain that activates Jovian mid-flight gets the right config from then on).
        // An evmc::VM is created per call — acceptable for an RPC dry-run, not a hot path.
        bcos::executor_v1::opstack::OpstackExecutor executor(m_receiptFactory, m_hashImpl, cfg);

        auto receipt = co_await executor.executeTransaction(view, header, *transaction,
            /*contextID=*/0, *ledgerConfig, /*call=*/true, fee, blockGasLeft, m_chainId, &hashes);

        if (hashErr.has_value())
            throw std::runtime_error("OpCallScheduler: block-hash lookup failed: " + *hashErr);
        co_return receipt;
    }

    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    uint64_t m_chainId;
    bcos::evm::opstack::OpForkTimestamps m_forkTimestamps;
    protocol::BlockFactory::Ptr m_blockFactory;
    Storage& m_storage;
};

}  // namespace bcos::executor_v1::opstack
