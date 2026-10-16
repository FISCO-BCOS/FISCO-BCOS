/// @file OpEthExecutor.h
/// @brief OP Stack transaction executor on the bcos-evm-free ethereum-executor
///        stack — the drop-in successor of OpstackExecutor.
///
/// Same outer contract as the legacy executor (OpstackExecutor.h):
///   * models bcos::executor_v1::TransactionExecutor (ExecuteContext with
///     prepare/execute/finish, 6-arg createExecuteContext for the concept probe);
///   * adds the 7-arg createExecuteContext that SchedulerSerialImpl's
///     `BlockContextOf` probe selects (serial=true, chunk=1, strict tx order);
///   * error classification is unchanged: bcos::evm::OpConsensusError ->
///     INVALID (with txHash / capacity / validateErrorCode tags),
///     bcos::evm::engine::OpStorageError -> -32603; a poisoned shared
///     OpStorageErrorSlot always means a storage fault and is checked first.
///
/// The internals run on the new-layer primitives instead of bcos-evm:
/// validateTransaction / runTransaction under OpPolicy (EthereumTransition.h)
/// for normal txs, opRunDeposit (OpEthDeposit.h) for 0x7E deposits,
/// buildOpEthBlockInfo (OpEthBlockExecute.h) for the block context, the
/// OpEnvelopeCheck.h fail-closed envelope<->mirror cross-checks, and
/// OpFaultRecordingStorage + OpStorageErrorGuard (OpStorageErrorGuard.h) for
/// the fail-loud storage channel (the legacy SharedErrorSlot /
/// Storage2State::applyDiff analogue).
///
/// Per-tx storage chain (view <- fault recorder <- journaling <- state):
///   Storage& view
///     <- OpFaultRecordingStorage<Storage>        (records escaping storage2
///     <- Rollbackable<...>                        faults into the block slot)
///     <- EthereumState<...>                      (per-tx; write-back via
///                                                 applyToStorage, rolled back
///                                                 on failure / for eth_call)
///
/// SERIAL-ONLY (same as the legacy BlockContext): the mutable fields of
/// OpEthBlockContext are written per-tx through a shared const& without
/// synchronization, and the cumulativeGasUsed backfill in finish() assumes
/// strict tx order. Correctness relies on a serial driver
/// (SchedulerSerialImpl with serial=true).
///
/// Discard-writes contract (unchanged): on any throw out of
/// prepare/execute/finish or executeTransaction, the caller must discard all
/// writes already applied to the storage view.

#pragma once

#include <bcos-codec/rlp/RLPEncode.h>  // sizing-envelope encode
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-framework/protocol/TransactionReceiptFactory.h>
#include <bcos-framework/protocol/TxGasModel.h>  // protocol::ethToAddress
#include <bcos-framework/storage2/RollbackableStorage.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/LogStream.h>
#include <bcos-utilities/DataConvertUtility.h>  // safeFromQuantity / fromBigEndian
#include <bcos-utilities/Exceptions.h>
#include <ethereum-executor/EthereumState.h>
#include <ethereum-executor/EthereumTransition.h>  // validateTransaction / runTransaction
#include <evmone/evmone.h>
#include <opstack-executor/OpCommon.h>         // OpConsensusError / OpStorageError / narrowGasUsed
#include <opstack-executor/OpEnvelopeCheck.h>  // the four fail-closed envelope checks
#include <opstack-executor/OpEthBlockExecute.h>    // buildOpEthBlockInfo / OpEthBlockError
#include <opstack-executor/OpEthDeposit.h>         // opRunDeposit / decodeOpDepositEnvelope
#include <opstack-executor/OpExecutionPolicy.h>    // OpPolicy
#include <opstack-executor/OpFeeParams.h>          // OpFeeParams / loadOpFeeParamsAsync
#include <opstack-executor/OpForkSpec.h>           // OpForkSpec
#include <opstack-executor/OpStorageErrorGuard.h>  // OpStorageErrorSlot / OpFaultRecordingStorage
#include <charconv>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <variant>

