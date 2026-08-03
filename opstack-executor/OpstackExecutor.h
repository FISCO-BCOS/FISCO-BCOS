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
/// the EEST-calibrated blockHeaderToBlockInfo (rev.2: buildOpBlockInfo keeps timestamp verbatim).
///
/// Adapter reuse: the storage-backed StateView, state-diff writeback and base receipt conversion
/// are shared with EthereumExecutor via ethereum-executor (PR #5366). This module links the
/// ethereum-executor target.

#pragma once

#include "bcos-evm/opstack/OpBlockFinalize.h"
#include "bcos-evm/opstack/OpDepositTx.h"
#include "bcos-evm/opstack/OpFeeParams.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpReceiptMeta.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-evm/opstack/OpValidate.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-task/TBBWait.h"
#include "ethereum-executor/BCOS2Evmone.h"
#include "ethereum-executor/StorageStateView.h"
#include <bcos-utilities/Exceptions.h>
#include <evmone/evmone.h>
#include <evmc/evmc.hpp>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace bcos::executor_v1::opstack
{

DERIVE_BCOS_EXCEPTION(EvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpForkRevisionMismatch);
DERIVE_BCOS_EXCEPTION(OpTxValidationFailed);

class OpstackExecutor
{
public:
    /// @param receiptFactory produces the BCOS receipt from the evmone receipt.
    /// @param hashImpl        keccak used by the state-diff writeback.
    /// @param forkConfig      the active OP fork. The config is a reference into a static
    ///        singleton (jovianConfig()), so storing a const& is safe.
    OpstackExecutor(protocol::TransactionReceiptFactory const& receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig const& forkConfig = bcos::evm::opstack::jovianConfig())
      : m_receiptFactory(receiptFactory),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(forkConfig),
        m_vm(evmc_create_evmone())
    {}

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    /// OP-specific blockInfo: timestamp verbatim (OP headers store seconds; the EEST-calibrated
    /// blockHeaderToBlockInfo would /1000 it to ~1970), gasLimit and baseFee injected by the
    /// orchestrator (from payload.gasLimit / payload.baseFeePerGas, not LedgerConfig). Sets
    /// number/timestamp/gas_limit/base_fee/coinbase; prev_randao and blob_gas_used default to zero
    /// (the OP validator's current scenario; extend with params when the orchestrator supplies
    /// non-default values). BlockInfo has no chain_id member
    /// (block.hpp:34-41) — chainId is a separate opValidate/opTransition parameter.
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
        bcos::evm::opstack::OpTxReceipt m_opReceipt;  // set by execute()

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
            m_opReceipt{}
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish
        // (writeback+receipt)
        task::Task<void> prepare()
        {
            m_props = co_await executor.m_prepare(storage, blockHeader, transaction, ledgerConfig);
        }
        task::Task<void> execute()
        {
            m_opReceipt = co_await executor.m_execute(
                storage, blockHeader, transaction, ledgerConfig, m_props);
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            co_return co_await executor.m_finish(
                storage, blockHeader, transaction, ledgerConfig, m_opReceipt);
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
    /// blockGasLeft, zero chainId). The pipeline is the same three stages
    /// (m_prepare/m_execute/m_finish) the ExecuteContext lifecycle uses — no duplicated logic.
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
        auto opReceipt = co_await m_execute(storage, blockHeader, transaction, ledgerConfig, props,
            chainId, blockGasLeft, blockHashes);
        co_return co_await m_finish(storage, blockHeader, transaction, ledgerConfig, opReceipt);
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

        auto blockInfo =
            buildOpBlockInfo(blockHeader, static_cast<uint64_t>(blockGasLeft), /*baseFeePerGas=*/0);
        eth::StorageStateView<Storage> stateView(storage);
        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;

        auto opDep = op::runDeposit(
            stateView, blockInfo, bh, dep, m_forkConfig, m_vm, chainId, blockGasLeft);

        co_await eth::applyStateDiff(storage, opDep.receipt.state_diff, rev, *m_hashImpl);

        auto out = eth::evmoneReceiptToBcos(opDep.receipt, m_receiptFactory, blockHeader.number());
        auto metaBytes =
            op::encodeOpDepositMeta(opDep.deposit_nonce, opDep.deposit_receipt_version);
        out->setOpReceiptMeta(std::string(metaBytes.begin(), metaBytes.end()));
        co_return out;
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
    // build, then injection-style opValidate (pairing constraint, OpTransition.h:15-18): fee is
    // supplied by the orchestrator (with D-1 calldata override), props.fee is snapshotted here and
    // reused by the transition stage. Returns the validated properties for the execute stage.
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

        // gasLimit (block info) and blockGasLeft (validation) are distinct: single-tx context
        // passes the full budget for both; an orchestrator decrementing blockGasLeft should pass
        // the block gas cap for buildOpBlockInfo and the remaining budget to opValidate. baseFee
        // comes from the orchestrator (payload.baseFeePerGas).
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

    // Stage 2 — execute: injection-style opTransition reusing props.fee (the snapshot taken by the
    // validate stage), so the validate/transition pair can never be fed different OpFeeParams. The
    // OP block info / evmone tx / signed envelope are rebuilt here (pure, storage-free conversions
    // from the header + tx), keeping the stage self-contained. chainId and real block hashes are
    // orchestrator-supplied (zero / empty for the concept lifecycle path).
    template <class Storage>
    task::Task<bcos::evm::opstack::OpTxReceipt> m_execute(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpTxProperties const& props,
        uint64_t chainId = 0, int64_t blockGasLeft = 0,
        evmone::state::BlockHashes const* blockHashes = nullptr)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        auto blockInfo =
            buildOpBlockInfo(blockHeader, static_cast<uint64_t>(blockGasLeft), /*baseFeePerGas=*/0);
        auto evmTx = eth::bcosTransactionToEvmone(transaction);
        eth::StorageStateView<Storage> stateView(storage);
        auto envRef = transaction.extraTransactionBytes();
        evmc::bytes_view env{envRef.data(), envRef.size()};

        eth::ZeroBlockHashes zeroBlockHashes;
        auto const& bh = (blockHashes != nullptr) ? *blockHashes : zeroBlockHashes;
        co_return op::opTransition(
            stateView, blockInfo, bh, evmTx, m_forkConfig, m_vm, props, chainId, env);
    }

    // Stage 3 — writeback + receipt: apply the transition's state diff to storage, convert the
    // evmone receipt to a BCOS receipt, and attach the opReceiptMeta byte string. rev is re-read
    // from the ledger config (the prepare stage already verified the fork/evmc revision match).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_finish(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig, bcos::evm::opstack::OpTxReceipt const& opReceipt)
    {
        namespace op = bcos::evm::opstack;
        namespace eth = bcos::executor_v1::eth;

        (void)transaction;

        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(EvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        auto rev = *revOpt;

        co_await eth::applyStateDiff(storage, opReceipt.receipt.state_diff, rev, *m_hashImpl);

        auto out =
            eth::evmoneReceiptToBcos(opReceipt.receipt, m_receiptFactory, blockHeader.number());
        auto metaBytes = op::encodeOpReceiptMeta(opReceipt.meta);
        if (!metaBytes.empty())
            out->setOpReceiptMeta(std::string(metaBytes.begin(), metaBytes.end()));
        co_return out;
    }

    protocol::TransactionReceiptFactory const& m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    bcos::evm::opstack::OpForkConfig const& m_forkConfig;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
