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
 * @brief EthBlockHeader — RLP bridge implementation
 * @date 2026/6/24
 */
#include "EthBlockHeader.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/tars/Block.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{
void EthBlockHeader::calculateRLPHash(
    bcos::protocol::BlockHeader::Ptr header, bcos::Error::UniquePtr& error)
{
    auto* tarsHeader =
        dynamic_cast<bcostars::protocol::BlockHeaderImpl*>(header.get());
    if (tarsHeader == nullptr)
    {
        error = BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::InvalidHeaderType),
            "EthBlockHeader: header is not a BlockHeaderImpl");
        return;
    }

    if (!validateTarsHeader(tarsHeader->inner(), error))
    {
        return;
    }

    auto ethHeader = EthBlockHeader(tarsHeader->inner());
    bcos::bytes encoded;
    ethHeader.rlpEncode(encoded);
    tarsHeader->setRLPHash(bcos::crypto::keccak256Hash(bcos::ref(encoded)));
}

bcos::protocol::BlockHeader::Ptr EthBlockHeader::toTarsHeader(
    bcos::bytesConstRef _data, bcos::Error::UniquePtr& error)
{
    auto ethHeader = toEthBlockHeader(_data, error);
    if (error)
    {
        return nullptr;
    }

    auto result = std::make_shared<bcostars::BlockHeader>();
    auto& tarsData = result->data;

    // parentInfo (Tars keeps it as a vector)
    auto& tarsParentInfo = tarsData.parentInfo;
    tarsParentInfo.clear();
    auto& tarsParentInfoItem = tarsParentInfo.emplace_back();
    tarsParentInfoItem.blockNumber = ethHeader.data().parentInfo.blockNumber;
    tarsParentInfoItem.blockHash.assign(
        ethHeader.data().parentInfo.blockHash.begin(), ethHeader.data().parentInfo.blockHash.end());

    tarsData.coinbase.assign(ethHeader.data().coinbase.begin(), ethHeader.data().coinbase.end());

    tarsData.stateRoot.assign(ethHeader.data().stateRoot.begin(), ethHeader.data().stateRoot.end());
    tarsData.txsRoot.assign(ethHeader.data().txsRoot.begin(), ethHeader.data().txsRoot.end());
    tarsData.receiptRoot.assign(
        ethHeader.data().receiptsRoot.begin(), ethHeader.data().receiptsRoot.end());

    tarsData.gasLimit = boost::lexical_cast<std::string>(ethHeader.data().gasLimit);
    tarsData.gasUsed = boost::lexical_cast<std::string>(ethHeader.data().gasUsed);

    tarsData.blockNumber = ethHeader.data().number;
    tarsData.timestamp = ethHeader.data().timestamp;

    tarsData.prevRandao.assign(
        ethHeader.data().prevRandao.begin(), ethHeader.data().prevRandao.end());

    tarsData.extraData.assign(ethHeader.data().extraData.begin(), ethHeader.data().extraData.end());

    tarsData.logsBloom.assign(ethHeader.data().logsBloom.begin(), ethHeader.data().logsBloom.end());

    if (ethHeader.data().baseFee.has_value())
    {
        tarsData.baseFee = boost::lexical_cast<std::string>(*ethHeader.data().baseFee);
    }
    if (ethHeader.data().withdrawalsHash.has_value())
    {
        tarsData.withdrawalsHash.assign(
            ethHeader.data().withdrawalsHash->begin(), ethHeader.data().withdrawalsHash->end());
    }
    if (ethHeader.data().blobGasUsed.has_value())
    {
        tarsData.blobGasUsed =
            boost::lexical_cast<std::string>(*ethHeader.data().blobGasUsed);
    }
    if (ethHeader.data().excessBlobGas.has_value())
    {
        tarsData.excessBlobGas =
            boost::lexical_cast<std::string>(*ethHeader.data().excessBlobGas);
    }
    if (ethHeader.data().parentBeaconRoot.has_value())
    {
        tarsData.parentBeaconRoot.assign(
            ethHeader.data().parentBeaconRoot->begin(), ethHeader.data().parentBeaconRoot->end());
    }
    if (ethHeader.data().requestsHash.has_value())
    {
        tarsData.requestsHash.assign(
            ethHeader.data().requestsHash->begin(), ethHeader.data().requestsHash->end());
    }
    if (ethHeader.data().blockAccessListHash.has_value())
    {
        tarsData.blockAccessListHash.assign(ethHeader.data().blockAccessListHash->begin(),
            ethHeader.data().blockAccessListHash->end());
    }
    if (ethHeader.data().slotNumber.has_value())
    {
        tarsData.slotNumber = static_cast<long>(*ethHeader.data().slotNumber);
    }

    // Wrap in a BlockHeaderImpl so the caller holds a base-class pointer whose
    // concrete type is bcostars::protocol::BlockHeaderImpl.
    auto impl = std::make_shared<bcostars::protocol::BlockHeaderImpl>(result);
    impl->setVersion(bcos::protocol::ETH_BLOCK_HEADER_VERSION);
    return impl;
}