namespace bcos::executor_v1::opstack
{
namespace eth = bcos::executor_v1::eth;

// New names (the Eth infix) so this header can coexist with OpstackExecutor.h
// in one translation unit: the legacy OpEvmcRevisionNotConfigured /
// OpForkRevisionMismatch live in this same namespace.
DERIVE_BCOS_EXCEPTION(OpEthEvmcRevisionNotConfigured);
DERIVE_BCOS_EXCEPTION(OpEthForkRevisionMismatch);

/// Per-block execution state threaded through ExecuteContext — the
/// bcos-evm-free counterpart of OpBlockExecutionContext (OpstackExecutor.h).
/// The bcos-evm types are replaced: fee is the new-layer OpFeeParams
/// (OpFeeParams.h) and the evmone::state::BlockHashes pointer becomes an
/// eth::BlockHashLookup function (EthereumHost.h; empty = BLOCKHASH reads
/// zero, the documented eth_call fallback — the block path fails loud on it).
/// fee is loaded lazily on the first NORMAL tx (after the L1 attributes
/// deposit has run); blockGasLeft / cumulativeGasUsed / seenNonDeposit are
/// mutated per tx; blockHashLookup / chainId are fixed at block construction;
/// daFootprintGasScalar (Jovian) overrides fee.da_footprint_gas_scalar when
/// set. The first five fields are mutable so a `BlockContext const&` can write
/// them. Namespace-scope (not nested) so OpScheduler / tests can
/// value-initialize it.
struct OpEthBlockContext
{
    mutable OpFeeParams fee{};
    mutable bool feeLoaded = false;
    mutable int64_t blockGasLeft = 0;
    mutable int64_t cumulativeGasUsed = 0;
    mutable bool seenNonDeposit = false;
    eth::BlockHashLookup blockHashLookup{};
    uint64_t chainId = 0;
    std::optional<uint16_t> daFootprintGasScalar;
};

namespace opeth_executor_detail
{
/// Canonical RLP integer append (u256; zero -> the empty item 0x80, a single
/// byte < 0x80 stays bare — rlp::encode(bytesConstRef) already implements both
/// rules).
inline void appendRlpUint(bcos::bytes& payload, bcos::u256 const& v)
{
    auto const be = bcos::toCompactBigEndian(v);
    bcos::codec::rlp::encode(payload, bcos::bytesConstRef{be.data(), be.size()});
}
}  // namespace opeth_executor_detail

/// Unsigned EIP-2718 sizing envelope for eth_call / estimateGas (ported
/// OpstackExecutor.h's synthesizeCallSizingEnvelope, reading the
/// protocol::Transaction + EthCallParams directly instead of an
/// evmone::state::Transaction). Used only for L1-cost / calldata sizing when
/// the call carries no signed envelope; the same KNOWN ESTIMATE BIAS applies
/// (~65 bytes shorter than the signed form, no v/r/s — the bias is confined to
/// estimates, never to executed transactions).
[[nodiscard]] inline bcos::bytes synthesizeOpEthCallSizingEnvelope(
    protocol::Transaction const& tx, eth::EthCallParams const& callParams)
{
    namespace rlp = bcos::codec::rlp;
    auto const kind = tx.web3TypedTxKind();
    if (kind > 4)
    {
        // Fail closed (ported toEvmoneTransaction): web3TypedTxKind is a tars
        // wire field (untrusted); folding an unknown kind into legacy would
        // bypass the policy's type whitelist.
        throw bcos::evm::OpConsensusError(
            "OpEthExecutor: unsupported web3TypedTxKind: " + std::to_string(kind));
    }
    // chainId: the tars field is DECIMAL (Web3Transaction.cpp
    // takeToTarsTransaction) — strict from_chars, any malformation falls
    // through to 0 (same fallback as the legacy converter; 0 never matches a
    // real chain id, and a malformed signed field implies an invalid
    // signature that admission rejects upstream).
    uint64_t const chainId = [&]() -> uint64_t {
        auto const s = tx.chainId();
        if (s.empty())
            return 0;
        uint64_t v = 0;
        auto const* const last = s.data() + s.size();
        auto const [ptr, ec] = std::from_chars(s.data(), last, v, 10);
        return (ec == std::errc{} && ptr == last) ? v : 0;
    }();
    auto const nonce = bcos::safeFromQuantity(tx.nonce()).value_or(0);
    auto const gasLimit = static_cast<uint64_t>(eth::effectiveGasLimit(tx, callParams));
    // Post-clamp prices (ethMaxGasPrice applies callParams.maxGasPriceFloor)
    // so the sizing matches what the simulation actually prices.
    auto const maxPrice = eth::ethMaxGasPrice(tx, callParams);
    auto const prioPrice = eth::ethMaxPriorityGasPrice(tx, callParams);
    auto const to = protocol::ethToAddress(tx);
    auto const& input = tx.input();

    bcos::bytes payload;
    auto appendTo = [&] {
        if (to.has_value())
            rlp::encode(payload, bcos::bytesConstRef{to->bytes, sizeof(to->bytes)});
        else
            rlp::encode(payload, bcos::bytesConstRef{});
    };

