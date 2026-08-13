#pragma once

#include <bcos-concepts/ByteBuffer.h>             // bcos::concepts::bytebuffer::toView
#include <bcos-framework/engine/Errors.h>         // OpExecutionInternalError
#include <bcos-framework/ledger/LedgerTypeDef.h>  // SYS_* table constants (LedgerTypeDef.h:106-112)
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/protocol/BlockHeader.h>
#include <bcos-framework/storage/Entry.h>                  // bcos::storage::Entry
#include <bcos-framework/storage2/Storage.h>               // storage2::writeOne
#include <bcos-framework/transaction-executor/StateKey.h>  // bcos::executor_v1::StateKey
#include <bcos-tars-protocol/protocol/TransactionImpl.h>  // bcostars::Transaction + TransactionImpl (carries tars Transaction.h)
#include <bcos-task/Task.h>
#include <opstack-executor/OpErrors.h>  // OpExecuteBlockResult
#include <boost/lexical_cast.hpp>
#include <functional>
#include <optional>
#include <vector>

namespace bcos::evm::engine
{
/// raw EIP-2718 envelope -> tars Transaction. Signature matches the engine's
/// `bcos::engine::detail::opEnvelopeToTars`; the composition root (Initializer) injects a lambda
/// invoking it — opstack-executor does not link bcos-rpc / bcos-engine.
using EnvelopeToTarsConverter = std::function<std::optional<bcostars::Transaction>(
    bcos::bytes const&, bcos::crypto::HashType const&)>;

/// Write the block tables — the OP equivalent of ledger::prewriteBlockToBuffer. Moved line by
/// line from the engine's registerOpBlock, with data sources switched to explicit parameters.
/// Five tables:
///   SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER / SYS_NUMBER_2_BLOCK_HEADER /
///   SYS_HASH_2_RECEIPT / SYS_HASH_2_TX
/// Error classification: receipt-count invariant / null receipt -> OpExecutionInternalError; write
/// failures propagate as-is (the engine barrier classifies -32603). blockHash is passed in
/// explicitly by the caller (already validated by engine step 2 == header.opHeaderHash(
/// opHeaderConst())); not recomputed here, avoiding constant drift.
template <class ViewType>
inline bcos::task::Task<void> opstackRegisterBlock(ViewType& view,
    bcos::protocol::BlockHeader const& header, bcos::crypto::HashType const& blockHash,
    std::vector<bcos::bytes> const& rawTxBytes, OpExecuteBlockResult const& result,
    bcos::protocol::BlockFactory& blockFactory, EnvelopeToTarsConverter const& envelopeToTars)
{
    const auto blockNumberStr = boost::lexical_cast<std::string>(header.number());

    bcos::storage::Entry numberToHashEntry;
    numberToHashEntry.set(blockHash.asBytes());
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, blockNumberStr},
        std::move(numberToHashEntry));

    bcos::storage::Entry hashToNumberEntry;
    hashToNumberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(hashToNumberEntry));

    // The OP header lands in the standard s_number_2_header table as a tars BlockHeader (same
    // table/format as ordinary FISCO blocks). dataHash is empty -> header.hash() throws
    // EmptyBlockHeaderHash; this path never calls it. encode() is a `void encode(bytes&)` out-param
    // (BlockHeader.h:50) — build a buffer first, then read it.
    bcos::storage::Entry headerEntry;
    bcos::bytes headerBuffer;
    header.encode(headerBuffer);
    headerEntry.set(std::move(headerBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(headerEntry));

    // ---- legacy-ledger read-path parity (ethereum executor `Ledger::asyncPrewriteBlock`): ----
    // The OP path used to write only 5 tables; the eth RPC read path depends on two more, without
    // which a VALID block is unqueryable:
    //   1. SYS_CURRENT_STATE / SYS_KEY_CURRENT_NUMBER — eth_blockNumber reads it and returns 0
    //      (block committed but head not advanced); entry = blockNumber string, matching the
    //      production precedent Ledger.cpp:266-270.
    //   2. SYS_NUMBER_2_TXS — tx metadata (hash + to list, persisted via Block::encode);
    //      getBlockData (RECEIPTS/TRANSACTIONS/TRANSACTIONS_HASH) reads it first to get the tx
    //      hash list, then queries SYS_HASH_2_*; without it receipts/txs read empty and
    //      getBlockByNumber(1) returns null. Format matches Ledger.cpp:284-310:
    //      createBlock + appendTransactionMetaData(hash, to) + block.encode().
    // Both writes use storage2::writeOne(view, ...), landing in the same mergeView batch as the
    // OP tables, with identical key encoding.
    bcos::storage::Entry numberEntry;
    numberEntry.set(blockNumberStr);
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{
            bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(numberEntry));

    auto& hashImpl = *blockFactory.cryptoSuite()->hashImpl();
    // Must use blockFactory.createBlock() (not any stateful object the blockFactory already
    // holds): appendTransactionMetaData expects an empty block to carry the metadata. The hash may
    // come from hashImpl (already computed) or the tars tx's hash(); this path uniformly uses
    // hashImpl.hash(rawEnvelope) — the same source as the SYS_HASH_2_TX / SYS_HASH_2_RECEIPT keys
    // (see the loop below), so the metadata's hash matches the lookup keys byte-for-byte. `to`
    // comes from the tars tx's to() (rows whose envelopeToTars fails are skipped, same
    // skip-on-conversion-failure semantics as SYS_HASH_2_TX, keeping metadata and the tx table
    // consistent).
    auto transactionsBlock = blockFactory.createBlock();
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        const auto txHash = hashImpl.hash(rawTxBytes[index]);
        std::string txTo;
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            txTo = std::string(txImpl.to());
        }
        auto txMetaData = blockFactory.createTransactionMetaData(txHash, std::move(txTo));
        transactionsBlock->appendTransactionMetaData(std::move(txMetaData));
    }
    bcos::bytes transactionsBuffer;
    transactionsBlock->encode(transactionsBuffer);
    bcos::storage::Entry number2TxEntry;
    number2TxEntry.set(std::move(transactionsBuffer));
    co_await bcos::storage2::writeOne(view,
        bcos::executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_TXS, blockNumberStr},
        std::move(number2TxEntry));

    // processOpBlock produces exactly one receipt per tx; a count mismatch is a broken execution
    // invariant, failing loudly (an internal error, not a verdict on the block).
    if (rawTxBytes.size() != result.receipts.size())
    {
        BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                  "OP block execution returned a receipt count differing from "
                                  "the transaction count"});
    }
    for (std::size_t index = 0; index < rawTxBytes.size(); ++index)
    {
        auto const& receipt = result.receipts[index];
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(bcos::engine::OpExecutionInternalError{} << bcos::errinfo_comment{
                                      "OP block execution returned a null receipt"});
        }
        bcos::bytes encodedReceipt;
        receipt->encode(encodedReceipt);
        const auto txHash = hashImpl.hash(rawTxBytes[index]);

        bcos::storage::Entry receiptEntry;
        receiptEntry.set(std::move(encodedReceipt));
        co_await bcos::storage2::writeOne(view,
            bcos::executor_v1::StateKey{
                bcos::ledger::SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(txHash)},
            std::move(receiptEntry));

        // Conversion failure (malformed / un-enumerated envelope) -> skip the row; the block stays
        // VALID, that tx is just not queryable by hash.
        if (auto tarsTx = envelopeToTars(rawTxBytes[index], txHash))
        {
            bcostars::protocol::TransactionImpl txImpl(
                [tarsTx = std::move(*tarsTx)]() mutable { return &tarsTx; });
            bcos::bytes encodedTx;
            txImpl.encode(encodedTx);
            bcos::storage::Entry txEntry;
            txEntry.set(std::move(encodedTx));
            co_await bcos::storage2::writeOne(view,
                bcos::executor_v1::StateKey{
                    bcos::ledger::SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(txHash)},
                std::move(txEntry));
        }
    }
}
}  // namespace bcos::evm::engine
