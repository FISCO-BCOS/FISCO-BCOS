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
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/throw_exception.hpp>
#include <cstring>

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{
bcos::Error::UniquePtr EthBlockHeader::calculateRLPHash(bcos::protocol::BlockHeader& header)
{
    bcos::Error::UniquePtr error;
    if (!validateHeader(header, error))
    {
        return error;
    }

    EthBlockHeader ethHeader(header);
    bcos::bytes encoded;
    ethHeader.rlpEncode(encoded);
    header.setRLPHash(bcos::crypto::keccak256Hash(bcos::ref(encoded)));
    return nullptr;
}

bcos::crypto::HashType EthBlockHeader::computeHash(const bcos::protocol::BlockHeader& header)
{
    EthBlockHeader ethHeader(header);
    bcos::bytes encoded;
    ethHeader.rlpEncode(encoded);
    return bcos::crypto::keccak256Hash(bcos::ref(encoded));
}

bcos::Error::UniquePtr EthBlockHeader::toTarsHeader(
    bcos::protocol::BlockHeader::Ptr header, bcos::bytesConstRef _data)
{
    if (header == nullptr)
    {
        return BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::InvalidHeaderType),
            "EthBlockHeader: header is null");
    }

    // Reset the destination first so reusing a header (previously holding higher-version
    // optional fields) cannot leak stale values into this decode.
    header->clear();

    EthBlockHeader ethHeader;
    if (auto err = toEthBlockHeader(ethHeader, _data))
    {
        return err;
    }

    // The Ethereum header RLP carries only the parent hash, not the parent block number.
    // parentInfo.blockNumber is therefore left at its default (0) after decode; callers that
    // need it must fill it from the chain context.
    header->setParentInfo(ethHeader.data().parentInfo);
    header->setCoinbase(ethHeader.data().coinbase);
    header->setUncleHash(ethHeader.data().uncleHash);
    header->setStateRoot(ethHeader.data().stateRoot);
    header->setTxsRoot(ethHeader.data().txsRoot);
    header->setReceiptsRoot(ethHeader.data().receiptsRoot);
    header->setDifficulty(ethHeader.data().difficulty);
    header->setGasLimit(ethHeader.data().gasLimit);
    header->setGasUsed(ethHeader.data().gasUsed);
    header->setNumber(ethHeader.data().number);
    header->setTimestamp(ethHeader.data().timestamp);
    header->setPrevRandao(ethHeader.data().prevRandao);
    header->setNonce(ethHeader.data().nonce);
    header->setExtraData(ethHeader.data().extraData);
    header->setLogsBloom(
        bcos::bytesConstRef(ethHeader.data().logsBloom.data(), ethHeader.data().logsBloom.size()));

    // Optional fork fields: every field is guarded independently by its own has_value(), so a
    // truncated RLP list can never dereference an empty optional.
    if (ethHeader.data().baseFee.has_value())
    {
        header->setBaseFee(*ethHeader.data().baseFee);
    }
    if (ethHeader.data().withdrawalsHash.has_value())
    {
        header->setWithdrawalsRoot(*ethHeader.data().withdrawalsHash);
    }
    if (ethHeader.data().blobGasUsed.has_value())
    {
        header->setBlobGasUsed(*ethHeader.data().blobGasUsed);
    }
    if (ethHeader.data().excessBlobGas.has_value())
    {
        header->setExcessBlobGas(*ethHeader.data().excessBlobGas);
    }
    if (ethHeader.data().parentBeaconRoot.has_value())
    {
        header->setParentBeaconBlockRoot(*ethHeader.data().parentBeaconRoot);
    }
    if (ethHeader.data().requestsHash.has_value())
    {
        header->setRequestsHash(*ethHeader.data().requestsHash);
    }

    // Set the fork version first, then validate: validateHeader checks the header's fields
    // against its EthBlockVersion, so the version must be in place before the check.
    header->setEthBlockVersion(ethHeader.version());

    // The RLP-to-header path requires the same fields the header-to-hash path requires for
    // the decoded fork version, so both directions agree on what constitutes a valid header.
    bcos::Error::UniquePtr validationError;
    if (!validateHeader(*header, validationError))
    {
        // Leave the destination header in a defined state on failure: the fields written
        // above are rolled back so the caller cannot mistake a half-populated header for a
        // valid one.
        header->clear();
        return validationError;
    }

    // Inject keccak256 of the canonical re-encoding (not of the raw input bytes): this is
    // exactly what calculateRLPHash does, so both paths agree even if the caller passed
    // trailing data after the header RLP list.
    bcos::bytes reencoded;
    ethHeader.rlpEncode(reencoded);
    header->setRLPHash(bcos::crypto::keccak256Hash(bcos::ref(reencoded)));
    return nullptr;
}