    if (kind == 0)
    {
        opeth_executor_detail::appendRlpUint(payload, nonce);
        opeth_executor_detail::appendRlpUint(payload, maxPrice);
        opeth_executor_detail::appendRlpUint(payload, gasLimit);
        appendTo();
        opeth_executor_detail::appendRlpUint(payload, tx.value());
        rlp::encode(payload, bcos::bytesConstRef{input.data(), input.size()});
    }
    else
    {
        opeth_executor_detail::appendRlpUint(payload, chainId);
        opeth_executor_detail::appendRlpUint(payload, nonce);
        if (kind == 1)  // access_list: single gas price field
            opeth_executor_detail::appendRlpUint(payload, maxPrice);
        else
        {
            opeth_executor_detail::appendRlpUint(payload, prioPrice);
            opeth_executor_detail::appendRlpUint(payload, maxPrice);
        }
        opeth_executor_detail::appendRlpUint(payload, gasLimit);
        appendTo();
        opeth_executor_detail::appendRlpUint(payload, tx.value());
        rlp::encode(payload, bcos::bytesConstRef{input.data(), input.size()});
        rlp::encodeHeader(payload, {.isList = true, .payloadLength = 0});  // accessList
    }

    bcos::bytes out;
    if (kind != 0)
        out.push_back(static_cast<bcos::byte>(kind));
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// The OP transaction executor. See the file header for the contract; the
/// per-stage comments name the legacy function each step is ported from.
class OpEthExecutor
{
public:
    /// @param spec the resolved fork spec for the executing block (OpForkSpec.h).
    /// @param errorSlot block-wide storage-error recorder (op-geth dbErr
    ///        analogue) shared by every per-tx EthereumState / storage2 access
    ///        this executor builds; a fresh one is created when omitted.
    OpEthExecutor(protocol::TransactionReceiptFactory::Ptr receiptFactory, OpForkSpec spec,
        std::shared_ptr<OpStorageErrorSlot> errorSlot = {})
      : m_receiptFactory(std::move(receiptFactory)),
        m_spec(spec),
        m_errorSlot(errorSlot ? std::move(errorSlot) : std::make_shared<OpStorageErrorSlot>()),
        m_vm(evmc_create_evmone())
    {}

    /// The block-wide storage-error slot shared by every per-tx instance this
    /// executor builds: a read/write error in ANY per-tx execution poisons the
    /// shared slot, which the block-boundary check then rejects.
    [[nodiscard]] const std::shared_ptr<OpStorageErrorSlot>& opErrorSlot() const noexcept
    {
        return m_errorSlot;
    }

    /// Access the EVM instance (needed for system_call functions).
    evmc::VM& vm() { return m_vm; }

    // ---- TransactionExecutor concept: ExecuteContext with prepare/execute/finish ----
    using BlockContext = OpEthBlockContext;

    template <class Storage>
    struct ExecuteContext
    {
        OpEthExecutor& executor;
        Storage& storage;
        protocol::BlockHeader const& blockHeader;
        protocol::Transaction const& transaction;
        int contextID;
        ledger::LedgerConfig const& ledgerConfig;
        // eth_call leniency: skips the chainId gate / envelope cross-checks
        // (prepare) and uses lenient header optionals; finish() rolls the
        // simulated writes back.
        bool call;

        // Per-tx storage chain, owned via unique_ptr: ExecuteContext must be
        // move_constructible (the concept + the scheduler's contexts vector)
        // while OpStorageErrorGuard's move is deleted and the chain links hold
        // references to each other — unique_ptr keeps every link address-stable.
        using FaultStorage = OpFaultRecordingStorage<Storage>;
        using RollableStorage = executor_v1::Rollbackable<FaultStorage>;
        using State = eth::EthereumState<RollableStorage>;
        std::unique_ptr<FaultStorage> m_probe;
        std::unique_ptr<RollableStorage> m_rollable;
        std::unique_ptr<State> m_state;
        std::unique_ptr<OpStorageErrorGuard<RollableStorage>> m_guard;

        // Shared per-block context; the mutable fields are written through this
        // const pointer. The caller owns the BlockContext and must keep it
        // alive across the lifecycle.
        BlockContext const* m_ctx;