EthBlockHeader EthBlockHeader::toEthBlockHeader(
    bcos::bytesConstRef _data, bcos::Error::UniquePtr& error)
{
    EthBlockHeader ethHeader;
    auto err = ethHeader.rlpDecode(_data);
    if (err)
    {
        error = BCOS_ERROR_UNIQUE_PTR(
            static_cast<int32_t>(EthBlockHeaderError::RlpDecodeFailed),
            "EthBlockHeader: rlpDecode failed: " + err->errorMessage());
        return EthBlockHeader{};
    }
    return ethHeader;
}



// Validate that the Tars header carries every Ethereum-required field.
// can be removed if we can guarantee the Tars header is always valid.
bool EthBlockHeader::validateTarsHeader(
    const bcostars::BlockHeader& _tarsHeader, bcos::Error::UniquePtr& error)
{
    const auto& _data = _tarsHeader.data;

    auto invalid = [&error](const std::string& msg) {
        error = BCOS_ERROR_UNIQUE_PTR(
            static_cast<int32_t>(EthBlockHeaderError::InvalidTarsHeader), msg);
        return false;
    };

    // Required hash/address fields (20 or 32 bytes)
    if (_data.parentInfo.empty() || _data.parentInfo.front().blockHash.size() < crypto::HashType::SIZE)
    {
        return invalid("EthBlockHeader: missing or short parentInfo.blockHash");
    }
    if (_data.coinbase.size() < Address::SIZE)
    {
        return invalid("EthBlockHeader: missing or short coinbase");
    }
    if (_data.stateRoot.size() < crypto::HashType::SIZE)
    {
        return invalid("EthBlockHeader: missing or short stateRoot");
    }
    if (_data.txsRoot.size() < crypto::HashType::SIZE)
    {
        return invalid("EthBlockHeader: missing or short txsRoot");
    }
    if (_data.receiptRoot.size() < crypto::HashType::SIZE)
    {
        return invalid("EthBlockHeader: missing or short receiptRoot");
    }
    if (_data.prevRandao.size() < h256::SIZE)
    {
        return invalid("EthBlockHeader: missing or short prevRandao");
    }

    // Required scalar/string fields
    if (_data.gasLimit.empty())
    {
        return invalid("EthBlockHeader: missing gasLimit");
    }
    if (_data.gasUsed.empty())
    {
        return invalid("EthBlockHeader: missing gasUsed");
    }
    if (_data.logsBloom.size() < bcos::Bloom{}.size())
    {
        return invalid("EthBlockHeader: missing or short logsBloom");
    }
    if (_data.timestamp == 0)
    {
        return invalid("EthBlockHeader: missing timestamp");
    }

    return true;
}

