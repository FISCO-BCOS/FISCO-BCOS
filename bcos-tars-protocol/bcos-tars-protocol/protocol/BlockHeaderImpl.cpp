/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief implementation for BlockHeader
 * @file BlockHeaderImpl.cpp
 * @author: ancelmo
 * @date 2021-04-20
 */

#include "BlockHeaderImpl.h"
#include "../Common.h"
#include "../impl/TarsHashable.h"
#include "bcos-concepts/Hash.h"
#include "bcos-rlp-protocol/EthBlockHeader.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Exceptions.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <boost/endian/conversion.hpp>
#include <boost/lexical_cast.hpp>
#include <cstring>
#include <limits>
#include <range/v3/view/any_view.hpp>
#include <range/v3/view/transform.hpp>
#include <stdexcept>
#include <bcos-utilities/BoostLog.h>

DERIVE_BCOS_EXCEPTION(EmptyBlockHeaderHash);


void bcostars::protocol::BlockHeaderImpl::decode(bcos::bytesConstRef _data)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_data.data(), _data.size());

    m_inner->readFrom(input);
}

void bcostars::protocol::BlockHeaderImpl::encode(bcos::bytes& _encodeData) const
{
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;

    m_inner->writeTo(output);
    output.getByteBuffer().swap(_encodeData);
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::hash() const
{
    if (m_inner->dataHash.empty())
    {
        BOOST_THROW_EXCEPTION(EmptyBlockHeaderHash{});
    }

    bcos::crypto::HashType hashResult(
        reinterpret_cast<bcos::byte*>(m_inner->dataHash.data()), m_inner->dataHash.size());

    return hashResult;
}

void bcostars::protocol::BlockHeaderImpl::calculateHash(const bcos::crypto::Hash& hashImpl)
{
    if (ethBlockVersion() != bcos::protocol::EthBlockVersion::NON_ETH)
    {
        // Eth header: recompute the RLP hash via the rlp-protocol bridge. If the header is
        // invalid for its EthBlockVersion, clear dataHash rather than keeping whatever came
        // off the wire — FIB-130's recompute-then-compare depends on calculateHash() never
        // leaving an attacker-supplied hash in place. hash() then throws EmptyBlockHeaderHash,
        // which is how the caller learns the header is not hashable.
        auto err = bcos::protocol::EthBlockHeader::calculateRLPHash(*this);
        if (err)
        {
            clearDataHash();
            BCOS_LOG(WARNING) << LOG_DESC("calculateHash: Eth header validation failed")
                              << LOG_KV("error", err->errorMessage());
        }
    }
    else
    {
        // FISCO-BCOS block — original Tars hash
        bcos::crypto::HashType hashResult;
        bcos::concepts::hash::calculate(*m_inner, hashImpl.hasher(), hashResult);
        m_inner->dataHash.assign(hashResult.begin(), hashResult.end());
    }
}

void bcostars::protocol::BlockHeaderImpl::clear()
{
    m_inner->resetDefautlt();
}

bcos::protocol::ParentInfo bcostars::protocol::BlockHeaderImpl::parentInfo() const
{
    const auto& parentInfos = m_inner->data.parentInfo;
    if (parentInfos.empty())
    {
        return {};
    }
    const auto& first = parentInfos.front();
    return bcos::protocol::ParentInfo{.blockNumber = first.blockNumber,
        .blockHash = bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(first.blockHash.data()), first.blockHash.size())};
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::txsRoot() const
{
    if (m_inner->data.txsRoot.size() >= bcos::crypto::HashType::SIZE)
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner->data.txsRoot.data()));
    }
    return {};
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::stateRoot() const
{
    if (m_inner->data.stateRoot.size() >= bcos::crypto::HashType::SIZE)
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner->data.stateRoot.data()));
    }
    return {};
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::receiptsRoot() const
{
    if (m_inner->data.receiptRoot.size() >= bcos::crypto::HashType::SIZE)
    {
        return *(reinterpret_cast<const bcos::crypto::HashType*>(m_inner->data.receiptRoot.data()));
    }
    return {};
}

