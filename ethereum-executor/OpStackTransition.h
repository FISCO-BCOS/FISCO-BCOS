/// @file OpStackTransition.h
/// @brief OP Stack (Karst) per-transaction execution lane of the executor.
///
/// Normal (non-deposit) transactions run through bcos-evm's opValidate /
/// opTransition — L1 data fee (FastLZ), operator fee, base/L1/operator fee
/// vault routing, Karst precompile overrides — and 0x7E deposits run through
/// runDeposit. Those functions are the single consensus implementation of the
/// OP semantics (see OpStackBridge.h for why they are bridged rather than
/// re-ported); this header converts the BCOS-side inputs (protocol::Transaction,
/// EthBlockInfo) to their evmone-side twins, feeds them through, applies the
/// resulting StateDiff to BCOS storage and produces the BCOS receipt.
///
/// Fee parameters are read from the OP_L1_BLOCK predeploy storage slots
/// (opValidateFromState -> loadOpFeeParams) through the same StateView the
/// transaction executes against.
///
/// Per-transaction lane ONLY: block-level coordination (EIP-4788/2935 system
/// calls, block gas accounting across transactions, receipt/DA roots) is the
/// block-lifecycle layer's job, not this header's.

#pragma once

#include "OpStackBridge.h"
#include "bcos-evm/opstack/OpForkSchedule.h"
#include "bcos-evm/opstack/OpTransition.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-framework/protocol/TransactionReceipt.h"
#include "bcos-framework/protocol/TransactionReceiptFactory.h"
#include <bcos-utilities/Exceptions.h>
#include <evmc/evmc.hpp>
#include <system_error>
#include <variant>

namespace bcos::executor_v1::eth
{

DERIVE_BCOS_EXCEPTION(InvalidDepositTransaction);
/// A StorageStateView read swallowed a storage failure (see readFailed()): the execution
/// result was computed against fabricated state and is discarded via the caller's
/// rollback/rethrow path.
DERIVE_BCOS_EXCEPTION(OpStateReadFailed);

// Declared in EthereumExecutor.h (defined in EthereumExecutor.cpp); redeclared here to
// avoid a circular include — EthereumExecutor.h includes this header for the dispatch.
protocol::TransactionReceipt::Ptr validationErrorReceipt(std::error_code const& error,
    protocol::TransactionReceiptFactory const& rf, int64_t blockNumber);

/// Why a 0x7e envelope failed to decode. Every value is a REJECT and which one it is
/// changes nothing about that verdict — the codes exist purely so the rejection can be
/// read. Collapsing them all into std::errc::invalid_argument made
/// InvalidDepositTransaction report "Invalid argument" for a wrong type byte, a
/// truncated field and a non-canonical integer alike, with nothing in the log to tell an
/// operator which one stalled the payload.
enum class DepositDecodeError : int
{
    WrongTypeByte = 1,    ///< Empty input, or the first byte is not 0x7e.
    MalformedEnvelope,    ///< The outer RLP header is undecodable or is not a list.
    TrailingBytes,        ///< Bytes remain after the RLP list / after the final field.
    TruncatedBody,        ///< A field is missing: the list ran out mid-way.
    MalformedField,       ///< An item's RLP is undecodable or the wrong width — a hash,
                          ///< an address, the data blob, or an integer field whose own
                          ///< header will not decode.
    NonCanonicalInteger,  ///< Integer payload starts with a zero byte (canonical zero is
                          ///< the empty payload).
    IntegerTooWide,       ///< Integer payload exceeds the field's width (mint/value 32
                          ///< bytes, gas 8, isSystemTx 1).
    NonCanonicalBool,     ///< isSystemTx is neither 0x80 (false) nor 0x01 (true).
    GasLimitOutOfRange,   ///< gas does not fit the executable int64 gas_limit.
};

/// Category for DepositDecodeError, in the same shape as evm_error_category()
/// (EVMSupport.h). Defined in OpStackTransition.cpp.
const std::error_category& depositDecodeCategory() noexcept;

/// Creates an error_code out of a deposit-decode error value.
std::error_code make_error_code(DepositDecodeError error) noexcept;

/// Decode a raw `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx,
/// data])` envelope into the executable DepositTx. Consensus-grade, unlike the
/// display decoder in bcos-rpc (DepositTransaction.h): the envelope must be fully
/// consumed (no trailing bytes after the RLP list), integer fields follow op-geth's
/// canonical rules (no leading zeros, mint/value <= 32 bytes, gas <= 8 bytes and
/// int64, isSystemTx only 0x80/0x01). The failure value is a DepositDecodeError-coded
/// error_code; the set of accepted envelopes does not depend on it. Defined in
/// OpStackTransition.cpp.
std::variant<bcos::evm::opstack::DepositTx, std::error_code> decodeDepositTx(
    bcos::bytesConstRef rawDeposit);

/// The evmone-side twin of the BCOS transaction (type / fee fields / to / access
/// list / blob hashes / authorization list). Defined in OpStackTransition.cpp.
evmone::state::Transaction toEvmoneTransaction(protocol::Transaction const& transaction);

/// The evmone-side twin of the executor's block parameters (field-for-field; the
/// EthBlockInfo was already derived from the BlockHeader + LedgerConfig by
/// buildBlockInfo). Defined in OpStackTransition.cpp.
evmone::state::BlockInfo toEvmoneBlockInfo(EthBlockInfo const& blockInfo);

/// BCOS receipt from an executed evmone receipt (status mapping / log conversion
/// shared with the pure-Ethereum lane's conventions). Defined in
/// OpStackTransition.cpp.
protocol::TransactionReceipt::Ptr toBcosReceipt(evmone::state::TransactionReceipt const& receipt,
    protocol::TransactionReceiptFactory const& receiptFactory, int64_t blockNumber);

/// The raw signed EIP-2718 envelope of a Web3 transaction (the L1 data fee is
/// priced over these exact bytes). Empty when the transaction has no wire form —
/// opValidate then rejects it. Defined in OpStackTransition.cpp.
bcos::bytes signedTxEnvelope(protocol::Transaction const& transaction);

/// Execute one normal (non-deposit) transaction under OP Karst semantics.
/// Writes the state diff into @p storage; the caller owns atomicity (wrap in a
/// Rollbackable and roll back on exception, as ExecuteContext::execute() does).
template <class Storage>
task::Task<protocol::TransactionReceipt::Ptr> executeOpStackTransaction(Storage& storage,
    EthBlockInfo const& blockInfo, BlockHashLookup const& blockHashLookup,
    protocol::Transaction const& transaction, evmc::VM& vm, uint64_t chainId,
    protocol::TransactionReceiptFactory const& receiptFactory, int64_t blockNumber)
{
    // Karst-only chain: every fork up to Karst is active from genesis
    // (rollup.json fork times all 0), so the fork config is not block-dependent.
    const auto& cfg = bcos::evm::opstack::karstConfig();
    const StorageStateView<Storage> view{storage};
    const auto evmBlock = toEvmoneBlockInfo(blockInfo);
    const LookupBlockHashes hashes{blockHashLookup, blockInfo.number};
    const auto evmTx = toEvmoneTransaction(transaction);
    const auto envelope = signedTxEnvelope(transaction);

