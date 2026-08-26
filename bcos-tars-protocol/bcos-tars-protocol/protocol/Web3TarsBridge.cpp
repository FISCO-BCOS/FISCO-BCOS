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
 * @file Web3TarsBridge.cpp
 * @brief Web3Transaction -> bcostars::Transaction projection.
 * @date 2026/8/25
 */

// Web3Transaction lives in bcos-rlp-protocol, which must stay free of tars so that modules below
// bcos-rpc can decode a signed Web3 envelope. takeToTarsTransaction() is its one member that
// touches tars, so the declaration stays with the class (over a forward-declared
// bcostars::Transaction) and the definition lives here, in the module that owns the tars types.
// protocol-tars already links rlp-protocol, and every caller of this function necessarily links
// protocol-tars -- it is what gives them a bcostars::Transaction to receive.
//
// Body moved verbatim from bcos-rpc/bcos-rpc/web3jsonrpc/model/Web3Transaction.cpp.

#include "bcos-framework/protocol/Transaction.h"  // bcos::protocol::TransactionType
#include "bcos-rlp-protocol/Web3Transaction.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <iterator>
#include <range/v3/algorithm/move.hpp>

namespace bcos::rpc
{
bcostars::Transaction Web3Transaction::takeToTarsTransaction()
{
    if (type == TransactionType::Deposit)
    {
        bcostars::Transaction tarsTx{};
        tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
        tarsTx.web3TypedTxKind =
            static_cast<tars::Char>(static_cast<uint8_t>(TransactionType::Deposit));
        tarsTx.sourceHash = sourceHash.hex();
        tarsTx.sender.assign(from.begin(), from.end());
        // 0x-prefixed, matching the read side u256(...) (parsed in TransactionImpl.cpp mint())
        tarsTx.mint = bcos::toQuantity(mint);
        tarsTx.isSystemTransaction = isSystemTx ? 1 : 0;
        // Full 0x7E envelope (encode()); extraTransactionHash = keccak of it verbatim so
        // deposits are indexable by hash (eth_getTransactionByHash/Receipt) like any other tx.
        auto encoded = encode();
        tarsTx.extraTransactionBytes.reserve(encoded.size());
        ::ranges::move(encoded, std::back_inserter(tarsTx.extraTransactionBytes));
        auto const hash = bcos::crypto::keccak256Hash(bcos::ref(encoded));
        tarsTx.extraTransactionHash.assign(hash.begin(), hash.end());
        // Generic fields (so consumers on the tars generic read path don't see empty values)
        tarsTx.data.to = to.has_value() ? to->hexPrefixed() : "";
        tarsTx.data.input.reserve(data.size());
        ::ranges::move(data, std::back_inserter(tarsTx.data.input));
        tarsTx.data.value = bcos::toQuantity(value);
        tarsTx.data.gasLimit = gasLimit;
        tarsTx.data.nonce = "0x0";  // deposit nonce is always 0
        tarsTx.data.chainID = "0";
        return tarsTx;
    }
    bcostars::Transaction tarsTx{};
    tarsTx.data.to = (this->to.has_value()) ? this->to.value().hexPrefixed() : "";
    tarsTx.data.input.reserve(this->data.size());
    ::ranges::move(this->data, std::back_inserter(tarsTx.data.input));

    tarsTx.data.value = "0x" + this->value.str(0, std::ios_base::hex);
    tarsTx.data.gasLimit = this->gasLimit;
    // Use explicit range check rather than `>=` so that Deposit (0x7e) is excluded;
    // EIP7702 is fee-market (maxFeePerGas/maxPriorityFeePerGas) so it is included
    if (static_cast<uint8_t>(this->type) >= static_cast<uint8_t>(TransactionType::EIP1559) &&
        static_cast<uint8_t>(this->type) <= static_cast<uint8_t>(TransactionType::EIP7702))
    {
        tarsTx.data.maxFeePerGas = "0x" + this->maxFeePerGas.str(0, std::ios_base::hex);
        tarsTx.data.maxPriorityFeePerGas =
            "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    else
    {
        tarsTx.data.gasPrice = "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tarsTx.web3TypedTxKind = static_cast<tars::Char>(static_cast<uint8_t>(this->type));
    if (!this->accessList.empty())
    {
        tarsTx.data.accessList.reserve(this->accessList.size());
        for (auto const& entry : this->accessList)
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

    // EIP-4844 blob fields: without these the executor sees a type-3 tx with 0
    // blobs, so no blob gas is charged (EEST test_blob_gas_subtraction fails).
    if (this->maxFeePerBlobGas > 0)
    {
        tarsTx.data.maxFeePerBlobGas = "0x" + this->maxFeePerBlobGas.str(0, std::ios_base::hex);
    }
    for (auto const& h : this->blobVersionedHashes)
    {
        tarsTx.data.blobVersionedHashes.emplace_back(h.begin(), h.end());
    }

    // EIP-7702 authorization list (set_code tx, Prague+). The executor
    // (bcosTransactionToEvmone) reads tx.authorizationList() from these entries.
    if (!this->authorizationList.empty())
    {
        tarsTx.data.authorizationList.reserve(this->authorizationList.size());
        for (auto const& entry : this->authorizationList)
        {
            bcostars::AuthorizationEntry tarsEntry;
            // EIP-7702 chain_id is a 256-bit value (see Web3Transaction.h — it is part of
            // the signed payload and must not be narrowed for the signing hash), but the
            // protocol::Authorization / tars field can only carry 64 bits. No chain id above
            // 64 bits can be this chain, so map EVERY such chain_id to UINT64_MAX — a value
            // that is neither a real chain id nor the 0 = "any chain" wildcard — and let
            // evmone skip the authorization on chain-id mismatch. (Mapping the low 64 bits
            // instead would be unsafe: 2**64 + 1 truncates to 1 = Ethereum mainnet.) The
            // entry must still be KEPT in the list: a set_code transaction with an empty
            // authorization list is rejected by the executor, and dropping it would change
            // the signing hash.
            tarsEntry.chainID =
                static_cast<int64_t>(entry.chainId > std::numeric_limits<uint64_t>::max() ?
                                         UINT64_MAX :
                                         static_cast<uint64_t>(entry.chainId));
            tarsEntry.address = entry.address.hex();  // 40-char hex, no 0x prefix
            tarsEntry.nonce = static_cast<int64_t>(entry.nonce);
            tarsEntry.v = static_cast<tars::Char>(entry.yParity);
            tarsEntry.r = "0x" + entry.r.str(0, std::ios_base::hex);
            tarsEntry.s = "0x" + entry.s.str(0, std::ios_base::hex);
            tarsTx.data.authorizationList.emplace_back(std::move(tarsEntry));
        }
    }

    // Only call encodeForSign() once, store in extraTransactionBytes for TxValidator::verify()
    auto encodedForSign = this->encodeForSign();
    tarsTx.extraTransactionBytes.reserve(encodedForSign.size());
    ::ranges::move(encodedForSign, std::back_inserter(tarsTx.extraTransactionBytes));

    // FISCO BCOS signature is r||s||v
    tarsTx.signature.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    ::ranges::move(this->signatureR, std::back_inserter(tarsTx.signature));
    ::ranges::move(this->signatureS, std::back_inserter(tarsTx.signature));
    tarsTx.signature.push_back(static_cast<tars::Char>(this->signatureV));

    tarsTx.data.nonce = toQuantity(this->nonce);
    tarsTx.data.chainID = std::to_string(this->chainId.value_or(0));

    // dataHash and sender left empty — TxValidator::verify() computes them
    return tarsTx;
}
}  // namespace bcos::rpc
