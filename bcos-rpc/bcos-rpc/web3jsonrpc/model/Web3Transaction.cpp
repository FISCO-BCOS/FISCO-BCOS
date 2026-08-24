/**
 *  Copyright (C) 2022 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file Web3Transaction.cpp
 * Tars conversion only — RLP encode/decode lives in bcos-codec.
 */

#include "Web3Transaction.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/move.hpp>

namespace bcos::rpc
{
bcostars::Transaction takeToTarsTransaction(Web3Transaction& tx)
{
    bcostars::Transaction tarsTx{};
    tarsTx.data.to = (tx.to.has_value()) ? tx.to.value().hexPrefixed() : "";
    tarsTx.data.input.reserve(tx.data.size());
    ::ranges::move(tx.data, std::back_inserter(tarsTx.data.input));

    tarsTx.data.value = "0x" + tx.value.str(0, std::ios_base::hex);
    tarsTx.data.gasLimit = tx.gasLimit;
    if (static_cast<uint8_t>(tx.type) >= static_cast<uint8_t>(TransactionType::EIP1559))
    {
        tarsTx.data.maxFeePerGas = "0x" + tx.maxFeePerGas.str(0, std::ios_base::hex);
        tarsTx.data.maxPriorityFeePerGas =
            "0x" + tx.maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    else
    {
        tarsTx.data.gasPrice = "0x" + tx.maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tarsTx.web3TypedTxKind = static_cast<tars::Char>(static_cast<uint8_t>(tx.type));
    if (!tx.accessList.empty())
    {
        tarsTx.data.accessList.reserve(tx.accessList.size());
        for (auto const& entry : tx.accessList)
        {
            bcostars::Web3AccessListEntry tarsEntry;
            tarsEntry.account = entry.account.hex();
            for (auto const& key : entry.storageKeys)
            {
                tarsEntry.storageKeys.emplace_back(key.begin(), key.end());
            }
            tarsTx.data.accessList.emplace_back(std::move(tarsEntry));
        }
    }

    auto encodedForSign = tx.encodeForSign();
    tarsTx.extraTransactionBytes.reserve(encodedForSign.size());
    ::ranges::move(encodedForSign, std::back_inserter(tarsTx.extraTransactionBytes));

    tarsTx.signature.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    ::ranges::move(tx.signatureR, std::back_inserter(tarsTx.signature));
    ::ranges::move(tx.signatureS, std::back_inserter(tarsTx.signature));
    tarsTx.signature.push_back(static_cast<tars::Char>(tx.signatureV));

    tarsTx.data.nonce = toQuantity(tx.nonce);
    tarsTx.data.chainID = std::to_string(tx.chainId.value_or(0));

    return tarsTx;
}
}  // namespace bcos::rpc
