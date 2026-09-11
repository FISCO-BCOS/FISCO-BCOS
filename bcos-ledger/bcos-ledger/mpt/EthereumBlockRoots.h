/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthereumBlockRoots.h
 * @brief The deterministic post-execution block values an Ethereum header commits to
 *        (txsRoot / receiptsRoot / gasUsed / logsBloom), computed from the executed
 *        receipts and the raw EIP-2718 transactions. Shared by the Sepolia sync /
 *        external-payload verifier (EthereumBlockVerifier) and the Engine API block
 *        builder (EngineServiceImpl::buildPayload) — moved here from
 *        EthereumBlockVerifier::computeEthereumRoots so both compute identical roots.
 */
#pragma once

#include "EthTrieRoots.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-rlp-protocol/EthReceipt.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <boost/throw_exception.hpp>
#include <range/v3/range.hpp>
#include <stdexcept>
#include <vector>

namespace bcos::ledger::mpt
{

/// The deterministic post-execution block values that must match the header.
struct EthereumBlockComputation
{
    crypto::HashType txsRoot;
    crypto::HashType receiptsRoot;
    u256 gasUsed;
    Bloom logsBloom;
};

/// Ethereum receipts trie root (receiptsRoot), computed AFTER the receipt-processing phase
/// that fills cumulativeGasUsed + logsBloom (computeEthereumRoots does it in-place;
/// BaselineScheduler::finishExecute does it in its receipt branch). Each receipt is
/// converted via toEthReceiptData (status remapped to EIP-658 0/1) and RLP-encoded, typed
/// by the transaction at the same index, then committed to the index-keyed trie
/// (calculateReceiptsRoot). Empty input -> emptyRootHash().
///
/// One receipt per executed transaction: a count mismatch is a scheduler contract
/// violation, and indexing the per-transaction types past their end would be UB, so it
/// throws instead.
///
/// @param receipts     executed receipts (elements dereference to TransactionReceipt)
/// @param transactions the executed transactions, parallel to `receipts` by index, supplying
///                     the EIP-2718 type per receipt; elements may be Transaction
///                     values/references or Transaction::Ptr
inline h256 calculateEthereumReceiptsRoot(
    ::ranges::input_range auto const& receipts, ::ranges::input_range auto const& transactions)
{
    std::vector<uint8_t> txTypes;
    txTypes.reserve(::ranges::size(transactions));
    for (auto const& transaction : transactions)
    {
        if constexpr (requires { transaction.web3TypedTxKind(); })
        {
            txTypes.push_back(transaction.web3TypedTxKind());
        }
        else
        {
            txTypes.push_back(transaction->web3TypedTxKind());
        }
    }
    if (txTypes.size() != ::ranges::size(receipts))
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{
            "calculateEthereumReceiptsRoot: receipt count does not match executed transaction "
            "count"});
    }
    std::vector<bcos::bytes> receiptRlps;
    receiptRlps.reserve(::ranges::size(receipts));
    size_t index = 0;
    for (auto const& receipt : receipts)
    {
        protocol::EthReceiptData eth;
        if (auto err = protocol::toEthReceiptData(*receipt, txTypes[index], eth); err != nullptr)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error("toEthReceiptData: " + err->errorMessage()));
        }
        bcos::bytes encoded;
        protocol::EthReceipt ethReceipt(std::move(eth));
        ethReceipt.rlpEncode(encoded);
        receiptRlps.push_back(std::move(encoded));
        ++index;
    }
    std::vector<bcos::bytesConstRef> refs;
    refs.reserve(receiptRlps.size());
    for (auto const& rlp : receiptRlps)
    {
        refs.emplace_back(bcos::ref(rlp));
    }
    return calculateReceiptsRoot(refs);
}

/// Compute the deterministic roots for a v2 (Ethereum executor) block. Fills each
/// receipt's cumulativeGasUsed + logsBloom IN PLACE (the receipts trie and the block-level
/// bloom need them; the raw SchedulerSerialImpl path skips BaselineScheduler's receipt
/// phase), then commits to the index-keyed transaction / receipt tries.
///
/// @param receipts        executed receipts, one per EXECUTED transaction
/// @param transactions    the executed transactions (protocol::Transaction elements), used
///                        for each receipt's EIP-2718 type — same index as receipts
/// @param rawTransactions raw EIP-2718 encodings of EVERY payload transaction, in payload
///                        order (a superset of `transactions` when raw-only forced entries
///                        ride along unexecuted, e.g. OP deposit transactions)
inline task::Task<EthereumBlockComputation> computeEthereumRoots(
    std::vector<protocol::TransactionReceipt::Ptr>& receipts,
    ::ranges::input_range auto const& transactions,
    ::ranges::input_range auto const& rawTransactions)
{
    EthereumBlockComputation computation;

    // Per-receipt cumulativeGasUsed + logsBloom.
    u256 cumulativeGasUsed;
    for (auto& receipt : receipts)
    {
        if (!receipt)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error{"computeEthereumRoots: null receipt returned by scheduler"});
        }
        auto logBloom = bcos::getLogsBloom(receipt->logEntries());
        receipt->setLogsBloom({logBloom.data(), logBloom.size()});
        cumulativeGasUsed += receipt->gasUsed();
        receipt->setCumulativeGasUsed(cumulativeGasUsed.str());
    }

    // txsRoot over the raw EIP-2718 encodings (index-keyed trie).
    std::vector<bcos::bytesConstRef> txRaws;
    txRaws.reserve(::ranges::size(rawTransactions));
    for (auto const& raw : rawTransactions)
    {
        txRaws.emplace_back(bcos::ref(raw));
    }
    computation.txsRoot = calculateTransactionsRoot(txRaws);

    // receiptsRoot over EthReceipt RLP, typed by the executed transaction at the same index.
    computation.receiptsRoot = calculateEthereumReceiptsRoot(receipts, transactions);

    // gasUsed + block-level bloom.
    u256 totalGasUsed;
    Bloom logsBloom{};
    for (auto const& receipt : receipts)
    {
        totalGasUsed += receipt->gasUsed();
        if (!receipt->logsBloom().empty())
        {
            bcos::orBloom(logsBloom, receipt->logsBloom());
        }
    }
    computation.gasUsed = totalGasUsed;
    computation.logsBloom = logsBloom;
    co_return computation;
}

}  // namespace bcos::ledger::mpt
