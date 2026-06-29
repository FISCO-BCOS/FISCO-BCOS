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
 * @file EthBlockHeaderImpl.cpp
 * @brief EthBlockHeaderImpl — RLP encoding, BlockHeader interface overrides
 * @date 2026/6/24
 */
#include "EthBlockHeaderImpl.h"
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
void EthBlockHeaderImpl::encode(bcos::bytes& _encodeData) const
{
    auto& data = m_inner->data;

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

bcos::Error::UniquePtr EthBlockHeaderImpl::decode(bcos::bytesConstRef _data)
{
    auto mutableData = _data.toBytes();
    bcos::bytesRef dataRef(mutableData.data(), mutableData.size());
    auto& data = m_inner->data;

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

bcos::crypto::HashType EthBlockHeaderImpl::hash() const
{
    if (m_inner->dataHash.empty())
    {
        BOOST_THROW_EXCEPTION(EmptyEthBlockHeaderHash{});
    }
    return bcos::crypto::HashType((bcos::byte*)m_inner->dataHash.data(), m_inner->dataHash.size());
}

void EthBlockHeaderImpl::calculateHash()
{
    bcos::bytes encoded;
    encode(encoded);
    auto result = bcos::crypto::keccak256Hash(bcos::ref(encoded));
    m_inner->dataHash.assign(result.begin(), result.end());
}

void EthBlockHeaderImpl::clear() { m_inner = std::make_shared<EthBlockHeader>(); }

size_t EthBlockHeaderImpl::size() const
{
    auto& data = m_inner->data;
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


bcos::crypto::HashType EthBlockHeaderImpl::txsRoot() const { return m_inner->data.txHash; }
bcos::crypto::HashType EthBlockHeaderImpl::stateRoot() const { return m_inner->data.stateRoot; }
bcos::crypto::HashType EthBlockHeaderImpl::receiptsRoot() const { return m_inner->data.receiptHash; }
int64_t EthBlockHeaderImpl::number() const { return m_inner->data.number; }
int64_t EthBlockHeaderImpl::timestamp() const { return m_inner->data.timestamp; }
bcos::u256 EthBlockHeaderImpl::gasUsed() const { return m_inner->data.gasUsed; }

bcos::bytesConstRef EthBlockHeaderImpl::extraData() const
{
    return {m_inner->data.extraData.data(), m_inner->data.extraData.size()};
}

bcos::h256 EthBlockHeaderImpl::parentHash() const { return m_inner->data.parentHash; }
bcos::Address EthBlockHeaderImpl::coinbase() const { return m_inner->data.coinbase; }
bcos::Bloom EthBlockHeaderImpl::logsBloom() const { return m_inner->data.logsBloom; }
bcos::u256 EthBlockHeaderImpl::gasLimit() const { return m_inner->data.gasLimit; }
bcos::h256 EthBlockHeaderImpl::mixDigest() const { return m_inner->data.mixDigest; }
std::optional<bcos::u256> EthBlockHeaderImpl::baseFee() const { return m_inner->data.baseFee; }
std::optional<bcos::h256> EthBlockHeaderImpl::withdrawalsRoot() const { return m_inner->data.withdrawalsHash; }
std::optional<bcos::u256> EthBlockHeaderImpl::blobGasUsed() const { return m_inner->data.blobGasUsed; }
std::optional<bcos::u256> EthBlockHeaderImpl::excessBlobGas() const { return m_inner->data.excessBlobGas; }
std::optional<bcos::h256> EthBlockHeaderImpl::parentBeaconBlockRoot() const { return m_inner->data.parentBeaconRoot; }
std::optional<bcos::h256> EthBlockHeaderImpl::requestsHash() const { return m_inner->data.requestsHash; }
std::optional<bcos::h256> EthBlockHeaderImpl::blockAccessListHash() const { return m_inner->data.blockAccessListHash; }
std::optional<uint64_t> EthBlockHeaderImpl::slotNumber() const { return m_inner->data.slotNumber; }

void EthBlockHeaderImpl::setTxsRoot(bcos::crypto::HashType _txsRoot) { m_inner->data.txHash = _txsRoot; clearDataHash(); }
void EthBlockHeaderImpl::setReceiptsRoot(bcos::crypto::HashType _receiptsRoot) { m_inner->data.receiptHash = _receiptsRoot; clearDataHash(); }
void EthBlockHeaderImpl::setStateRoot(bcos::crypto::HashType _stateRoot) { m_inner->data.stateRoot = _stateRoot; clearDataHash(); }
void EthBlockHeaderImpl::setNumber(int64_t _blockNumber) { m_inner->data.number = _blockNumber; clearDataHash(); }
void EthBlockHeaderImpl::setGasUsed(bcos::u256 _gasUsed) { m_inner->data.gasUsed = _gasUsed; clearDataHash(); }
void EthBlockHeaderImpl::setTimestamp(int64_t _timestamp) { m_inner->data.timestamp = _timestamp; clearDataHash(); }

void EthBlockHeaderImpl::setExtraData(bcos::bytes const& _extraData) { m_inner->data.extraData = _extraData; clearDataHash(); }
void EthBlockHeaderImpl::setExtraData(bcos::bytes&& _extraData) { m_inner->data.extraData = std::move(_extraData); clearDataHash(); }

void EthBlockHeaderImpl::setParentHash(const bcos::h256& _parentHash) { m_inner->data.parentHash = _parentHash; clearDataHash(); }
void EthBlockHeaderImpl::setCoinbase(const bcos::Address& _coinbase) { m_inner->data.coinbase = _coinbase; clearDataHash(); }
void EthBlockHeaderImpl::setLogsBloom(const bcos::Bloom& _logsBloom) { m_inner->data.logsBloom = _logsBloom; clearDataHash(); }
void EthBlockHeaderImpl::setGasLimit(const bcos::u256& _gasLimit) { m_inner->data.gasLimit = _gasLimit; clearDataHash(); }
void EthBlockHeaderImpl::setMixDigest(const bcos::h256& _mixDigest) { m_inner->data.mixDigest = _mixDigest; clearDataHash(); }

void EthBlockHeaderImpl::setBaseFee(const std::optional<bcos::u256>& val) { m_inner->data.baseFee = val; clearDataHash(); }
void EthBlockHeaderImpl::setWithdrawalsRoot(const std::optional<bcos::h256>& val) { m_inner->data.withdrawalsHash = val; clearDataHash(); }
void EthBlockHeaderImpl::setBlobGasUsed(const std::optional<bcos::u256>& val) { m_inner->data.blobGasUsed = val; clearDataHash(); }
void EthBlockHeaderImpl::setExcessBlobGas(const std::optional<bcos::u256>& val) { m_inner->data.excessBlobGas = val; clearDataHash(); }
void EthBlockHeaderImpl::setParentBeaconBlockRoot(const std::optional<bcos::h256>& val) { m_inner->data.parentBeaconRoot = val; clearDataHash(); }
void EthBlockHeaderImpl::setRequestsHash(const std::optional<bcos::h256>& val) { m_inner->data.requestsHash = val; clearDataHash(); }
void EthBlockHeaderImpl::setBlockAccessListHash(const std::optional<bcos::h256>& val) { m_inner->data.blockAccessListHash = val; clearDataHash(); }
void EthBlockHeaderImpl::setSlotNumber(std::optional<uint64_t> val) { m_inner->data.slotNumber = val; clearDataHash(); }

}  // namespace bcos::protocol