EthBlockHeader::EthBlockHeader(const bcostars::BlockHeader& _tarsHeader) noexcept
{
    const auto& _data = _tarsHeader.data;

    // Required fields — converted directly, with defensive zero-fill for empty fields so
    // constructing from an incomplete header never crashes (validation is the caller's job,
    // e.g. via calculateRLPHash -> validateTarsHeader).
    auto const& tarsParentInfo = _data.parentInfo;
    if (!tarsParentInfo.empty() &&
        tarsParentInfo.front().blockHash.size() >= crypto::HashType::SIZE)
    {
        m_data.parentInfo.blockNumber = tarsParentInfo.front().blockNumber;
        m_data.parentInfo.blockHash = *reinterpret_cast<const crypto::HashType*>(
            tarsParentInfo.front().blockHash.data());
    }

    if (_data.coinbase.size() >= Address::SIZE)
    {
        m_data.coinbase = *reinterpret_cast<const Address*>(_data.coinbase.data());
    }
    if (_data.stateRoot.size() >= crypto::HashType::SIZE)
    {
        m_data.stateRoot = *reinterpret_cast<const crypto::HashType*>(_data.stateRoot.data());
    }
    if (_data.txsRoot.size() >= crypto::HashType::SIZE)
    {
        m_data.txsRoot = *reinterpret_cast<const crypto::HashType*>(_data.txsRoot.data());
    }
    if (_data.receiptRoot.size() >= crypto::HashType::SIZE)
    {
        m_data.receiptsRoot = *reinterpret_cast<const crypto::HashType*>(_data.receiptRoot.data());
    }

    if (!_data.gasLimit.empty())
    {
        m_data.gasLimit = boost::lexical_cast<u256>(_data.gasLimit);
    }
    if (!_data.gasUsed.empty())
    {
        m_data.gasUsed = boost::lexical_cast<u256>(_data.gasUsed);
    }

    m_data.number = _data.blockNumber;
    m_data.timestamp = _data.timestamp;

    if (_data.prevRandao.size() >= h256::SIZE)
    {
        m_data.prevRandao = *reinterpret_cast<const h256*>(_data.prevRandao.data());
    }

    m_data.extraData.assign(
        _data.extraData.begin(), _data.extraData.end());

    if (_data.logsBloom.size() >= m_data.logsBloom.size())
    {
        std::memcpy(m_data.logsBloom.data(), _data.logsBloom.data(), m_data.logsBloom.size());
    }

    // Optional fields — check presence
    if (!_data.baseFee.empty())
    {
        m_data.baseFee = boost::lexical_cast<u256>(_data.baseFee);
    }
    if (!_data.withdrawalsHash.empty())
    {
        m_data.withdrawalsHash = *reinterpret_cast<const h256*>(_data.withdrawalsHash.data());
    }
    if (!_data.blobGasUsed.empty())
    {
        m_data.blobGasUsed = boost::lexical_cast<u256>(_data.blobGasUsed);
    }
    if (!_data.excessBlobGas.empty())
    {
        m_data.excessBlobGas = boost::lexical_cast<u256>(_data.excessBlobGas);
    }
    if (!_data.parentBeaconRoot.empty())
    {
        m_data.parentBeaconRoot = *reinterpret_cast<const h256*>(_data.parentBeaconRoot.data());
    }
    if (!_data.requestsHash.empty())
    {
        m_data.requestsHash = *reinterpret_cast<const h256*>(_data.requestsHash.data());
    }
    if (!_data.blockAccessListHash.empty())
    {
        m_data.blockAccessListHash =
            *reinterpret_cast<const h256*>(_data.blockAccessListHash.data());
    }
    if (_data.slotNumber != -1)  // -1 is the Tars unset sentinel
    {
        m_data.slotNumber = static_cast<uint64_t>(_data.slotNumber);
    }
}

void EthBlockHeader::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out,
        m_data.parentInfo.blockHash,
        bcos::h256{std::string_view(
            "1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347"),
            bcos::h256::FromHex},       // uncleHash, fixed (hardcoded)
        m_data.coinbase,
        m_data.stateRoot,
        m_data.txsRoot,
        m_data.receiptsRoot,
        bcos::bytesConstRef(m_data.logsBloom.data(), m_data.logsBloom.size()),
        bcos::u256{0},                      // difficulty, fixed to 0
        static_cast<uint64_t>(m_data.number),
        m_data.gasLimit,
        m_data.gasUsed,
        static_cast<uint64_t>(m_data.timestamp),
        m_data.extraData,
        m_data.prevRandao,
        bcos::h64{0},              // nonce, fixed to 0
        m_data.baseFee,
        m_data.withdrawalsHash,
        m_data.blobGasUsed,
        m_data.excessBlobGas,
        m_data.parentBeaconRoot,
        m_data.requestsHash,
        m_data.blockAccessListHash,
        m_data.slotNumber);
}


bcos::Error::UniquePtr EthBlockHeader::rlpDecode(bcos::bytesConstRef data)
{
    auto mutableData = data.toBytes();
    bytesRef ref(mutableData.data(), mutableData.size());

    uint64_t _number = 0;
    uint64_t _timestamp = 0;

    // Deprecated fields — read and discard
    h256 _uncleHash;
    u256 _difficulty;
    h64 _nonce;

    auto error = codec::rlp::decode(ref,
        m_data.parentInfo.blockHash,
        _uncleHash,
        m_data.coinbase,
        m_data.stateRoot,
        m_data.txsRoot,
        m_data.receiptsRoot,
        m_data.logsBloom,
        _difficulty,
        _number,
        m_data.gasLimit,
        m_data.gasUsed,
        _timestamp,
        m_data.extraData,
        m_data.prevRandao,
        _nonce,
        m_data.baseFee,
        m_data.withdrawalsHash,
        m_data.blobGasUsed,
        m_data.excessBlobGas,
        m_data.parentBeaconRoot,
        m_data.requestsHash,
        m_data.blockAccessListHash,
        m_data.slotNumber);
    if (error)
    {
        return error;
    }

    m_data.number = static_cast<int64_t>(_number);
    m_data.timestamp = static_cast<int64_t>(_timestamp);

    return nullptr;
}
}  // namespace bcos::protocol
