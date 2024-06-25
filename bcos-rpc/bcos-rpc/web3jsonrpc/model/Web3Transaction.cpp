/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file Web3Transaction.cpp
 * @author: kyonGuo
 * @date 2024/4/8
 */

#include "Web3Transaction.h"

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/jsonrpc/Common.h>

namespace bcos
{
namespace rpc
{
using codec::rlp::decode;
using codec::rlp::encode;
using codec::rlp::header;
using codec::rlp::length;
bcos::bytes Web3Transaction::encodeForSign() const
{
    bcos::bytes out;
    if (type == TransactionType::Legacy)
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data])
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, nonce);
        // for legacy tx, it means gas price
        codec::rlp::encode(out, maxFeePerGas);
        codec::rlp::encode(out, gasLimit);
        if (to != Address())
        {
            codec::rlp::encode(out, to.ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        if (chainId)
        {
            // EIP-155
            codec::rlp::encode(out, chainId.value());
            codec::rlp::encode(out, 0u);
            codec::rlp::encode(out, 0u);
        }
    }
    else
    {
        // EIP2930: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList])

        // EIP1559: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, destination, amount, data, access_list])

        // EIP4844: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes])
        out.push_back(static_cast<byte>(type));
        codec::rlp::encodeHeader(out, codec::rlp::headerForSign(*this));
        codec::rlp::encode(out, chainId.value_or(0));
        codec::rlp::encode(out, nonce);
        if (type != TransactionType::EIP2930)
        {
            codec::rlp::encode(out, maxPriorityFeePerGas);
        }
        // for EIP2930 it means gasPrice; for EIP1559 and EIP4844, it means max priority fee per gas
        codec::rlp::encode(out, maxFeePerGas);
        codec::rlp::encode(out, gasLimit);
        codec::rlp::encode(out, to.ref());
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        codec::rlp::encode(out, accessList);
        if (type == TransactionType::EIP4844)
        {
            codec::rlp::encode(out, maxFeePerBlobGas);
            codec::rlp::encode(out, blobVersionedHashes);
        }
    }
    return out;
}

bcos::crypto::HashType Web3Transaction::txHash() const
{
    bcos::bytes encoded{};
    codec::rlp::encode(encoded, *this);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::crypto::HashType Web3Transaction::hashForSign() const
{
    auto encodeForSign = this->encodeForSign();
    return bcos::crypto::keccak256Hash(bcos::ref(encodeForSign));
}

bcostars::Transaction Web3Transaction::takeToTarsTransaction()
{
    bcostars::Transaction tarsTx{};
    tarsTx.data.to = (this->to == Address()) ? "" : this->to.hexPrefixed();
    tarsTx.data.input.reserve(this->data.size());
    RANGES::move(this->data, std::back_inserter(tarsTx.data.input));

    tarsTx.data.value = "0x" + this->value.str(0, std::ios_base::hex);
    tarsTx.data.gasLimit = this->gasLimit;
    if (static_cast<uint8_t>(this->type) >= static_cast<uint8_t>(TransactionType::EIP1559))
    {
        tarsTx.data.maxFeePerGas = "0x" + this->maxFeePerGas.str(0, std::ios_base::hex);
        tarsTx.data.maxPriorityFeePerGas =
            "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    else
    {
        tarsTx.data.gasPrice = "0x" + this->maxPriorityFeePerGas.str(0, std::ios_base::hex);
    }
    tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transacion);
    auto hashForSign = this->hashForSign();
    auto encodedForSign = this->encodeForSign();
    // FISCO BCOS signature is r||s||v
    tarsTx.signature.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    RANGES::move(this->signatureR, std::back_inserter(tarsTx.signature));
    RANGES::move(this->signatureS, std::back_inserter(tarsTx.signature));
    tarsTx.signature.push_back(static_cast<tars::Char>(this->signatureV));

    tarsTx.extraTransactionBytes.reserve(encodedForSign.size());
    RANGES::move(encodedForSign, std::back_inserter(tarsTx.extraTransactionBytes));

    const bcos::crypto::Secp256k1Crypto signatureImpl;
    bcos::crypto::Keccak256 hashImpl;
    auto signRef = bcos::bytesConstRef(
        reinterpret_cast<const bcos::byte*>(tarsTx.signature.data()), tarsTx.signature.size());
    auto [_, sender] = signatureImpl.recoverAddress(hashImpl, hashForSign, signRef);
    tarsTx.data.nonce = toHexStringWithPrefix(sender) + toQuantity(this->nonce);
    tarsTx.data.chainID = std::to_string(this->chainId.value_or(0));
    tarsTx.dataHash.reserve(crypto::HashType::SIZE);
    RANGES::move(hashForSign, std::back_inserter(tarsTx.dataHash));
    tarsTx.sender.reserve(sender.size());
    RANGES::move(sender, std::back_inserter(tarsTx.sender));
    return tarsTx;
}
}  // namespace rpc

