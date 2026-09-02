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
#include <limits>
#include <stdexcept>

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

bcos::crypto::HashType EthBlockHeader::computeHash(
    const bcos::protocol::BlockHeader& header) noexcept(false)
{
    // No validateHeader here (unlike calculateRLPHash): this is the block-identity hash
    // for FISCO-native/OP headers (EthBlockVersion::NON_ETH) that validateHeader rejects.
    // The ctor performs the ms->s conversion and throws on a sub-second timestamp.
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
    // EthBlockHeaderData stores SECONDS; the internal BlockHeader stores MILLISECONDS.
    // Bound by int64 before the ×1000 so a hostile wire value cannot trigger signed
    // overflow on the conversion.
    if (ethHeader.data().timestamp >
        std::numeric_limits<int64_t>::max() / 1000)
    {
        // Leave the destination in the same defined empty state as the validateHeader
        // failure path below: the fields above are already written, so without a clear a
        // caller that checks the error but reuses the header could observe a half-populated
        // header from rejected hostile input.
        header->clear();
        return BCOS_ERROR_UNIQUE_PTR(static_cast<int32_t>(EthBlockHeaderError::InvalidHeader),
            "EthBlockHeader: timestamp out of representable millisecond range");
    }
    header->setTimestamp(ethHeader.data().timestamp * 1000);
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
    // Internal timestamps are milliseconds and must be whole seconds so the ms->s
    // conversion (constructor /1000) is lossless. Rejecting here keeps the calculateRLPHash
    // path on its Error-return contract (and BlockHeaderImpl::calculateHash's
    // clear-on-failure promise); the constructor throws the same condition for direct
    // ctor+rlpEncode callers that skip validateHeader.
    if (_header.timestamp() % 1000 != 0)
    {
        return invalid("EthBlockHeader: timestamp must be a whole number of seconds, got " +
                       std::to_string(_header.timestamp()) + " ms");
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
    // Timestamp domain model: EthBlockHeaderData carries SECONDS (the Ethereum RLP domain);
    // the internal BlockHeader stores MILLISECONDS. The ms->s conversion happens exactly
    // here (constructor) and the inverse at the tar boundary (toTarsHeader ×1000).
    //
    // A sub-second millisecond value (e.g. 1001 ms) would silently floor to a whole second
    // and corrupt the RLP hash input. rlpEncode no longer rejects it (only negatives), so
    // reject it at this sole ms->s conversion point — validateHeader's %1000 gate covers
    // the calculateRLPHash path, and this covers every direct ctor+rlpEncode caller.
    if (_header.timestamp() % 1000 != 0)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument(
            "timestamp must be a whole number of seconds, got " +
            std::to_string(_header.timestamp()) + " ms"));
    }
    m_version = _header.ethBlockVersion();
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
    // EthBlockHeaderData::timestamp is wire seconds (ms /1000); the only conversion point
    // to/from the internal BlockHeader milliseconds domain is the tar boundary.
    m_data.timestamp = _header.timestamp() / 1000;
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
}

void EthBlockHeader::rlpEncode(bcos::bytes& out) const
{
    // Timestamp domain model: EthBlockHeaderData stores SECONDS (the Ethereum RLP domain);
    // the RLP surface carries seconds directly — no conversion here. The ms->s conversion
    // happens only at the EthBlockHeader<->BlockHeader boundary (constructor /1000,
    // toTarsHeader ×1000).
    //
    // Defense-in-depth: a negative number/timestamp would wrap into a huge u64 on the
    // encode below, silently corrupting the RLP. calculateRLPHash guards via validateHeader,
    // but rlpEncode is public — reject here so a direct caller cannot produce a wrong
    // encoding. The field order itself is delegated to the EthBlockHeaderData codec (shared
    // with EthBlockBody).
    if (m_data.number < 0)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("number must be non-negative"));
    }
    if (m_data.timestamp < 0)
    {
        BOOST_THROW_EXCEPTION(std::invalid_argument("timestamp must be non-negative"));
    }
    codec::rlp::encode(out, m_data);
}