        // Per-transaction state threaded across the concept lifecycle.
        bool m_isDeposit = false;  // envelope first byte 0x7E (never the tars mirror)
        eth::EthCallParams m_callParams{};
        std::optional<eth::EthBlockInfo> m_blockInfo;  // normal txs: built in prepare
        std::optional<DepositTx> m_deposit;            // set by prepare() for deposits
        eth::EthTxProperties m_txProps{};              // set by prepare() for normal txs
        // Validate-time fee snapshot shared by the prepare- and execute-time
        // OpPolicy instances (additionalMaxCost writes, settleFees/buildReceipt
        // read) — the fork-boundary discipline of OpTxSnapshot.
        OpTxSnapshot m_snapshot{};
        bcos::bytes m_synthesizedEnvelope;  // call path only; backs envelopeView()
        typename RollableStorage::Savepoint m_savepoint = 0;
        protocol::TransactionReceipt::Ptr m_receipt;  // set by execute()

        ExecuteContext(OpEthExecutor& exec, Storage& st, protocol::BlockHeader const& bh,
            protocol::Transaction const& tx, int cid, ledger::LedgerConfig const& cfg, bool c,
            BlockContext const* blockCtx)
          : executor(exec),
            storage(st),
            blockHeader(bh),
            transaction(tx),
            contextID(cid),
            ledgerConfig(cfg),
            call(c),
            // Initializer order must match declaration order (GCC -Werror=reorder).
            m_probe(std::make_unique<FaultStorage>(st, exec.m_errorSlot)),
            m_rollable(std::make_unique<RollableStorage>(*m_probe)),
            m_state(std::make_unique<State>(*m_rollable)),
            m_guard(
                std::make_unique<OpStorageErrorGuard<RollableStorage>>(*m_state, exec.m_errorSlot)),
            m_ctx(blockCtx)
        {}

        // The 6-arg createExecuteContext leaves m_ctx null; every lifecycle call
        // on that form is unsupported for OP execution.
        void requireBlockContext() const
        {
            if (m_ctx == nullptr)
                throw bcos::evm::OpConsensusError(
                    "OpEthExecutor: createExecuteContext called without a BlockContext (the "
                    "6-arg form is unsupported for OP execution)");
        }

        /// The envelope the policy prices: the raw signed envelope, or the
        /// synthesized sizing envelope when an eth_call carries none.
        [[nodiscard]] evmc::bytes_view envelopeView() const
        {
            if (!m_synthesizedEnvelope.empty())
                return {m_synthesizedEnvelope.data(), m_synthesizedEnvelope.size()};
            auto const raw = transaction.extraTransactionBytes();
            return {raw.data(), raw.size()};
        }

        task::Task<void> rollbackQuietly()
        {
            // Undo every write this transaction applied to the underlying
            // storage. If the rollback itself fails, keep the original failure
            // — it is the real cause worth reporting.
            try
            {
                co_await m_rollable->rollback(m_savepoint);
            }
            catch (...)
            {}
        }