bcos::u256 bcostars::protocol::BlockHeaderImpl::gasUsed() const
{
    if (!m_inner->data.gasUsed.empty())
    {
        return boost::lexical_cast<bcos::u256>(m_inner->data.gasUsed);
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setParentInfo(bcos::protocol::ParentInfo parentInfo)
{
    auto& parentInfos = m_inner->data.parentInfo;
    parentInfos.clear();
    auto& _parentInfo = parentInfos.emplace_back();
    _parentInfo.blockNumber = parentInfo.blockNumber;
    _parentInfo.blockHash.assign(parentInfo.blockHash.begin(), parentInfo.blockHash.end());
    clearDataHash();
}

void bcostars::protocol::BlockHeaderImpl::setSealerList(
    gsl::span<const bcos::bytes> const& _sealerList)
{
    m_inner->data.sealerList.clear();
    for (auto const& it : _sealerList)
    {
        m_inner->data.sealerList.emplace_back(it.begin(), it.end());
    }
    clearDataHash();
}

void bcostars::protocol::BlockHeaderImpl::setSignatureList(
    gsl::span<const bcos::protocol::Signature> const& _signatureList)
{
    // Note: must clear the old signatureList when set the new signatureList
    // in case of the consensus module get the cached-sync-blockHeader with signatureList which will
    // cause redundant signature lists to be stored
    m_inner->signatureList.clear();
    for (const auto& it : _signatureList)
    {
        bcostars::Signature signature;
        signature.sealerIndex = it.index;
        signature.signature.assign(it.signature.begin(), it.signature.end());
        m_inner->signatureList.emplace_back(signature);
    }
}
gsl::span<const bcos::bytes> bcostars::protocol::BlockHeaderImpl::sealerList() const
{
    return gsl::span(reinterpret_cast<const bcos::bytes*>(m_inner->data.sealerList.data()),
        m_inner->data.sealerList.size());
}
bcos::bytesConstRef bcostars::protocol::BlockHeaderImpl::extraData() const
{
    return {reinterpret_cast<const bcos::byte*>(m_inner->data.extraData.data()),
        m_inner->data.extraData.size()};
}
gsl::span<const bcos::protocol::Signature> bcostars::protocol::BlockHeaderImpl::signatureList()
    const
{
    return {reinterpret_cast<const bcos::protocol::Signature*>(m_inner->signatureList.data()),
        m_inner->signatureList.size()};
}
gsl::span<const uint64_t> bcostars::protocol::BlockHeaderImpl::consensusWeights() const
{
    return {reinterpret_cast<const uint64_t*>(m_inner->data.consensusWeights.data()),
        m_inner->data.consensusWeights.size()};
}
void bcostars::protocol::BlockHeaderImpl::setVersion(uint32_t _version)
{
    m_inner->data.version = _version;
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setTxsRoot(bcos::crypto::HashType _txsRoot)
{
    m_inner->data.txsRoot.assign(_txsRoot.begin(), _txsRoot.end());
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setReceiptsRoot(bcos::crypto::HashType _receiptsRoot)
{
    m_inner->data.receiptRoot.assign(_receiptsRoot.begin(), _receiptsRoot.end());
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setStateRoot(bcos::crypto::HashType _stateRoot)
{
    m_inner->data.stateRoot.assign(_stateRoot.begin(), _stateRoot.end());
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setNumber(bcos::protocol::BlockNumber _blockNumber)
{
    m_inner->data.blockNumber = _blockNumber;
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setGasUsed(bcos::u256 _gasUsed)
{
    m_inner->data.gasUsed = boost::lexical_cast<std::string>(_gasUsed);
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setTimestamp(int64_t _timestamp)
{
    m_inner->data.timestamp = _timestamp;
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setSealer(int64_t _sealerId)
{
    m_inner->data.sealer = _sealerId;
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setSealerList(std::vector<bcos::bytes>&& _sealerList)
{
    setSealerList(gsl::span(_sealerList.data(), _sealerList.size()));
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setConsensusWeights(
    gsl::span<const uint64_t> const& _weightList)
{
    m_inner->data.consensusWeights.assign(_weightList.begin(), _weightList.end());
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setConsensusWeights(std::vector<uint64_t>&& _weightList)
{
    setConsensusWeights(gsl::span(_weightList.data(), _weightList.size()));
    clearDataHash();
}
void bcostars::protocol::BlockHeaderImpl::setExtraData(bcos::bytes _extraData)
{
    m_inner->data.extraData.assign(_extraData.begin(), _extraData.end());
    clearDataHash();
}

void bcostars::protocol::BlockHeaderImpl::setSignatureList(
    bcos::protocol::SignatureList&& _signatureList)
{
    setSignatureList(gsl::span(_signatureList.data(), _signatureList.size()));
}
const bcostars::BlockHeader& bcostars::protocol::BlockHeaderImpl::inner() const
{
    return *m_inner;
}
bcostars::BlockHeader& bcostars::protocol::BlockHeaderImpl::inner()
{
    return *m_inner;
}
void bcostars::protocol::BlockHeaderImpl::setInner(bcostars::BlockHeader blockHeader)
{
    *m_inner = std::move(blockHeader);
}
void bcostars::protocol::BlockHeaderImpl::clearDataHash()
{
    m_inner->dataHash.clear();
}

size_t bcostars::protocol::BlockHeaderImpl::size() const
{
    size_t size = 0;
    size += m_inner->data.txsRoot.size();
    size += m_inner->data.stateRoot.size();
    size += m_inner->data.receiptRoot.size();
    size += m_inner->data.extraData.size();
    return size;
}
bcostars::protocol::BlockHeaderImpl::BlockHeaderImpl(std::shared_ptr<bcostars::BlockHeader> inner)
  : m_inner(std::move(inner))
{}
bcostars::protocol::BlockHeaderImpl::BlockHeaderImpl()
  : m_inner(std::make_shared<bcostars::BlockHeader>())
{}
uint32_t bcostars::protocol::BlockHeaderImpl::version() const
{
    return m_inner->data.version;
}
bcos::protocol::EthBlockVersion bcostars::protocol::BlockHeaderImpl::ethBlockVersion() const
{
    return static_cast<bcos::protocol::EthBlockVersion>(m_inner->ethBlockVersion);
}
void bcostars::protocol::BlockHeaderImpl::setEthBlockVersion(
    bcos::protocol::EthBlockVersion _version)
{
    m_inner->ethBlockVersion = static_cast<uint8_t>(_version);
    // Changing the Eth marker invalidates any previously computed hash (a FISCO Tars hash
    // must not be returned for a header now marked as Ethereum).
    clearDataHash();
}
bcos::protocol::BlockNumber bcostars::protocol::BlockHeaderImpl::number() const
{
    return m_inner->data.blockNumber;
}
int64_t bcostars::protocol::BlockHeaderImpl::timestamp() const
{
    return m_inner->data.timestamp;
}
int64_t bcostars::protocol::BlockHeaderImpl::sealer() const
{
    return m_inner->data.sealer;
}

// ---- Eth-specific field accessors ----

bcos::Address bcostars::protocol::BlockHeaderImpl::coinbase() const
{
    const auto& _coinbase = inner().data.coinbase;
    if (_coinbase.size() >= bcos::Address::SIZE)
    {
        return *reinterpret_cast<const bcos::Address*>(_coinbase.data());
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setCoinbase(bcos::Address _addr)
{
    m_inner->data.coinbase.assign(_addr.begin(), _addr.end());
    clearDataHash();
}

bcos::bytesConstRef bcostars::protocol::BlockHeaderImpl::logsBloom() const
{
    const auto& data = m_inner->data.logsBloom;
    return {reinterpret_cast<const bcos::byte*>(data.data()), data.size()};
}

void bcostars::protocol::BlockHeaderImpl::setLogsBloom(bcos::bytesConstRef _bloom)
{
    m_inner->data.logsBloom.assign(_bloom.data(), _bloom.data() + _bloom.size());
    clearDataHash();
}

bcos::u256 bcostars::protocol::BlockHeaderImpl::gasLimit() const
{
    const auto& _gasLimit = m_inner->data.gasLimit;
    if (!_gasLimit.empty())
    {
        return boost::lexical_cast<bcos::u256>(_gasLimit);
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setGasLimit(bcos::u256 _limit)
{
    m_inner->data.gasLimit = boost::lexical_cast<std::string>(_limit);
    clearDataHash();
}

bcos::h256 bcostars::protocol::BlockHeaderImpl::prevRandao() const
{
    const auto& _prevRandao = inner().data.prevRandao;
    if (_prevRandao.size() >= bcos::h256::SIZE)
    {
        return *(reinterpret_cast<const bcos::h256*>(_prevRandao.data()));
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setPrevRandao(bcos::h256 _digest)
{
    m_inner->data.prevRandao.assign(_digest.begin(), _digest.end());
    clearDataHash();
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::uncleHash() const
{
    const auto& _uncleHash = inner().data.uncleHash;
    if (_uncleHash.size() >= bcos::crypto::HashType::SIZE)
    {
        return bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(_uncleHash.data()), _uncleHash.size());
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setUncleHash(bcos::crypto::HashType _hash)
{
    m_inner->data.uncleHash.assign(_hash.begin(), _hash.end());
    clearDataHash();
}

bcos::u256 bcostars::protocol::BlockHeaderImpl::difficulty() const
{
    const auto& _difficulty = m_inner->data.difficulty;
    if (!_difficulty.empty())
    {
        return boost::lexical_cast<bcos::u256>(_difficulty);
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setDifficulty(bcos::u256 _difficulty)
{
    m_inner->data.difficulty = boost::lexical_cast<std::string>(_difficulty);
    clearDataHash();
}

bcos::h64 bcostars::protocol::BlockHeaderImpl::nonce() const
{
    const auto& _nonce = inner().data.nonce;
    if (_nonce.size() >= bcos::h64::SIZE)
    {
        return bcos::h64(reinterpret_cast<const bcos::byte*>(_nonce.data()), _nonce.size());
    }
    return {};
}

void bcostars::protocol::BlockHeaderImpl::setNonce(bcos::h64 _nonce)
{
    m_inner->data.nonce.assign(_nonce.begin(), _nonce.end());
    clearDataHash();
}

std::optional<bcos::u256> bcostars::protocol::BlockHeaderImpl::baseFee() const
{
    const auto& _baseFee = inner().data.baseFee;
    if (_baseFee.empty())
    {
        return std::nullopt;
    }
    return boost::lexical_cast<bcos::u256>(_baseFee);
}

void bcostars::protocol::BlockHeaderImpl::setBaseFee(bcos::u256 _fee)
{
    m_inner->data.baseFee = boost::lexical_cast<std::string>(_fee);
    clearDataHash();
}

std::optional<bcos::h256> bcostars::protocol::BlockHeaderImpl::withdrawalsRoot() const
{
    const auto& _withdrawalsHash = inner().data.withdrawalsHash;
    if (_withdrawalsHash.size() >= bcos::h256::SIZE)
    {
        return *(reinterpret_cast<const bcos::h256*>(_withdrawalsHash.data()));
    }
    return std::nullopt;
}

void bcostars::protocol::BlockHeaderImpl::setWithdrawalsRoot(bcos::h256 _hash)
{
    m_inner->data.withdrawalsHash.assign(_hash.begin(), _hash.end());
    clearDataHash();
}

std::optional<bcos::u256> bcostars::protocol::BlockHeaderImpl::blobGasUsed() const
{
    const auto& _blobGasUsed = m_inner->data.blobGasUsed;
    if (_blobGasUsed.empty())
    {
        return std::nullopt;
    }
    return boost::lexical_cast<bcos::u256>(_blobGasUsed);
}

void bcostars::protocol::BlockHeaderImpl::setBlobGasUsed(bcos::u256 _val)
{
    m_inner->data.blobGasUsed = boost::lexical_cast<std::string>(_val);
    clearDataHash();
}

std::optional<bcos::u256> bcostars::protocol::BlockHeaderImpl::excessBlobGas() const
{
    const auto& _excessBlobGas = m_inner->data.excessBlobGas;
    if (_excessBlobGas.empty())
    {
        return std::nullopt;
    }
    return boost::lexical_cast<bcos::u256>(_excessBlobGas);
}

void bcostars::protocol::BlockHeaderImpl::setExcessBlobGas(bcos::u256 _val)
{
    m_inner->data.excessBlobGas = boost::lexical_cast<std::string>(_val);
    clearDataHash();
}

std::optional<bcos::h256> bcostars::protocol::BlockHeaderImpl::parentBeaconBlockRoot() const
{
    const auto& _parentBeaconRoot = inner().data.parentBeaconRoot;
    if (_parentBeaconRoot.size() >= bcos::h256::SIZE)
    {
        return *(reinterpret_cast<const bcos::h256*>(_parentBeaconRoot.data()));
    }
    return std::nullopt;
}

void bcostars::protocol::BlockHeaderImpl::setParentBeaconBlockRoot(bcos::h256 _root)
{
    m_inner->data.parentBeaconRoot.assign(_root.begin(), _root.end());
    clearDataHash();
}

std::optional<bcos::h256> bcostars::protocol::BlockHeaderImpl::requestsHash() const
{
    const auto& _requestsHash = inner().data.requestsHash;
    if (_requestsHash.size() >= bcos::h256::SIZE)
    {
        return *(reinterpret_cast<const bcos::h256*>(_requestsHash.data()));
    }
    return std::nullopt;
}

void bcostars::protocol::BlockHeaderImpl::setRequestsHash(bcos::h256 _hash)
{
    m_inner->data.requestsHash.assign(_hash.begin(), _hash.end());
    clearDataHash();
}

void bcostars::protocol::BlockHeaderImpl::setRLPHash(bcos::crypto::HashType _hash)
{
    m_inner->dataHash.assign(_hash.begin(), _hash.end());
}
