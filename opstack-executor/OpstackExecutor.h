/// @file OpstackExecutor.h
/// @brief OP Stack (Optimism L2) transaction executor based on bcos-evm/opstack.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept (ExecuteContext with
/// prepare/execute/finish): opValidate + opTransition for NORMAL transactions, runDeposit for
/// 0x7E deposits, finalizeOpBlock for block finalize. The caller passes an already-decoded
/// DepositTx. Storage-backed StateView and state-diff writeback are shared with EthereumExecutor
/// via ethereum-executor.

#pragma once

#include "bcos-evm/opstack/OpFeeParams.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/BCOS2Evmone.h"
#include "ethereum-executor/StorageStateView.h"
#include "opstack-executor/OpRlpDecode.h"  // detail::narrowU256ToU64 / toEvmcAddress / toEvmcBytes32
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <evmc/evmc.hpp>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace bcos::evm::opstack
{
// Defined in OpBlockExecute.cpp — forward-declared here to avoid including OpBlockExecute.h
// (which includes this header).
evmone::state::StateDiff finalizeOpBlock(
    const evmone::state::StateView& view, const OpForkConfig& cfg, const evmc::address& coinbase);
}  // namespace bcos::evm::opstack

namespace bcos::executor_v1::opstack
{

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

class OpstackExecutor
{
public:
    OpstackExecutor(protocol::TransactionReceiptFactory::Ptr receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig const& forkConfig = bcos::evm::opstack::jovianConfig())
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(forkConfig),
        m_vm(evmc_create_evmone())
    {}

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// eth_call block context, mirroring detail::toBlockInfo: lenient optionals (unset header
    /// fields read as 0 rather than throwing), gasLimit injected as blockGasLeft.
    static evmone::state::BlockInfo buildBlockInfo(
        protocol::BlockHeader const& header, uint64_t gasLimit)
    {
        return bcos::evm::engine::detail::toBlockInfo(header, gasLimit, /*lenientOptionals=*/true);
    }

    /// Real header gasLimit, falling back to the caller's blockGasLeft when the header leaves it
    /// unset (==0, e.g. minimal test headers).
    static uint64_t opBlockGasLimit(protocol::BlockHeader const& header, uint64_t fallback)
    {
        namespace detail = bcos::evm::engine::detail;
        auto const gl = header.gasLimit();  // non-optional u256 (BlockHeader.h:156)
        return (gl == 0) ? fallback : detail::narrowU256ToU64(gl, "BlockInfo::gasLimit");
    }

    // ---- TransactionExecutor concept: ExecuteContext with prepare/execute/finish ----
    template <class Storage>
    struct ExecuteContext
    {
        OpstackExecutor& executor;
        Storage& storage;
        protocol::BlockHeader const& blockHeader;
        protocol::Transaction const& transaction;
        int contextID;
        ledger::LedgerConfig const& ledgerConfig;
        bool call;

        // Per-transaction state threaded across the concept lifecycle.
        bcos::evm::opstack::OpTxProperties m_props;   // set by prepare()
        protocol::TransactionReceipt::Ptr m_receipt;  // set by execute()
        evmone::state::StateDiff m_diff;              // writeback deferred to finish()

