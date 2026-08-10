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

DERIVE_BCOS_EXCEPTION(EmptyBlockHeaderHash);

namespace
{
// Field readers for decodeOpHeader. Local to this TU on purpose: RLPDecode.h's generic overloads
// have other consumers whose behaviour must not change, and these are deliberately STRICTER than
// they are (fixed-width fields must arrive at EXACTLY their width; scalars must be canonical with
// no leading zero; trailing bytes are an error). Names are `op`-prefixed so they do not collide
// with the retired EthBlockHeader.cpp's helpers while both files coexisted in the UNITY_BUILD TU.

bcos::Error::UniquePtr opExpectString(bcos::bytesRef& in, bcos::codec::rlp::Header& header)
{
    auto&& [error, decoded] = bcos::codec::rlp::decodeHeader(in);
    if (error)
    {
        return std::move(error);
    }
    if (decoded.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            bcos::codec::rlp::DecodingError::UnexpectedList, "Unexpected list");
    }
    header = decoded;
    return nullptr;
}

/// Fixed-width field: the payload must be EXACTLY N bytes.
template <unsigned N>
bcos::Error::UniquePtr opDecodeFixed(bcos::bytesRef& in, bcos::FixedBytes<N>& out)
{
    bcos::codec::rlp::Header header;
    if (auto error = opExpectString(in, header))
    {
        return error;
    }
    if (header.payloadLength != N)
    {
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::UnexpectedLength,
            "Fixed-width header field has the wrong length");
    }
    std::memcpy(out.data(), in.data(), N);
    in = in.getCroppedData(N);
    return nullptr;
}

/// Canonical unsigned scalar: no leading zero byte, and no wider than `MaxBytes`.
bcos::Error::UniquePtr opDecodeScalarBytes(
    bcos::bytesRef& in, std::size_t maxBytes, bcos::byte* out, std::size_t outSize)
{
    bcos::codec::rlp::Header header;
    if (auto error = opExpectString(in, header))
    {
        return error;
    }
    if (header.payloadLength > maxBytes)
    {
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::Overflow,
            "Header scalar field is wider than its type");
    }
    if (header.payloadLength > 0 && in[0] == 0)
    {
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::LeadingZero,
            "Header scalar field has a leading zero byte");
    }
    std::memset(out, 0, outSize);
    std::memcpy(out + (outSize - header.payloadLength), in.data(), header.payloadLength);
    in = in.getCroppedData(header.payloadLength);
    return nullptr;
}

bcos::Error::UniquePtr opDecodeU64(bcos::bytesRef& in, uint64_t& out)
{
    std::array<bcos::byte, sizeof(uint64_t)> buffer{};
    if (auto error = opDecodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
    {
        return error;
    }
    out = 0;
    for (auto byteValue : buffer)
    {
        out = (out << 8U) | byteValue;
    }
    return nullptr;
}

bcos::Error::UniquePtr opDecodeU256(bcos::bytesRef& in, bcos::u256& out)
{
    std::array<bcos::byte, 32> buffer{};
    if (auto error = opDecodeScalarBytes(in, buffer.size(), buffer.data(), buffer.size()))
    {
        return error;
    }
    out = bcos::fromBigEndian<bcos::u256>(bcos::bytesConstRef(buffer.data(), buffer.size()));
    return nullptr;
}

bcos::Error::UniquePtr opDecodeByteString(bcos::bytesRef& in, bcos::bytes& out)
{
    bcos::codec::rlp::Header header;
    if (auto error = opExpectString(in, header))
    {
        return error;
    }
    out = in.getCroppedData(0, header.payloadLength).toBytes();
    in = in.getCroppedData(header.payloadLength);
    return nullptr;
}

/// The gas fields are u256 on the FISCO interface but u64-bounded in the OP protocol. An overflow
/// is an internal invariant violation, never a payload verdict — throwing is the loud, correct
/// behaviour.
uint64_t opNarrowU256(bcos::u256 const& value, std::string_view what)
{
    if (value > std::numeric_limits<uint64_t>::max())
    {
        throw std::overflow_error(std::string(what) + " exceeds uint64 in encodeOpHeader");
    }
    return static_cast<uint64_t>(value);
}
}  // namespace

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
// ---- OP Stack header capability ----

bcos::bytes bcostars::protocol::BlockHeaderImpl::encodeOpHeader(
    const bcos::protocol::BlockHeader::OpHeaderConst& c) const
{
    bcos::bytes out;
    // Field order is load-bearing: spec §5.1's 21-field order (== go-ethereum core/types.Header),
    // pinned byte-for-byte by the golden gate (bcos-evm/test/opstack/EthBlockHeaderTest.cpp, 33
    // vectors). The header's timestamp is tars-stored in MILLISECONDS; the RLP field is SECONDS —
    // this /1000 is what keeps the bytes identical to the golden corpus.
    bcos::codec::rlp::encode(out, parentInfo().blockHash, c.ommersHash, coinbase(), stateRoot(),
        txsRoot(), receiptsRoot(), logsBloom(), c.difficulty, static_cast<uint64_t>(number()),
        opNarrowU256(gasLimit(), "gasLimit"), opNarrowU256(gasUsed(), "gasUsed"),
        static_cast<uint64_t>(timestamp()) / 1000, extraData(), prevRandao(), c.nonce,
        baseFee().value(), withdrawalsRoot().value(),
        opNarrowU256(blobGasUsed().value(), "blobGasUsed"),
        opNarrowU256(excessBlobGas().value(), "excessBlobGas"), parentBeaconBlockRoot().value(),
        requestsHash().value());
    return out;
}