namespace codec::rlp
{
using namespace bcos::rpc;
Header header(const AccessListEntry& entry) noexcept
{
    auto len = length(entry.storageKeys);
    return {.isList = true, .payloadLength = Address::SIZE + 1 + len};
}

size_t length(AccessListEntry const& entry) noexcept
{
    auto head = header(entry);
    return lengthOfLength(head.payloadLength) + head.payloadLength;
}
Header headerTxBase(const Web3Transaction& tx) noexcept
{
    Header h{.isList = true};

    if (tx.type != TransactionType::Legacy)
    {
        h.payloadLength += length(tx.chainId.value_or(0));
    }

    h.payloadLength += length(tx.nonce);
    if (tx.type == TransactionType::EIP1559 || tx.type == TransactionType::EIP4844)
    {
        h.payloadLength += length(tx.maxPriorityFeePerGas);
    }
    h.payloadLength += length(tx.maxFeePerGas);
    h.payloadLength += length(tx.gasLimit);
    h.payloadLength += (tx.to != Address()) ? (Address::SIZE + 1) : 1;
    h.payloadLength += length(tx.value);
    h.payloadLength += length(tx.data);

    if (tx.type != TransactionType::Legacy)
    {
        h.payloadLength += codec::rlp::length(tx.accessList);
        if (tx.type == TransactionType::EIP4844)
        {
            h.payloadLength += length(tx.maxFeePerBlobGas);
            h.payloadLength += length(tx.blobVersionedHashes);
        }
    }

    return h;
}
Header header(Web3Transaction const& tx) noexcept
{
    auto header = headerTxBase(tx);
    header.payloadLength += (tx.type == TransactionType::Legacy) ? length(tx.getSignatureV()) : 1;
    header.payloadLength += length(tx.signatureR);
    header.payloadLength += length(tx.signatureS);
    return header;
}
Header headerForSign(Web3Transaction const& tx) noexcept
{
    auto header = headerTxBase(tx);
    if (tx.type == TransactionType::Legacy && tx.chainId)
    {
        header.payloadLength += length(tx.chainId.value()) + 2;
    }
    return header;
}
size_t length(Web3Transaction const& tx) noexcept
{
    auto head = header(tx);
    auto len = lengthOfLength(head.payloadLength) + head.payloadLength;
    len = (tx.type == TransactionType::Legacy) ? len : lengthOfLength(len + 1) + len + 1;
    return len;
}
void encode(bcos::bytes& out, const AccessListEntry& entry) noexcept
{
    encodeHeader(out, header(entry));
    encode(out, entry.account.ref());
    encode(out, entry.storageKeys);
}
void encode(bcos::bytes& out, const Web3Transaction& tx) noexcept
{
    if (tx.type == TransactionType::Legacy)
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
        encodeHeader(out, header(tx));
        encode(out, tx.nonce);
        // for legacy tx, it means gas price
        encode(out, tx.maxFeePerGas);
        encode(out, tx.gasLimit);
        encode(out, tx.to.ref());
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.getSignatureV());
        encode(out, tx.signatureR);
        encode(out, tx.signatureS);
    }
    else
    {
        // EIP2930: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
        // signatureYParity, signatureR, signatureS])

        // EIP1559: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, destination, amount, data, access_list, signature_y_parity, signature_r,
        // signature_s])

        // EIP4844: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes,
        // signature_y_parity, signature_r, signature_s])
        out.push_back(static_cast<bcos::byte>(tx.type));
        encodeHeader(out, header(tx));
        encode(out, tx.chainId.value_or(0));
        encode(out, tx.nonce);
        if (tx.type != TransactionType::EIP2930)
        {
            encode(out, tx.maxPriorityFeePerGas);
        }
        // for EIP2930 it means gasPrice; for EIP1559 and EIP4844, it means max priority fee per gas
        encode(out, tx.maxFeePerGas);
        encode(out, tx.gasLimit);
        encode(out, tx.to.ref());
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.accessList);
        if (tx.type == TransactionType::EIP4844)
        {
            encode(out, tx.maxFeePerBlobGas);
            encode(out, tx.blobVersionedHashes);
        }
        encode(out, tx.signatureV);
        encode(out, tx.signatureR);
        encode(out, tx.signatureS);
    }
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, AccessListEntry& out) noexcept
{
    return decode(in, out.account, out.storageKeys);
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, Web3Transaction& out) noexcept
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input too short");
    }
    Error::UniquePtr decodeError = nullptr;
    if (auto const& firstByte = in[0]; 0 < firstByte && firstByte < BYTES_HEAD_BASE)
    {
        // EIP-2718: Transaction Type
        // EIP2930: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
        // signatureYParity, signatureR, signatureS])

        // EIP1559: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, destination, amount, data, access_list, signature_y_parity, signature_r,
        // signature_s])

        // EIP4844: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes,
        // signature_y_parity, signature_r, signature_s])

        out.type = static_cast<TransactionType>(firstByte);
        in = in.getCroppedData(1);
        auto&& [e, header] = decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!header.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(UnexpectedString, "Unexpected String");
        }
        uint64_t chainId = 0;
        if (auto error = decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        if (out.type == TransactionType::EIP2930)
        {
            out.maxFeePerGas = out.maxPriorityFeePerGas;
        }
        else if (auto error = decode(in, out.maxFeePerGas); error != nullptr)
        {
            return error;
        }

        if (auto error = decodeItems(in, out.gasLimit, out.to, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        if (out.type == TransactionType::EIP4844)
        {
            if (auto error = decodeItems(in, out.maxFeePerBlobGas, out.blobVersionedHashes);
                error != nullptr)
            {
                return error;
            }
        }

        decodeError = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
    }
    else
    {
        // rlp([nonce, gasPrice, gasLimit, to, value, data, v, r, s])
        auto&& [error, header] = decodeHeader(in);
        if (error != nullptr)
        {
            return std::move(error);
        }
        if (!header.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(UnexpectedList, "Unexpected list");
        }
        out.type = TransactionType::Legacy;
        decodeError = decodeItems(in, out.nonce, out.maxPriorityFeePerGas, out.gasLimit, out.to,
            out.value, out.data, out.signatureV, out.signatureR, out.signatureS);
        out.maxFeePerGas = out.maxPriorityFeePerGas;
        // TODO: EIP-155 chainId decode from encoded bytes for sign
        auto v = out.signatureV;
        if (v == 27 || v == 28)
        {
            // pre EIP-155
            out.chainId = std::nullopt;
            out.signatureV = v - 27;
        }
        else if (v == 0 || v == 1)
        {
            out.chainId = std::nullopt;
            return decodeError;
        }
        else if (v < 35)
        {
            return BCOS_ERROR_UNIQUE_PTR(InvalidVInSignature, "Invalid V in signature");
        }
        else
        {
            // https://eips.ethereum.org/EIPS/eip-155
            // Find chain_id and y_parity ∈ {0, 1} such that
            // v = chain_id * 2 + 35 + y_parity
            out.signatureV = (v - 35) % 2;
            out.chainId = ((v - 35) >> 1);
        }
    }
    // rehandle signature and chainId
    if (out.signatureR.size() < crypto::SECP256K1_SIGNATURE_R_LEN)
    {
        out.signatureR.insert(
            out.signatureR.begin(), crypto::SECP256K1_SIGNATURE_R_LEN - out.signatureR.size(), 0);
    }
    if (out.signatureS.size() < crypto::SECP256K1_SIGNATURE_S_LEN)
    {
        out.signatureS.insert(out.signatureS.begin(),
            crypto::SECP256K1_SIGNATURE_S_LEN - out.signatureS.size(), bcos::byte(0));
    }
    return decodeError;
}

bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction& out) noexcept
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(InputTooShort, "Input too short");
    }
    Error::UniquePtr decodeError = nullptr;
    if (auto const& firstByte = in[0]; 0 < firstByte && firstByte < BYTES_HEAD_BASE)
    {
        // EIP-2718: Transaction Type
        // EIP2930: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value, data, accessList,
        // signatureYParity, signatureR, signatureS])

        // EIP1559: 0x02 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, destination, amount, data, access_list, signature_y_parity, signature_r,
        // signature_s])

        // EIP4844: 0x03 || rlp([chain_id, nonce, max_priority_fee_per_gas, max_fee_per_gas,
        // gas_limit, to, value, data, access_list, max_fee_per_blob_gas, blob_versioned_hashes,
        // signature_y_parity, signature_r, signature_s])

        out.type = static_cast<TransactionType>(firstByte);
        in = in.getCroppedData(1);
        auto&& [e, header] = decodeHeader(in);
        if (e != nullptr)
        {
            return std::move(e);
        }
        if (!header.isList)
        {
            return BCOS_ERROR_UNIQUE_PTR(UnexpectedString, "Unexpected String");
        }
        uint64_t chainId = 0;
        if (auto error = decodeItems(in, chainId, out.nonce, out.maxPriorityFeePerGas);
            error != nullptr)
        {
            return error;
        }
        out.chainId.emplace(chainId);
        if (out.type == TransactionType::EIP2930)
        {
            out.maxFeePerGas = out.maxPriorityFeePerGas;
        }
        else if (auto error = decode(in, out.maxFeePerGas); error != nullptr)
        {
            return error;
        }

        if (auto error = decodeItems(in, out.gasLimit, out.to, out.value, out.data, out.accessList);
            error != nullptr)
        {
            return error;
        }

        if (out.type == TransactionType::EIP4844)
        {
            if (auto error = decodeItems(in, out.maxFeePerBlobGas, out.blobVersionedHashes);
                error != nullptr)
            {
                return error;
            }
        }
        return nullptr;
    }
    // rlp([nonce, gasPrice, gasLimit, to, value, data, chainId])
    auto&& [error, header] = decodeHeader(in);
    if (error != nullptr)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(UnexpectedList, "Unexpected list");
    }
    out.type = TransactionType::Legacy;
    uint64_t chainId = 0;
    decodeError = decodeItems(in, out.nonce, out.maxPriorityFeePerGas, out.gasLimit, out.to,
        out.value, out.data, chainId);
    out.chainId.emplace(chainId);
    return decodeError;
}

}  // namespace codec::rlp
}  // namespace bcos