        ExecuteContext(OpstackExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c),
            m_props{},
            m_receipt{},
            m_diff{}
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish (writeback)
        task::Task<void> prepare()
        {
            m_props = co_await executor.m_prepare(storage, blockHeader, transaction, ledgerConfig);
        }
        task::Task<void> execute()
        {
            m_receipt = co_await executor.m_execute(
                storage, blockHeader, transaction, ledgerConfig, m_props, m_diff);
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            co_return co_await executor.m_finish(
                storage, blockHeader, ledgerConfig, m_receipt, m_diff);
        }
    };

    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call};
    }

    /// Execute a single OP normal transaction (injection-style, mirroring processOpBlock).
    /// Orchestrator supplies fee, decrementing blockGasLeft, chainId, and real block hashes.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        bcos::evm::opstack::OpFeeParams const& fee = {}, int64_t blockGasLeft = 0,
        uint64_t chainId = 0, evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        (void)contextID;

        auto props = co_await m_prepare(
            storage, blockHeader, transaction, ledgerConfig, fee, blockGasLeft, chainId, call);
        evmone::state::StateDiff diff;
        auto receipt = co_await m_execute(storage, blockHeader, transaction, ledgerConfig, props,
            diff, chainId, blockGasLeft, blockHashes);
        co_return co_await m_finish(storage, blockHeader, ledgerConfig, receipt, diff);
    }

    /// Execute a single OP 0x7E deposit transaction (reuses runDeposit).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeDeposit(Storage& storage,
        protocol::BlockHeader const& blockHeader, bcos::evm::opstack::DepositTx const& dep,
        uint64_t chainId, int64_t blockGasLeft, ledger::LedgerConfig const& ledgerConfig,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        eth::StorageStateView<Storage> stateView(storage);
        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;

        evmone::state::StateDiff diff;
        auto receipt = op::runDeposit(stateView, blockInfo, bh, dep, m_forkConfig, m_vm, chainId,
            blockGasLeft, m_receiptFactory, diff);
        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
        co_return receipt;
    }

    /// OP block-level finalize (no block reward, via finalizeOpBlock).
    template <class Storage>
    task::Task<void> finalizeBlock(Storage& storage, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        eth::StorageStateView<Storage> stateView(storage);
        evmc_address coinbase{};
        auto const& cb = blockHeader.coinbase();
        if (cb.size() == sizeof(evmc_address))
            std::copy_n(cb.begin(), sizeof(evmc_address), coinbase.bytes);

        auto diff = op::finalizeOpBlock(stateView, m_forkConfig, coinbase);
        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
    }

private:
    // ---- Shared normal-tx pipeline: the concept lifecycle and executeTransaction drive the same
    // ---- three stages.
    // Stage 1 — validate: fork/evmc revision check, block info + evmone tx + signed envelope, then
    // injection-style opValidate (fee supplied by the orchestrator; props.fee snapshotted for the
    // transition stage).
    template <class Storage>
    task::Task<bcos::evm::opstack::OpTxProperties> m_prepare(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpFeeParams const& fee = {},
        int64_t blockGasLeft = 0, uint64_t chainId = 0, bool call = false)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;
        namespace detail = bcos::evm::engine::detail;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;
        if (m_forkConfig.rev != rev)
            BOOST_THROW_EXCEPTION(OpForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto evmTx = eth::bcosTransactionToEvmone(transaction);
        eth::StorageStateView<Storage> stateView(storage);
        auto envRef = transaction.extraTransactionBytes();
        if (!call && !envRef.empty())
            // Consensus envelope checks (chain-id binding, EIP-2 low-s, yParity<=1) run before
            // opValidate on real (call=false) transactions; eth_call skips them. envRef is a
            // bytesConstRef — validateEnvelopeSignature takes bcos::bytes, so .toBytes() copies
            // (per-tx, acceptable). The !envRef.empty() gate covers an empty envelope, which the
            // validation helper would otherwise reject.
            detail::validateEnvelopeSignature(envRef.toBytes(), chainId);
        evmc::bytes_view env{envRef.data(), envRef.size()};

        auto validated =
            op::opValidate(stateView, blockInfo, evmTx, env, m_forkConfig, fee, blockGasLeft);
        if (auto const* err = std::get_if<std::error_code>(&validated))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(err->message()));
        co_return std::get<op::OpTxProperties>(validated);
    }

    // Stage 2 — execute: injection-style opTransition reusing props.fee (the validate-time
    // snapshot), so the pair can never be fed different OpFeeParams. Returns the final FISCO
    // receipt and the state diff via `diff` out-param.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_execute(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpTxProperties const& props,
        evmone::state::StateDiff& diff, uint64_t chainId = 0, int64_t blockGasLeft = 0,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        (void)ledgerConfig;
        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
        auto evmTx = eth::bcosTransactionToEvmone(transaction);
        eth::StorageStateView<Storage> stateView(storage);

        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
        co_return op::opTransition(stateView, blockInfo, bh, evmTx, m_forkConfig, m_vm, props,
            chainId, m_receiptFactory, diff);
    }

    // Stage 3 — writeback: apply the transition's state diff to storage, return the final receipt.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_finish(Storage& storage,
        protocol::BlockHeader const& blockHeader, ledger::LedgerConfig const& ledgerConfig,
        protocol::TransactionReceipt::Ptr receipt, evmone::state::StateDiff const& diff)
    {
        namespace eth = bcos::executor_v1::eth;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;

        co_await eth::applyStateDiff(storage, diff, rev, *m_hashImpl);
        co_return std::move(receipt);
    }

    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    bcos::evm::opstack::OpForkConfig const& m_forkConfig;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