        /// Stage 1 — validate (ported ExecuteContext::prepare / m_prepare):
        /// deposit decode, lazy fee load, block info, eth_call normalization,
        /// the block-path envelope<->mirror gates, then validateTransaction
        /// under OpPolicy.
        task::Task<void> prepare()
        {
            requireBlockContext();
            executor.checkForkRevision(ledgerConfig);

            // Deposit-ness is decided by the envelope's type byte (0x7E) —
            // never the forgeable tars mirror (op-geth's rule).
            auto const rawEnv = transaction.extraTransactionBytes();
            m_isDeposit = !rawEnv.empty() && rawEnv[0] == OP_DEPOSIT_TX_TYPE;
            if (m_isDeposit)
            {
                if (m_ctx->seenNonDeposit)
                    // Deposit after a non-deposit: warn only. op-geth/op-reth accept
                    // this at validation (they enforce deposit-first at the sequencer).
                    BCOS_LOG(WARNING)
                        << LOG_BADGE("OPSTACK") << "deposit after non-deposit in block — accepted";
                try
                {
                    m_deposit = decodeOpDepositEnvelope(rawEnv);
                }
                catch (const OpEthDepositValidationFailed& e)
                {
                    // Bad deposit envelope is a consensus reject, not an internal error.
                    throw bcos::evm::OpConsensusError(
                        std::string("OpScheduler: deposit envelope validation failed: ") +
                        e.what());
                }
                co_return;  // deposit has no validateTransaction
            }

            if (!m_ctx->feeLoaded)
            {  // Load fee params after the L1 attributes deposit (consensus-critical
               // ordering). Read the raw view: a fault here must surface as a
               // storage error (-32603), never a consensus reject.
                try
                {
                    m_ctx->fee = co_await loadOpFeeParamsAsync(storage);
                }
                catch (const std::exception& e)
                {
                    throw bcos::evm::engine::OpStorageError(
                        std::string("op-eth executor: fee-param read failed: ") + e.what());
                }
                catch (...)
                {
                    throw bcos::evm::engine::OpStorageError(
                        "op-eth executor: fee-param read failed: unknown exception");
                }
                if (m_ctx->daFootprintGasScalar)
                    m_ctx->fee.da_footprint_gas_scalar = *m_ctx->daFootprintGasScalar;
                m_ctx->feeLoaded = true;
            }

            m_blockInfo = executor.buildBlockInfoChecked(blockHeader, call, m_ctx->blockGasLeft);

            // eth_call clamp (ported m_prepare): only a pricing-less call (no
            // gasPrice / maxFeePerGas) gets its cap floored to the block base
            // fee — the EIP-1559 priority term is capped at max-baseFee, so the
            // effective price becomes exactly baseFee, never above. A caller
            // that explicitly requested a lower cap keeps it and validation
            // rejects with FEE_CAP_LESS_THAN_BLOCKS, exactly like op-geth's
            // ErrFeeCapTooLow.
            if (call && !transaction.gasPrice().has_value() &&
                !transaction.maxFeePerGas().has_value())
            {
                m_callParams.maxGasPriceFloor = bcos::u256{m_blockInfo->base_fee};
            }

            if (call)
            {
                // CallSimulationView equivalent: the simulated sender reports
                // uint256::max() balance so validate and the OP 512-bit cost cap
                // both run (and pass) for an unfunded sender. get_or_insert first
                // LOADS the real account — nonce and code_hash stay intact, so
                // EIP-3607 (SENDER_NOT_EOA) still fires for a contract sender; a
                // fresh sender materializes an EMPTY_CODE_HASH account. finish()
                // rolls every simulated write back.
                auto& acc = m_state->get_or_insert(eth::ethSender(transaction));
                acc.balance = std::numeric_limits<eth::uint256>::max();
            }
            else
            {
                // Block path, fixed order (ported m_prepare's chainId-gated block):
                // sender size first — ethSender() zero-fills a short sender, which
                // would otherwise masquerade as the zero-sender case.
                auto const& sb = transaction.sender();
                if (!sb.empty() && sb.size() != sizeof(evmc_address))
                    throw bcos::evm::OpConsensusError(
                        "op block: sender must be empty or exactly 20 bytes");
                if (auto missing = opEthBlockPathZeroSender(transaction))
                    throw bcos::evm::OpConsensusError("op block: " + *missing, transaction.hash());
                // The SIGNED envelope's chainId vs the node (op-geth ErrInvalidChainId)
                // — never the tars mirror.
                if (auto gate = opEthEnvelopeChainIdMismatch(transaction, m_ctx->chainId))
                    throw bcos::evm::OpConsensusError("op block: " + *gate, transaction.hash());
                // Execution fields must match the signed envelope. Runs before the
                // 7702 gate so the tx type is envelope-bound when it reads it.
                if (auto mismatch = opEthEnvelopeExecutionFieldsMismatch(transaction))
                    throw bcos::evm::OpConsensusError(
                        "op block: tx execution fields diverge from the signed envelope: " +
                            *mismatch,
                        transaction.hash());
                if (auto unbound = opEthBlockPathUnboundAuthorizationList(transaction))
                    throw bcos::evm::OpConsensusError("op block: " + *unbound, transaction.hash());
            }

            // eth_call USUALLY carries no signed envelope (but may — CallRequest's
            // TARS path stores one). Synthesize a sizing envelope only when none
            // is present; the block path never synthesizes (there the envelope is
            // the trust anchor).
            if (call && rawEnv.empty())
                m_synthesizedEnvelope =
                    synthesizeOpEthCallSizingEnvelope(transaction, m_callParams);

            OpPolicy const policy{executor.m_spec, m_ctx->fee, *m_blockInfo, envelopeView(),
                transaction, m_callParams, m_snapshot};
            auto validated = eth::validateTransaction(*m_state, *m_blockInfo, transaction,
                executor.m_spec.rev, m_ctx->blockGasLeft,
                0 /*blobGasLeft — blob txs are rejected by the policy*/, m_callParams, policy);
            if (auto const* err = std::get_if<std::error_code>(&validated))
            {
                // On the block path a full gas pool is a capacity fault (keep the
                // transaction), not a poisoned-tx reject; eth_call / estimateGas
                // keep GAS_LIMIT_REACHED as ordinary validation.
                if (*err == make_error_code(eth::evm::ErrorCode::GAS_LIMIT_REACHED) && !call)
                {
                    throw bcos::evm::OpConsensusError(
                        std::string("OpScheduler: block gas pool full: ") + err->message(),
                        transaction.hash(), /*capacity=*/true);
                }
                // DEBUG not WARNING: reachable from unauthenticated eth_call /
                // estimateGas — a WARNING here would be a log-amplification vector.
                BCOS_LOG(DEBUG) << LOG_BADGE("OPSTACK") << LOG_DESC("op-eth validate failed")
                                << LOG_KV("reason", err->message())
                                << LOG_KV("hash", transaction.hash().hexPrefixed())
                                << LOG_KV("call", call);
                bcos::evm::OpConsensusError reject(
                    std::string("OpScheduler: normal tx validation failed: ") + err->message(),
                    transaction.hash());
                reject.validateErrorCode = *err;
                throw reject;
            }
            m_txProps = std::get<eth::EthTxProperties>(validated);
            // Only after a successful validate: a rejected normal tx must not flip
            // the deposit-after-non-deposit warn path for a later deposit.
            m_ctx->seenNonDeposit = true;
        }