bcos::Error::UniquePtr EthBlockHeader::decodeTarsHeader(
    bcos::protocol::BlockHeader::Ptr header, bcos::bytesConstRef _data)
{
    if (header == nullptr)
    {
        return BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::InvalidHeaderType),
            "EthBlockHeader: header is null");
    }
    header->clear();

    EthBlockHeader ethHeader;
    if (auto err = ethHeader.rlpDecode(_data))
    {
        return err;
    }

    // Same field writes as toTarsHeader but WITHOUT validateHeader — usable for FISCO-native/OP
    // (NON_ETH) headers that validateHeader rejects. For NON_ETH the RLP timestamp is SECONDS and
    // the FISCO header stores MILLISECONDS, so ×1000 (ETH-version headers already carry seconds).
    header->setParentInfo(ethHeader.data().parentInfo);
    header->setCoinbase(ethHeader.data().coinbase);
    header->setUncleHash(ethHeader.data().uncleHash);
    header->setStateRoot(ethHeader.data().stateRoot);
    header->setTxsRoot(ethHeader.data().txsRoot);
    header->setReceiptsRoot(ethHeader.data().receiptsRoot);
    header->setDifficulty(ethHeader.data().difficulty);
    header->setGasLimit(ethHeader.data().gasLimit);
    header->setGasUsed(ethHeader.data().gasUsed);
    header->setNumber(ethHeader.data().number);
    header->setTimestamp(ethHeader.data().timestamp * 1000);  // NON_ETH RLP seconds -> ms
    header->setPrevRandao(ethHeader.data().prevRandao);
    header->setNonce(ethHeader.data().nonce);
    header->setExtraData(ethHeader.data().extraData);
    header->setLogsBloom(
        bcos::bytesConstRef(ethHeader.data().logsBloom.data(), ethHeader.data().logsBloom.size()));
    if (ethHeader.data().baseFee.has_value())
    {
        header->setBaseFee(*ethHeader.data().baseFee);
    }
    if (ethHeader.data().withdrawalsHash.has_value())
    {
        header->setWithdrawalsRoot(*ethHeader.data().withdrawalsHash);
    }
    if (ethHeader.data().blobGasUsed.has_value())
    {
        header->setBlobGasUsed(*ethHeader.data().blobGasUsed);
    }
    if (ethHeader.data().excessBlobGas.has_value())
    {
        header->setExcessBlobGas(*ethHeader.data().excessBlobGas);
    }
    if (ethHeader.data().parentBeaconRoot.has_value())
    {
        header->setParentBeaconBlockRoot(*ethHeader.data().parentBeaconRoot);
    }
    if (ethHeader.data().requestsHash.has_value())
    {
        header->setRequestsHash(*ethHeader.data().requestsHash);
    }
    return nullptr;
}

bcos::Error::UniquePtr EthBlockHeader::toEthBlockHeader(
    EthBlockHeader& ethHeader, bcos::bytesConstRef _data)
{
    auto err = ethHeader.rlpDecode(_data);
    if (err)
    {
        return BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::RlpDecodeFailed),
            "EthBlockHeader: rlpDecode failed: " + err->errorMessage());
    }
    return nullptr;
}

