/// @file OpstackExecutor.h
/// @brief An OP Stack (Optimism L2) transaction executor based on bcos-evm/opstack.
///
/// Implements the bcos::executor_v1::TransactionExecutor concept (ExecuteContext with
/// prepare/execute/finish). It is the OP analogue of EthereumExecutor: instead of evmone's stock
/// validate_transaction / transition, it drives the OP transition pipeline in bcos-evm/opstack —
/// opValidate (L1 + operator fee pre-charge, blob rejection) followed by opTransition (base/L1/
/// operator fees routed to the OP fee vaults).
///
/// Scope: NORMAL transactions (executeTransaction), 0x7E deposits (executeDeposit), and block
/// finalize (finalizeBlock). Deposit decoding (raw envelope -> DepositTx) is NOT this module's
/// concern — the caller passes an already-decoded DepositTx.
///
/// Semantics: uses the INJECTION-style opValidate/opTransition with an orchestrator-supplied
/// OpFeeParams (including the D-1 attributes-calldata DA-scalar override), a decrementing
/// blockGasLeft, the chain id, and real block hashes — mirroring processOpBlock. It does NOT use
/// the EEST-calibrated blockHeaderToBlockInfo (buildOpBlockInfo keeps timestamp verbatim).
///
/// Adapter reuse: the storage-backed StateView and state-diff writeback are shared with
/// EthereumExecutor via ethereum-executor (PR #5366). This module links the ethereum-executor
/// target. v2 adaptation: opTransition/runDeposit now produce the final FISCO receipt directly
/// (OP metadata + effective gas price already projected), so no receipt-meta conversion is needed
/// here.

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
#include "opstack-executor/OpBlockFinalize.h"
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <evmc/evmc.hpp>
#include <memory>
#include <optional>
#include <string>

namespace bcos::executor_v1::opstack
{

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

class OpstackExecutor
{
public:
    /// @param receiptFactory produces the BCOS receipt (v2 opTransition/runDeposit take it and
    ///        return the final receipt with OP metadata already projected).
    /// @param hashImpl        keccak used by the state-diff writeback.
    /// @param forkConfig      the active OP fork. The config is a reference into a static
    ///        singleton (jovianConfig()), so storing a const& is safe.
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

    /// OP-specific blockInfo: timestamp verbatim (OP headers store seconds; the EEST-calibrated
    /// blockHeaderToBlockInfo would /1000 it to ~1970), gasLimit and baseFee injected by the
    /// orchestrator (from payload.gasLimit / payload.baseFeePerGas, not LedgerConfig). Sets
    /// number/timestamp/gas_limit/base_fee/coinbase; prev_randao and blob_gas_used default to zero.
    static evmone::state::BlockInfo buildOpBlockInfo(
        protocol::BlockHeader const& header, uint64_t gasLimit, uint64_t baseFeePerGas)
    {
        evmone::state::BlockInfo blk;
        blk.number = header.number();
        blk.timestamp = header.timestamp();
        blk.gas_limit = static_cast<int64_t>(gasLimit);
        blk.base_fee = baseFeePerGas;
        auto const& cb = header.coinbase();
        if (cb.size() == sizeof(evmc_address))
            std::copy_n(cb.begin(), sizeof(evmc_address), blk.coinbase.bytes);
        return blk;
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

        // Per-transaction state threaded across the concept lifecycle. The concept call carries no
        // injection params, so the stages run with default injection (zero fee / zero blockGasLeft
        // / zero chainId / no block hashes); the orchestrator's executeTransaction path threads
        // the real injection params through the same three stages.
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

    /// Execute a single OP Stack normal transaction (INJECTION-style, semantics mirror
    /// processOpBlock). Orchestrator supplies: fee (with D-1 calldata override), decrementing
    /// blockGasLeft, chainId, and real block hashes. fee/blockGasLeft/chainId default so the
    /// concept's 6-argument call lands on this injection path (default injection: zero fee, zero
    /// blockGasLeft, zero chainId).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        bcos::evm::opstack::OpFeeParams const& fee = {}, int64_t blockGasLeft = 0,
        uint64_t chainId = 0, evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        (void)contextID;
        (void)call;

        auto props =
            co_await m_prepare(storage, blockHeader, transaction, ledgerConfig, fee, blockGasLeft);
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

        auto blockInfo = buildOpBlockInfo(blockHeader, static_cast<uint64_t>(blockGasLeft), 0);
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
    // ---- Shared normal-tx pipeline: concept lifecycle (ExecuteContext::prepare/execute/finish)
    // ---- and executeTransaction drive the same three stages (no duplicated logic).
    // Stage 1 — validate: fork/evmc revision check, OP block info + evmone tx + signed envelope
    // build, then injection-style opValidate (pairing constraint, OpTransition.h): fee is supplied
    // by the orchestrator, props.fee is snapshotted here and reused by the transition stage.
    template <class Storage>
    task::Task<bcos::evm::opstack::OpTxProperties> m_prepare(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpFeeParams const& fee = {},
        int64_t blockGasLeft = 0)
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

        auto blockInfo =
            buildOpBlockInfo(blockHeader, static_cast<uint64_t>(blockGasLeft), /*baseFeePerGas=*/0);
        auto evmTx = eth::bcosTransactionToEvmone(transaction);
        eth::StorageStateView<Storage> stateView(storage);
        auto envRef = transaction.extraTransactionBytes();
        evmc::bytes_view env{envRef.data(), envRef.size()};

        auto validated =
            op::opValidate(stateView, blockInfo, evmTx, env, m_forkConfig, fee, blockGasLeft);
        if (auto const* err = std::get_if<std::error_code>(&validated))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(err->message()));
        co_return std::get<op::OpTxProperties>(validated);
    }

    // Stage 2 — execute: injection-style opTransition reusing props.fee (the validate-time
    // snapshot), so the validate/transition pair can never be fed different OpFeeParams. The OP
    // block info / evmone tx / signed envelope are rebuilt here (pure conversions), keeping the
    // stage self-contained. chainId and real block hashes are orchestrator-supplied. v2
    // opTransition returns the final FISCO receipt (OP meta + effective gas price already
    // projected) and returns the state diff via `diff` out-param.
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
        auto blockInfo =
            buildOpBlockInfo(blockHeader, static_cast<uint64_t>(blockGasLeft), /*baseFeePerGas=*/0);
        auto evmTx = eth::bcosTransactionToEvmone(transaction);
        eth::StorageStateView<Storage> stateView(storage);

        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
        co_return op::opTransition(stateView, blockInfo, bh, evmTx, m_forkConfig, m_vm, props,
            chainId, m_receiptFactory, diff);
    }

    // Stage 3 — writeback: apply the transition's state diff to storage and return the already
    // final receipt (v2 opTransition projected the OP metadata + effective gas price onto it).
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
