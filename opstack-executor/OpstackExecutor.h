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
#include "opstack-executor/OpCommon.h"  // detail::narrowU256ToU64 / toEvmcAddress / toEvmcBytes32
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

/// Per-block execution state threaded through ExecuteContext (shared scheduler plan Task 3).
/// fee is loaded lazily on the first NORMAL tx (after the L1 attributes deposit has run);
/// blockGasLeft / cumulativeGasUsed / seenNonDeposit are mutated per tx; hashes / chainId are
/// fixed at block construction; daFootprintGasScalar (Jovian) overrides
/// fee.da_footprint_gas_scalar when set. The first five fields are mutable so a
/// `BlockContext const*` can write them. Namespace-scope (not nested) so OpScheduler / tests can
/// value-initialize it (a nested struct's default member initializers are unusable outside
/// OpstackExecutor's member functions).
struct OpBlockExecutionContext
{
    mutable bcos::evm::opstack::OpFeeParams fee;   // lazy-load + DA scalar override (H1/H1c)
    mutable bool feeLoaded = false;                // fee lazy-load flag (H1)
    mutable int64_t blockGasLeft;                  // decremented per tx
    mutable int64_t cumulativeGasUsed = 0;         // accumulated across txs (H4)
    mutable bool seenNonDeposit = false;           // deposit-after-non-deposit gate (M2)
    evmone::state::BlockHashes* blockHashes;       // built once at block level (H3)
    uint64_t chainId;                              // constant (H3)
    std::optional<uint16_t> daFootprintGasScalar;  // Jovian DA scalar (H1c)
};

class OpstackExecutor
{
public:
    OpstackExecutor(protocol::TransactionReceiptFactory::Ptr receiptFactory,
        crypto::Hash::Ptr hashImpl,
        bcos::evm::opstack::OpForkConfig forkConfig = bcos::evm::opstack::jovianConfig())
      : m_receiptFactory(std::move(receiptFactory)),
        m_hashImpl(std::move(hashImpl)),
        m_forkConfig(std::move(forkConfig)),
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
    // BlockContext aliases the namespace-scope OpBlockExecutionContext (defined above).
    using BlockContext = OpBlockExecutionContext;

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
        bcos::evm::opstack::DepositTx m_deposit;      // set by prepare() for deposit txs

        // Shared per-block context; the mutable fields above are written through this const
        // pointer. The caller owns the BlockContext and must keep it alive across the lifecycle.
        BlockContext const* m_ctx;