bcos::Error::UniquePtr EthBlockHeader::rlpDecode(bcos::bytesConstRef data)
{
    // The shared scalar codec now fails closed on over-wide uint payloads (> target width),
    // so a malformed header is rejected here at decode time instead of being accepted and
    // later normalised when hashing the canonical re-encoding. Leading zeros are still
    // accepted by the codec (the shared UnsignedIntegral walker in RLPDecode.h has no
    // CanonInt check — geth rejects ErrCanonInt); the canonical re-encode below is what
    // guards the hash against them. FOLLOW-UP: reject leading-zero integers in the shared
    // decoder (ywy F4; shared codec, outside this PR's file set).
    //
    // The codec's decode only advances a view cursor and never writes the buffer, so
    // take the view directly; the const_cast is confined to this read-only entry point.
    bytesRef out(const_cast<bcos::byte*>(data.data()), data.size());

    // The EthBlockHeaderData codec decodes the full 21-field list and bounds number /
    // timestamp to int64 (rejecting over-wide wire scalars). m_data.timestamp is the wire
    // SECONDS value; the ×1000 to internal milliseconds happens at the tar boundary.
    auto error = codec::rlp::decode(out, m_data);
    if (error)
    {
        return error;
    }

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

namespace bcos::codec::rlp
{
// EthBlockHeaderData codec: the single source of truth for the 21-field canonical header
// order, shared by EthBlockHeader::rlpEncode/rlpDecode (which delegate here) and by
// EthBlockBody (which embeds a header and an ommers list). The timestamp here is the WIRE
// domain (seconds); the ms bridge lives at the EthBlockHeader<->BlockHeader boundary.
size_t length(const protocol::EthBlockHeaderData& _header) noexcept
{
    return length(_header.parentInfo.blockHash, _header.uncleHash, _header.coinbase,
        _header.stateRoot, _header.txsRoot, _header.receiptsRoot,
        bytesConstRef(_header.logsBloom.data(), _header.logsBloom.size()), _header.difficulty,
        static_cast<uint64_t>(_header.number), _header.gasLimit, _header.gasUsed,
        static_cast<uint64_t>(_header.timestamp), _header.extraData, _header.prevRandao,
        _header.nonce, _header.baseFee, _header.withdrawalsHash, _header.blobGasUsed,
        _header.excessBlobGas, _header.parentBeaconRoot, _header.requestsHash);
}
void encode(bcos::bytes& _out, const protocol::EthBlockHeaderData& _header) noexcept
{
    encode(_out, _header.parentInfo.blockHash, _header.uncleHash, _header.coinbase,
        _header.stateRoot, _header.txsRoot, _header.receiptsRoot,
        bytesConstRef(_header.logsBloom.data(), _header.logsBloom.size()), _header.difficulty,
        static_cast<uint64_t>(_header.number), _header.gasLimit, _header.gasUsed,
        static_cast<uint64_t>(_header.timestamp), _header.extraData, _header.prevRandao,
        _header.nonce, _header.baseFee, _header.withdrawalsHash, _header.blobGasUsed,
        _header.excessBlobGas, _header.parentBeaconRoot, _header.requestsHash);
}
bcos::Error::UniquePtr decode(bcos::bytesRef& _in, protocol::EthBlockHeaderData& _header) noexcept
{
    uint64_t number = 0;
    uint64_t timestamp = 0;
    auto err = decode(_in, _header.parentInfo.blockHash, _header.uncleHash, _header.coinbase,
        _header.stateRoot, _header.txsRoot, _header.receiptsRoot, _header.logsBloom,
        _header.difficulty, number, _header.gasLimit, _header.gasUsed, timestamp, _header.extraData,
        _header.prevRandao, _header.nonce, _header.baseFee, _header.withdrawalsHash,
        _header.blobGasUsed, _header.excessBlobGas, _header.parentBeaconRoot, _header.requestsHash);
    if (err)
    {
        return err;
    }
    // Block number is internal int64; reject wire values above INT64_MAX instead
    // of narrowing into a negative number (downstream code assumes a non-negative
    // BlockNumber).
    if (number > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedLength, "block number exceeds int64 range");
    }
    // The timestamp arm of the same narrowing: a wire value in (INT64_MAX, UINT64_MAX]
    // would otherwise become a negative int64 (EthBlock and the ommers list decode via
    // this codec with no bridge to catch it).
    if (timestamp > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedLength, "block timestamp exceeds int64 range");
    }
    _header.number = static_cast<int64_t>(number);
    _header.timestamp = static_cast<int64_t>(timestamp);
    return nullptr;
}
}  // namespace bcos::codec::rlp
