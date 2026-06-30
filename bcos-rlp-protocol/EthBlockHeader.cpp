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
 * @file EthBlockHeader.cpp
 * @brief EthBlockHeader — RLP encoding, BlockHeader interface overrides
 * @date 2026/6/24
 */
#include "EthBlockHeader.h"
#include "bcos-codec/rlp/Common.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <range/v3/view/single.hpp>
#include <range/v3/view/transform.hpp>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{

DERIVE_BCOS_EXCEPTION(EmptyEthBlockHeaderHash);

// encode — RLP list of 23 fields in Ethereum standard order
void EthBlockHeader::encode(bcos::bytes& _encodeData) const
{
    codec::rlp::encode(_encodeData, 
        data.parentHash,
        data.uncleHash,
        data.coinbase,
        data.stateRoot,
        data.txHash,
        data.receiptHash,
        bcos::bytesConstRef(data.logsBloom.data(), data.logsBloom.size()),
        data.difficulty,
        static_cast<uint64_t>(data.number),
        data.gasLimit,
        data.gasUsed,
        static_cast<uint64_t>(data.timestamp),
        data.extraData,
        data.mixDigest,
        data.nonce,
        data.baseFee,
        data.withdrawalsHash,
        data.blobGasUsed,
        data.excessBlobGas,
        data.parentBeaconRoot,
        data.requestsHash,
        data.blockAccessListHash,
        data.slotNumber
    );
}

bcos::Error::UniquePtr EthBlockHeader::decode(bcos::bytesConstRef _data)
{
    auto mutableData = _data.toBytes();
    bcos::bytesRef dataRef(mutableData.data(), mutableData.size());

    uint64_t _number;
    uint64_t _timestamp;

    auto error = codec::rlp::decode(dataRef, 
        data.parentHash,
        data.uncleHash,
        data.coinbase,
        data.stateRoot,
        data.txHash,
        data.receiptHash,
        data.logsBloom,
        data.difficulty,
        _number,
        data.gasLimit,
        data.gasUsed,
        _timestamp,
        data.extraData,
        data.mixDigest,
        data.nonce,
        data.baseFee,
        data.withdrawalsHash,
        data.blobGasUsed,
        data.excessBlobGas,
        data.parentBeaconRoot,
        data.requestsHash,
        data.blockAccessListHash,
        data.slotNumber
    );
    if (error)
    {
        return error;
    }
    data.number = static_cast<int64_t>(_number);
    data.timestamp = static_cast<int64_t>(_timestamp);

    return nullptr;
}

size_t EthBlockHeader::encodedLength() const
{
    return codec::rlp::length(data.parentHash, data.uncleHash, data.coinbase, data.stateRoot, data.txHash,
        data.receiptHash, bcos::bytesConstRef(data.logsBloom.data(), data.logsBloom.size()),
        data.difficulty, static_cast<uint64_t>(data.number), data.gasLimit, data.gasUsed, 
        static_cast<uint64_t>(data.timestamp), data.extraData, data.mixDigest, data.nonce,
        data.baseFee, data.withdrawalsHash, data.blobGasUsed, data.excessBlobGas,
        data.parentBeaconRoot, data.requestsHash, data.blockAccessListHash, data.slotNumber);
}

size_t EthBlockHeader::size() const
{
    size_t size = 588;
    size += data.extraData.size();
    if (data.baseFee.has_value()) {size += sizeof(bcos::u256);}
    if (data.withdrawalsHash.has_value()) {size += sizeof(bcos::h256);}
    if (data.blobGasUsed.has_value()) {size += sizeof(bcos::u256);}
    if (data.excessBlobGas.has_value()) {size += sizeof(bcos::u256);}
    if (data.parentBeaconRoot.has_value()) {size += sizeof(bcos::h256);}
    if (data.requestsHash.has_value()) {size += sizeof(bcos::h256);}
    if (data.blockAccessListHash.has_value()) {size += sizeof(bcos::h256);}
    if (data.slotNumber.has_value()) {size += sizeof(uint64_t);}
    return size;
}

bcos::crypto::HashType EthBlockHeader::hash() const
{
    if (dataHash == bcos::crypto::HashType{})
    {
        BOOST_THROW_EXCEPTION(EmptyEthBlockHeaderHash{});
    }
    return dataHash;
}

