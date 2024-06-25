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
 * @file Web3Transaction.h
 * @author: kyonGuo
 * @date 2024/4/8
 */

#pragma once
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-rpc/web3jsonrpc/utils/util.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-utilities/FixedBytes.h>
#include <magic_enum.hpp>
#include <ostream>
namespace bcos
{
namespace rpc
{
// EIP-2718 transaction type
// https://github.com/ethereum/eth1.0-specs/tree/master/lists/signature-types
enum class TransactionType : uint8_t
{
    Legacy = 0,
    EIP2930 = 1,  // https://eips.ethereum.org/EIPS/eip-2930
    EIP1559 = 2,  // https://eips.ethereum.org/EIPS/eip-1559
    EIP4844 = 3,  // https://eips.ethereum.org/EIPS/eip-4844
};

[[maybe_unused]] constexpr auto operator<=>(TransactionType const& ltype, auto rtype)
    requires std::same_as<decltype(rtype), TransactionType> ||
             std::unsigned_integral<decltype(rtype)>
{
    return static_cast<uint8_t>(ltype) <=> static_cast<uint8_t>(rtype);
}

[[maybe_unused]] static std::ostream& operator<<(std::ostream& _out, const TransactionType& _in)
{
    _out << magic_enum::enum_name(_in);
    return _out;
}

// EIP-2930: Access lists
struct AccessListEntry
{
    Address account{};
    std::vector<crypto::HashType> storageKeys{};
    friend bool operator==(const AccessListEntry& lhs, const AccessListEntry& rhs) noexcept
    {
        return lhs.account == rhs.account && lhs.storageKeys == rhs.storageKeys;
    }
};

class Web3Transaction
{
public:
    Web3Transaction() = default;
    ~Web3Transaction() = default;
    Web3Transaction(const Web3Transaction&) = delete;
    Web3Transaction(Web3Transaction&&) = default;
    Web3Transaction& operator=(const Web3Transaction&) = delete;
    Web3Transaction& operator=(Web3Transaction&&) = default;

    // encode for sign, rlp(tx_payload)
    bcos::bytes encodeForSign() const;
    // tx hash = keccak256(rlp(tx_payload,v,r,s))
    bcos::crypto::HashType txHash() const;
    // hash for sign = keccak256(rlp(tx_payload))
    bcos::crypto::HashType hashForSign() const;
    bcostars::Transaction takeToTarsTransaction();
    uint64_t getSignatureV() const
    {
        // EIP-155: Simple replay attack protection
        if (chainId.has_value())
        {
            return chainId.value() * 2 + 35 + signatureV;
        }
        return signatureV + 27;
    }

    std::string sender() const
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

    std::string toString() const noexcept
    {
        std::stringstream stringstream{};
        stringstream << " chainId: " << this->chainId.value_or(0)
                     << " type: " << static_cast<uint16_t>(this->type) << " to: " << this->to
                     << " data: " << toHex(this->data) << " value: " << this->value
                     << " nonce: " << this->nonce << " gasLimit: " << this->gasLimit
                     << " maxPriorityFeePerGas: " << this->maxPriorityFeePerGas
                     << " maxFeePerGas: " << this->maxFeePerGas
                     << " maxFeePerBlobGas: " << this->maxFeePerBlobGas
                     << " blobVersionedHashes: " << this->blobVersionedHashes
                     << " signatureR: " << toHex(this->signatureR)
                     << " signatureS: " << toHex(this->signatureS)
                     << " signatureV: " << this->signatureV;
        return stringstream.str();
    }

    std::optional<uint64_t> chainId{std::nullopt};  // nullopt means a pre-EIP-155 transaction
    TransactionType type{TransactionType::Legacy};
    Address to{};
    bcos::bytes data{};
    u256 value{0};
    uint64_t nonce{0};
    uint64_t gasLimit{0};
    // EIP-2930: Optional access lists
    std::vector<AccessListEntry> accessList{};
    // EIP-1559: Fee market change for ETH 1.0 chain
    u256 maxPriorityFeePerGas{0};  // for legacy tx, it stands for gasPrice
    u256 maxFeePerGas{0};
    // EIP-4844: Shard Blob Transactions
    u256 maxFeePerBlobGas{0};
    h256s blobVersionedHashes{};
    // TODO)) blob
    bcos::bytes signatureR{};
    bcos::bytes signatureS{};
    uint64_t signatureV{0};
};
}  // namespace rpc
namespace codec::rlp
{
Header header(const rpc::AccessListEntry& entry) noexcept;
void encode(bcos::bytes& out, const rpc::AccessListEntry&) noexcept;
size_t length(const rpc::AccessListEntry&) noexcept;

size_t length(const rpc::Web3Transaction&) noexcept;
Header headerForSign(const rpc::Web3Transaction& tx) noexcept;
Header headerTxBase(const rpc::Web3Transaction& tx) noexcept;
Header header(const rpc::Web3Transaction& tx) noexcept;
void encode(bcos::bytes& out, const rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::AccessListEntry&) noexcept;
bcos::Error::UniquePtr decode(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
bcos::Error::UniquePtr decodeFromPayload(bcos::bytesRef& in, rpc::Web3Transaction&) noexcept;
}  // namespace codec::rlp
}  // namespace bcos