bcos::crypto::HashType bcostars::protocol::BlockHeaderImpl::opHeaderHash(
    const bcos::protocol::BlockHeader::OpHeaderConst& c) const
{
    auto encoded = encodeOpHeader(c);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::Error::UniquePtr bcostars::protocol::BlockHeaderImpl::decodeOpHeader(
    bcos::bytesRef in, bcos::protocol::BlockHeader::OpHeaderConst& c)
{
    // Same 21 fields, same order as encodeOpHeader() — the golden round-trip assertion pins the
    // two to each other.
    auto&& [listError, listHeader] = bcos::codec::rlp::decodeHeader(in);
    if (listError)
    {
        return std::move(listError);
    }
    if (!listHeader.isList)
    {
        return BCOS_ERROR_UNIQUE_PTR(
            bcos::codec::rlp::DecodingError::UnexpectedString, "Block header must be an RLP list");
    }
    if (listHeader.payloadLength != in.size())
    {
        // Trailing bytes after the list, or a list header claiming less than it was given.
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::InputTooLong,
            "Block header RLP has trailing bytes after the field list");
    }

    bcos::h256 parentHash, ommersHash, stateRoot, transactionsRoot, receiptsRoot, prevRandao;
    bcos::h256 withdrawalsRoot, parentBeaconBlockRoot, requestsHash;
    bcos::Address feeRecipient;
    bcos::h2048 logsBloom;
    bcos::u256 difficulty, baseFeePerGas;
    uint64_t number = 0, gasLimit = 0, gasUsed = 0, timestamp = 0, blobGasUsed = 0,
             excessBlobGas = 0;
    bcos::bytes extraData;
    bcos::h64 nonce;

    bcos::Error::UniquePtr error;
    const auto step = [&](bcos::Error::UniquePtr fieldError) {
        if (!error)
        {
            error = std::move(fieldError);
        }
    };
    step(opDecodeFixed(in, parentHash));
    step(opDecodeFixed(in, ommersHash));
    step(opDecodeFixed(in, feeRecipient));
    step(opDecodeFixed(in, stateRoot));
    step(opDecodeFixed(in, transactionsRoot));
    step(opDecodeFixed(in, receiptsRoot));
    step(opDecodeFixed(in, logsBloom));
    step(opDecodeU256(in, difficulty));
    step(opDecodeU64(in, number));
    step(opDecodeU64(in, gasLimit));
    step(opDecodeU64(in, gasUsed));
    step(opDecodeU64(in, timestamp));
    step(opDecodeByteString(in, extraData));
    step(opDecodeFixed(in, prevRandao));
    step(opDecodeFixed(in, nonce));
    step(opDecodeU256(in, baseFeePerGas));
    step(opDecodeFixed(in, withdrawalsRoot));
    step(opDecodeU64(in, blobGasUsed));
    step(opDecodeU64(in, excessBlobGas));
    step(opDecodeFixed(in, parentBeaconBlockRoot));
    step(opDecodeFixed(in, requestsHash));
    if (error)
    {
        return error;
    }
    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(bcos::codec::rlp::DecodingError::UnexpectedListElements,
            "Block header RLP has extra fields");
    }

    // Write the 18 tars-carried fields back through the interface; the 3 constants have no carrier
    // and return via `c`. `parentInfo` is the single-parent carrier: the RLP carries only the
    // parent hash, and the parent height is not in the RLP. `blockNumber = number - 1` is the
    // consistent convention with `rebuildOpEthHeader` (审查 F1:decode 必须与生产者同语义,否则
    // round-trip 后 parentInfo.blockNumber 会漂移;golden 全是 block 1,N-1=0,测试断言它)。
    setParentInfo(bcos::protocol::ParentInfo{
        .blockNumber = static_cast<bcos::protocol::BlockNumber>(number) - 1,
        .blockHash = parentHash});
    setCoinbase(feeRecipient);
    setStateRoot(stateRoot);
    setTxsRoot(transactionsRoot);
    setReceiptsRoot(receiptsRoot);
    setLogsBloom(bcos::bytesConstRef(logsBloom.data(), logsBloom.size()));
    setNumber(static_cast<bcos::protocol::BlockNumber>(number));
    setGasLimit(bcos::u256(gasLimit));
    setGasUsed(bcos::u256(gasUsed));
    // RLP seconds -> tars milliseconds.
    setTimestamp(static_cast<int64_t>(timestamp) * 1000);
    setExtraData(std::move(extraData));
    setPrevRandao(prevRandao);
    setBaseFee(baseFeePerGas);
    setWithdrawalsRoot(withdrawalsRoot);
    setBlobGasUsed(bcos::u256(blobGasUsed));
    setExcessBlobGas(bcos::u256(excessBlobGas));
    setParentBeaconBlockRoot(parentBeaconBlockRoot);
    setRequestsHash(requestsHash);
    c = bcos::protocol::BlockHeader::OpHeaderConst{
        .ommersHash = ommersHash, .difficulty = difficulty, .nonce = nonce};
    return nullptr;
}