// Validate that the header carries every field its EthBlockVersion requires.
// can be removed if we can guarantee the header is always valid.
bool EthBlockHeader::validateHeader(
    const bcos::protocol::BlockHeader& _header, bcos::Error::UniquePtr& error)
{
    auto invalid = [&error](const std::string& msg) {
        error =
            BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::InvalidHeader), msg);
        return false;
    };

    auto version = _header.ethBlockVersion();
    if (version == EthBlockVersion::NON_ETH)
    {
        return invalid("EthBlockHeader: not an Ethereum header (EthBlockVersion == NON_ETH)");
    }
    // Reject wire-supplied versions outside the known fork range: an unknown value would
    // behave like PRAGUE (demanding every fork field) without being a named enum value.
    if (static_cast<uint8_t>(version) > static_cast<uint8_t>(EthBlockVersion::PRAGUE))
    {
        return invalid("EthBlockHeader: unsupported EthBlockVersion " +
                       std::to_string(static_cast<int>(version)));
    }

    // Mandatory fields, required by every Eth version.
    // parentInfo: the Ethereum header RLP does not carry the parent block number, so
    // parentInfo.blockNumber is only meaningful when filled in by the caller. The genesis
    // block (number 0) has an empty parent hash by definition; only require a non-empty
    // parent hash when this header is not the genesis block.
    auto parent = _header.parentInfo();
    if (_header.number() != 0 && parent.blockHash == bcos::crypto::HashType{})
    {
        return invalid("EthBlockHeader: missing or bad parentInfo.blockHash");
    }
    // The base-class accessors return fixed-size FixedBytes: a "not set" field surfaces as
    // all-zero bytes. Check for all-zero rather than size.
    // uncleHash is never legitimately zero on Ethereum — the empty-ommers constant is
    // 0x1dcc4de8…, so an all-zero uncleHash means "missing" (unlike coinbase/prevRandao,
    // which can be zero on real chains).
    if (_header.uncleHash() == bcos::crypto::HashType{})
    {
        return invalid("EthBlockHeader: missing or bad uncleHash");
    }
    if (_header.stateRoot() == bcos::crypto::HashType{})
    {
        return invalid("EthBlockHeader: missing or bad stateRoot");
    }
    if (_header.txsRoot() == bcos::crypto::HashType{})
    {
        return invalid("EthBlockHeader: missing or bad txsRoot");
    }
    if (_header.receiptsRoot() == bcos::crypto::HashType{})
    {
        return invalid("EthBlockHeader: missing or bad receiptRoot");
    }
    if (_header.logsBloom().size() != bcos::Bloom{}.size())
    {
        return invalid("EthBlockHeader: missing or bad logsBloom");
    }

    // Required scalar fields.
    if (_header.number() < 0)
    {
        return invalid("EthBlockHeader: negative blockNumber");
    }
    if (_header.timestamp() < 0)
    {
        return invalid("EthBlockHeader: invalid timestamp");
    }

    // Fork-gated optional fields: a version N header must carry every field introduced by
    // version N and all earlier fork-gated fields.
    auto requireForkField = [&](EthBlockVersion _minVersion, const std::string& _field,
                                bool _present) {
        if (static_cast<uint8_t>(version) >= static_cast<uint8_t>(_minVersion) && !_present)
        {
            return invalid("EthBlockHeader: missing " + _field + " for EthBlockVersion " +
                           std::to_string(static_cast<int>(version)));
        }
        return true;
    };

    if (!requireForkField(EthBlockVersion::LONDON, "baseFee", _header.baseFee().has_value()))
    {
        return false;
    }
    if (!requireForkField(
            EthBlockVersion::SHANGHAI, "withdrawalsRoot", _header.withdrawalsRoot().has_value()))
    {
        return false;
    }
    if (!requireForkField(
            EthBlockVersion::CANCUN, "blobGasUsed", _header.blobGasUsed().has_value()))
    {
        return false;
    }
    if (!requireForkField(
            EthBlockVersion::CANCUN, "excessBlobGas", _header.excessBlobGas().has_value()))
    {
        return false;
    }
    if (!requireForkField(EthBlockVersion::CANCUN, "parentBeaconBlockRoot",
            _header.parentBeaconBlockRoot().has_value()))
    {
        return false;
    }
    if (!requireForkField(
            EthBlockVersion::PRAGUE, "requestsHash", _header.requestsHash().has_value()))
    {
        return false;
    }

    // Symmetric check: a fork-gated field must not be present when the header's version is
    // older than the fork that introduced it. RLP lists are positional — a field above the
    // declared version would shift every later field down one slot on re-encode, so a header
    // that passes this check is guaranteed to be a canonical Ethereum header for its version.
    auto forbidForkField = [&](EthBlockVersion _minVersion, const std::string& _field,
                               bool _present) {
        if (static_cast<uint8_t>(version) < static_cast<uint8_t>(_minVersion) && _present)
        {
            return invalid("EthBlockHeader: unexpected " + _field + " for EthBlockVersion " +
                           std::to_string(static_cast<int>(version)));
        }
        return true;
    };

    if (!forbidForkField(EthBlockVersion::LONDON, "baseFee", _header.baseFee().has_value()))
    {
        return false;
    }
    if (!forbidForkField(
            EthBlockVersion::SHANGHAI, "withdrawalsRoot", _header.withdrawalsRoot().has_value()))
    {
        return false;
    }
    if (!forbidForkField(EthBlockVersion::CANCUN, "blobGasUsed", _header.blobGasUsed().has_value()))
    {
        return false;
    }
    if (!forbidForkField(
            EthBlockVersion::CANCUN, "excessBlobGas", _header.excessBlobGas().has_value()))
    {
        return false;
    }
    if (!forbidForkField(EthBlockVersion::CANCUN, "parentBeaconBlockRoot",
            _header.parentBeaconBlockRoot().has_value()))
    {
        return false;
    }
    if (!forbidForkField(
            EthBlockVersion::PRAGUE, "requestsHash", _header.requestsHash().has_value()))
    {
        return false;
    }

    return true;
}