        /// Stage 2 — execute (ported ExecuteContext::execute / m_execute +
        /// executeDeposit): deposits via opRunDeposit, normal txs via
        /// runTransaction; both write straight through the journaling chain.
        task::Task<void> execute()
        {
            requireBlockContext();
            // A missing block-hash source on the block path would silently degrade
            // BLOCKHASH to zeros (the empty-lookup zero answer is the documented
            // eth_call/standalone fallback) — fail loud instead. Node-wiring fault:
            // thrown WITHOUT the per-tx txHash tag (a set txHash marks a
            // pool-evictable culprit, and evicting one innocent tx per retry cannot
            // fix an unwired RecentBlockHashes source).
            if (!call && !m_ctx->blockHashLookup)
                throw bcos::evm::OpConsensusError(
                    "OpEthExecutor: block execution requires wired RecentBlockHashes");

            // Per-tx all-or-nothing: take the savepoint before any write so a
            // part-way failure (e.g. a storage fault inside applyToStorage) can be
            // undone instead of leaving partial writes behind.
            m_savepoint = m_rollable->current();
            std::exception_ptr failure;

            if (m_isDeposit)
            {
                // Deposit BlockInfo is built at execute time (the legacy
                // executeDeposit pattern), not in prepare.
                auto const blockInfo =
                    executor.buildBlockInfoChecked(blockHeader, call, m_ctx->blockGasLeft);
                try
                {
                    m_receipt = co_await opRunDeposit(*m_state, blockInfo, m_ctx->blockHashLookup,
                        *m_deposit, executor.m_spec, executor.m_vm, m_ctx->chainId,
                        m_ctx->blockGasLeft, *executor.m_receiptFactory, blockHeader.number());
                }
                catch (...)
                {
                    failure = std::current_exception();
                }
                if (failure)
                {
                    co_await rollbackQuietly();
                    // A poisoned slot means a storage fault — it always wins the
                    // classification (ported rethrowStorageFaultIfPoisoned ordering).
                    if (m_guard->poisoned())
                        m_guard->throwIfPoisoned();
                    try
                    {
                        std::rethrow_exception(failure);
                    }
                    catch (...)
                    {
                        OpEthExecutor::rethrowExecError("deposit execution");
                    }
                }
                co_return;
            }

            try
            {
                // Fresh policy over the SAME snapshot member that prepare's
                // validate filled (the validate/transition fork-boundary
                // discipline — see OpTxSnapshot).
                OpPolicy const policy{executor.m_spec, m_ctx->fee, *m_blockInfo, envelopeView(),
                    transaction, m_callParams, m_snapshot};
                m_receipt =
                    co_await eth::runTransaction(*m_state, *m_blockInfo, m_ctx->blockHashLookup,
                        transaction, executor.m_spec.rev, executor.m_vm, m_txProps, m_ctx->chainId,
                        m_callParams, *executor.m_receiptFactory, blockHeader.number(), policy);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            if (failure)
            {
                co_await rollbackQuietly();
                if (m_guard->poisoned())
                    m_guard->throwIfPoisoned();
                try
                {
                    std::rethrow_exception(failure);
                }
                catch (...)
                {
                    OpEthExecutor::rethrowExecError("tx execution");
                }
            }
        }

        /// Stage 3 — finalize (ported ExecuteContext::finish / m_finish): the
        /// state was already written through in execute(); an eth_call rolls the
        /// simulated writes back here (the legacy "discard the diff for
        /// call=true" equivalent). This stage solely owns the cumulative-gas
        /// backfill + blockGasLeft decrement.
        task::Task<protocol::TransactionReceipt::Ptr> finish()
        {
            requireBlockContext();
            executor.checkForkRevision(ledgerConfig);
            if (call)
            {
                // Discard the dry-run writes (fabricated balance, deposit mint,
                // nonce bumps): the simulation ran against a throwaway in-tx
                // journal and none of it may reach the caller's storage.
                try
                {
                    co_await m_rollable->rollback(m_savepoint);
                }
                catch (const std::exception& e)
                {
                    throw bcos::evm::engine::OpStorageError(
                        std::string("op-eth executor: dry-run rollback failed: ") + e.what());
                }
                catch (...)
                {
                    throw bcos::evm::engine::OpStorageError(
                        "op-eth executor: dry-run rollback failed: unknown exception");
                }
            }
            // Read/write-path poison observed anywhere in this tx (the shared
            // slot aggregates per-tx instances) — a storage fault, -32603.
            m_guard->throwIfPoisoned();
            // narrowGasUsed throws OpConsensusError on an over-wide receipt
            // (OpCommon.h's engine::detail) — the INVALID side of the boundary, as legacy.
            auto const gasUsed = bcos::evm::engine::detail::narrowGasUsed(m_receipt->gasUsed());
            m_ctx->cumulativeGasUsed += gasUsed;
            // Decimal string: the RPC read path lexical_casts decimal only.
            m_receipt->setCumulativeGasUsed(bcos::evm::engine::detail::decimalCumulative(
                static_cast<uint64_t>(m_ctx->cumulativeGasUsed)));
            m_ctx->blockGasLeft -= gasUsed;
            co_return m_receipt;
        }
    };

