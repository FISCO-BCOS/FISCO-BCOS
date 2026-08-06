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
#include "bcos-utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <range/v3/algorithm/find_if.hpp>
#include <range/v3/algorithm/move.hpp>
#include <utility>

namespace bcos
{
namespace rpc
{
using codec::rlp::decode;
using codec::rlp::encode;
using codec::rlp::header;
using codec::rlp::length;

static bytesConstRef getSignatureRef(bytesConstRef input)
{
    const auto* it = ::ranges::find_if(input, [](byte b) { return b != 0; });
    return {it, input.size() - (it - input.begin())};
}

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
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
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
            codec::rlp::encode(out, 0U);
            codec::rlp::encode(out, 0U);
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
        if (to.has_value())
        {
            codec::rlp::encode(out, to.value().ref());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        codec::rlp::encode(out, value);
        codec::rlp::encode(out, data);
        codec::rlp::encode(out, accessList);
        if (type == TransactionType::EIP4844)
        {
            codec::rlp::encode(out, maxFeePerBlobGas);
            codec::rlp::encode(out, blobVersionedHashes);
        }
        if (type == TransactionType::EIP7702)
        {
            codec::rlp::encode(out, authorizationList);
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
    tarsTx.data.to = (this->to.has_value()) ? this->to.value().hexPrefixed() : "";
    tarsTx.data.input.reserve(this->data.size());
    ::ranges::move(this->data, std::back_inserter(tarsTx.data.input));

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
            tarsEntry.chainID = static_cast<int64_t>(entry.chainId);
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
std::ostream& operator<<(std::ostream& _out, const TransactionType& _in)
{
    _out << magic_enum::enum_name(_in);
    return _out;
}
uint64_t Web3Transaction::getSignatureV() const
{
    // EIP-155: Simple replay attack protection
    if (chainId.has_value())
    {
        return chainId.value() * 2 + 35 + signatureV;
    }
    return signatureV + 27;
}
std::string Web3Transaction::sender() const
{
    bcos::bytes sign{};
    sign.reserve(crypto::SECP256K1_SIGNATURE_LEN);
    sign.insert(sign.end(), signatureR.begin(), signatureR.end());
    sign.insert(sign.end(), signatureS.begin(), signatureS.end());
    sign.push_back(signatureV);
    bcos::crypto::Keccak256 hashImpl;
    auto encodeForSign = this->encodeForSign();
    auto hash = bcos::crypto::keccak256Hash(ref(encodeForSign));
    const bcos::crypto::Secp256k1Crypto signatureImpl;
    auto [_, addr] = signatureImpl.recoverAddress(hashImpl, hash, ref(sign));
    return toHexStringWithPrefix(addr);
}
std::string Web3Transaction::toString() const noexcept
{
    std::stringstream stringstream{};
    stringstream << " chainId: " << this->chainId.value_or(0) << " hash:" << this->txHash().hex()
                 << " type: " << static_cast<uint16_t>(this->type)
                 << " to: " << this->to.value_or(Address()).hex() << " data: " << toHex(this->data)
                 << " value: " << this->value << " nonce: " << this->nonce
                 << " gasLimit: " << this->gasLimit
                 << " maxPriorityFeePerGas: " << this->maxPriorityFeePerGas
                 << " maxFeePerGas: " << this->maxFeePerGas
                 << " maxFeePerBlobGas: " << this->maxFeePerBlobGas
                 << " blobVersionedHashes: " << this->blobVersionedHashes
                 << " sender: " << this->sender() << " signatureR: " << toHex(this->signatureR)
                 << " signatureS: " << toHex(this->signatureS)
                 << " signatureV: " << this->signatureV;
    return stringstream.str();
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

Header header(const AuthorizationListEntry& entry) noexcept
{
    auto len = codec::rlp::length(entry.chainId) + Address::SIZE + 1 +
               codec::rlp::length(entry.nonce) +
               codec::rlp::length(static_cast<uint64_t>(entry.yParity)) +
               codec::rlp::length(entry.r) + codec::rlp::length(entry.s);
    return {.isList = true, .payloadLength = len};
}

size_t length(AuthorizationListEntry const& entry) noexcept
{
    auto head = header(entry);
    return lengthOfLength(head.payloadLength) + head.payloadLength;
}

void encode(bcos::bytes& out, const AuthorizationListEntry& entry) noexcept
{
    encodeHeader(out, header(entry));
    encode(out, entry.chainId);
    encode(out, entry.address.ref());
    encode(out, entry.nonce);
    encode(out, static_cast<uint64_t>(entry.yParity));
    encode(out, entry.r);
    encode(out, entry.s);
}
Header headerTxBase(const Web3Transaction& tx) noexcept
{
    Header h{.isList = true};

    if (tx.type != TransactionType::Legacy)
    {
        h.payloadLength += length(tx.chainId.value_or(0));
    }

    h.payloadLength += length(tx.nonce);
    if (tx.type == TransactionType::EIP1559 || tx.type == TransactionType::EIP4844 ||
        tx.type == TransactionType::EIP7702)
    {
        h.payloadLength += length(tx.maxPriorityFeePerGas);
    }
    h.payloadLength += length(tx.maxFeePerGas);
    h.payloadLength += length(tx.gasLimit);
    h.payloadLength += (tx.to.has_value()) ? (Address::SIZE + 1) : 1;
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
        if (tx.type == TransactionType::EIP7702)
        {
            h.payloadLength += codec::rlp::length(tx.authorizationList);
        }
    }

    return h;
}
Header header(Web3Transaction const& tx) noexcept
{
    auto header = headerTxBase(tx);
    header.payloadLength += (tx.type == TransactionType::Legacy) ? length(tx.getSignatureV()) : 1;
    header.payloadLength += length(getSignatureRef(ref(tx.signatureR)));
    header.payloadLength += length(getSignatureRef(ref(tx.signatureS)));
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
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.getSignatureV());
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
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
        if (tx.to.has_value())
        {
            encode(out, tx.to.value());
        }
        else
        {
            out.push_back(codec::rlp::BYTES_HEAD_BASE);
        }
        encode(out, tx.value);
        encode(out, tx.data);
        encode(out, tx.accessList);
        if (tx.type == TransactionType::EIP4844)
        {
            encode(out, tx.maxFeePerBlobGas);
            encode(out, tx.blobVersionedHashes);
        }
        if (tx.type == TransactionType::EIP7702)
        {
            encode(out, tx.authorizationList);
        }
        encode(out, tx.signatureV);
        encode(out, getSignatureRef(ref(tx.signatureR)));
        encode(out, getSignatureRef(ref(tx.signatureS)));
    }
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, AccessListEntry& out) noexcept
{
    return decode(in, out.account, out.storageKeys);
}

bcos::Error::UniquePtr decode(bcos::bytesRef& in, AuthorizationListEntry& out) noexcept
{
    // Each authorization entry is itself an RLP list:
    // [chain_id, address, nonce, y_parity, r, s]
    auto&& [error, header] = decodeHeader(in);
    if (error != nullptr)
    {
        return std::move(error);
    }
    if (!header.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(DecodingError::UnexpectedString, "Unexpected string");
    }
    const uint64_t leftover{in.size() - header.payloadLength};
    uint64_t chainId = 0;
    if (auto e = decodeItems(in, chainId, out.address); e != nullptr)
    {
        return e;
    }
    uint64_t nonce = 0;
    if (auto e = decode(in, nonce); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.yParity); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.r); e != nullptr)
    {
        return e;
    }
    if (auto e = decode(in, out.s); e != nullptr)
    {
        return e;
    }
    if (in.size() != leftover)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "Unexpected list elements");
    }
    out.chainId = chainId;
    out.nonce = nonce;
    return nullptr;
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, Web3Transaction& out) noexcept
{
    return decodeTransaction(in, out, true);
}

bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction& out) noexcept
{
    return decodeTransaction(in, out, false);
}

bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
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

        const auto txType = magic_enum::enum_cast<TransactionType>(firstByte);
        if (!txType.has_value())
        {
            return BCOS_ERROR_UNIQUE_PTR(
                DecodingError::UnsupportedTransactionType, "Unsupported transaction type");
        }
        out.type = txType.value();
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

        if (auto error = decode(in, out.gasLimit); error != nullptr)
        {
            return error;
        }

        if (in[0] == BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (auto error = decode(in, addr); error != nullptr)
            {
                return error;
            }
            out.to.emplace(addr);
        }

        if (auto error = decodeItems(in, out.value, out.data, out.accessList); error != nullptr)
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
        if (out.type == TransactionType::EIP7702)
        {
            if (auto error = decode(in, out.authorizationList); error != nullptr)
            {
                return error;
            }
        }
        if (withSignature)
        {
            decodeError = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
        }
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
        if (decodeError = decodeItems(in, out.nonce, out.maxPriorityFeePerGas);
            decodeError != nullptr)
        {
            return decodeError;
        }
        out.maxFeePerGas = out.maxPriorityFeePerGas;

        if (decodeError = decode(in, out.gasLimit); decodeError != nullptr)
        {
            return decodeError;
        }

        if (in[0] == BYTES_HEAD_BASE)
        {
            out.to = std::nullopt;
            in = in.getCroppedData(1);
        }
        else
        {
            Address addr{};
            if (decodeError = decode(in, addr); decodeError != nullptr)
            {
                return decodeError;
            }
            out.to.emplace(addr);
        }

        decodeError = decodeItems(in, out.value, out.data);
        if (withSignature)
        {
            if (decodeError = decodeItems(in, out.signatureV, out.signatureR, out.signatureS);
                decodeError != nullptr)
            {
                return decodeError;
            }
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
        else
        {
            uint64_t chainId = 0;
            decodeError = decode(in, chainId);
            out.chainId.emplace(chainId);
        }
    }
    if (withSignature)
    {
        // rehandle signature and chainId
        if (out.signatureR.size() < crypto::SECP256K1_SIGNATURE_R_LEN)
        {
            out.signatureR.insert(out.signatureR.begin(),
                crypto::SECP256K1_SIGNATURE_R_LEN - out.signatureR.size(), 0);
        }
        if (out.signatureS.size() < crypto::SECP256K1_SIGNATURE_S_LEN)
        {
            out.signatureS.insert(out.signatureS.begin(),
                crypto::SECP256K1_SIGNATURE_S_LEN - out.signatureS.size(), bcos::byte(0));
        }
    }
    return decodeError;
}
}  // namespace codec::rlp
}  // namespace bcos