EthBlockHeader::EthBlockHeader(const bcos::protocol::BlockHeader& _header)
{
    // Required fields — converted directly, with defensive defaults for empty fields so
    // constructing from an incomplete header never crashes (validation is the caller's job,
    // e.g. via calculateRLPHash -> validateHeader).
    auto parent = _header.parentInfo();
    m_data.parentInfo.blockNumber = parent.blockNumber;
    m_data.parentInfo.blockHash = parent.blockHash;

    m_data.coinbase = _header.coinbase();
    m_data.uncleHash = _header.uncleHash();
    m_data.stateRoot = _header.stateRoot();
    m_data.txsRoot = _header.txsRoot();
    m_data.receiptsRoot = _header.receiptsRoot();
    m_data.difficulty = _header.difficulty();
    m_data.gasLimit = _header.gasLimit();
    m_data.gasUsed = _header.gasUsed();
    m_data.number = _header.number();
    m_data.timestamp = _header.timestamp();
    m_data.prevRandao = _header.prevRandao();
    m_data.nonce = _header.nonce();

    auto extra = _header.extraData();
    m_data.extraData.assign(extra.begin(), extra.end());

    auto bloom = _header.logsBloom();
    // logsBloom is a fixed 256-byte array. A shorter input (e.g. a header built without
    // going through validateHeader) is copied up to 256 bytes and the remainder stays zero —
    // this is a defensive default, not a silent guarantee of fidelity. validateHeader rejects
    // any bloom whose size != 256 on the hash path.
    std::memcpy(
        m_data.logsBloom.data(), bloom.data(), (std::min)(bloom.size(), m_data.logsBloom.size()));

    // Optional fork fields.
    if (_header.baseFee().has_value())
    {
        m_data.baseFee = _header.baseFee();
    }
    if (_header.withdrawalsRoot().has_value())
    {
        m_data.withdrawalsHash = _header.withdrawalsRoot();
    }
    if (_header.blobGasUsed().has_value())
    {
        m_data.blobGasUsed = _header.blobGasUsed();
    }
    if (_header.excessBlobGas().has_value())
    {
        m_data.excessBlobGas = _header.excessBlobGas();
    }
    if (_header.parentBeaconBlockRoot().has_value())
    {
        m_data.parentBeaconRoot = _header.parentBeaconBlockRoot();
    }
    if (_header.requestsHash().has_value())
    {
        m_data.requestsHash = _header.requestsHash();
    }

    m_version = _header.ethBlockVersion();
}