    /// 7-arg form (OP path): the caller owns the BlockContext and must keep it
    /// alive across the prepare/execute/finish lifecycle (SchedulerSerialImpl
    /// forwards the ctx that lives in the caller's coroutine frame).
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        BlockContext const& blockCtx)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, &blockCtx};
    }

    // Deleted rvalue overload: this is a lazy coroutine, so a temporary bound to
    // the const& above dies at the call-site full expression — before the body
    // first runs — leaving m_ctx dangling. Compile error instead of UB.
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call,
        BlockContext const&& blockCtx) = delete;

    /// 6-arg form (generic scheduler + the TransactionExecutor concept probe):
    /// no BlockContext is available, so m_ctx is null and any
    /// prepare/execute/finish throws. OP is never driven through this form
    /// (SchedulerSerialImpl's requires probe always picks the 7-arg overload),
    /// but it must stay valid for the concept, which calls with 6 args.
    template <class Storage>
    task::Task<ExecuteContext<Storage>> createExecuteContext(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        co_return ExecuteContext<Storage>{
            *this, storage, blockHeader, transaction, contextID, ledgerConfig, call, nullptr};
    }

    /// 6-arg form matching the TransactionExecutor concept probe
    /// (TransactionExecutor.h). This IS the eth_call / estimateGas entry point
    /// (BaselineScheduler's coCallLatest / callAtBlock resolve to this
    /// overload), so it must drive a real simulation rather than throw. A local
    /// BlockContext is built in the coroutine frame (an lvalue, so it outlives
    /// the awaits and does not trip the deleted rvalue createExecuteContext
    /// overload); ctx.blockHashLookup stays empty, which execute() accepts for
    /// call=true. The block path (call=false) still throws: it needs the fee /
    /// blockGasLeft / blockHashLookup that only a scheduler-provided
    /// BlockContext carries.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call)
    {
        if (!call)
        {
            throw bcos::evm::OpConsensusError(
                "OpEthExecutor: 6-arg executeTransaction block execution requires a "
                "scheduler-provided BlockContext (use the 10-arg form)");
        }
        BlockContext ctx{};
        if (auto const& chainId = ledgerConfig.chainId(); chainId.has_value())
        {
            ctx.chainId = static_cast<uint64_t>(bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef{
                reinterpret_cast<bcos::byte const*>(chainId->bytes), sizeof(chainId->bytes)}));
        }
        ctx.blockGasLeft = static_cast<int64_t>(
            bcos::evm::engine::detail::narrowU256ToI64(blockHeader.gasLimit(), "gasLimit"));
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call, ctx);
        co_await executeContext.prepare();
        co_await executeContext.execute();
        co_return co_await executeContext.finish();
    }

    /// Execute a single OP transaction with an explicit per-block context
    /// (injection-style, mirroring the legacy 10-arg form). The orchestrator
    /// supplies the fee (pre-loaded), the decrementing blockGasLeft, the node
    /// chainId and the real block-hash lookup. All trailing params are
    /// required: these are coroutines (task::Task is lazy — the body runs
    /// after the call expression), so defaulted reference parameters would
    /// bind temporaries destroyed before first use.
    template <class Storage>
    task::Task<protocol::TransactionReceipt::Ptr> executeTransaction(Storage& storage,
        protocol::BlockHeader const& blockHeader, protocol::Transaction const& transaction,
        int contextID, ledger::LedgerConfig const& ledgerConfig, bool call, OpFeeParams const& fee,
        int64_t blockGasLeft, uint64_t chainId, eth::BlockHashLookup blockHashLookup)
    {
        (void)contextID;
        BlockContext ctx{};
        ctx.fee = fee;
        ctx.feeLoaded = true;
        ctx.blockGasLeft = blockGasLeft;
        ctx.chainId = chainId;
        ctx.blockHashLookup = std::move(blockHashLookup);
        auto executeContext = co_await createExecuteContext(
            storage, blockHeader, transaction, contextID, ledgerConfig, call, ctx);
        co_await executeContext.prepare();
        co_await executeContext.execute();
        co_return co_await executeContext.finish();
    }

