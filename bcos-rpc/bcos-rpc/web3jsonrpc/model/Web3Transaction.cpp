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
#include "TxHandler.h"
#include "bcos-utilities/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <range/v3/algorithm/move.hpp>
#include <utility>

namespace bcos
{
namespace rpc
{
// These three using-declarations are NOT dead: they are the ADL bridge that lets the generic
// codec templates (RLPEncode.h encodeItems/Common.h lengthOfItems/RLPDecode.h decodeItems) find
// the AccessListEntry/Web3Transaction overloads defined at the bottom of this file. Those overloads
// live in namespace codec::rlp, which is not an associated namespace of bcos::rpc::AccessListEntry;
// without the using-declaration the unity build fails with "neither visible in the template
// definition nor found by argument-dependent lookup". Keep the three in sync with the overloads.
using codec::rlp::decode;
using codec::rlp::encode;
using codec::rlp::length;

bcos::bytes Web3Transaction::encodeForSign() const
{
    // 委托 handler:签名预映像(RLP 无 type byte、无签名)
    return handlerFor(type).encodeForSign(*this);
}

bcos::bytes Web3Transaction::encode() const
{
    // 完整 RLP(含 type byte,typed 交易)
    return handlerFor(type).encode(*this);
}

bcos::Error::UniquePtr Web3Transaction::decode(bcos::bytesRef& in, bool withSig)
{
    if (in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(codec::rlp::DecodingError::InputTooShort, "Input too short");
    }
    const auto firstByte = in[0];
    if (firstByte >= codec::rlp::BYTES_HEAD_BASE)
    {
        // Legacy: 无 type byte
        type = TransactionType::Legacy;
        return handlerFor(type).decode(in, *this, withSig);
    }
    auto txType = magic_enum::enum_cast<TransactionType>(firstByte);
    if (!txType.has_value())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            codec::rlp::DecodingError::UnsupportedTransactionType, "Unsupported transaction type");
    }
    type = txType.value();
    // ⚠️ 不预先裁剪 type byte:typed handler 自行消费 envelope(TxHandler.h decode 契约),
    // 这里再裁剪会二次跳过列表头,导致所有 typed 交易解码失败。
    return handlerFor(type).decode(in, *this, withSig);
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
    if (type == TransactionType::Deposit)
    {
        bcostars::Transaction tarsTx{};
        tarsTx.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
        tarsTx.web3TypedTxKind =
            static_cast<tars::Char>(static_cast<uint8_t>(TransactionType::Deposit));
        tarsTx.sourceHash = sourceHash.hex();
        tarsTx.sender.assign(from.begin(), from.end());
        // 带 0x 前缀,与读取端 u256(...) 匹配(TransactionImpl.cpp mint() 解析)
        tarsTx.mint = "0x" + mint.str(0, std::ios_base::hex);
        tarsTx.isSystemTransaction = isSystemTx ? 1 : 0;
        // 完整 0x7E envelope(encode());calculateHash 对 web3TypedTxKind==0x7e 直接
        // keccak256(extraTransactionBytes) 得到 extraTransactionHash,无需在此填充
        auto encoded = encode();
        tarsTx.extraTransactionBytes.reserve(encoded.size());
        ::ranges::move(encoded, std::back_inserter(tarsTx.extraTransactionBytes));
        // 通用字段(避免走 tars 通用读路径的消费者读到空值)
        tarsTx.data.to = to.has_value() ? to->hexPrefixed() : "";
        tarsTx.data.input.assign(data.begin(), data.end());
        tarsTx.data.value = "0x" + value.str(0, std::ios_base::hex);
        tarsTx.data.gasLimit = gasLimit;
        tarsTx.data.nonce = "0x0";  // deposit nonce 恒 0
        tarsTx.data.chainID = "0";
        return tarsTx;
    }
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
    // deposit(0x7e):无签名,sender 直接取 from 字段
    if (type == TransactionType::Deposit)
        return toHexStringWithPrefix(from);
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
size_t length(Web3Transaction const& tx) noexcept
{
    auto head = handlerFor(tx.type).header(tx);
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
    // 委托 handler 保持兼容(EthEndpoint.cpp:561、测试均调此自由函数)
    auto encoded = handlerFor(tx.type).encode(tx);
    out.insert(out.end(), encoded.begin(), encoded.end());
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, AccessListEntry& out) noexcept
{
    return decode(in, out.account, out.storageKeys);
}
bcos::Error::UniquePtr decode(bcos::bytesRef& in, Web3Transaction& out) noexcept
{
    return out.decode(in, true);
}

bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction& out) noexcept
{
    return out.decode(in, false);
}

bcos::Error::UniquePtr decodeTransaction(
    bcos::bytesRef& in, rpc::Web3Transaction& out, bool withSignature) noexcept
{
    // 保留为入口(decodeOpEnvelope/decodeOpEnvelopeWithSig 的调用目标,EthEndpoint.cpp:73),
    // 内部改调成员委托,确保走新的 handler 分派路径。
    return out.decode(in, withSignature);
}
}  // namespace codec::rlp
}  // namespace bcos
