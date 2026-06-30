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
 * @file EthBlockHeader.h
 * @brief Ethereum-standard block header — inherits BlockHeader, shared_ptr aliasing, RLP encoding
 * @date 2026/6/24
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <gsl/span>
#include <memory>
#include <optional>

namespace bcos::protocol
{

/// Internal data block holding all 23 Ethereum header fields.
/// Embedded in EthBlock (value) or heap-allocated by EthBlockHeader (shared_ptr).
struct EthBlockHeaderData
{
    // Non-optional fields (1–15)
    // uncleHash, difficulty, and nonce have all been deprecated and are now using fixed values
    bcos::h256 parentHash;
    bcos::h256 uncleHash{std::string_view("1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"), bcos::h256::FromHex};
    bcos::Address coinbase; 
    bcos::crypto::HashType stateRoot;
    bcos::crypto::HashType txHash;
    bcos::crypto::HashType receiptHash;
    bcos::Bloom logsBloom{};
    bcos::u256 difficulty{0};
    int64_t number{0};
    bcos::u256 gasLimit{0};
    bcos::u256 gasUsed{0};
    int64_t timestamp{0};  // seconds
    bcos::bytes extraData;
    bcos::h256 mixDigest;
    bcos::h64 nonce{std::string_view("0x0000000000000000"), bcos::h64::FromHex};

    // Optional fields (16–23)
    std::optional<bcos::u256> baseFee;
    std::optional<bcos::h256> withdrawalsHash;
    std::optional<bcos::u256> blobGasUsed;
    std::optional<bcos::u256> excessBlobGas;
    std::optional<bcos::h256> parentBeaconRoot;
    std::optional<bcos::h256> requestsHash;
    std::optional<bcos::h256> blockAccessListHash;
    std::optional<uint64_t> slotNumber;

    bool operator==(const EthBlockHeaderData& other) const = default;
};



class EthBlockHeader
{
public:
    using Ptr = std::shared_ptr<EthBlockHeader>;

    EthBlockHeader() = default;
    explicit EthBlockHeader(EthBlockHeaderData _data) : data(std::move(_data)){}
    explicit EthBlockHeader(int64_t _number) {data.number = _number;}
    explicit EthBlockHeader(bcos::bytesConstRef _data) 
    {
        if (auto err = decode(_data); err != nullptr)
        {
            clear();
            return;
        }
        calculateHash();
    }

    ~EthBlockHeader() noexcept  = default;
    EthBlockHeader(const EthBlockHeader&) = default;
    EthBlockHeader(EthBlockHeader&&) noexcept = default;
    EthBlockHeader& operator=(const EthBlockHeader&) = default;
    EthBlockHeader& operator=(EthBlockHeader&&) noexcept = default;
    bool operator==(const EthBlockHeader& other) const
    {
        return data == other.data;
    }
    
    // ---- BlockHeader interfaces ----

    bcos::Error::UniquePtr decode(bcos::bytesConstRef _data);
    void encode(bcos::bytes& _encodeData) const;
    size_t encodedLength() const;

    bcos::crypto::HashType hash() const;
    void calculateHash();

    size_t size() const;
    void clear();    

    bcos::crypto::HashType txsRoot() const;
    bcos::crypto::HashType stateRoot() const;
    bcos::crypto::HashType receiptsRoot() const;
    int64_t number() const;
    bcos::u256 gasUsed() const;
    int64_t timestamp() const;
    bcos::bytesConstRef extraData() const;

    void setTxsRoot(bcos::crypto::HashType _txsRoot);
    void setReceiptsRoot(bcos::crypto::HashType _receiptsRoot);
    void setStateRoot(bcos::crypto::HashType _stateRoot);
    void setNumber(int64_t _blockNumber);
    void setGasUsed(bcos::u256 _gasUsed);
    void setTimestamp(int64_t _timestamp);
    void setExtraData(bcos::bytes const& _extraData);
    void setExtraData(bcos::bytes&& _extraData);

    bcos::h256 parentHash() const;
    void setParentHash(const bcos::h256& _hash);

    bcos::Address coinbase() const;
    void setCoinbase(const bcos::Address& _addr);

    bcos::Bloom logsBloom() const;
    void setLogsBloom(const bcos::Bloom& _bloom);

    bcos::u256 gasLimit() const;
    void setGasLimit(const bcos::u256& _limit);

    bcos::h256 mixDigest() const;
    void setMixDigest(const bcos::h256& _digest);


    // Optional fields
    std::optional<bcos::u256> baseFee() const;
    void setBaseFee(const std::optional<bcos::u256>& _fee);

    std::optional<bcos::h256> withdrawalsRoot() const;
    void setWithdrawalsRoot(const std::optional<bcos::h256>& _hash);

    std::optional<bcos::u256> blobGasUsed() const;
    void setBlobGasUsed(const std::optional<bcos::u256>& _val);

    std::optional<bcos::u256> excessBlobGas() const;
    void setExcessBlobGas(const std::optional<bcos::u256>& _val);

    std::optional<bcos::h256> parentBeaconBlockRoot() const;
    void setParentBeaconBlockRoot(const std::optional<bcos::h256>& _root);

    std::optional<bcos::h256> requestsHash() const;
    void setRequestsHash(const std::optional<bcos::h256>& _hash);

    std::optional<bcos::h256> blockAccessListHash() const;
    void setBlockAccessListHash(const std::optional<bcos::h256>& _hash);

    std::optional<uint64_t> slotNumber() const;
    void setSlotNumber(std::optional<uint64_t> _val);


private:
    void clearDataHash() { dataHash.clear(); }

    EthBlockHeaderData data;
    bcos::crypto::HashType dataHash;
};

}  // namespace bcos::protocol