private:
    /// BlockInfo for tx execution (buildOpEthBlockInfo), with the legacy
    /// fallback: a header that leaves gasLimit unset (==0, e.g. minimal test
    /// headers) uses the caller's blockGasLeft. OpEthBlockError (the new-layer
    /// header-validation error) is reclassified to OpConsensusError — the
    /// legacy path's narrowU256ToI64/requireHeaderField threw
    /// OpConsensusError for the same faults (INVALID, never -32603).
    eth::EthBlockInfo buildBlockInfoChecked(
        protocol::BlockHeader const& header, bool callKind, int64_t blockGasLeftFallback) const
    {
        try
        {
            auto blk = buildOpEthBlockInfo(header, m_spec, callKind);
            if (blk.gas_limit == 0)
                blk.gas_limit = blockGasLeftFallback;
            return blk;
        }
        catch (OpEthBlockError const& e)
        {
            throw bcos::evm::OpConsensusError(e.what());
        }
    }

    /// Fork/revision gate shared by the lifecycle stages: ledger evmcRevision
    /// must be configured and match this executor's fork spec.
    void checkForkRevision(ledger::LedgerConfig const& ledgerConfig) const
    {
        auto revOpt = ledgerConfig.evmcRevision();
        if (!revOpt.has_value())
            BOOST_THROW_EXCEPTION(OpEthEvmcRevisionNotConfigured{}
                                  << bcos::errinfo_comment("evmcRevision not configured"));
        if (m_spec.rev != *revOpt)
            BOOST_THROW_EXCEPTION(OpEthForkRevisionMismatch{} << bcos::errinfo_comment(
                                      "OP fork revision does not match ledger evmcRevision"));
    }

    /// Shared execution-path error ladder (ported rethrowExecError): typed
    /// engine errors and local misconfiguration pass through; anything else
    /// (opRunDeposit/runTransaction's block-level errors arrive as bare
    /// std::runtime_error) is reclassified to the consensus-rejection channel
    /// (INVALID, never -32603).
    [[noreturn]] static void rethrowExecError(std::string const& what)
    {
        try
        {
            throw;
        }
        catch (const bcos::evm::OpConsensusError&)
        {
            throw;
        }
        catch (const bcos::evm::engine::OpStorageError&)
        {
            throw;
        }
        catch (const OpEthEvmcRevisionNotConfigured&)
        {
            throw;  // local misconfiguration, not a consensus rejection
        }
        catch (const OpEthForkRevisionMismatch&)
        {
            throw;
        }
        catch (const OpEthDepositGasLimitReached& e)
        {
            // Deposit gas_limit exceeds the remaining block gas (op-geth
            // ErrGasLimitReached): a capacity fault, not a poisoned tx.
            bcos::evm::OpConsensusError err("OpScheduler: " + what + " failed: " + e.what());
            err.capacity = true;
            throw err;
        }
        catch (const std::exception& e)
        {
            throw bcos::evm::OpConsensusError("OpScheduler: " + what + " failed: " + e.what());
        }
        catch (...)
        {
            throw bcos::evm::OpConsensusError(
                "OpScheduler: " + what + " failed: unknown exception");
        }
    }

    protocol::TransactionReceiptFactory::Ptr m_receiptFactory;
    // Value copy, not a reference: OpForkSpec is small (once per block) and a
    // reference member to a caller's config is a lifetime footgun.
    OpForkSpec m_spec;
    /// Block-wide storage-error slot: shared by every per-tx storage chain this
    /// executor constructs so per-tx faults surface at the block-level check.
    std::shared_ptr<OpStorageErrorSlot> m_errorSlot;
    evmc::VM m_vm;
};

}  // namespace bcos::executor_v1::opstack