void EthBlockHeader::rlpEncode(bcos::bytes& out) const
{
    // FISCO-native / OP headers (EthBlockVersion::NON_ETH) store the timestamp in MILLISECONDS;
    // the Ethereum RLP field is SECONDS — /1000 applies only to those (ETH-version headers
    // already carry seconds, e.g. via EthBlockHeader::toTarsHeader's passthrough).
    const auto rlpTimestamp =
        m_version == EthBlockVersion::NON_ETH ? m_data.timestamp / 1000 : m_data.timestamp;
    codec::rlp::encode(out, m_data.parentInfo.blockHash, m_data.uncleHash, m_data.coinbase,
        m_data.stateRoot, m_data.txsRoot, m_data.receiptsRoot,
        bcos::bytesConstRef(m_data.logsBloom.data(), m_data.logsBloom.size()), m_data.difficulty,
        static_cast<uint64_t>(m_data.number), m_data.gasLimit, m_data.gasUsed,
        static_cast<uint64_t>(rlpTimestamp), m_data.extraData, m_data.prevRandao, m_data.nonce,
        m_data.baseFee, m_data.withdrawalsHash, m_data.blobGasUsed, m_data.excessBlobGas,
        m_data.parentBeaconRoot, m_data.requestsHash);
}

bcos::Error::UniquePtr EthBlockHeader::rlpDecode(bcos::bytesConstRef data)
{
    // The scalar codec accepts non-canonical integer encodings (leading zeros) and over-long
    // payloads (truncated mod 2^64). This bridge therefore hashes the canonical re-encoding
    // (rlpEncode) rather than the raw input bytes, so a non-canonical input is normalised
    // before hashing. geth would reject such input as malformed; rejecting it here is tracked
    // as a shared-codec change (see RLP scalar decode), out of scope for this bridge.
    //
    // The codec's decode takes a mutable bytesRef& (it advances a view cursor); the bytes
    // themselves are never written, so a single copy into a mutable buffer is enough.
    auto mutableData = data.toBytes();
    bytesRef out(mutableData.data(), mutableData.size());
    uint64_t _number = 0;
    uint64_t _timestamp = 0;

    auto error = codec::rlp::decode(out, m_data.parentInfo.blockHash, m_data.uncleHash,
        m_data.coinbase, m_data.stateRoot, m_data.txsRoot, m_data.receiptsRoot, m_data.logsBloom,
        m_data.difficulty, _number, m_data.gasLimit, m_data.gasUsed, _timestamp, m_data.extraData,
        m_data.prevRandao, m_data.nonce, m_data.baseFee, m_data.withdrawalsHash, m_data.blobGasUsed,
        m_data.excessBlobGas, m_data.parentBeaconRoot, m_data.requestsHash);
    if (error)
    {
        return error;
    }

    m_data.number = static_cast<int64_t>(_number);
    m_data.timestamp = static_cast<int64_t>(_timestamp);

    // Optional fork fields are decoded positionally, so the set of present optionals is
    // always a contiguous prefix — a later field can only be present if the view was still
    // non-empty when the earlier one was read. A truncated or shifted input therefore
    // surfaces as a version/field mismatch, which validateHeader's require/forbidForkField
    // pair rejects, or as a wrong-width item, which the FixedBytes length check rejects at
    // decode time (or as more than 21 items, rejected as UnexpectedListElements).

    // Derive the fork version from the presence of optional fields.
    if (m_data.requestsHash.has_value())
    {
        m_version = EthBlockVersion::PRAGUE;
    }
    else if (m_data.parentBeaconRoot.has_value())
    {
        m_version = EthBlockVersion::CANCUN;
    }
    else if (m_data.withdrawalsHash.has_value())
    {
        m_version = EthBlockVersion::SHANGHAI;
    }
    else if (m_data.baseFee.has_value())
    {
        m_version = EthBlockVersion::LONDON;
    }
    else
    {
        m_version = EthBlockVersion::PRE_LONDON;
    }

    return nullptr;
}
}  // namespace bcos::protocol
