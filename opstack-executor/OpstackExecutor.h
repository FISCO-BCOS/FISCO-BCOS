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
#include "ethereum-executor/BCOS2Evmone.h"
#include "ethereum-executor/StorageStateView.h"
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
    /// orchestrator (from payload.gasLimit / payload.baseFeePerGas, not LedgerConfig). Mirrors the
    /// field set of OpSchedulerImpl::detail::toBlockInfo (OpSchedulerImpl.h:218-231). prev_randao
    /// / blob_gas_used default to zero (the OP validator's current scenario; extend with params
    /// when the orchestrator supplies non-default values). BlockInfo has no chain_id member
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
        if (cb.size() >= sizeof(evmc_address))
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

        ExecuteContext(OpstackExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c)
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish
        // (writeback+receipt)
        task::Task<void> prepare()
        {
            co_return co_await executor.m_prepare(storage, blockHeader, transaction, ledgerConfig);
        }
        task::Task<void> execute()
        {
            co_return co_await executor.m_execute(storage, blockHeader, transaction, ledgerConfig);
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            co_return co_await executor.m_finish(storage, blockHeader, transaction, ledgerConfig);
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
    /// blockGasLeft, chainId, and real block hashes.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        bcos::evm::opstack::OpFeeParams const& fee, int64_t blockGasLeft, uint64_t chainId,
        evmone::state::BlockHashes const* blockHashes = nullptr);

    /// Execute a single OP 0x7E deposit transaction (reuses runDeposit).
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeDeposit(Storage& storage,
        protocol::BlockHeader const& blockHeader, bcos::evm::opstack::DepositTx const& dep,
        uint64_t chainId, int64_t blockGasLeft, ledger::LedgerConfig const& ledgerConfig,
        evmone::state::BlockHashes const* blockHashes = nullptr);

    /// OP block-level finalize (no block reward, via finalizeOpBlock).
    template <class Storage>
    task::Task<void> finalizeBlock(Storage& storage, protocol::BlockHeader const& blockHeader,
        ledger::LedgerConfig const& ledgerConfig);

private:
    // concept lifecycle helpers (shared with executeTransaction)
    template <class Storage>
    task::Task<void> m_prepare(Storage& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, ledger::LedgerConfig const& ledgerConfig);
    template <class Storage>
    task::Task<void> m_execute(Storage& storage, protocol::BlockHeader const& blockHeader,
        protocol::Transaction const& transaction, ledger::LedgerConfig const& ledgerConfig);
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> m_finish(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        ledger::LedgerConfig const& ledgerConfig);

    protocol::TransactionReceiptFactory const& m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    bcos::evm::opstack::OpForkConfig const& m_forkConfig;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