    auto validated = bcos::evm::opstack::opValidateFromState(view, evmBlock, evmTx,
        evmc::bytes_view{envelope.data(), envelope.size()}, cfg,
        /*blockGasLeft=*/evmBlock.gas_limit);
    if (const auto* error = std::get_if<std::error_code>(&validated))
    {
        // A "validation failure" derived from fabricated state (a swallowed storage
        // read failure) must not become a failure receipt.
        if (view.readFailed())
        {
            BOOST_THROW_EXCEPTION(OpStateReadFailed() << errinfo_comment(
                                      "storage read failed during OP transaction validation"));
        }
        co_return validationErrorReceipt(*error, receiptFactory, blockNumber);
    }

    auto opReceipt = bcos::evm::opstack::opTransition(view, evmBlock, hashes, evmTx, cfg, vm,
        std::get<bcos::evm::opstack::OpTxProperties>(validated), chainId);
    // The StateView is noexcept, so a storage failure inside validate/transition was
    // answered as "absent / empty" and latched instead of thrown. Discard the result
    // BEFORE it reaches storage — the caller's rollback/rethrow path takes over.
    if (view.readFailed())
    {
        BOOST_THROW_EXCEPTION(OpStateReadFailed() << errinfo_comment(
                                  "storage read failed during OP transaction execution"));
    }
    co_await applyEvmoneStateDiff(storage, opReceipt.receipt.state_diff);
    co_return toBcosReceipt(opReceipt.receipt, receiptFactory, blockNumber);
}

/// Execute one 0x7E deposit from its raw envelope. A malformed envelope or a
/// deposit runDeposit rejects (is_system_tx, block gas) is a BLOCK-level error
/// and throws — deposits are consensus-injected (OP payload attributes), so an
/// invalid one invalidates the whole payload rather than producing a failure
/// receipt. Same atomicity contract as executeOpStackTransaction.
template <class Storage>
task::Task<protocol::TransactionReceipt::Ptr> executeOpStackDeposit(Storage& storage,
    EthBlockInfo const& blockInfo, BlockHashLookup const& blockHashLookup,
    bcos::bytesConstRef rawDeposit, evmc::VM& vm, uint64_t chainId,
    protocol::TransactionReceiptFactory const& receiptFactory, int64_t blockNumber)
{
    auto decoded = decodeDepositTx(rawDeposit);
    if (const auto* error = std::get_if<std::error_code>(&decoded))
    {
        BOOST_THROW_EXCEPTION(InvalidDepositTransaction() << errinfo_comment(
                                  "malformed 0x7e deposit envelope: " + error->message()));
    }

    const auto& cfg = bcos::evm::opstack::karstConfig();
    const StorageStateView<Storage> view{storage};
    const auto evmBlock = toEvmoneBlockInfo(blockInfo);
    const LookupBlockHashes hashes{blockHashLookup, blockInfo.number};

    // runDeposit throws std::runtime_error for its block-level rejections;
    // propagate.
    auto depositReceipt = bcos::evm::opstack::runDeposit(view, evmBlock, hashes,
        std::get<bcos::evm::opstack::DepositTx>(decoded), cfg, vm, chainId,
        /*blockGasLeft=*/evmBlock.gas_limit);
    // Same read-failure gate as executeOpStackTransaction: a deposit executed against
    // fabricated state must not be applied.
    if (view.readFailed())
    {
        BOOST_THROW_EXCEPTION(OpStateReadFailed() << errinfo_comment(
                                  "storage read failed during OP deposit execution"));
    }
    co_await applyEvmoneStateDiff(storage, depositReceipt.receipt.state_diff);
    co_return toBcosReceipt(depositReceipt.receipt, receiptFactory, blockNumber);
}

}  // namespace bcos::executor_v1::eth