        ExecuteContext(OpstackExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c,
            BlockContext const* blockCtx)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c),
            m_props{},
            m_receipt{},
            m_diff{},
            m_deposit{},
            m_ctx(blockCtx)
        {}

        // concept lifecycle: prepare (validate) -> execute (transition) -> finish (writeback).
        // Deposit txs short-circuit (no opValidate / fee / m_finish writeback); normal txs run the
        // three shared stages with the block-context fee (lazily loaded) + blockGasLeft.
        task::Task<void> prepare()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            if (transaction.isDepositTx())
            {
                if (m_ctx->seenNonDeposit)  // M2 order gate
                    throw bcos::evm::engine::OpConsensusError(
                        "op block: deposit after non-deposit");
                m_deposit = OpstackExecutor::depositFromTransaction(transaction);
                co_return;  // deposit has no opValidate
            }
            m_ctx->seenNonDeposit = true;
            if (!m_ctx->feeLoaded)
            {  // H1 fee lazy load (after the L1 attributes deposit has executed)
                namespace eth = bcos::executor_v1::eth;
                namespace op = bcos::evm::opstack;
                eth::StorageStateView<Storage> stateView(storage);
                m_ctx->fee = op::loadOpFeeParams(stateView);
                if (m_ctx->daFootprintGasScalar)  // H1c DA scalar override
                    m_ctx->fee.da_footprint_gas_scalar = *m_ctx->daFootprintGasScalar;
                m_ctx->feeLoaded = true;
            }
            try
            {  // M1 normalization: validation failure -> consensus rejection
                m_props = co_await executor.m_prepare(storage, blockHeader, transaction,
                    ledgerConfig, m_ctx->fee, m_ctx->blockGasLeft);
            }
            catch (const OpTxValidationFailed& e)
            {
                throw bcos::evm::engine::OpConsensusError(
                    std::string("OpScheduler: normal tx validation failed: ") + e.what());
            }
        }
        task::Task<void> execute()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            if (transaction.isDepositTx())
            {
                // executeDeposit member (not the op::runDeposit free function); applies the state
                // diff internally.
                m_receipt = co_await executor.executeDeposit(storage, blockHeader, m_deposit,
                    m_ctx->chainId, m_ctx->blockGasLeft, ledgerConfig, m_ctx->blockHashes);
            }
            else
            {
                m_receipt = co_await executor.m_execute(storage, blockHeader, transaction,
                    ledgerConfig, m_props, m_diff, m_ctx->chainId, m_ctx->blockGasLeft,
                    m_ctx->blockHashes);  // H3
            }
        }
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            if (m_ctx == nullptr)
            {
                throw bcos::evm::engine::OpConsensusError(
                    "OpstackExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
            }
            namespace op = bcos::evm::opstack;
            protocol::TransactionReceipt::Ptr receipt;
            if (transaction.isDepositTx())
            {
                receipt = m_receipt;  // executeDeposit already applied the state diff
            }
            else
            {
                receipt = co_await executor.m_finish(
                    storage, blockHeader, ledgerConfig, m_receipt, m_diff);
            }
            // H4: sole owner of cumulative-gas backfill + blockGasLeft decrement (narrowGasUsed /
            // hexCumulative live in OpCommon.h).
            auto gasUsed = op::narrowGasUsed(receipt->gasUsed());
            m_ctx->cumulativeGasUsed += gasUsed;
            receipt->setCumulativeGasUsed(op::hexCumulative(m_ctx->cumulativeGasUsed));
            m_ctx->blockGasLeft -= gasUsed;
            co_return receipt;
        }
    };

    /// 7-arg form (OP path): the caller owns the BlockContext and must keep it alive across the
    /// prepare/execute/finish lifecycle (SchedulerSerialImpl forwards the ctx that lives in
    /// OpScheduler's coroutine frame).
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        BlockContext const& blockCtx)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, &blockCtx};
    }

    /// 6-arg form (generic scheduler + the TransactionExecutor concept probe): no BlockContext is
    /// available, so m_ctx is null and any prepare/execute/finish throws. The previous default
    /// argument `= BlockContext{}` bound a temporary to the const-ref parameter whose lifetime
    /// ended at the full expression — m_ctx stayed dangling (footgun). OP is never driven through
    /// this form (SchedulerSerialImpl's requires probe always picks the 7-arg overload), but it
    /// must stay valid for the concept, which calls with 6 args.
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, nullptr};
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

        if (transaction.isDepositTx())
        {
            auto dep = depositFromTransaction(transaction);
            co_return co_await executeDeposit(
                storage, blockHeader, dep, chainId, blockGasLeft, ledgerConfig, blockHashes);
        }

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
    // ---- Shared normal-tx pipeline: three stages (prepare/execute/finish). ----
    // Stage 1 — validate: fork/evmc revision check, block info + evmone tx + signed envelope, then
    // injection-style opValidate (props.fee snapshotted for the transition stage).
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

        auto blockInfo = buildBlockInfo(
            blockHeader, opBlockGasLimit(blockHeader, static_cast<uint64_t>(blockGasLeft)));
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
    // snapshot), so the pair can never be fed different OpFeeParams.
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

    /// Build a DepositTx from a protocol::Transaction whose isDepositTx() is true, reading the
    /// deposit-only mirrors (sourceHash/mint/isSystemTransaction) from the object. No raw-envelope
    /// RLP re-parse (buildOpBlock decoded it via opEnvelopeToTars); the width checks below
    /// (sourceHash 32-byte / sender 20-byte / to 20-byte) reject malformed fields. mint is always
    /// Some(value) (0 == no mint).
public:
    static bcos::evm::opstack::DepositTx depositFromTransaction(protocol::Transaction const& tx)
    {
        namespace op = bcos::evm::opstack;
        op::DepositTx dep;

        // source_hash: unprefixed hex string_view -> evmc::bytes32 (fail loud on malformed input)
        auto sh = bcos::safeFromHex(tx.sourceHash());
        if (!sh || sh->size() != sizeof(evmc::bytes32))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(
                                      "deposit sourceHash is not a 32-byte hex string"));
        std::copy(sh->begin(), sh->end(), dep.source_hash.bytes);

        // from: 20-byte raw string_view -> evmc::address (deposit has no signature; from == sender)
        auto const& sb = tx.sender();
        if (sb.size() != sizeof(evmc::address))
            BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(
                                      "deposit sender is not a 20-byte address"));
        std::copy_n(sb.begin(), sizeof(evmc::address), dep.from.bytes);

        // to: hex string_view -> optional<evmc::address> (empty = contract creation)
        auto const& tb = tx.to();
        if (!tb.empty())
        {
            auto dec = bcos::safeFromHex(tb);
            if (!dec || dec->size() != sizeof(evmc::address))
                BOOST_THROW_EXCEPTION(OpTxValidationFailed{} << bcos::errinfo_comment(
                                          "deposit to is not a 20-byte address"));
            evmc::address ta{};
            std::copy(dec->begin(), dec->end(), ta.bytes);
            dep.to = ta;
        }

        dep.mint = bcos::executor_v1::eth::evm::toIntxU256(tx.mint());
        dep.value = bcos::executor_v1::eth::evm::toIntxU256(tx.value());
        dep.gas_limit = tx.gasLimit();
        dep.is_system_tx = tx.depositIsSystemTransaction();
        auto const& input = tx.input();
        dep.data = evmc::bytes(input.begin(), input.end());
        return dep;
    }

private:
    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    crypto::Hash::Ptr m_hashImpl;
    // Value copy, not a reference: OpForkConfig is small (~32B, once per block) and a reference
    // member to a caller's config is the same lifetime footgun class that m_ctx had.
    bcos::evm::opstack::OpForkConfig m_forkConfig;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