void EthBlockHeader::calculateHash()
{
    bcos::bytes encoded;
    encode(encoded);
    dataHash = bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

void EthBlockHeader::clear() { data = EthBlockHeaderData(); }

bcos::crypto::HashType EthBlockHeader::txsRoot() const { return data.txHash; }
bcos::crypto::HashType EthBlockHeader::stateRoot() const { return data.stateRoot; }
bcos::crypto::HashType EthBlockHeader::receiptsRoot() const { return data.receiptHash; }
int64_t EthBlockHeader::number() const { return data.number; }
int64_t EthBlockHeader::timestamp() const { return data.timestamp; }
bcos::u256 EthBlockHeader::gasUsed() const { return data.gasUsed; }

bcos::bytesConstRef EthBlockHeader::extraData() const
{
    return {data.extraData.data(), data.extraData.size()};
}

bcos::h256 EthBlockHeader::parentHash() const { return data.parentHash; }
bcos::Address EthBlockHeader::coinbase() const { return data.coinbase; }
bcos::Bloom EthBlockHeader::logsBloom() const { return data.logsBloom; }
bcos::u256 EthBlockHeader::gasLimit() const { return data.gasLimit; }
bcos::h256 EthBlockHeader::mixDigest() const { return data.mixDigest; }
std::optional<bcos::u256> EthBlockHeader::baseFee() const { return data.baseFee; }
std::optional<bcos::h256> EthBlockHeader::withdrawalsRoot() const { return data.withdrawalsHash; }
std::optional<bcos::u256> EthBlockHeader::blobGasUsed() const { return data.blobGasUsed; }
std::optional<bcos::u256> EthBlockHeader::excessBlobGas() const { return data.excessBlobGas; }
std::optional<bcos::h256> EthBlockHeader::parentBeaconBlockRoot() const { return data.parentBeaconRoot; }
std::optional<bcos::h256> EthBlockHeader::requestsHash() const { return data.requestsHash; }
std::optional<bcos::h256> EthBlockHeader::blockAccessListHash() const { return data.blockAccessListHash; }
std::optional<uint64_t> EthBlockHeader::slotNumber() const { return data.slotNumber; }

void EthBlockHeader::setTxsRoot(bcos::crypto::HashType _txsRoot) { data.txHash = _txsRoot; clearDataHash(); }
void EthBlockHeader::setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) { data.receiptHash = _receiptsRoot; clearDataHash(); }
void EthBlockHeader::setStateRoot(bcos::crypto::HashType _stateRoot) { data.stateRoot = _stateRoot; clearDataHash(); }
void EthBlockHeader::setNumber(int64_t _blockNumber) { data.number = _blockNumber; clearDataHash(); }
void EthBlockHeader::setGasUsed(bcos::u256 _gasUsed) { data.gasUsed = _gasUsed; clearDataHash(); }
void EthBlockHeader::setTimestamp(int64_t _timestamp) { data.timestamp = _timestamp; clearDataHash(); }

void EthBlockHeader::setExtraData(bcos::bytes const& _extraData) { data.extraData = _extraData; clearDataHash(); }
void EthBlockHeader::setExtraData(bcos::bytes&& _extraData) { data.extraData = std::move(_extraData); clearDataHash(); }

void EthBlockHeader::setParentHash(const bcos::h256& _parentHash) { data.parentHash = _parentHash; clearDataHash(); }
void EthBlockHeader::setCoinbase(const bcos::Address& _coinbase) { data.coinbase = _coinbase; clearDataHash(); }
void EthBlockHeader::setLogsBloom(const bcos::Bloom& _logsBloom) { data.logsBloom = _logsBloom; clearDataHash(); }
void EthBlockHeader::setGasLimit(const bcos::u256& _gasLimit) { data.gasLimit = _gasLimit; clearDataHash(); }
void EthBlockHeader::setMixDigest(const bcos::h256& _mixDigest) { data.mixDigest = _mixDigest; clearDataHash(); }
void EthBlockHeader::setBaseFee(const std::optional<bcos::u256>& val) { data.baseFee = val; clearDataHash(); }
void EthBlockHeader::setWithdrawalsRoot(const std::optional<bcos::h256>& val) { data.withdrawalsHash = val; clearDataHash(); }
void EthBlockHeader::setBlobGasUsed(const std::optional<bcos::u256>& val) { data.blobGasUsed = val; clearDataHash(); }
void EthBlockHeader::setExcessBlobGas(const std::optional<bcos::u256>& val) { data.excessBlobGas = val; clearDataHash(); }
void EthBlockHeader::setParentBeaconBlockRoot(const std::optional<bcos::h256>& val) { data.parentBeaconRoot = val; clearDataHash(); }
void EthBlockHeader::setRequestsHash(const std::optional<bcos::h256>& val) { data.requestsHash = val; clearDataHash(); }
void EthBlockHeader::setBlockAccessListHash(const std::optional<bcos::h256>& val) { data.blockAccessListHash = val; clearDataHash(); }
void EthBlockHeader::setSlotNumber(std::optional<uint64_t> val) { data.slotNumber = val; clearDataHash(); }

}  // namespace bcos::protocol
